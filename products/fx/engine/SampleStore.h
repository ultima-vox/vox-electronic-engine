#pragma once
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>
#include <vector>

namespace vox::fx::sample {
// Ownership foundation only; not yet connected to the instrument or file
// decoding. Worker/controller publishes immutable mono float samples; an audio
// consumer holds an explicit lease until all events using it have retired.
// All sample storage and destruction belong to the controller, never audio.
// The Store must outlive all leases and audio processing must stop before it
// is destroyed. A failed acquire is not permission to substitute a fallback.
class Store {
public:
    static constexpr std::size_t capacity=3, maxFrames=4u*1024u*1024u;
    class Lease {
    public:
        Lease()=default;
        Lease(const Lease&)=delete;
        Lease& operator=(const Lease&)=delete;
        const float* data() const noexcept { return data_; }
        std::size_t frames() const noexcept { return frames_; }
        std::uint32_t rate() const noexcept { return rate_; }
    private:
        friend class Store;
        Store* owner_=nullptr;
        std::size_t slot_=0;
        const float* data_=nullptr;
        std::size_t frames_=0;
        std::uint32_t rate_=0;
    };
    Store()=default;
    Store(const Store&)=delete;
    Store& operator=(const Store&)=delete;
    // Controller/worker only. All validation and allocation precede publish;
    // if no retired slot is free, the current sample is unchanged.
    bool publish(std::span<const float> samples,std::uint32_t sampleRate) {
        if(samples.empty()||samples.size()>maxFrames||sampleRate<8000||sampleRate>384000)return false;
        for(float sample:samples)if(!std::isfinite(sample)||sample < -1 || sample > 1)return false;
        std::vector<float> prepared;
        try {prepared.assign(samples.begin(),samples.end());} catch(...) {return false;}
        const std::lock_guard<std::mutex> lock(writerMutex_);
        if(generation_ >= (UINT64_MAX>>2))return false; // never recycle a generation token
        const auto current=current_.load(std::memory_order_acquire);
        const auto active=static_cast<std::size_t>(current&3u);
        for(std::size_t index=0;index<capacity;++index){
            if(active==index+1)continue;
            std::uint32_t expected=0;
            if(!slots_[index].leases.compare_exchange_strong(expected,locked,std::memory_order_acq_rel))continue;
            auto& slot=slots_[index];slot.samples.swap(prepared);slot.rate=sampleRate;
            // Generation participates in the token, so stale acquisitions can
            // never confuse an old incarnation of the same slot with a new one.
            ++generation_;
            const auto token=(generation_<<2)|(index+1);
            slot.leases.store(0,std::memory_order_release);
            current_.store(token,std::memory_order_release);
            return true;
        }
        return false;
    }
    // Controller only. Outstanding leases remain valid; future acquires fail.
    void clear() { const std::lock_guard<std::mutex> lock(writerMutex_);current_.store(0,std::memory_order_release); }
    // Audio thread: exactly one CAS and one validation, no retries or locks.
    // A concurrent replacement may cause a transient failure; caller decides
    // its explicit policy rather than using an invalid or implicit sample.
    bool acquire(Lease& lease) noexcept {
        if(lease.owner_!=nullptr)return false;
        const auto token=current_.load(std::memory_order_acquire);
        const auto id=static_cast<std::size_t>(token&3u);
        if(id==0 || id>capacity)return false;
        auto& slot=slots_[id-1];auto count=slot.leases.load(std::memory_order_acquire);
        if((count&locked)!=0 || count==locked-1)return false;
        if(!slot.leases.compare_exchange_strong(count,count+1,std::memory_order_acq_rel))return false;
        if(current_.load(std::memory_order_acquire)!=token){slot.leases.fetch_sub(1,std::memory_order_release);return false;}
        lease.owner_=this;lease.slot_=id-1;lease.data_=slot.samples.data();lease.frames_=slot.samples.size();lease.rate_=slot.rate;
        return true;
    }
    // Audio thread: explicit bounded refcount decrement, no memory destruction.
    void release(Lease& lease) noexcept {
        if(lease.owner_!=this)return;
        slots_[lease.slot_].leases.fetch_sub(1,std::memory_order_release);
        lease.owner_=nullptr;lease.data_=nullptr;lease.frames_=0;lease.rate_=0;
    }
private:
    static constexpr std::uint32_t locked=0x80000000u;
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free&&std::atomic<std::uint64_t>::is_always_lock_free);
    struct Slot { std::vector<float> samples;std::uint32_t rate=0;std::atomic<std::uint32_t> leases{0}; };
    std::array<Slot,capacity> slots_{};
    std::atomic<std::uint64_t> current_{0};
    std::uint64_t generation_=0;
    std::mutex writerMutex_;
};
} // namespace vox::fx::sample
