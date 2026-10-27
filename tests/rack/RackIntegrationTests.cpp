#include "modules/BuiltInProvider.h"
#include "rack/Rack.h"
#include "rack/RackState.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <numeric>
#include <thread>

namespace {
int run = 0;
#define REQUIRE(c, m) do { ++run; if (!(c)) { std::cerr << "FAILED: " << m \
    << " line " << __LINE__ << '\n'; return EXIT_FAILURE; } } while (false)
VoxMidiEventV1 noteOn(int channel, int note)
{
    return { 0, 3, { static_cast<std::uint8_t>(0x90 | (channel - 1)),
                      static_cast<std::uint8_t>(note), 110 } };
}
double energy(const std::vector<float>& audio)
{
    return std::inner_product(audio.begin(), audio.end(), audio.begin(), 0.0);
}
double waveformDifference(const std::vector<float>& first,
                          const std::vector<float>& second)
{
    double result {};
    for (std::size_t index = 0; index < first.size(); ++index)
        result += std::abs(first[index] - second[index]);
    return result;
}
std::vector<float> render(vstengine::rack::Rack& rack,
                          std::span<const VoxMidiEventV1> midi)
{
    std::vector<float> left(2048), right(2048);
    std::array<float*, 2> outputs { left.data(), right.data() };
    rack.process(outputs, static_cast<std::uint32_t>(left.size()), midi,
                 145.0, 0.0, true);
    return left;
}
} // namespace

int main()
{
    using namespace vstengine;
    instrument::InstrumentRegistry registry;
    std::string diagnostic;
    REQUIRE(registry.registerProvider(modules::createBuiltInProvider(), diagnostic),
            "built-in provider registers");
    REQUIRE(registry.descriptors().size() == 5,
            "all five production instrument families registered");

    rack::Rack rack(registry);
    REQUIRE(rack.prepare({ 48000.0, 2048, 2 }), "rack prepares");
    auto state = rack.state();
    state[0].instrumentId = modules::bassInstrumentId;
    state[0].routing = { rack::RouteMode::channel, 1, 0, 127, 1, 127, 0 };
    state[0].macros = { 0.45f, 0.6f, 0.5f, 0.25f, 0.35f, 0.5f, 0.0f, 0.1f };
    state[1].instrumentId = modules::acidInstrumentId;
    state[1].routing = { rack::RouteMode::channel, 2, 0, 127, 1, 127, 0 };
    state[1].macros[0] = 0.5f;
    state[1].macros[1] = 0.7f;
    REQUIRE(rack.replaceState(state, nullptr, diagnostic),
            "rack state loads transactionally");
    REQUIRE(rack.descriptor(0)->id == modules::bassInstrumentId
                && rack.descriptor(1)->id == modules::acidInstrumentId,
            "slots resolve through descriptor boundary");

    const std::array bassMidi { noteOn(1, 36) };
    rack.reset();
    const auto bass = render(rack, bassMidi);
    REQUIRE(energy(bass) > 1.0e-5, "CH1 renders Bass module");
    const std::array referenceMidi { noteOn(2, 60) };
    rack.reset();
    const auto reference = render(rack, referenceMidi);
    REQUIRE(energy(reference) > 1.0e-5, "CH2 renders Acid module");
    double difference = 0.0;
    for (std::size_t i = 0; i < bass.size(); ++i)
        difference += std::abs(bass[i] - reference[i]);
    REQUIRE(difference > 1.0, "independent modules produce different renders");

    const std::array wrongMidi { noteOn(3, 48) };
    rack.reset();
    REQUIRE(energy(render(rack, wrongMidi)) < 1.0e-12,
            "wrong channel reaches no engine");

    // Shared modulation runs inside each production slot and persists with
    // SlotId/InstrumentId state. Envelope source is driven by routed NoteOn.
    const std::array moduleIds { modules::bassInstrumentId,
        modules::acidInstrumentId, modules::leadInstrumentId,
        modules::semanticFxInstrumentId, modules::atmosInstrumentId };
    for (std::size_t slot = 2; slot < moduleIds.size(); ++slot) {
        REQUIRE(rack.loadModule(slot, moduleIds[slot], nullptr, diagnostic),
                "production module loads for modulation integration");
        auto routing = rack.state()[slot].routing;
        routing.mode = rack::RouteMode::channel;
        routing.channel = static_cast<std::uint8_t>(slot + 1);
        rack.updateControls(slot, routing, true, false, false, false, 1.0f,
                            0.0f);
    }
    for (std::size_t slot = 0; slot < moduleIds.size(); ++slot) {
        const auto* descriptor = rack.descriptor(slot);
        REQUIRE(descriptor != nullptr, "modulation target module resolved");
        const auto parameter = std::find_if(descriptor->parameters.begin(),
            descriptor->parameters.end(), [](const auto& candidate) {
                return candidate.modulatable;
            });
        REQUIRE(parameter != descriptor->parameters.end(),
                "module exposes real modulation destination");
        const modulation::Route route {
            { modulation::SourceKind::envelope, 0 },
            modulation::SourceTransform::direct,
            modulation::destinationKey(descriptor->id, parameter->id), -0.35f
        };
        rack.reset();
        const std::array midi { noteOn(static_cast<int>(slot + 1),
                                       slot == 0 ? 36 : 60) };
        const auto unmodulated = render(rack, midi);
        REQUIRE(rack.configureModulation(slot, { &route, 1 }, diagnostic),
                "per-slot modulation route configures");
        rack.reset();
        const auto modulated = render(rack, midi);
        REQUIRE(energy(modulated) > 1.0e-9,
                "modulated production module renders finite audio");
        REQUIRE(waveformDifference(unmodulated, modulated) > 1.0e-4,
                "route materially changes production module DSP");
        REQUIRE(!rack.state()[slot].modulationPayload.empty(),
                "modulation graph captured in slot state");
    }
    const auto modulatedSerialized = rack::state::serialize(rack.state());
    std::array<rack::PersistentSlotState, instrument::maxSlots>
        modulatedDecoded;
    REQUIRE(rack::state::deserialize(modulatedSerialized, modulatedDecoded,
                                     diagnostic),
            "modulation-bearing rack state parses");
    for (std::size_t slot = 0; slot < moduleIds.size(); ++slot)
        REQUIRE(modulatedDecoded[slot].modulationPayload
                    == rack.state()[slot].modulationPayload,
                "per-slot modulation graph round-trips canonically");
    auto modulationRestored = std::make_unique<rack::Rack>(registry);
    REQUIRE(modulationRestored->prepare({ 48000.0, 2048, 2 })
                && modulationRestored->replaceState(modulatedDecoded, nullptr,
                                                     diagnostic),
            "modulation graph restores transactionally for all engines");

    // One instrument = one MIDI channel: a second owner of CH1 is rejected
    // and the previous state stays active.
    auto sharedChannel = state;
    sharedChannel[1].routing = { rack::RouteMode::channel, 1, 48, 72, 80, 127, 0 };
    REQUIRE(!rack.replaceState(sharedChannel, nullptr, diagnostic)
                && diagnostic == "duplicate rack MIDI channel"
                && rack.state()[1].routing.channel == 2,
            "duplicate channel owner is rejected transactionally");

    // Key/velocity zones remain a filter on the slot's own channel.
    state[1].routing = { rack::RouteMode::channel, 2, 48, 72, 80, 127, 0 };
    REQUIRE(rack.replaceState(state, nullptr, diagnostic), "zoned own-channel state loads");
    rack.reset();
    const std::array zonedMidi { noteOn(2, 60) };
    REQUIRE(energy(render(rack, zonedMidi)) > energy(reference) * 0.1,
            "note inside the zone renders on the slot channel");
    rack.reset();
    const std::array outsideZone { noteOn(2, 30) };
    REQUIRE(energy(render(rack, outsideZone)) < 1.0e-12,
            "note outside the zone reaches no engine");

    const auto beforeInvalid = rack.state()[0].instrumentId;
    auto invalid = rack.state();
    invalid[0].routing.keyLow = 100;
    invalid[0].routing.keyHigh = 20;
    REQUIRE(!rack.replaceState(invalid, nullptr, diagnostic),
            "invalid state rejected");
    REQUIRE(rack.state()[0].instrumentId == beforeInvalid,
            "failed state load leaves live rack untouched");

    REQUIRE(rack.loadModule(1, "com.ultimavox.missing", nullptr, diagnostic),
            "missing module commits as unresolved state");
    REQUIRE(rack.runtimeState()[1].resolution
                == instrument::ResolutionStatus::missingModule,
            "missing module status explicit");
    REQUIRE(rack.state()[1].instrumentId == "com.ultimavox.missing",
            "missing InstrumentId preserved without fallback");
    const auto serialized = rack::state::serialize(rack.state());
    std::array<rack::PersistentSlotState, instrument::maxSlots> decoded;
    REQUIRE(rack::state::deserialize(serialized, decoded, diagnostic),
            "canonical rack state parses");
    REQUIRE(rack::state::serialize(decoded).toXmlString()
                == serialized.toXmlString(),
            "save-load-save serialization canonical");
    REQUIRE(decoded[1].instrumentId == "com.ultimavox.missing",
            "unresolved identity survives serialization");

    auto patternedState = rack.state();
    patternedState[0].patternPreset = "Acid Pattern A";
    patternedState[0].patternSchemaVersion = VOX_PATTERN_SCHEMA_V1;
    patternedState[0].patternPayload = {
        std::byte { 0x00 }, std::byte { 0x56 }, std::byte { 0xff },
        std::byte { 0x10 }, std::byte { 0x00 }
    };
    const auto patterned = rack::state::serialize(patternedState);
    REQUIRE(rack::state::deserialize(patterned, decoded, diagnostic),
            "pattern-bearing rack state parses");
    REQUIRE(decoded[0].patternPreset == "Acid Pattern A"
                && decoded[0].patternSchemaVersion == VOX_PATTERN_SCHEMA_V1
                && decoded[0].patternPayload == patternedState[0].patternPayload,
            "pattern identity schema and opaque bytes round-trip");
    REQUIRE(rack::state::serialize(decoded).toXmlString()
                == patterned.toXmlString(),
            "pattern-bearing save-load-save is canonical");

    auto versionOne = patterned.createCopy();
    versionOne.setProperty("schemaVersion", 1, nullptr);
    for (int childIndex = 0; childIndex < versionOne.getNumChildren();
         ++childIndex) {
        auto child = versionOne.getChild(childIndex);
        child.setProperty("schemaVersion", 1, nullptr);
        child.removeProperty("patternSchemaVersion", nullptr);
        child.removeProperty("patternPayload", nullptr);
    }
    REQUIRE(rack::state::deserialize(versionOne, decoded, diagnostic),
            "rack schema v1 migrates to current schema");
    REQUIRE(decoded[0].schemaVersion == rack::state::schemaVersion
                && decoded[0].patternSchemaVersion == VOX_PATTERN_SCHEMA_V1
                && decoded[0].patternPayload.empty()
                && decoded[0].patternPreset == "Acid Pattern A",
            "v1 migration defaults empty v1 payload without losing reference");
    const auto migrated = rack::state::serialize(decoded);
    REQUIRE(static_cast<int>(migrated.getProperty("schemaVersion"))
                    == rack::state::schemaVersion
                && migrated.getChild(0).hasProperty("patternSchemaVersion")
                && migrated.getChild(0).hasProperty("patternPayload"),
            "v1 migration serializes canonical current pattern fields");

    for (const auto& [property, raw] : std::array {
             std::pair { "routeMode", -1 }, std::pair { "routeMode", 2 },
             std::pair { "routeMode", 3 },
             std::pair { "channel", 0 }, std::pair { "channel", 17 },
             std::pair { "keyLow", -1 }, std::pair { "keyLow", 128 },
             std::pair { "keyHigh", -1 }, std::pair { "keyHigh", 128 },
             std::pair { "velocityLow", 0 },
             std::pair { "velocityLow", 128 },
             std::pair { "velocityHigh", 0 },
             std::pair { "velocityHigh", 128 },
             std::pair { "transpose", -49 },
             std::pair { "transpose", 49 } }) {
        auto invalidRoute = patterned.createCopy();
        invalidRoute.getChild(0).setProperty(property, raw, nullptr);
        REQUIRE(!rack::state::deserialize(invalidRoute, decoded, diagnostic),
                "raw routing integer outside contract rejected before narrowing");
    }
    auto overflowedRoute = patterned.createCopy();
    overflowedRoute.getChild(0).setProperty("channel", "2147483648", nullptr);
    REQUIRE(!rack::state::deserialize(overflowedRoute, decoded, diagnostic),
            "overflowed raw routing integer rejected before narrowing");

    auto incompatiblePattern = patternedState;
    incompatiblePattern[0].patternSchemaVersion = VOX_PATTERN_SCHEMA_V1 + 1;
    incompatiblePattern[0].patternPayload = {
        std::byte { 0xde }, std::byte { 0xad }, std::byte { 0xbe },
        std::byte { 0xef }
    };
    auto incompatibleRack = std::make_unique<rack::Rack>(registry);
    REQUIRE(incompatibleRack->prepare({ 48000.0, 2048, 2 }),
            "incompatible-pattern rack prepares");
    REQUIRE(incompatibleRack->replaceState(incompatiblePattern, nullptr,
                                           diagnostic),
            "newer pattern schema commits as unresolved state");
    REQUIRE(incompatibleRack->runtimeState()[0].resolution
                    == instrument::ResolutionStatus::incompatibleSchema
                && incompatibleRack->descriptor(0) == nullptr,
            "newer pattern schema cannot instantiate instrument silently");
    REQUIRE(incompatibleRack->state()[0].patternSchemaVersion
                    == VOX_PATTERN_SCHEMA_V1 + 1
                && incompatibleRack->state()[0].patternPayload
                    == incompatiblePattern[0].patternPayload,
            "incompatible pattern schema and opaque payload remain recoverable");
    const auto incompatibleEncoded = rack::state::serialize(
        incompatibleRack->state());
    REQUIRE(rack::state::deserialize(incompatibleEncoded, decoded, diagnostic)
                && decoded[0].patternSchemaVersion == VOX_PATTERN_SCHEMA_V1 + 1
                && decoded[0].patternPayload
                    == incompatiblePattern[0].patternPayload,
            "incompatible pattern state survives canonical persistence");

    auto malformed = serialized.createCopy();
    malformed.getChild(0).setProperty("instrumentId", "Invalid/Instrument", nullptr);
    REQUIRE(!rack::state::deserialize(malformed, decoded, diagnostic),
            "invalid persisted InstrumentId rejected");
    malformed = serialized.createCopy();
    malformed.getChild(0).setProperty("macroMap1", "Invalid Parameter", nullptr);
    REQUIRE(!rack::state::deserialize(malformed, decoded, diagnostic),
            "invalid persisted macro ParameterId rejected");
    malformed = serialized.createCopy();
    malformed.getChild(0).setProperty("instrumentVersion", "4294967296", nullptr);
    REQUIRE(!rack::state::deserialize(malformed, decoded, diagnostic),
            "overflowed uint32 metadata rejected");
    malformed = serialized.createCopy();
    malformed.getChild(0).setProperty("contentVersion", -1, nullptr);
    REQUIRE(!rack::state::deserialize(malformed, decoded, diagnostic),
            "negative unsigned metadata rejected");
    malformed = serialized.createCopy();
    const auto tooLargeBase64 = juce::String::repeatedString(
        "A", static_cast<int>(4u * ((rack::state::maxModulePayloadBytes + 2u)
                                    / 3u) + 1u));
    malformed.getChild(1).setProperty("modulePayload", tooLargeBase64, nullptr);
    REQUIRE(!rack::state::deserialize(malformed, decoded, diagnostic),
            "oversized unresolved module payload rejected before decode");

    auto nonCanonical = serialized.createCopy();
    nonCanonical.setProperty("unknownRootField", 99, nullptr);
    nonCanonical.getChild(0).setProperty("unknownSlotField", "ignored", nullptr);
    REQUIRE(rack::state::deserialize(nonCanonical, decoded, diagnostic),
            "unknown fields parse without changing known state");
    REQUIRE(rack::state::serialize(decoded).toXmlString()
                == serialized.toXmlString(),
            "canonical serialization strips unknown fields");
    rack.reset();
    REQUIRE(energy(render(rack, bassMidi)) > 1.0e-5,
            "unresolved slot cannot leak into working Bass slot");

    rack.reset();
    const auto first = render(rack, bassMidi);
    rack.reset();
    const auto second = render(rack, bassMidi);
    REQUIRE(first == second, "rack reset render deterministic");
    REQUIRE(rack.latencySamples() == 0 && rack.tailSamples() > 0,
            "rack aggregates latency and tail");

    // Audio publishes diagnostic counters atomically for UI polling.
    const VoxMidiEventV1 modulationEvent { 0, 3, { 0xb0, 1, 64 } };
    std::vector<VoxMidiEventV1> overflow(
        rack::RackRouter::eventCapacity + 17, modulationEvent);
    rack.reset();
    (void) render(rack, overflow);
    REQUIRE(rack.runtimeState()[0].droppedMidiEvents == 17,
            "dropped MIDI counter published without touching runtime state");
    std::atomic<bool> counterReadOk { true };
    std::thread counterReader([&] {
        for (int iteration = 0; iteration < 10000; ++iteration)
            if (rack.runtimeState()[0].droppedMidiEvents > 1717)
                counterReadOk.store(false, std::memory_order_relaxed);
    });
    for (int iteration = 0; iteration < 100; ++iteration)
        (void) render(rack, overflow);
    counterReader.join();
    REQUIRE(counterReadOk.load(std::memory_order_relaxed)
                && rack.runtimeState()[0].droppedMidiEvents == 1717,
            "dropped MIDI counter supports concurrent UI reads");

    std::cout << "Rack integration tests passed (" << run << ")\n";
    return EXIT_SUCCESS;
}
