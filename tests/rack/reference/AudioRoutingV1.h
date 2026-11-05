// Frozen copy of the pre-optimisation (98782ea) AudioRouting class, for differential
// equivalence tests only (review 6647774929). Never linked into the plugin.
#pragma once
#include "rack/AudioRouting.h"

namespace vstengine::rack::reference {
class AudioRouting final {
public:
  bool prepare(const instrument::PrepareSpec &);
  void reset() noexcept;
  static bool valid(const AudioRoutingControls &) noexcept;
  // Builds this block's duck envelope. Triggers are the kick source's routed
  // MIDI notes and the notes it generated itself this block (`generated`,
  // e.g. a drum module's internal sequencer), so the Rack must call this after
  // the source has rendered. A kick that is both routed MIDI and generated at
  // the same sample triggers once. `routedIgnored`: the source reported
  // (validated VOX_ROUTED_NOTES_ALL_IGNORED) that none of its routed NoteOns
  // sounded this block, so only `generated` can trigger.
  bool begin(const AudioRoutingControls &, std::span<const ProcessSlotControls>,
             std::span<const ::vstengine::rack::RackRouter::DestinationBuffer>, std::uint32_t samples,
             std::span<const VoxNoteEventV2> generated = {},
             bool routedIgnored = false) noexcept;
  // `moduleSends`: the slot module's SEND buses. Each goes only into its
  // return, after the slot's ducking and gated like the slot, never scaled by
  // the slot level and never into Main.
  void mixSlot(std::size_t, std::span<float *>, std::span<float *>,
               const ProcessSlotControls &, bool audible,
               std::uint32_t, const ModuleSends *moduleSends = nullptr) noexcept;
  // B2: one OUTPUT bus of a slot into its host output, through the same slot
  // chain as mixSlot (ducking, level/pan, audibility); feeds the slot's own
  // pre/post sends, so they tap Main plus every OUTPUT bus. Module SEND buses
  // stay with mixSlot (once per slot).
  void mixSlotOutput(std::size_t, std::span<float *> input, std::span<float *> output,
                     const ProcessSlotControls &, bool audible,
                     std::uint32_t) noexcept;
  void finish(std::span<float *>, std::uint32_t) noexcept;
  [[nodiscard]] std::uint32_t tailSamples() const noexcept;
  [[nodiscard]] float gainReduction() const noexcept { return envelope_; }

private:
  struct ReturnRuntime {
    effects::EffectsChain chain;
    std::array<std::vector<float>, 2> audio;
    std::array<float *, 2> pointers{};
    effects::EffectType previousType{effects::EffectType::reverb};
    bool wasEnabled{};
  };
  bool prepared_{}, blockReady_{};
  std::uint32_t blockSamples_{};
  instrument::PrepareSpec spec_{};
  AudioRoutingControls controls_{};
  std::array<ReturnRuntime, sendBusCount> returns_;
  std::vector<float> envelopeBuffer_;
  float envelope_{}, attackStart_{};
  std::uint32_t phasePosition_{}, phaseLength_{};
  enum class Phase { idle, attack, hold, release } phase_{Phase::idle};
  instrument::SlotId previousSource_{};
  std::uint8_t previousNote_{36};
};
} // namespace vstengine::rack::reference
