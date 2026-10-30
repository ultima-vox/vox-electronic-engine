#pragma once
#include "effects/EffectsChain.h"
#include "rack/RackRouter.h"
#include <array>
#include <vector>
namespace vstengine::rack {
// Fixed acyclic topology: slots -> four returns -> Main. No return-to-return or
// return-to-slot edges exist. Bus identities never depend on effect selection.
inline constexpr std::size_t sendBusCount = 4;
inline constexpr std::array<std::uint32_t, sendBusCount> sendBusIds{
    0x56585201, 0x56585202, 0x56585203, 0x56585204};
struct SendControls {
  float gain{};
  bool preFader{};
};
struct ReturnControls {
  effects::EffectType type{effects::EffectType::reverb};
  bool enabled{true};
  float level{0.5f}, pan{}, amount{0.5f}, rate{0.5f}, character{0.5f};
};
struct AudioRoutingControls {
  // Each route owns its stable recipient identity, independently of render
  // order.
  std::array<instrument::SlotId, instrument::maxSlots> routeSlotIds{};
  std::array<std::array<SendControls, sendBusCount>, instrument::maxSlots>
      sends{};
  std::array<ReturnControls, sendBusCount> returns{};
  // Explicit note-lane event trigger, not an audio detector/external sidechain.
  instrument::SlotId kickSource{};
  std::uint8_t kickNote{36};
  float attackMs{1}, holdMs{40}, releaseMs{120};
  std::array<float, instrument::maxSlots> duckDepth{};
};
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
             std::span<const RackRouter::DestinationBuffer>, std::uint32_t samples,
             std::span<const VoxNoteEventV2> generated = {},
             bool routedIgnored = false) noexcept;
  void mixSlot(std::size_t, std::span<float *>, std::span<float *>,
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
} // namespace vstengine::rack
