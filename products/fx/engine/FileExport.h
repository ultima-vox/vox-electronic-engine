#pragma once
#include "OfflineRender.h"
#include "FileCommit.h"
#include <atomic>
#include <optional>
#include <thread>

namespace vox::fx::fileexport {
// The cancel/publish race is resolved before calling into the filesystem.
// Cancellation accepted before commit guarantees no publication. Once commit
// starts, its actual success/failure wins; there is no rollback of a published file.
class Cancellation {
public:
    bool request() noexcept {
        auto expected=Stage::open;
        if(!stage_.compare_exchange_strong(expected,Stage::cancelled))return expected==Stage::cancelled;
        renderCancelled_.store(true,std::memory_order_release);
        return true;
    }
    bool cancelled() const noexcept { return stage_.load()==Stage::cancelled; }
    bool committing() const noexcept { return stage_.load()==Stage::committing; }
    const std::atomic<bool>& renderFlag() const noexcept { return renderCancelled_; }
    bool beginCommit() noexcept {
        auto expected=Stage::open;
        return stage_.compare_exchange_strong(expected,Stage::committing);
    }
    // Seal an early failure before publishing completion. If cancellation
    // already won, preserve its result; otherwise reject later requests.
    bool sealFailure() noexcept {
        auto expected=Stage::open;
        return !stage_.compare_exchange_strong(expected,Stage::failed)&&expected==Stage::cancelled;
    }
private:
    enum class Stage { open, cancelled, committing, failed };
    std::atomic<Stage> stage_{Stage::open};
    std::atomic<bool> renderCancelled_{false};
};

// Worker-side observer for deterministic write/cancel tests; null in the UI.
using WriteObserver=void(*)(void*,std::size_t) noexcept;
using EncodeObserver=void(*)(void*,std::size_t) noexcept;

enum class Result { running, success, failure, cancelled, destinationExists };

// Observe before rendering for diagnostics and to avoid costly work. The
// final OS operation is always no-replace even when this saw an absent path.
struct Destination {
    juce::File file;
    detail::TargetState original;
    static Destination observe(const juce::File& file) { return {file,detail::observeTarget(file)}; }
    Result rejection() const noexcept {
        if(original==detail::TargetState::occupied)return Result::destinationExists;
        return original==detail::TargetState::absent?Result::running:Result::failure;
    }
};

// Off the audio thread. An exclusively created sibling directory gives the
// stage its own pathname. Only a complete, closed file can enter publication.
// This is a name-publication guarantee, not a crash-durability guarantee.
inline Result writeBytesTo(const Destination& destination,const std::vector<std::uint8_t>& bytes,
                           Cancellation* cancel=nullptr,WriteObserver observe=nullptr,void* observerContext=nullptr) {
    if(cancel&&cancel->cancelled())return Result::cancelled;
    if(const auto rejected=destination.rejection();rejected!=Result::running)return rejected;
    if(bytes.empty())return Result::failure;
    struct Stage {
        juce::File directory,file;
        bool owned=false;
        ~Stage(){if(owned){file.deleteFile();directory.deleteFile();}}
    } stage;
    stage.directory=destination.file.getParentDirectory().getChildFile(".vox-fx-export-"+juce::Uuid().toString());
    stage.file=stage.directory.getChildFile("payload");
    stage.owned=detail::createPrivateDirectory(stage.directory);
    if(!stage.owned)return Result::failure;
    bool written=false;
    {
        juce::FileOutputStream stream(stage.file);
        written=stream.openedOk();
        for(std::size_t offset=0;written&&offset<bytes.size()&&!(cancel&&cancel->cancelled());){
            const auto count=std::min<std::size_t>(16384,bytes.size()-offset);
            written=stream.write(bytes.data()+offset,count);offset+=count;
            if(written&&observe)observe(observerContext,offset);
        }
        stream.flush();written=written&&stream.getStatus().wasOk();
    }
    // JUCE can report a short buffered write through file size only.
    if(!written||stage.file.getSize()!=static_cast<juce::int64>(bytes.size()))return Result::failure;
    if(cancel&&!cancel->beginCommit())return Result::cancelled;
    const auto committed=detail::publishNewFile(stage.file,destination.file);
    if(committed==detail::PublishResult::published)return Result::success;
    if(committed==detail::PublishResult::occupied)return Result::destinationExists;
    return Result::failure;
}
inline bool writeBytes(const juce::File& target,const std::vector<std::uint8_t>& bytes,
                       Cancellation* cancel=nullptr,WriteObserver observe=nullptr,void* observerContext=nullptr) {
    if(cancel&&cancel->cancelled())return false;
    return writeBytesTo(Destination::observe(target),bytes,cancel,observe,observerContext)==Result::success;
}
enum class Kind { wav, midi };
enum class Purpose { save, drag };
struct Completion { Kind kind; Result result; Purpose purpose; };
struct Status { Kind kind; Result result; float progress; bool cancelling, committing; Purpose purpose; };

// One worker-created scratch file. Without a native handoff it is removed on
// retirement. After a native offer the target may read lazily, so never unlink
// on drag-end or plugin unload. The OS temporary area isn't a project asset.
class DragFile {
public:
    static std::shared_ptr<DragFile> create(Kind kind,const juce::File& scratchRoot) {
        const auto root=scratchRoot==juce::File{}?juce::File::getSpecialLocation(juce::File::tempDirectory):scratchRoot;
        if(root==juce::File{})return {};
        const auto directory=root.getChildFile("vox-fx-drag-"+juce::Uuid().toString());
        auto payload=std::shared_ptr<DragFile>(new DragFile(kind,directory));
        if(!detail::createPrivateDirectory(directory))return {};
        payload->ownsDirectory_=true;
        return payload;
    }
    ~DragFile() {
        if(ownsDirectory_&&!offered){if(ownsPayload_)file.deleteFile();directory_.deleteFile();}
    }
    // Read-only validation for a completed scratch payload. This is controller/
    // worker-thread work only, never an audio-callback operation. Checking the
    // original byte length catches expired/truncated files without retaining a
    // second copy of a potentially large WAV in memory.
    bool hasReadablePayload() const noexcept {
        if(!ownsDirectory_||!ownsPayload_||payloadSize_<=0)return false;
        try {
            if(!file.existsAsFile()||file.isDirectory()||!file.hasReadAccess()||file.getSize()!=payloadSize_)
                return false;
            auto input=file.createInputStream();
            if(!input||!input->getStatus().wasOk()||input->getTotalLength()!=payloadSize_)return false;
            char firstByte=0;
            return input->read(&firstByte,1)==1&&input->getStatus().wasOk();
        } catch(...) { return false; }
    }
    const Kind kind;
    const juce::File file;
    bool offered=false; // message-thread only, or after ownership moved off it
    bool launching=false;          // guard synchronous native reentry
    std::uint64_t sequence=0;      // message-thread only; zero is reserved
    std::atomic<std::uint64_t> activeTicket{0}; // native completion may be asynchronous
private:
    friend class Job;
    DragFile(Kind k,const juce::File& directory)
        : kind(k),file(directory.getChildFile(k==Kind::wav?"VOX FX.wav":"VOX FX Pattern.mid")),directory_(directory) {}
    const juce::File directory_;
    juce::int64 payloadSize_=0; // published with Job::result_ before controller access
    bool ownsDirectory_=false,ownsPayload_=false;
};

inline Result renderTo(const offline::Capture& capture,const Destination& destination,
                       Cancellation& cancel,std::atomic<float>& progress,WriteObserver observe,void* context) {
    if(const auto rejected=destination.rejection();rejected!=Result::running)return rejected;
    std::vector<std::uint8_t> bytes;
    if(!offline::renderWav(capture,bytes,&cancel.renderFlag(),&progress))return Result::failure;
    return writeBytesTo(destination,bytes,&cancel,observe,context);
}
inline Result renderFile(const offline::Capture& capture,const juce::File& target,
                         Cancellation& cancel,std::atomic<float>& progress,
                         WriteObserver observe=nullptr,void* observerContext=nullptr) {
    auto outcome=Result::failure;
    try {
        if(!cancel.cancelled())outcome=renderTo(capture,Destination::observe(target),cancel,progress,observe,observerContext);
    } catch(...) { outcome=Result::failure; }
    return outcome!=Result::success&&cancel.sealFailure()?Result::cancelled:outcome;
}
class Job {
public:
    Job(offline::Capture capture,juce::File target,WriteObserver observe=nullptr,void* context=nullptr,
        Purpose purpose=Purpose::save,std::shared_ptr<DragFile> retired={})
        : Job(Kind::wav,purpose,std::move(target),std::move(retired),
          [capture=std::move(capture),observe,context](const Destination& destination,Cancellation& cancel,std::atomic<float>& progress){
              return renderTo(capture,destination,cancel,progress,observe,context);
          }) {}
    Job(pattern::Pattern snapshot,double bpm,juce::File target,WriteObserver observe=nullptr,void* context=nullptr,
        EncodeObserver observeEncoding=nullptr,Purpose purpose=Purpose::save,std::shared_ptr<DragFile> retired={})
        : Job(Kind::midi,purpose,std::move(target),std::move(retired),
          [snapshot,bpm,observe,context,observeEncoding](const Destination& destination,Cancellation& cancel,std::atomic<float>&){
              std::vector<std::uint8_t> bytes;
              if(!pattern::exportMidiChecked(snapshot,bpm,bytes,[&](std::size_t count) noexcept {
                  if(observeEncoding&&count!=0)observeEncoding(context,count);
                  return !cancel.cancelled();
              }))return Result::failure;
              return writeBytesTo(destination,bytes,&cancel,observe,context);
          }) {}
    Job(const Job&)=delete;
    Job& operator=(const Job&)=delete;
    ~Job() { (void)cancel();wait(); }
    Kind kind() const noexcept { return kind_; }
    Purpose purpose() const noexcept { return purpose_; }
    bool cancel() noexcept { return result()==Result::running&&cancel_.request(); }
    float progress() const noexcept { return progress_.load(std::memory_order_relaxed); }
    Result result() const noexcept { return result_.load(std::memory_order_acquire); }
    Status status() const noexcept { return {kind_,result(),progress(),cancel_.cancelled(),cancel_.committing(),purpose_}; }
    std::shared_ptr<DragFile> takeDragFile() { return result()==Result::success?std::move(drag_):nullptr; }
    // Lifecycle/controller thread only; destructor joins before destroying state.
    void wait() { if(worker_.joinable())worker_.join(); }
private:
    template <typename Work>
    Job(Kind kind,Purpose purpose,juce::File target,std::shared_ptr<DragFile> retired,Work work)
        : kind_(kind),purpose_(purpose),
          worker_([this,target=std::move(target),retired=std::move(retired),work=std::optional<Work>(std::move(work))]() mutable {
              retired.reset();
              auto outcome=Result::failure;
              try {
                  if(!cancel_.cancelled()){
                      if(purpose_==Purpose::drag){
                          auto payload=DragFile::create(kind_,target);
                          if(payload){
                              outcome=(*work)(Destination{payload->file,detail::TargetState::absent},cancel_,progress_);
                              if(outcome==Result::success){
                                  payload->ownsPayload_=true;
                                  payload->payloadSize_=payload->file.getSize();
                                  if(payload->hasReadablePayload())drag_=std::move(payload);
                                  else outcome=Result::failure;
                              }
                          }
                      } else {
                          const auto destination=Destination::observe(target);
                          outcome=destination.rejection();
                          if(outcome==Result::running)outcome=(*work)(destination,cancel_,progress_);
                      }
                  }
              } catch(...) { outcome=Result::failure; }
              // Retire captured data and unsuccessful staging before publication.
              work.reset();
              if(outcome==Result::success)progress_.store(1.f,std::memory_order_relaxed);
              else if(cancel_.sealFailure())outcome=Result::cancelled;
              result_.store(outcome,std::memory_order_release);
          }) {}
    const Kind kind_;
    const Purpose purpose_;
    Cancellation cancel_;
    std::atomic<float> progress_{0};
    std::atomic<Result> result_{Result::running};
    std::shared_ptr<DragFile> drag_;
    std::thread worker_;
};
} // namespace vox::fx::fileexport
