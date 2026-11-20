#pragma once
#include "OfflineRender.h"
#include "FileCommit.h"
#include <atomic>
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
struct Completion { Kind kind; Result result; };
struct Status { Kind kind; Result result; float progress; bool cancelling, committing; };
inline Result renderFile(const offline::Capture& capture,const juce::File& target,
                         Cancellation& cancel,std::atomic<float>& progress,
                         WriteObserver observe=nullptr,void* observerContext=nullptr) {
    bool success=false;
    try {
        if(!cancel.cancelled()){
            const auto destination=Destination::observe(target);
            std::vector<std::uint8_t> bytes;
            success=offline::renderWav(capture,bytes,&cancel.renderFlag(),&progress) &&
                    writeBytesTo(destination,bytes,&cancel,observe,observerContext);
        }
    } catch(...) { success=false; }
    return success?Result::success:(cancel.sealFailure()?Result::cancelled:Result::failure);
}
class Job {
public:
    Job(offline::Capture capture,juce::File target,WriteObserver observe=nullptr,void* context=nullptr)
        : kind_(Kind::wav),worker_([this,capture=std::move(capture),target=std::move(target),observe,context]() mutable {
            const auto outcome=renderFile(capture,target,cancel_,progress_,observe,context);
            // Retire the potentially large sample before publishing completion;
            // an editor reaping this job has no file or audio payload work left.
            std::vector<float>().swap(capture.sample);
            result_.store(outcome,std::memory_order_release);
        }) {}
    Job(pattern::Pattern snapshot,double bpm,juce::File target,WriteObserver observe=nullptr,void* context=nullptr,
        EncodeObserver observeEncoding=nullptr)
        : kind_(Kind::midi),worker_([this,snapshot,bpm,target=std::move(target),observe,context,observeEncoding] {
            bool success=false;
            try {
                if(!cancel_.cancelled()){
                    const auto destination=Destination::observe(target);
                    std::vector<std::uint8_t> bytes;
                    success=pattern::exportMidiChecked(snapshot,bpm,bytes,[&](std::size_t count) noexcept {
                        if(observeEncoding&&count!=0)observeEncoding(context,count);
                        return !cancel_.cancelled();
                    })&&writeBytesTo(destination,bytes,&cancel_,observe,context);
                }
            } catch(...) { success=false; }
            if(success)progress_.store(1.f,std::memory_order_relaxed);
            const auto result=success?Result::success:(cancel_.sealFailure()?Result::cancelled:Result::failure);
            result_.store(result,std::memory_order_release);
        }) {}
    Job(const Job&)=delete;
    Job& operator=(const Job&)=delete;
    ~Job() { (void)cancel();wait(); }
    Kind kind() const noexcept { return kind_; }
    bool cancel() noexcept { return result()==Result::running&&cancel_.request(); }
    float progress() const noexcept { return progress_.load(std::memory_order_relaxed); }
    Result result() const noexcept { return result_.load(std::memory_order_acquire); }
    Status status() const noexcept { return {kind_,result(),progress(),cancel_.cancelled(),cancel_.committing()}; }
    // Lifecycle/controller thread only; destructor joins before destroying state.
    void wait() { if(worker_.joinable())worker_.join(); }
private:
    const Kind kind_;
    Cancellation cancel_;
    std::atomic<float> progress_{0};
    std::atomic<Result> result_{Result::running};
    std::thread worker_;
};
} // namespace vox::fx::fileexport
