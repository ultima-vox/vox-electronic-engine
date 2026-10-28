#pragma once
#include "instrument/HostParameterSchema.h"
#include "instrument/ModuleState.h"
#include <algorithm>
#include <array>
#include <cstdint>

namespace vstengine::rack {

// One instrument = one MIDI channel: an enabled route owns its channel
// exclusively (validated in RackState; the processor resolves host conflicts).
enum class RouteMode : std::uint8_t { off, channel };

struct Routing {
    RouteMode mode { RouteMode::off };
    std::uint8_t channel { 1 };
    std::uint8_t keyLow { 0 };
    std::uint8_t keyHigh { 127 };
    std::uint8_t velocityLow { 1 };
    std::uint8_t velocityHigh { 127 };
    std::int8_t transpose {};
};

struct PersistentSlotState {
  std::uint32_t schemaVersion{6};
  instrument::SlotId slotId{instrument::invalidSlotId};
  instrument::InstrumentId instrumentId;
  std::string resolvedProviderId;
  std::uint32_t instrumentVersion{};
  std::uint32_t instrumentStateVersion{1};
  std::uint32_t contentVersion{};
  Routing routing;
  bool enabled{true};
  bool mute{};
  bool solo{};
  bool locked{};
  float level{1.0f};
  float pan{};
  std::uint32_t
      outputDestination{}; // 0 = Main Out; future buses stay separate.
  std::string soundPreset;
  std::string patternPreset;
  std::uint32_t patternSchemaVersion{VOX_PATTERN_SCHEMA_V1};
  std::vector<std::byte> patternPayload;
  std::array<float, instrument::macrosPerSlot> macros{};
  std::array<instrument::ParameterId, instrument::macrosPerSlot>
      macroAssignments{};
  std::vector<std::byte> modulationPayload;
  std::vector<std::byte> modulePayload;
  struct DirectValue {
    instrument::ParameterId id;
    float value{};
    // Pool endpoint the value was bound to when saved (-1: unknown, e.g. a
    // pre-schema-6 state). Append-only migration requires it to match.
    std::int16_t poolIndex{-1};
  };
  std::vector<DirectValue> directParameters;
};

struct RuntimeSlotState {
    instrument::ResolutionStatus resolution {
        instrument::ResolutionStatus::missingModule };
    std::uint64_t droppedMidiEvents {};
    std::uint32_t ownedNotes {};
};

// Fixed, allocation-free block snapshot. Host automation populates this from
// APVTS atomics; persistent strings/vectors stay off audio thread.
struct ProcessSlotControls {
    instrument::SlotId slotId { instrument::invalidSlotId };
    Routing routing;
    bool enabled { true };
    bool mute {};
    bool solo {};
    bool locked {};
    float level { 1.0f };
    float pan {};
    std::array<float, instrument::macrosPerSlot> macros {};
    struct DirectValue {
      std::uint64_t key{};
      float value{};
    };
    std::array<DirectValue, instrument::maxDirectParameters> directParameters{};
    std::size_t directParameterCount{};
    std::span<const DirectValue> directValues() const noexcept {
      return {directParameters.data(),
              std::min(directParameterCount, directParameters.size())};
    }
};

inline ProcessSlotControls processControls(const PersistentSlotState& state) noexcept
{
  ProcessSlotControls result{state.slotId, state.routing, state.enabled,
                             state.mute,   state.solo,    state.locked,
                             state.level,  state.pan,     state.macros};
  result.directParameterCount =
      std::min(state.directParameters.size(), result.directParameters.size());
  for (std::size_t i = 0; i < std::min(state.directParameters.size(),
                                       result.directParameters.size());
       ++i)
    result.directParameters[i] = {
        instrument::hostparams::directKey(state.instrumentId,
                                          state.directParameters[i].id),
        state.directParameters[i].value};
  return result;
}

inline bool valid(const Routing& routing) noexcept
{
    return (routing.mode == RouteMode::off || routing.mode == RouteMode::channel)
        && routing.channel >= 1 && routing.channel <= 16
        && routing.keyLow <= routing.keyHigh && routing.keyHigh <= 127
        && routing.velocityLow >= 1
        && routing.velocityLow <= routing.velocityHigh
        && routing.velocityHigh <= 127
        && routing.transpose >= -48 && routing.transpose <= 48;
}

} // namespace vstengine::rack
