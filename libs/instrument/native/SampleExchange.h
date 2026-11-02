#pragma once
// Sample hand-off of one native instance: contract C rev. 3.1
// (docs/architecture/FULLON_DRUMS_PRODUCTION.md, section C).
//
// Every buffer is in exactly one state with one owner:
//   staged      controller   publish() builds the entry
//   pending     the word     leaves by exactly one exchange
//   superseded  controller   a later publish got it back: released at once
//   current     audio        installed at a block start; voices may use it
//   retiring    controller   replaced in a valid report; released by the free
//                            rule or by verified quiescence
// The audio thread never allocates, frees, locks or waits here.
#include "instrument/InstrumentContract.h"
#include "instrument/abi/VoxModuleAbiV2.h"

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

namespace vstengine::instrument::native {

class SampleExchange final : public SampleHost {
public:
  // At most this many live buffers per sample slot: 1 pending, 1 current and
  // 4 retiring. publish() refuses beyond it until collect() releases some.
  static constexpr std::size_t maxLivePerSlot = 6;
  static constexpr std::uint32_t usagePerSlot = 4;

  explicit SampleExchange(std::uint32_t slots);
  // Releases every buffer, each exactly once. Only after the module instance
  // is destroyed, so no process() can run or start.
  ~SampleExchange() override;
  SampleExchange(const SampleExchange &) = delete;
  SampleExchange &operator=(const SampleExchange &) = delete;

  // SampleHost (controller threads).
  std::uint32_t sampleSlotCount() const noexcept override { return slots_; }
  bool publish(std::uint32_t slot, std::unique_ptr<SampleBuffer> buffer,
               std::string &diagnostic, std::uint64_t memoryLimitBytes = 0) override;
  std::size_t collect() override;
  SampleStats stats() const override;
  void setReleaseSinkForTesting(ReleaseSink sink) override;

  // Controller, only while no process() can run (after a successful module
  // prepare in the Rack's exclusive section, which ends every voice):
  // releases every buffer that was installed and then replaced.
  void quiesce();

  // Audio thread, around one module process() call.
  void beginBlock() noexcept; // installs pending buffers, resets the report
  const VoxSampleV2 *table() const noexcept { return table_.data(); }
  VoxSampleUsageV2 *usage() noexcept { return &usage_; }
  void endBlock(bool processed) noexcept; // publishes this block's report

private:
  struct Entry {
    std::uint64_t id{}; // > 0, increasing per instance (publish order)
    std::uint32_t slot{};
    std::unique_ptr<SampleBuffer> buffer; // null: the entry clears the slot
    VoxSampleV2 view{};
    // Controller only: epoch of the first valid report that showed a newer
    // install in this slot (0 = not yet replaced).
    std::uint64_t replacedAt{};
  };
  struct Report {
    std::uint64_t epoch{};
    bool valid{};
    bool overflow{};
    std::uint32_t count{};
    std::vector<std::uint64_t> current; // installed entry id per slot, 0 = none
    std::vector<std::uint64_t> ids;     // the module's usage ids
  };

  std::size_t collectLocked();
  bool acceptable(const Report &report);
  void releaseLocked(const Entry *entry);
  std::size_t liveInSlot(std::uint32_t slot) const noexcept;
  // Sample memory of the live entries, optionally without one entry.
  std::uint64_t liveBytes(const Entry *except = nullptr) const noexcept;
  static std::uint64_t bytesOf(const SampleBuffer *buffer) noexcept;

  const std::uint32_t slots_;
  const std::uint32_t capacity_;

  // Shared: one pending word per slot (controller and audio exchange it) and
  // the report triple buffer's index word (bits 0-1 middle buffer, bit 2
  // fresh).
  std::unique_ptr<std::atomic<Entry *>[]> pending_;
  static constexpr std::uint32_t fresh = 4u;
  std::atomic<std::uint32_t> triple_{1};
  std::array<Report, 3> reports_;

  // Audio thread only.
  std::vector<Entry *> current_;
  std::vector<VoxSampleV2> table_;
  std::vector<std::uint64_t> usageIds_;
  VoxSampleUsageV2 usage_{};
  std::uint64_t epoch_{};
  std::uint32_t write_{0};

  // Controller only, under mutex_.
  mutable std::mutex mutex_;
  std::uint32_t read_{2};
  std::uint64_t nextId_{};
  std::vector<std::unique_ptr<Entry>> live_;
  std::vector<std::uint64_t> scratch_;
  ReleaseSink sink_;
  SampleStats stats_{};
};

} // namespace vstengine::instrument::native
