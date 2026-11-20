#pragma once
#include "SampleDecode.h"
#include "SampleReference.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <limits>
#include <functional>
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
    struct PreparedRestore { Reference reference;std::string requestPath,expectedHash; };
    static bool prepareRestore(const Reference& reference,PreparedRestore& output) noexcept {
        if(!reference.valid())return false;
        try { PreparedRestore next;next.reference=reference;
            if(reference.kind==Reference::Kind::external){next.requestPath=reference.path;next.expectedHash=reference.sha256;}
            output=std::move(next);return true;
        } catch(...) {return false;}
    }
    using Loader=std::function<bool(const juce::File&,Decoded&)>;
    using CancelLoader=std::function<bool(const juce::File&,Decoded&,const std::atomic<bool>*)>;
    Controller():Controller(CancelLoader{loadWavCancelable}){}
    explicit Controller(Loader loader):Controller(CancelLoader{[loader=std::move(loader)](const juce::File& file,Decoded& result,const std::atomic<bool>*){
        return loader(file,result);
    }}){}
    explicit Controller(CancelLoader loader):loader_(std::move(loader)),worker_([this]{run();}){}
    Controller(const Controller&)=delete;
    Controller& operator=(const Controller&)=delete;
    ~Controller(){
        {const std::lock_guard<std::mutex> lock(mutex_);stopping_=true;pending_=false;cancelLoad_.store(true,std::memory_order_relaxed);}
        condition_.notify_all();if(worker_.joinable())worker_.join();
    }
    Store& store() noexcept { return store_; }
    std::uint64_t selection() const noexcept { return selection_.load(std::memory_order_acquire); }
    struct Snapshot { Reference reference;Status status; };
    Snapshot snapshot() const { const std::lock_guard<std::mutex> lock(mutex_);return {desired_,status_}; }
    Reference reference() const { const std::lock_guard<std::mutex> lock(mutex_);return desired_; }
    Status status() const { const std::lock_guard<std::mutex> lock(mutex_);return status_; }
    // A pending import saves its intended path as unverified and is silent for
    // granular playback. On failure, roll back the pre-import selection; a
    // superseding import keeps that same rollback point.
    bool import(const juce::File& file) {
        const auto name=file.getFullPathName();
        if(name.isEmpty()||!juce::File::isAbsolutePath(name))return false;
        Reference candidate;candidate.kind=Reference::Kind::pending;candidate.path=name.toRawUTF8();
        if(!candidate.valid())return false;
        Request next;
        try { next={candidate.path,{},true,0}; } catch(...) {return false;}
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            if(stopping_||disabled_||revision_==std::numeric_limits<std::uint64_t>::max())return false;
            if(!importPending_){
                rollback_=std::move(desired_);rollbackSelection_=selection_.load(std::memory_order_relaxed);
                importPending_=true;
            }
            desired_=std::move(candidate);selection_.store(1,std::memory_order_release);
            next.revision=++revision_;cancelLoad_.store(true,std::memory_order_relaxed);request_=std::move(next);pending_=true;status_=Status::loading;
        }
        condition_.notify_all();return true;
    }
    // A relink is not an import: only a byte-identical asset may replace the
    // saved path. Keep the original reference and generation usable until
    // verification succeeds. The chooser's reference may have been replaced
    // by a host restore while it was open.
    bool relink(const juce::File& file, const Reference& expectedReference) {
        if(expectedReference.kind!=Reference::Kind::external || !expectedReference.valid()) return false;
        const auto path=file.getFullPathName();
        if(path.isEmpty() || !juce::File::isAbsolutePath(path))return false;
        Request next;
        try {
            Reference candidate{Reference::Kind::external,path.toRawUTF8(),expectedReference.sha256};
            if(!candidate.valid())return false;
            next={std::move(candidate.path),std::move(candidate.sha256),false,0,true};
        } catch(...) {return false;}
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            if(stopping_||disabled_||importPending_||revision_==std::numeric_limits<std::uint64_t>::max()||
               desired_.kind!=Reference::Kind::external||desired_.path!=expectedReference.path||
               desired_.sha256!=expectedReference.sha256)return false;
            next.revision=++revision_;cancelLoad_.store(true,std::memory_order_relaxed);request_=std::move(next);pending_=true;status_=Status::loading;
        }
        condition_.notify_all();return true;
    }
    // Project restore publishes the external reference immediately as missing;
    // it becomes playable only after file hash verification. No fallback.
    bool restore(const Reference& reference) {
        PreparedRestore prepared;if(!prepareRestore(reference,prepared))return false;
        commitRestore(std::move(prepared));return true;
    }
    // All allocations were done by prepareRestore; call only from the
    // controller/host state thread, after the containing state is validated.
    void commitRestore(PreparedRestore&& prepared) {
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            if(revision_==std::numeric_limits<std::uint64_t>::max())disabled_=true;
            else ++revision_;
            cancelLoad_.store(true,std::memory_order_relaxed);
            importPending_=false;rollback_={};
            desired_=std::move(prepared.reference);
            if(desired_.kind==Reference::Kind::builtin){
                selection_.store(0,std::memory_order_release);pending_=false;status_=Status::builtin;
            } else {
                selection_.store(1,std::memory_order_release);
                if(desired_.kind==Reference::Kind::pending||disabled_||stopping_){pending_=false;status_=Status::missing;}
                else {
                    request_={std::move(prepared.requestPath),std::move(prepared.expectedHash),false,revision_};
                    pending_=true;status_=Status::loading;
                }
            }
        }
        condition_.notify_all();
    }
    // Controller/test thread only; never call on audio or GUI hot paths.
    bool waitUntilSettled(std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        return condition_.wait_for(lock,timeout,[this]{return status_!=Status::loading || stopping_;});
    }
private:
    struct Request { std::string path,expected;bool importing=false;std::uint64_t revision=0;bool relinking=false; };
    void run() noexcept {
        for(;;){
            Request request;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                condition_.wait(lock,[this]{return stopping_||pending_;});
                if(stopping_)return;
                request=std::move(request_);pending_=false;cancelLoad_.store(false,std::memory_order_relaxed);
            }
            Decoded decoded;Reference imported;bool verified=false;
            try {
                const auto path=juce::String::fromUTF8(request.path.c_str(),static_cast<int>(request.path.size()));
                const bool loaded=juce::File::isAbsolutePath(path)&&loader_(juce::File(path),decoded,&cancelLoad_);
                const auto hash=loaded?decoded.sha256.toStdString():std::string{};
                verified=loaded&&(request.importing||hash==request.expected);
                if(verified&&(request.importing||request.relinking))imported={Reference::Kind::external,request.path,hash};
            } catch(...) {verified=false;}
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                if(stopping_)return;
                if(disabled_||request.revision!=revision_)continue;
                std::uint64_t token=0;
                if(verified&&store_.publish(decoded.mono,decoded.rate,&token)){
                    if(request.importing||request.relinking){desired_=std::move(imported);rollback_={};importPending_=false;}
                    selection_.store(token,std::memory_order_release);status_=Status::ready;
                } else if(request.importing){
                    desired_=std::move(rollback_);selection_.store(rollbackSelection_,std::memory_order_release);
                    importPending_=false;status_=Status::failed;
                } else status_=request.relinking?Status::failed:Status::missing;
            }
            condition_.notify_all();
        }
    }
    Store store_; // must be destroyed after worker and audio leases
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    Reference desired_,rollback_;
    std::uint64_t rollbackSelection_=0;
    Request request_;
    std::atomic<std::uint64_t> selection_{0};
    Status status_=Status::builtin;
    std::uint64_t revision_=0;
    bool stopping_=false,pending_=false,disabled_=false,importPending_=false;
    std::atomic<bool> cancelLoad_{false};
    CancelLoader loader_;
    std::thread worker_;
};
} // namespace vox::fx::sample
