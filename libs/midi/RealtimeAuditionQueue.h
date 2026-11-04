#pragma once
#include "instrument/InstrumentAbi.h"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
namespace vstengine::midi {
// The keyboard serializes producers under its controller-side lock. Exactly one
// audio consumer reads this SPSC handoff; it never calls back into the
// keyboard.
class RealtimeAuditionQueue final {
  static constexpr std::uint32_t capacity = 2048;
  std::array<VoxMidiEventV1, capacity + 1> events_{};
  std::atomic<std::uint32_t> read_{}, write_{};
  std::atomic<bool> overflow_{};

public:
  bool push(const VoxMidiEventV1 &event) noexcept {
    const auto write = write_.load(std::memory_order_relaxed);
    const auto next = (write + 1) % events_.size();
    if (next == read_.load(std::memory_order_acquire)) {
      overflow_.store(true, std::memory_order_release);
      return false;
    }
    events_[write] = event;
    write_.store(static_cast<std::uint32_t>(next), std::memory_order_release);
    return true;
  }
  bool pop(VoxMidiEventV1 &event) noexcept {
    const auto read = read_.load(std::memory_order_relaxed);
    if (read == write_.load(std::memory_order_acquire))
      return false;
    event = events_[read];
    read_.store(static_cast<std::uint32_t>((read + 1) % events_.size()),
                std::memory_order_release);
    return true;
  }
  void discard() noexcept {
    read_.store(write_.load(std::memory_order_acquire),
                std::memory_order_release);
  }
  bool takeOverflow() noexcept {
    return overflow_.exchange(false, std::memory_order_acq_rel);
  }
};
} // namespace vstengine::midi
