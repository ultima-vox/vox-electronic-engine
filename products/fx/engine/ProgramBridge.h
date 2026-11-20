#pragma once
#include "NoiseRiser.h"
#include <array>
#include <atomic>
#include <cstdint>

namespace vox::fx {

// Single producer (controller/worker) to single consumer (audio) publication.
// Fixed storage, no allocation, waits or destruction on the audio thread. The
// producer validates before publishing and returns false if all slots are in
// use; callers must retain/retry the edit instead of silently losing it.
// The consumer claims every published slot present in its bounded scan,
// copies only the newest, then releases the claimed slots. A concurrently
// published edit may be observed at the next block. Bridge lifetime must
// extend past both threads, and prepare/destruction must not race either.
class ProgramBridge {
public:
    bool publish(const NoiseProgram& program) noexcept {
        if (!program.valid()) return false;
        for (auto& slot : slots_) {
            std::uint32_t expected = free;
            if (slot.state.compare_exchange_strong(expected, writing, std::memory_order_acquire, std::memory_order_relaxed)) {
                slot.program = program;
                slot.sequence = ++nextSequence_; // single producer only
                slot.state.store(ready, std::memory_order_release);
                return true;
            }
        }
        return false;
    }
    bool consumeLatest(NoiseProgram& destination, std::uint64_t& sequence) noexcept {
        std::array<bool, 3> owned {};
        std::size_t newest = slots_.size();
        for (std::size_t i = 0; i < slots_.size(); ++i) {
            auto& slot = slots_[i];
            std::uint32_t expected = ready;
            if (slot.state.compare_exchange_strong(expected, reading, std::memory_order_acquire, std::memory_order_relaxed)) {
                owned[i] = true;
                if (slot.sequence > lastConsumed_ &&
                    (newest == slots_.size() || slot.sequence > slots_[newest].sequence)) newest = i;
            }
        }
        if (newest != slots_.size()) {
            destination = slots_[newest].program;
            sequence = slots_[newest].sequence;
            lastConsumed_ = sequence;
        }
        for (std::size_t i = 0; i < slots_.size(); ++i)
            if (owned[i]) slots_[i].state.store(free, std::memory_order_release);
        return newest != slots_.size();
    }
private:
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
    enum : std::uint32_t { free, writing, ready, reading };
    struct Slot {
        std::atomic<std::uint32_t> state { free };
        NoiseProgram program;
        std::uint64_t sequence = 0;
    };
    std::array<Slot, 3> slots_ {};
    std::uint64_t nextSequence_ = 0; // producer only
    std::uint64_t lastConsumed_ = 0; // consumer only; discard late observations
};
} // namespace vox::fx
