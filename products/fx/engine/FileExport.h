#pragma once
#include "OfflineRender.h"
#include <juce_core/juce_core.h>
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

// Off the audio thread. The temporary lives beside the destination; the
// destination is replaced only after a complete, closed write. Filesystem
// replacement guarantees depend on the platform/filesystem.
inline bool writeBytes(const juce::File& target,const std::vector<std::uint8_t>& bytes,
                       Cancellation* cancel=nullptr,WriteObserver observe=nullptr,void* observerContext=nullptr) {
    if(cancel&&cancel->cancelled())return false;
    // Do not attempt to replace a directory even if its name ends in .wav.
    if(target.isDirectory())return false;
    juce::TemporaryFile temporary(target);
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
           (!cancel||cancel->beginCommit())&&temporary.overwriteTargetFileWithTemporary();
}
enum class Result { running, success, failure, cancelled };
struct Status { Result result; float progress; bool cancelling, committing; };
inline Result renderFile(const offline::Capture& capture,const juce::File& target,
                         Cancellation& cancel,std::atomic<float>& progress,
                         WriteObserver observe=nullptr,void* observerContext=nullptr) {
    bool success=false;
    try {
        std::vector<std::uint8_t> bytes;
        success=offline::renderWav(capture,bytes,&cancel.renderFlag(),&progress) &&
                writeBytes(target,bytes,&cancel,observe,observerContext);
    } catch(...) { success=false; }
    return success?Result::success:(cancel.sealFailure()?Result::cancelled:Result::failure);
}
class WavJob {
public:
    WavJob(offline::Capture capture,juce::File target,WriteObserver observe=nullptr,void* context=nullptr)
        : worker_([this,capture=std::move(capture),target=std::move(target),observe,context]() mutable {
            const auto outcome=renderFile(capture,target,cancel_,progress_,observe,context);
            // Retire the potentially large sample before publishing completion;
            // an editor reaping this job has no file or audio payload work left.
            std::vector<float>().swap(capture.sample);
            result_.store(outcome,std::memory_order_release);
        }) {}
    WavJob(const WavJob&)=delete;
    WavJob& operator=(const WavJob&)=delete;
    ~WavJob() { (void)cancel();wait(); }
    bool cancel() noexcept { return result()==Result::running&&cancel_.request(); }
    float progress() const noexcept { return progress_.load(std::memory_order_relaxed); }
    Result result() const noexcept { return result_.load(std::memory_order_acquire); }
    Status status() const noexcept { return {result(),progress(),cancel_.cancelled(),cancel_.committing()}; }
    // Lifecycle/controller thread only; destructor joins before destroying state.
    void wait() { if(worker_.joinable())worker_.join(); }
private:
    Cancellation cancel_;
    std::atomic<float> progress_{0};
    std::atomic<Result> result_{Result::running};
    std::thread worker_;
};
} // namespace vox::fx::fileexport
