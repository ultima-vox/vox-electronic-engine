#pragma once
#include "instrument/InstrumentRegistry.h"
#include "modulation/ModulationState.h"
#include "rack/RackRouter.h"
#include <array>
#include <atomic>
#include <mutex>

namespace vstengine::rack {
class AudioRouting;
struct AudioRoutingControls;

class Rack final {
public:
  struct SlotSummary {
    instrument::SlotId slotId{};
    std::string instrumentId, name, vendor;
    std::uint32_t instrumentVersion{};
    instrument::ResolutionStatus resolution{};
  };
  [[nodiscard]] std::array<SlotSummary, instrument::maxSlots> summaries() const;
  explicit Rack(instrument::InstrumentRegistry &);
  bool prepare(const instrument::PrepareSpec &);
  void reset() noexcept;
  bool
  replaceState(const std::array<PersistentSlotState, instrument::maxSlots> &,
               instrument::ResourceResolver *, std::string &diagnostic);
  bool loadModule(std::size_t slotIndex, std::string_view instrumentId,
                  instrument::ResourceResolver *, std::string &diagnostic);
  bool applySoundPreset(std::size_t slotIndex, std::string_view presetId,
                        instrument::ResourceResolver *,
                        std::string &diagnostic);
  bool updatePatternState(std::size_t slotIndex, std::string presetId,
                          std::uint32_t schemaVersion,
                          std::vector<std::byte> payload);
  bool configureModulation(std::size_t slotIndex,
                           std::span<const modulation::Route> routes,
                           std::string &diagnostic);
  void process(std::span<float *> outputs, std::uint32_t sampleCount,
               std::span<const VoxMidiEventV1> midi, double bpm,
               double ppqPosition, bool playing,
               std::span<const VoxMidiEventV1> audition = {},
               instrument::SlotId selectedSlot = instrument::invalidSlotId,
               std::span<const ProcessSlotControls> controls = {},
               std::span<const RackRouter::GeneratedSlotEvents> generated = {},
               AudioRouting *audioRouting = nullptr,
               const AudioRoutingControls *audioControls = nullptr) noexcept;
  // Non-realtime defaults: may wait for an active callback. DSP applies the
  // change at the next block; explicit ProcessSlotControls take precedence.
  // Host automation uses process() block snapshots, never these setters.
  void setMacro(std::size_t slotIndex, std::size_t macroIndex,
                float value) noexcept;
  void updateControls(std::size_t slotIndex, Routing, bool enabled, bool mute,
                      bool solo, bool locked, float level, float pan) noexcept;
  // Borrowed views require caller serialization against control mutations.
  [[nodiscard]] const auto &state() const noexcept { return state_; }
  [[nodiscard]] std::array<PersistentSlotState, instrument::maxSlots>
  snapshotState(std::span<const ProcessSlotControls> controls = {}) const;
  // Non-realtime metadata snapshot; takes the control-state mutex.
  [[nodiscard]] std::array<RuntimeSlotState, instrument::maxSlots>
  runtimeState() const noexcept;
  [[nodiscard]] const instrument::InstrumentDescriptor *
  descriptor(std::size_t slotIndex) const noexcept;
  // Query only while lifecycle/module mutations are externally serialized.
  [[nodiscard]] std::uint32_t latencySamples() const noexcept;
  [[nodiscard]] std::uint32_t tailSamples() const noexcept;

private:
    struct ModulationRuntime {
        modulation::DestinationRegistry registry;
        modulation::ModulationMatrix matrix;
        modulation::SourceValues sources;
        std::array<float, modulation::maxDestinations> baseNormalized {};
        std::array<double, modulation::maxLfos> lfoPhase {};
        std::uint32_t randomState { 0x6d2b79f5u };
        std::uint32_t initialRandomState { 0x6d2b79f5u };
        std::int64_t sampleHoldStep { -1 };
    };
    struct SlotRuntime {
        std::unique_ptr<instrument::InstrumentInstance> instance;
        const instrument::InstrumentDescriptor* descriptor {};
        std::array<std::vector<float>, 2> audio;
        std::array<float*, 2> pointers {};
        std::unique_ptr<ModulationRuntime> modulation;
        struct DirectBinding {
          std::uint64_t key{};
          const instrument::ParameterDescriptor *parameter{};
          float base{};
        };
        std::array<DirectBinding, instrument::maxDirectParameters> direct{};
        std::size_t directCount{};
    };
    bool buildSlot(std::size_t, const PersistentSlotState&,
                   instrument::ResourceResolver*, SlotRuntime&,
                   RuntimeSlotState&, std::string& diagnostic);
    void beginExclusive() const noexcept;
    void endExclusive() const noexcept;
    static void applyModulation(SlotRuntime&, const PersistentSlotState&,
                                const ProcessSlotControls&,
                                const RackRouter::DestinationBuffer&,
                                std::uint32_t sampleCount, double sampleRate,
                                double bpm, double ppq) noexcept;

    instrument::InstrumentRegistry& registry_;
    instrument::PrepareSpec spec_;
    std::array<PersistentSlotState, instrument::maxSlots> state_ {};
    std::array<RuntimeSlotState, instrument::maxSlots> runtimeState_ {};
    std::array<std::atomic<std::uint64_t>, instrument::maxSlots>
        droppedMidiEvents_ {};
    std::array<SlotRuntime, instrument::maxSlots> runtime_ {};
    // Registry descriptors have process lifetime; only pointer publication is
    // mutable. UI metadata reads never race runtime replacement or lock audio.
    std::array<std::atomic<const instrument::InstrumentDescriptor *>,
               instrument::maxSlots>
        publishedDescriptors_{};
    std::array<RackRouter::DestinationBuffer, instrument::maxSlots> routed_ {};
    std::array<ProcessSlotControls, instrument::maxSlots> defaultControls_ {};
    RackRouter router_;
    mutable std::atomic<bool> mutationGate_ { false };
    mutable std::atomic<std::uint32_t> activeProcessors_ { 0 };
    // Non-realtime state serialization only. Audio callback never locks.
    mutable std::mutex persistentStateMutex_;
};

} // namespace vstengine::rack
