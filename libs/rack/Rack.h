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
  // Per-slot runtime diagnostics for the controller thread (a support view
  // for "silence in Cubase" reports). Counters are bounded atomics the audio
  // thread only increments; nothing here allocates, locks or logs in
  // process(). Counters restart when the slot's instrument is (re)built.
  struct SlotDiagnostics {
    instrument::SlotId slotId{};
    std::string instrumentId, name, publisherId;
    std::uint32_t instrumentVersion{};
    instrument::ResolutionStatus resolution{};
    bool active{};   // a playable instance is installed
    bool bypassed{}; // disabled, muted or solo-excluded in the last block
    instrument::InstanceHealth health;
    std::uint64_t droppedMidiEvents{};        // in the last block
    std::uint64_t droppedMidiEventsTotal{};   // since the instrument was built
    std::uint64_t noteOutputOverflowBlocks{}; // generated notes beyond the buffer
    // Why the last load/restore of this slot failed, or why the accepted
    // slot stays unresolved (missing module, other publisher, ...); empty
    // when its instrument resolved.
    std::string lastError;
  };
  [[nodiscard]] std::array<SlotDiagnostics, instrument::maxSlots> diagnostics() const;
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
  // Rack-owned V2 pattern of an INTERNAL-playback instrument (controller
  // thread). setPattern validates against the slot's caps, stores the VPT2
  // payload and publishes a snapshot to the audio thread; patternPreset
  // records the factory name, or is cleared for a user edit.
  bool setPattern(std::size_t slotIndex, const pattern::Pattern &,
                  std::string &diagnostic);
  bool loadFactoryPattern(std::size_t slotIndex, std::size_t factoryIndex,
                          std::string &diagnostic);
  [[nodiscard]] bool pattern(std::size_t slotIndex, pattern::Pattern &out) const;
  // One pass of an INTERNAL slot's pattern, played offline by a fresh
  // instance of the slot's own module with the slot's saved values (no
  // automation or modulation): exactly the notes it plays. Controller thread.
  struct RenderedHit {
    double ppq{};
    std::uint8_t note{}, velocity{}, lane{};
    bool accent{};
  };
  bool renderPatternHits(std::size_t slotIndex, const PersistentSlotState &slot,
                         std::vector<RenderedHit> &hits, double &lengthPpq,
                         std::string &diagnostic);
  static constexpr std::size_t noteEventCapacity = 512;
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
        // INTERNAL-playback instruments: snapshots of the Rack-owned pattern
        // and the notes the instrument reports per block.
        std::unique_ptr<pattern::SnapshotBuffer> patterns;
        std::vector<VoxNoteEventV2> noteEvents;
        VoxNoteOutputV2 noteOutput{};
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
    struct SlotCounters {
      std::atomic<std::uint64_t> droppedMidiEvents{}, noteOutputOverflowBlocks{};
      std::atomic<bool> bypassed{};
      void clear() noexcept {
        droppedMidiEvents.store(0, std::memory_order_relaxed);
        noteOutputOverflowBlocks.store(0, std::memory_order_relaxed);
        bypassed.store(false, std::memory_order_relaxed);
      }
    };
    std::array<SlotCounters, instrument::maxSlots> counters_{};
    // Controller thread, under persistentStateMutex_.
    std::array<std::string, instrument::maxSlots> lastError_{};
    // Large bounded processing state is constructed once off audio. Keeping it
    // out of Rack's inline footprint leaves room on a host's 1 MiB thread
    // stack.
    struct ProcessingStorage {
      std::array<SlotRuntime, instrument::maxSlots> runtime{};
      std::array<RackRouter::DestinationBuffer, instrument::maxSlots> routed{};
      std::array<ProcessSlotControls, instrument::maxSlots> defaultControls{};
      RackRouter router;
    };
    std::unique_ptr<ProcessingStorage> processing_{
        std::make_unique<ProcessingStorage>()};
    // Registry descriptors have process lifetime; only pointer publication is
    // mutable. UI metadata reads never race runtime replacement or lock audio.
    std::array<std::atomic<const instrument::InstrumentDescriptor *>,
               instrument::maxSlots>
        publishedDescriptors_{};
    mutable std::atomic<bool> mutationGate_ { false };
    mutable std::atomic<std::uint32_t> activeProcessors_ { 0 };
    // Non-realtime state serialization only. Audio callback never locks.
    mutable std::mutex persistentStateMutex_;
};

static_assert(
    sizeof(Rack) <= 64u * 1024u,
    "Rack must leave room for nested lifecycle/callback frames on host stacks");

} // namespace vstengine::rack
