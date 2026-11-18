#pragma once
#include "SampleDecode.h"
#include "SampleReference.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <thread>
#include <utility>

namespace vox::fx::sample {
// Non-RT controller. Owns a persistent loader thread and Store; destroy only
// after the audio engine using store() has stopped and released its leases.
// Selection tokens: 0 = explicitly built-in, 1 = external but unavailable,
// >= 5 = exact immutable Store generation. Audio reads selection lock-free.
class Controller {
public:
    enum class Status { builtin, loading, ready, missing, failed };
    Controller() : worker_([this]{run();}) {}
    Controller(const Controller&)=delete;
    Controller& operator=(const Controller&)=delete;
    ~Controller(){
        {const std::lock_guard<std::mutex> lock(mutex_);stopping_=true;pending_=false;}
        condition_.notify_all();if(worker_.joinable())worker_.join();
    }
    Store& store() noexcept { return store_; }
    std::uint64_t selection() const noexcept { return selection_.load(std::memory_order_acquire); }
    Reference reference() const { const std::lock_guard<std::mutex> lock(mutex_);return desired_; }
    Status status() const { const std::lock_guard<std::mutex> lock(mutex_);return status_; }
    // Import is transactional: failure leaves the prior reference and token
    // selected. A new request coalesces older unfinished requests.
    bool import(const juce::File& file) {
        const auto name=file.getFullPathName();
        if(name.isEmpty()||!juce::File::isAbsolutePath(name))return false;
        Reference candidate;candidate.kind=Reference::Kind::external;candidate.path=name.toRawUTF8();candidate.sha256=std::string(64,'0');
        if(!candidate.valid())return false;
        Request next;
        try { next={candidate.path,{},true,0}; } catch(...) {return false;}
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            if(stopping_||revision_==std::numeric_limits<std::uint64_t>::max())return false;
            next.revision=++revision_;request_=std::move(next);pending_=true;status_=Status::loading;
        }
        condition_.notify_all();return true;
    }
    // Project restore publishes the external reference immediately as missing;
    // it becomes playable only after file hash verification. No fallback.
    bool restore(const Reference& reference) {
        if(!reference.valid())return false;
        Reference next;Request request;
        try {next=reference;if(reference.kind==Reference::Kind::external)request={reference.path,reference.sha256,false,0};}
        catch(...) {return false;}
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            if(stopping_||revision_==std::numeric_limits<std::uint64_t>::max())return false;
            ++revision_;desired_=std::move(next);
            if(reference.kind==Reference::Kind::builtin){
                selection_.store(0,std::memory_order_release);pending_=false;status_=Status::builtin;store_.clear();
            } else {
                selection_.store(1,std::memory_order_release);status_=Status::loading;
                request.revision=revision_;request_=std::move(request);pending_=true;
            }
        }
        condition_.notify_all();return true;
    }
    // Controller/test thread only; never call on audio or GUI hot paths.
    bool waitUntilSettled(std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        return condition_.wait_for(lock,timeout,[this]{return status_!=Status::loading || stopping_;});
    }
private:
    struct Request { std::string path,expected;bool importing=false;std::uint64_t revision=0; };
    void run() noexcept {
        for(;;){
            Request request;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                condition_.wait(lock,[this]{return stopping_||pending_;});
                if(stopping_)return;
                request=std::move(request_);pending_=false;
            }
            Decoded decoded;Reference imported;bool verified=false;
            try {
                const auto path=juce::String::fromUTF8(request.path.c_str(),static_cast<int>(request.path.size()));
                const bool loaded=juce::File::isAbsolutePath(path)&&loadWav(juce::File(path),decoded);
                const auto hash=loaded?decoded.sha256.toStdString():std::string{};
                verified=loaded&&(request.importing||hash==request.expected);
                if(verified&&request.importing)imported={Reference::Kind::external,request.path,hash};
            } catch(...) {verified=false;}
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                if(stopping_)return;
                if(request.revision!=revision_)continue;
                std::uint64_t token=0;
                if(verified&&store_.publish(decoded.mono,decoded.rate,&token)){
                    if(request.importing)desired_=std::move(imported);
                    selection_.store(token,std::memory_order_release);status_=Status::ready;
                } else status_=request.importing?Status::failed:Status::missing;
            }
            condition_.notify_all();
        }
    }
    Store store_; // must be destroyed after worker and audio leases
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    Reference desired_;
    Request request_;
    std::atomic<std::uint64_t> selection_{0};
    Status status_=Status::builtin;
    std::uint64_t revision_=0;
    bool stopping_=false,pending_=false;
    std::thread worker_;
};
} // namespace vox::fx::sample
