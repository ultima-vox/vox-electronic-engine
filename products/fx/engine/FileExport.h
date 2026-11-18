#pragma once
#include "OfflineRender.h"
#include <juce_core/juce_core.h>
#include <atomic>
#include <thread>

namespace vox::fx::fileexport {
// Off the audio thread. The temporary lives beside the destination; the
// destination is replaced only after a complete, closed write. Filesystem
// replacement guarantees depend on the platform/filesystem.
inline bool writeBytes(const juce::File& target,const std::vector<std::uint8_t>& bytes,
                       const std::atomic<bool>* cancel=nullptr) {
    if(cancel && cancel->load(std::memory_order_relaxed))return false;
    juce::TemporaryFile temporary(target);
    bool written=false;
    {
        juce::FileOutputStream stream(temporary.getFile());
        written=stream.openedOk();
        for(std::size_t offset=0;written&&offset<bytes.size()&&!(cancel&&cancel->load(std::memory_order_relaxed));){
            const auto count=std::min<std::size_t>(16384,bytes.size()-offset);
            written=stream.write(bytes.data()+offset,count);offset+=count;
        }
        stream.flush();written=written&&stream.getStatus().wasOk();
    }
    // JUCE flush can report an incomplete buffered write only through the
    // final file size on some platforms. Never publish a truncated file.
    return written&&temporary.getFile().getSize()==static_cast<juce::int64>(bytes.size())&&
           !(cancel&&cancel->load(std::memory_order_relaxed))&&temporary.overwriteTargetFileWithTemporary();
}
enum class Result { running, success, failure, cancelled };
inline Result renderFile(const offline::Capture& capture,const juce::File& target,
                         std::atomic<bool>& cancel,std::atomic<float>& progress) {
    bool success=false;
    try {
        std::vector<std::uint8_t> bytes;
        success=offline::renderWav(capture,bytes,&cancel,&progress) &&
                writeBytes(target,bytes,&cancel);
    } catch(...) { success=false; }
    return success?Result::success:(cancel.load(std::memory_order_relaxed)?Result::cancelled:Result::failure);
}
class WavJob {
public:
    WavJob(offline::Capture capture,juce::File target)
        : worker_([this,capture=std::move(capture),target=std::move(target)]{
            result_.store(renderFile(capture,target,cancel_,progress_),std::memory_order_release);
        }) {}
    WavJob(const WavJob&)=delete;
    WavJob& operator=(const WavJob&)=delete;
    ~WavJob() { cancel();wait(); }
    void cancel() noexcept { cancel_.store(true,std::memory_order_relaxed); }
    bool cancelled() const noexcept { return cancel_.load(std::memory_order_relaxed); }
    float progress() const noexcept { return progress_.load(std::memory_order_relaxed); }
    Result result() const noexcept { return result_.load(std::memory_order_acquire); }
    // Controller thread only; destructor also joins before destroying state.
    void wait() { if(worker_.joinable())worker_.join(); }
private:
    std::atomic<bool> cancel_{false};
    std::atomic<float> progress_{0};
    std::atomic<Result> result_{Result::running};
    std::thread worker_;
};
} // namespace vox::fx::fileexport
