#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>
namespace vstengine::rack {
// One controller serializes prepare/commit operations. Audio never locks or
// waits: an overlapping block is silent and requests deterministic note
// recovery.
class RealtimeMutationGate final {
  std::recursive_mutex controller_;
  std::atomic<bool> closed_{false}, recovery_{false};
  std::atomic<std::uint32_t> readers_{0};
  unsigned depth_{};

public:
  class ReadScope final {
    std::unique_lock<std::recursive_mutex> lock_;

  public:
    explicit ReadScope(RealtimeMutationGate &g) : lock_(g.controller_) {}
  };
  class ControllerScope final {
    RealtimeMutationGate &gate_;
    std::unique_lock<std::recursive_mutex> lock_;

  public:
    explicit ControllerScope(RealtimeMutationGate &g)
        : gate_(g), lock_(g.controller_) {
      if (gate_.depth_++ == 0) {
        gate_.closed_.store(true, std::memory_order_seq_cst);
        while (gate_.readers_.load(std::memory_order_seq_cst) != 0)
          std::this_thread::yield();
      }
    }
    ~ControllerScope() {
      if (--gate_.depth_ == 0)
        gate_.closed_.store(false, std::memory_order_seq_cst);
    }
    ControllerScope(const ControllerScope &) = delete;
    ControllerScope &operator=(const ControllerScope &) = delete;
  };
  class AudioScope final {
    RealtimeMutationGate *gate_{};

  public:
    explicit AudioScope(RealtimeMutationGate &g) noexcept {
      if (g.closed_.load(std::memory_order_seq_cst)) {
        g.recovery_.store(true);
        return;
      }
      g.readers_.fetch_add(1, std::memory_order_seq_cst);
      if (g.closed_.load(std::memory_order_seq_cst)) {
        g.readers_.fetch_sub(1, std::memory_order_seq_cst);
        g.recovery_.store(true);
        return;
      }
      gate_ = &g;
    }
    ~AudioScope() {
      if (gate_)
        gate_->readers_.fetch_sub(1, std::memory_order_seq_cst);
    }
    explicit operator bool() const noexcept { return gate_ != nullptr; }
    AudioScope(const AudioScope &) = delete;
    AudioScope &operator=(const AudioScope &) = delete;
  };
  bool takeRecovery() noexcept {
    return recovery_.exchange(false, std::memory_order_acq_rel);
  }
};
} // namespace vstengine::rack
