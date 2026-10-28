#pragma once
#include "instrument/InstrumentAbi.h"
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace vstengine::instrument {

using SlotId = std::uint64_t;
using InstrumentId = std::string;
using ParameterId = std::string;
inline constexpr SlotId invalidSlotId = 0;
inline constexpr SlotId initialSlotId(std::size_t creationOrdinal) noexcept
{
    return 0x5654450000000001ULL + creationOrdinal;
}

inline constexpr std::uint32_t contractVersion = VOX_INSTRUMENT_ABI_V1;
inline constexpr std::uint32_t descriptorSchemaVersion =
    VOX_INSTRUMENT_DESCRIPTOR_SCHEMA_V1;
inline constexpr std::uint32_t stateSchemaVersion =
    VOX_INSTRUMENT_STATE_SCHEMA_V1;
inline constexpr std::uint32_t hostContractVersion = 1;
inline constexpr std::size_t maxSlots = 16;
// APVTS transports plain values as float. Every supported integer seed is
// exact.
inline constexpr std::uint32_t maxGenerationSeed = 0x00ffffffu;
inline constexpr std::size_t macrosPerSlot = 8;
// Fixed host pool (owner decision 27.10.2026): every slot exposes the same 64
// generic endpoints; an instrument's named parameters bind to them in
// descriptor order, so installing an instrument never changes the host layout.
inline constexpr std::size_t poolParametersPerSlot = 64;
inline constexpr std::size_t maxDirectParameters = poolParametersPerSlot;
inline constexpr std::size_t maxHostDirectParameters = 512;

struct ResourceBudget {
    std::uint32_t maxVoices {};
    std::uint32_t maxMidiEventsPerBlock {};
    std::uint32_t maxStateBytes {};
    std::uint32_t maxResourceBytes {};
    std::uint32_t maxLatencySamples {};
    std::uint32_t maxTailSamples {};
    std::uint32_t maxPatternEvents { 2048 };
    std::uint32_t maxModulationRoutes { 64 };
    std::uint32_t scratchBytes {};
};

enum class ParameterType : std::uint8_t { floating, integer, boolean, choice };
enum class TailPolicy : std::uint8_t { none, bounded, preserveOnBypass };
enum class ContentKind : std::uint8_t {
    soundPreset,
    patternPreset,
    generatorProfile
};
enum class ContentStatus : std::uint8_t {
    ok,
    unsupported,
    notFound,
    incompatible,
    invalidArgument,
    budgetExceeded
};
enum Capability : std::uint64_t {
    notes = 1ULL << 0,
    sequence = 1ULL << 1,
    patternGenerator = 1ULL << 2,
    modulation = 1ULL << 3,
    standardEditor = 1ULL << 4
};

struct ParameterDescriptor {
    ParameterId id;
    std::string name;
    std::string unit;
    float minimum {};
    float maximum { 1.0f };
    float defaultValue {};
    float step {};
    ParameterType type { ParameterType::floating };
    std::string group;
    std::vector<std::string> choices;
    bool automatable { true };
    bool modulatable {};
    std::int8_t preferredMacro { -1 };
};

struct InstrumentDescriptor {
    InstrumentId id;
    std::string providerId;
    std::string name;
    std::string vendor;
    std::uint32_t instrumentVersion { 1 };
    std::uint32_t abiVersion { contractVersion };
    std::uint32_t descriptorVersion { descriptorSchemaVersion };
    std::uint32_t stateVersion { stateSchemaVersion };
    std::uint32_t minimumHostVersion { hostContractVersion };
    std::uint32_t contentVersion { 1 };
    std::uint64_t capabilities { Capability::notes | Capability::standardEditor };
    TailPolicy tailPolicy { TailPolicy::bounded };
    std::vector<std::uint32_t> supportedPresetSchemaVersions { 1 };
    ResourceBudget budget;
    std::vector<ParameterDescriptor> parameters;
    // Internal host integration policy, not an extension of the external C ABI.
    bool directParameters{};
};

struct ParameterValue {
  ParameterId id;
  float value{};
};

struct ContentDescriptor {
    InstrumentId instrumentId;
    std::string id;
    std::string name;
    ContentKind kind { ContentKind::soundPreset };
    std::uint32_t schemaVersion { 1 };
    std::uint32_t contentVersion { 1 };
    std::uint32_t supportedSequenceFields { VOX_SEQUENCE_ALL };
    std::uint32_t supportedPresetClasses {};
    std::uint32_t macroValueMask {};
    std::array<float, macrosPerSlot> macroValues {};
    // Complete, named base values for a direct-parameter sound preset. The host
    // validates/prepares them transactionally; no live DSP read is required.
    std::vector<ParameterValue> parameterValues;
    // Optional versioned non-parameter sound state (e.g. resource identities).
    // Absence preserves it; present empty payload requests native Init. Named
    // bases remain authoritative and are applied after native preparation.
    std::optional<std::vector<std::byte>> nativeState;
    std::uint32_t nativeStateVersion{};
};

// Stable traversal-order-independent seed domain. SlotId is hashed as explicit
// little-endian bytes; std::hash and registry/index order are never involved.
inline constexpr std::uint32_t generationSubSeed(
    std::uint32_t globalSeed, SlotId slotId,
    std::string_view instrumentId) noexcept
{
    std::uint32_t hash = 2166136261u ^ globalSeed;
    for (std::size_t byte = 0; byte < sizeof(slotId); ++byte) {
        hash ^= static_cast<std::uint8_t>(slotId >> (byte * 8));
        hash *= 16777619u;
    }
    for (const auto character : instrumentId) {
        hash ^= static_cast<std::uint8_t>(character);
        hash *= 16777619u;
    }
    return hash;
}

enum class ResourceStatus { found, missing, corrupt, incompatible };
struct ResourceView {
    ResourceStatus status { ResourceStatus::missing };
    std::span<const std::byte> bytes;
    std::uint32_t contentVersion {};
};
class ResourceResolver {
public:
    virtual ~ResourceResolver() = default;
    // Called during construction/state preparation only; never audio callback.
    virtual ResourceView resolve(std::string_view resourceId) noexcept = 0;
};

struct PrepareSpec {
    double sampleRate {};
    std::uint32_t maximumBlockSize {};
    std::uint32_t outputChannels {};
};

struct ProcessBlock {
    std::span<float*> outputs;
    std::uint32_t sampleCount {};
    std::span<const VoxMidiEventV1> midi;
    double sampleRate {};
    double bpm {};
    double ppqPosition {};
    bool transportPlaying {};
};

class InstrumentInstance {
public:
    virtual ~InstrumentInstance() = default;
    virtual bool prepare(const PrepareSpec&) = 0;
    virtual void reset() noexcept = 0;
    virtual void suspend() noexcept = 0;
    virtual void resume() noexcept = 0;
    virtual void setBypassed(bool) noexcept = 0;
    virtual void process(const ProcessBlock&) noexcept = 0;
    virtual bool setParameter(std::string_view id, float plainValue) noexcept = 0;
    // Persistent sound state only. Runtime voices/envelopes never cross this boundary.
    virtual bool loadState(std::uint32_t schemaVersion,
                           std::span<const std::byte> payload) noexcept = 0;
    virtual bool saveState(std::span<std::byte> destination,
                           std::uint32_t& bytesWritten) const noexcept = 0;
    virtual std::uint32_t latencySamples() const noexcept = 0;
    virtual std::uint32_t tailSamples() const noexcept = 0;
};

struct CreateContext {
    SlotId slotId {};
    ResourceResolver* resources {};
};

class InstrumentProvider {
public:
    virtual ~InstrumentProvider() = default;
    virtual std::span<const InstrumentDescriptor> descriptors() const noexcept = 0;
    // Host calls create only outside realtime processing.
    virtual std::unique_ptr<InstrumentInstance> create(
        std::string_view instrumentId, const CreateContext&) = 0;
    // Optional control-thread musical-content service. Defaults preserve ABI
    // behavior for DSP-only providers. Future external providers adapt these
    // operations through the separate VoxInstrumentContentApiV1 POD table.
    virtual std::span<const ContentDescriptor> contentDescriptors(
        std::string_view) const noexcept { return {}; }
    virtual ContentStatus applySoundPreset(
        std::string_view, std::string_view,
        InstrumentInstance&) const noexcept
    {
        return ContentStatus::unsupported;
    }
    virtual ContentStatus generatePattern(
        std::string_view, std::string_view,
        const VoxGenerationContextV1&, const VoxPatternV1*,
        VoxPatternV1&) const noexcept
    {
        return ContentStatus::unsupported;
    }
};

enum class ResolutionStatus {
    resolved,
    missingModule,
    incompatibleAbi,
    incompatibleSchema,
    invalidDescriptor,
    budgetExceeded,
    constructionFailed
};

struct Resolution {
    ResolutionStatus status { ResolutionStatus::missingModule };
    const InstrumentDescriptor* descriptor {};
    InstrumentProvider* provider {};
    std::string diagnostic;
    explicit operator bool() const noexcept
    {
        return status == ResolutionStatus::resolved;
    }
};

} // namespace vstengine::instrument
