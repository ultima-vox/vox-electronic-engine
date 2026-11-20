#pragma once
#include "OfflineRender.h"
#include "FileCommit.h"
#include <atomic>
#include <optional>
#include <thread>

namespace vox::fx::fileexport {
// The cancel/replace race is resolved before calling into the filesystem.
// Cancellation accepted before commit guarantees no replacement. Once commit
// starts, its actual success/failure wins; there is no rollback of a rename.
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

// Sample the overwrite policy once, before any expensive render or encode.
// This is worker-side for WAV/MIDI. Files that appear after this observation
// are not eligible for replacement. An already-existing file still follows
// the save dialog's overwrite policy; this is not an identity check.
struct Destination {
    juce::File file;
    bool replaceExisting;
    static Destination observe(const juce::File& file) { return {file,file.existsAsFile()}; }
};

// Off the audio thread. Keep the staging file beside the destination. Only a
// complete, closed write can enter commit; failed publication is not retried
// via a copy. These are publication semantics, not crash-durability guarantees.
inline bool writeBytesTo(const Destination& destination,const std::vector<std::uint8_t>& bytes,
                         Cancellation* cancel=nullptr,WriteObserver observe=nullptr,void* observerContext=nullptr) {
    if((cancel&&cancel->cancelled())||bytes.empty()||destination.file==juce::File{}||destination.file.isDirectory())return false;
    juce::TemporaryFile temporary(destination.file);
    bool written=false;
    {
        juce::FileOutputStream stream(temporary.getFile());
        written=stream.openedOk();
        for(std::size_t offset=0;written&&offset<bytes.size()&&!(cancel&&cancel->cancelled());){
            const auto count=std::min<std::size_t>(16384,bytes.size()-offset);
            written=stream.write(bytes.data()+offset,count);offset+=count;
            if(written&&observe)observe(observerContext,offset);
        }
        stream.flush();written=written&&stream.getStatus().wasOk();
    }
    // JUCE flush can report an incomplete buffered write only through the
    // final file size on some platforms. Never publish a truncated file.
    return written&&temporary.getFile().getSize()==static_cast<juce::int64>(bytes.size())&&
           (!cancel||cancel->beginCommit())&&detail::commitFile(temporary.getFile(),destination.file,destination.replaceExisting);
}
inline bool writeBytes(const juce::File& target,const std::vector<std::uint8_t>& bytes,
                       Cancellation* cancel=nullptr,WriteObserver observe=nullptr,void* observerContext=nullptr) {
    if(cancel&&cancel->cancelled())return false;
    return writeBytesTo(Destination::observe(target),bytes,cancel,observe,observerContext);
}
enum class Result { running, success, failure, cancelled };
enum class Kind { wav, midi };
enum class Purpose { save, drag };
struct Completion { Kind kind; Result result; Purpose purpose; };
struct Status { Kind kind; Result result; float progress; bool cancelling, committing; Purpose purpose; };

// One worker-created scratch file. Without a native handoff it is removed on
// retirement. After a native offer the target may read lazily, so never unlink
// on drag-end or plugin unload. The OS temporary area isn't a project asset.
class DragFile {
public:
    static std::shared_ptr<DragFile> create(Kind kind) {
        const auto root=juce::File::getSpecialLocation(juce::File::tempDirectory);
        if(root==juce::File{})return {};
        const auto directory=root.getChildFile("vox-fx-drag-"+juce::Uuid().toString());
        auto payload=std::shared_ptr<DragFile>(new DragFile(kind,directory));
        if(!detail::createPrivateDirectory(directory))return {};
        payload->ownsDirectory_=true;
        return payload;
    }
    ~DragFile() {
        if(ownsDirectory_&&!offered){file.deleteFile();directory_.deleteFile();}
    }
    const Kind kind;
    const juce::File file;
    bool offered=false; // message-thread only, or after ownership moved off it
    std::atomic<bool> active{false}; // platform completion may be asynchronous
private:
    DragFile(Kind k,const juce::File& directory)
        : kind(k),file(directory.getChildFile(k==Kind::wav?"VOX FX.wav":"VOX FX Pattern.mid")),directory_(directory) {}
    const juce::File directory_;
    bool ownsDirectory_=false;
};

inline bool renderTo(const offline::Capture& capture,const Destination& destination,
                     Cancellation& cancel,std::atomic<float>& progress,WriteObserver observe,void* context) {
    std::vector<std::uint8_t> bytes;
    return offline::renderWav(capture,bytes,&cancel.renderFlag(),&progress)&&
           writeBytesTo(destination,bytes,&cancel,observe,context);
}
inline Result renderFile(const offline::Capture& capture,const juce::File& target,
                         Cancellation& cancel,std::atomic<float>& progress,
                         WriteObserver observe=nullptr,void* observerContext=nullptr) {
    bool success=false;
    try {
        if(!cancel.cancelled())success=renderTo(capture,Destination::observe(target),cancel,progress,observe,observerContext);
    } catch(...) { success=false; }
    return success?Result::success:(cancel.sealFailure()?Result::cancelled:Result::failure);
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
              return pattern::exportMidiChecked(snapshot,bpm,bytes,[&](std::size_t count) noexcept {
                  if(observeEncoding&&count!=0)observeEncoding(context,count);
                  return !cancel.cancelled();
              })&&writeBytesTo(destination,bytes,&cancel,observe,context);
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
              bool success=false;
              try {
                  if(!cancel_.cancelled()){
                      if(purpose_==Purpose::drag){
                          auto payload=DragFile::create(kind_);
                          if(payload&&(*work)(Destination{payload->file,false},cancel_,progress_)){
                              drag_=std::move(payload);success=true;
                          }
                      } else success=(*work)(Destination::observe(target),cancel_,progress_);
                  }
              } catch(...) { success=false; }
              // Retire captured data and unsuccessful staging before publication.
              work.reset();
              if(success)progress_.store(1.f,std::memory_order_relaxed);
              const auto outcome=success?Result::success:(cancel_.sealFailure()?Result::cancelled:Result::failure);
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
