#pragma once
// Host model of the V2 pattern contract (VoxModuleAbiV2.h). The Rack owns the
// durable Pattern of each slot; modules only see an immutable snapshot.
#include "instrument/abi/VoxModuleAbiV2.h"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace vstengine::pattern {

// What an instrument plays, as declared through VoxContentApiV2.
struct Caps {
  std::uint32_t playback{VOX_PATTERN_PLAYBACK_HOST};
  std::uint32_t laneCount{1};
  std::uint32_t maxStepsPerBar{VOX_PATTERN_V2_MAX_STEPS_PER_BAR};
  std::uint32_t maxBars{VOX_PATTERN_V2_MAX_BARS};
  std::uint32_t stepFields{};
  std::vector<std::string> laneNames;
  std::vector<std::uint8_t> laneNotes; // INTERNAL playback only
};

// Fixed-capacity pattern: no allocation after construction, so a snapshot can
// be copied without touching the heap. Cells are [lane][bar][step].
struct Pattern {
  std::uint32_t laneCount{1};
  std::uint32_t stepsPerBar{16};
  std::uint32_t barCount{1};
  std::uint32_t timing{VOX_TIMING_SIXTEENTH};
  std::array<VoxPatternStepV2, VOX_PATTERN_V2_MAX_CELLS> cells{};

  [[nodiscard]] std::size_t cellCount() const noexcept {
    return static_cast<std::size_t>(laneCount) * stepsPerBar * barCount;
  }
  [[nodiscard]] std::size_t index(std::uint32_t lane, std::uint32_t bar,
                                  std::uint32_t step) const noexcept {
    return (static_cast<std::size_t>(lane) * barCount + bar) * stepsPerBar + step;
  }
  [[nodiscard]] VoxPatternStepV2 &at(std::uint32_t lane, std::uint32_t bar,
                                     std::uint32_t step) noexcept {
    return cells[index(lane, bar, step)];
  }
  [[nodiscard]] const VoxPatternStepV2 &at(std::uint32_t lane, std::uint32_t bar,
                                           std::uint32_t step) const noexcept {
    return cells[index(lane, bar, step)];
  }
  // ABI view for a module call. The module must not write through it.
  [[nodiscard]] VoxPatternV2 view() const noexcept;
};

[[nodiscard]] bool operator==(const Pattern &, const Pattern &) noexcept;

[[nodiscard]] VoxPatternStepV2 defaultStep() noexcept;
// All-rest pattern with the instrument's lanes, one bar of 16 steps (or fewer
// when the instrument supports fewer).
[[nodiscard]] Pattern emptyPattern(const Caps &) noexcept;

// The same pattern with other dimensions: every cell that exists in both
// keeps its value; new cells are rests. Lane count is unchanged.
void reshape(const Pattern &source, std::uint32_t stepsPerBar, std::uint32_t barCount,
             Pattern &out) noexcept;
// Step length of a VOX_TIMING_* value, in quarter notes.
[[nodiscard]] double stepLengthPpq(std::uint32_t timing) noexcept;

[[nodiscard]] bool validateCaps(const Caps &, std::string &why);
// Every cell inside the dimensions is in range and holds defaults in the
// fields the instrument does not read; cells outside are default.
[[nodiscard]] bool validate(const Pattern &, const Caps &, std::string &why);

// Imports a module-declared caps table. Strict: unknown bits, unbounded
// strings or duplicate lane notes reject the instrument.
[[nodiscard]] bool importCaps(const VoxPatternCapsV2 *, Caps &, std::string &why);
// Copies a module-written view (with its stride) into `out`; validates it.
[[nodiscard]] bool fromView(const VoxPatternV2 &, const Caps &, Pattern &out,
                            std::string &why);

// Durable encoding stored in the Rack slot state ("VPT2", version 1).
inline constexpr std::uint32_t encodingVersion = 1;
inline constexpr std::size_t encodedCellBytes = 10;
inline constexpr std::size_t encodedHeaderBytes = 12;
inline constexpr std::size_t maximumEncodedBytes =
    encodedHeaderBytes + VOX_PATTERN_V2_MAX_CELLS * encodedCellBytes;
void encode(const Pattern &, std::vector<std::byte> &destination);
[[nodiscard]] bool decode(std::span<const std::byte>, const Caps &, Pattern &out,
                          std::string &why);
[[nodiscard]] std::uint32_t fingerprint(const Pattern &) noexcept;

// Factory patterns an instrument ships, validated at module load.
struct FactoryPattern {
  std::string name;
  std::shared_ptr<const Pattern> pattern;
};
struct Content {
  Caps caps;
  std::vector<FactoryPattern> factory;
};

// Message thread -> audio thread hand-off of immutable pattern snapshots.
// Triple buffer: the writer only fills the buffer that neither the reader nor
// the pending slot holds, so a rapid series of edits never overwrites a
// snapshot the audio thread is reading. Lock- and allocation-free on both
// sides once constructed.
class SnapshotBuffer final {
public:
  SnapshotBuffer();
  SnapshotBuffer(const SnapshotBuffer &) = delete;
  SnapshotBuffer &operator=(const SnapshotBuffer &) = delete;

  // Message thread (single writer). The generation strictly increases.
  void publish(const Pattern &) noexcept;
  // Audio thread (single reader). Picks up the newest published snapshot, if
  // any, and returns the one now held; it stays valid until the next acquire.
  // Returns nullptr until something has been published.
  [[nodiscard]] const Pattern *acquire(std::uint64_t &generation) noexcept;

private:
  static constexpr std::uint32_t fresh = 4u;
  struct Slot {
    Pattern pattern;
    std::uint64_t generation{};
  };
  std::unique_ptr<std::array<Slot, 3>> slots_;
  std::atomic<std::uint32_t> middle_{1};
  std::uint32_t back_{2};  // writer only
  std::uint32_t front_{0}; // reader only
  std::uint64_t written_{}; // writer only
  bool published_{};        // reader only
};

} // namespace vstengine::pattern
