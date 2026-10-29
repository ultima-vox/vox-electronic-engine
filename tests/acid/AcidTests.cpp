#include "acid/AcidEngine.h"
#include "acid/AcidGenerator.h"
#include "acid/AcidParameterBridge.h"
#include "acid/AcidProvider.h"
#include "instrument/InstrumentComplianceHarness.h"
#include "sequence/PatternAdapter.h"
#include "support/ReferenceInstrument.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <numeric>
#include <set>
#include <vector>

namespace {
int checks {};
#define REQUIRE(condition, message) do { ++checks; if (!(condition)) { \
    std::cerr << "FAILED: " << message << " line " << __LINE__ << '\n'; \
    return EXIT_FAILURE; } } while (false)

double energy(const std::vector<float>& audio)
{
    return std::inner_product(audio.begin(), audio.end(), audio.begin(), 0.0);
}

std::vector<float> render(vstengine::instrument::InstrumentInstance& instance,
                          int note = 48, int velocity = 110)
{
    constexpr std::uint32_t samples = 4096;
    std::vector<float> left(samples), right(samples);
    std::array<float*, 2> outputs { left.data(), right.data() };
    const std::array midi { VoxMidiEventV1 { 0, 3,
        { 0x90, static_cast<std::uint8_t>(note),
          static_cast<std::uint8_t>(velocity) } } };
    instance.process({ outputs, samples, midi, 48000.0, 138.0, 0.0, true });
    return left;
}

std::uint64_t patternSignature(const vstengine::sequence::Sequence& pattern)
{
    std::uint64_t hash = 1469598103934665603ULL;
    for (int i = 0; i < pattern.size(); ++i) {
        const auto& step = pattern[i];
        hash ^= static_cast<std::uint64_t>(step.gate)
            | (static_cast<std::uint64_t>(step.accent) << 1)
            | (static_cast<std::uint64_t>(step.noteOffset + 64) << 2)
            | (static_cast<std::uint64_t>(step.ratchetCount) << 12)
            | (static_cast<std::uint64_t>(step.slideDuration > 0.0f) << 16);
        hash *= 1099511628211ULL;
    }
    return hash;
}
} // namespace

int main()
{
    using namespace vstengine;
    // Reject non-finite sample rates before entering realtime math.
    acid::AcidEngine invalidRate;
    REQUIRE(!invalidRate.prepare(std::numeric_limits<double>::quiet_NaN(), 512, 2),
            "NaN sample rate must not enter the audio render path");
    REQUIRE(!invalidRate.prepare(std::numeric_limits<double>::infinity(), 512, 2),
            "Infinite sample rate must not enter the audio render path");
    REQUIRE(invalidRate.prepare(1.0e12, 512, 2),
            "finite high sample rate has a defined tail-count behavior");
    REQUIRE(invalidRate.tailSamples() == std::numeric_limits<std::uint32_t>::max(),
            "oversized but finite tail count saturates rather than overflowing uint32");
    // Neighboring normalized values within one selector bucket must keep the
    // same sound, while oscillator morph must remain continuous within it.
    const auto renderTypedControl = [](const char* id, float value, bool useLfo) {
        acid::AcidEngine engine;
        (void) engine.setParameter(id, value);
        if (useLfo) {
            (void) engine.setParameter("lfo1_depth", 1.0f);
            (void) engine.setParameter("lfo1_rate", 0.75f);
            (void) engine.setParameter("lfo1_target", 0.0f);
        }
        (void) engine.prepare(48000.0, 4096, 2);
        std::array<float, 4096> left{}, right{};
        std::array<float*, 2> outputs {left.data(), right.data()};
        const std::array events {VoxMidiEventV1 {0, 3, {0x90, 48, 110}}};
        engine.process(outputs, 4096, events);
        return left;
    };
    const auto difference = [](const auto& a, const auto& b) {
        double result = 0.0;
        for (std::size_t i = 0; i < a.size(); ++i)
            result += std::abs(static_cast<double>(a[i]) - b[i]);
        return result;
    };
    REQUIRE(difference(renderTypedControl("waveform", 0.05f, false),
                       renderTypedControl("waveform", 0.15f, false)) > 0.01,
            "oscillator waveform changes continuously within a selector bucket");
    REQUIRE(difference(renderTypedControl("filter_mode", 0.05f, false),
                       renderTypedControl("filter_mode", 0.15f, false)) == 0.0,
            "filter mode values in the same bucket select the same filter");
    REQUIRE(difference(renderTypedControl("lfo1_wave", 0.05f, true),
                       renderTypedControl("lfo1_wave", 0.15f, true)) == 0.0,
            "LFO waveform values in the same bucket select the same waveform");
    REQUIRE(difference(renderTypedControl("filter_mode", 0.05f, false),
                       renderTypedControl("filter_mode", 0.65f, false)) > 0.01,
            "different filter mode buckets change sound");
    REQUIRE(difference(renderTypedControl("lfo1_wave", 0.05f, true),
                       renderTypedControl("lfo1_wave", 0.65f, true)) > 0.01,
            "different LFO waveform buckets change sound");
    auto provider = acid::createAcidProvider();
    // Rack may deliver initial values before prepare or after construction on
    // restore. The first render must start from the same authoritative values.
    auto initial = provider->create(acid::instrumentId, {instrument::initialSlotId(0), nullptr});
    auto restored = provider->create(acid::instrumentId, {instrument::initialSlotId(0), nullptr});
    REQUIRE(initial && restored, "Acid initialization probes created");
    REQUIRE(initial->setParameter("waveform", 0.05f), "initial waveform");
    REQUIRE(initial->prepare({48000.0, 4096, 2}) && restored->prepare({48000.0, 4096, 2}),
            "Acid initialization probes prepared");
    REQUIRE(initial->setParameter("waveform", 0.5f) && restored->setParameter("waveform", 0.5f),
            "same authoritative waveform delivered before first block");
    const auto initialAudio = render(*initial);
    const auto restoredAudio = render(*restored);
    REQUIRE(energy(initialAudio) > 0.01 && initialAudio == restoredAudio,
            "first render independent of pre-prepare parameter history");
    acid::AcidParameterBridge initialBridge;
    std::atomic<float> initialFine {0.8f};
    initialBridge.setSlotId(0, instrument::initialSlotId(0));
    initialBridge.bind(0, acid::AcidEngine::oscFine, &initialFine);
    auto bridgedProvider = acid::createAcidProvider(&initialBridge);
    auto bridgedInitial = bridgedProvider->create(acid::instrumentId, {instrument::initialSlotId(0), nullptr});
    auto bridgedRestored = bridgedProvider->create(acid::instrumentId, {instrument::initialSlotId(0), nullptr});
    REQUIRE(bridgedInitial && bridgedRestored, "bridged Acid probes created");
    REQUIRE(bridgedInitial->setParameter("osc_fine", 0.2f), "initial fine tuning");
    REQUIRE(bridgedInitial->prepare({48000.0, 4096, 2}) && bridgedRestored->prepare({48000.0, 4096, 2}),
            "bridged Acid probes prepared");
    const auto bridgedAudio = render(*bridgedInitial);
    const auto bridgedRestoredAudio = render(*bridgedRestored);
    REQUIRE(energy(bridgedAudio) > 0.01 && bridgedAudio == bridgedRestoredAudio,
            "bridge values initialize smoothing before first audio");
    const auto compliance = tests::runInstrumentCompliance(
        *provider, acid::instrumentId);
    if (!compliance) {
        for (const auto& failure : compliance.failures)
            std::cerr << "COMPLIANCE: " << failure << '\n';
    }
    REQUIRE(compliance.passed(), "Acid passes common module compliance");

    auto rateProbe = provider->create(acid::instrumentId,
                                      {instrument::initialSlotId(15), nullptr});
    REQUIRE(rateProbe != nullptr, "Acid rate/budget probe creates");
    for (double rate :
         {8000.0, 44100.0, 48000.0, 96000.0, 192000.0, 384000.0}) {
      REQUIRE(rateProbe->prepare({rate, 4096, 2}),
              "Acid provider supports declared sample-rate range");
      REQUIRE(rateProbe->setParameter("amp_release", 1),
              "maximum Acid Release is available");
      REQUIRE(rateProbe->tailSamples() <=
                  provider->descriptors().front().budget.maxTailSamples,
              "Acid declared maximum tail fits provider budget at every "
              "supported rate");
    }
    for (double rate : {7999.0, 384001.0, 1.0e12}) {
      REQUIRE(!rateProbe->prepare({rate, 4096, 2}),
              "Acid provider rejects rates outside production contract");
      REQUIRE(energy(render(*rateProbe)) == 0,
              "failed Acid prepare cannot render with an earlier sample rate");
    }

    auto reference = tests::createReferenceInstrumentProvider();
    REQUIRE(tests::runInstrumentCompliance(*reference,
                tests::referenceInstrumentId).passed(),
            "test-only ReferenceInstrument passes same compliance");

    REQUIRE(provider->descriptors().size() == 1,
            "Acid provider exposes one instrument");
    const auto& descriptor = provider->descriptors().front();
    REQUIRE(descriptor.id == acid::instrumentId,
            "Acid durable InstrumentId");
    REQUIRE((descriptor.capabilities & instrument::Capability::sequence) != 0
            && (descriptor.capabilities
                & instrument::Capability::patternGenerator) != 0,
            "Acid declares sequence and generator capabilities");
    REQUIRE(provider->contentDescriptors(acid::instrumentId).size() == 18,
            "six Sound, Pattern and Generator factory entries");

    // Acid Production Gate 1: one stable semantic ID per DSP parameter.
    // Do not alias controls to other parameters merely to fill eight host macros.
    REQUIRE(descriptor.parameters.size() == acid::AcidEngine::parameterCount,
            "all Acid parameters are advertised by the production descriptor");
    std::set<std::string> semanticIds;
    for (std::size_t i = 0; i < acid::AcidEngine::parameterCount; ++i) {
        const auto id = acid::AcidEngine::parameterIds[i];
        REQUIRE(descriptor.parameters[i].id == id,
                "Acid descriptor ordering matches stable DSP IDs");
        REQUIRE(semanticIds.insert(std::string(id)).second,
                "every Acid parameter ID is unique");
    }
    REQUIRE(semanticIds.size() == acid::AcidEngine::parameterCount,
            "no duplicate Acid controls in the parameter schema");
    for (std::size_t i = 0; i < instrument::macrosPerSlot; ++i)
        REQUIRE(descriptor.parameters[i].preferredMacro == static_cast<int>(i),
                "legacy Acid host macro assignment remains stable");
    for (std::size_t i = instrument::macrosPerSlot;
         i < descriptor.parameters.size(); ++i)
        REQUIRE(descriptor.parameters[i].preferredMacro == -1,
                "extended Acid controls never alias an unrelated host macro");

    acid::AcidEngine isolated;
    std::array<float, acid::AcidEngine::parameterCount> initialValues {};
    for (std::size_t i = 0; i < initialValues.size(); ++i)
        initialValues[i] = isolated.parameter(acid::AcidEngine::parameterIds[i]);
    for (std::size_t i = 0; i < initialValues.size(); ++i) {
        const float probeValue = initialValues[i] < 0.5f ? 0.937f : 0.063f;
        REQUIRE(isolated.setParameter(acid::AcidEngine::parameterIds[i], probeValue),
                "each Acid control has a writable semantic DSP target");
        REQUIRE(std::abs(isolated.parameter(acid::AcidEngine::parameterIds[i])
                         - probeValue) < 0.00001f,
                "parameter write reads back its own exact target");
        for (std::size_t j = 0; j < initialValues.size(); ++j)
            if (i != j)
                REQUIRE(std::abs(isolated.parameter(acid::AcidEngine::parameterIds[j])
                                 - initialValues[j]) < 0.00001f,
                        "changing one Acid knob must not alter any other parameter");
        REQUIRE(isolated.setParameter(acid::AcidEngine::parameterIds[i],
                                       initialValues[i]),
                "restore isolated probe parameter");
    }
    REQUIRE(!isolated.setParameter("not_an_acid_parameter", 0.5f),
            "unknown control IDs cannot write arbitrary DSP state");


    float outputValue = -1.0f;
    REQUIRE(acid::factorySoundParameterValue(
                "classic-303", "output", outputValue)
                && std::abs(outputValue - 0.72f) < 0.00001f,
            "factory Acid output gain remains preset-specific after host sync");
    REQUIRE(acid::factorySoundParameterValue(
                "hi-tech-acid", "output", outputValue)
                && std::abs(outputValue - 0.60f) < 0.00001f,
            "Acid preset output gain is not replaced with generic defaults");
    REQUIRE(!acid::factorySoundParameterValue(
                "not-a-preset", "output", outputValue)
            && !acid::factorySoundParameterValue(
                "classic-303", "osc_fine", outputValue),
            "factory value lookup never fabricates unsupported fields");

    std::set<std::uint64_t> signatures;
    constexpr std::array profiles { acid::AcidProfile::classic303,
        acid::AcidProfile::psyAcid, acid::AcidProfile::darkAcid,
        acid::AcidProfile::forestAcid, acid::AcidProfile::hiTechAcid,
        acid::AcidProfile::hypnoticAcid };
    for (const auto profile : profiles) {
        const auto first = acid::AcidGenerator::generate(
            profile, 0x12345678u, instrument::initialSlotId(2), acid::instrumentId);
        const auto second = acid::AcidGenerator::generate(
            profile, 0x12345678u, instrument::initialSlotId(2), acid::instrumentId);
        REQUIRE(patternSignature(first) == patternSignature(second),
                "same Acid seed and stable identities are deterministic");
        signatures.insert(patternSignature(first));
    }
    REQUIRE(signatures.size() == profiles.size(),
            "all Acid generator profiles materially differ");
    const auto basePsy = acid::AcidGenerator::generate(
        acid::AcidProfile::psyAcid, 0x12345678u,
        instrument::initialSlotId(2), acid::instrumentId);
    const auto differentSlot = acid::AcidGenerator::generate(
        acid::AcidProfile::psyAcid, 0x12345678u,
        instrument::initialSlotId(3), acid::instrumentId);
    REQUIRE(patternSignature(differentSlot) != patternSignature(basePsy),
            "SlotId participates in generation domain");

    // Empty payload carries no versioned bytes. A fresh #59 slot still uses
    // schema 1; that must succeed and must not alter parameters. Non-empty
    // v1–v7 blobs stay rejected.
    acid::AcidEngine soundState;
    REQUIRE(soundState.loadState(acid::AcidEngine::stateVersion, {}),
            "new Acid slot accepts empty current module state");
    const float factoryCutoff = soundState.parameter("cutoff");
    for (const auto fresh : {
             acid::AcidEngine::legacyStateVersion,
             acid::AcidEngine::previousStateVersion,
             acid::AcidEngine::lfo1StateVersion,
             acid::AcidEngine::env2StateVersion,
             acid::AcidEngine::lfo23StateVersion,
             acid::AcidEngine::stepStateVersion,
             acid::AcidEngine::stateVersion + 1u}) {
        REQUIRE(soundState.loadState(fresh, {}),
                "empty Acid payload is a fresh slot, not a migration");
        REQUIRE(std::abs(soundState.parameter("cutoff") - factoryCutoff) < 0.00001f,
                "empty payload does not change cutoff");
    }
    REQUIRE(soundState.setParameter("cutoff", 0.42f)
                && soundState.loadState(acid::AcidEngine::legacyStateVersion, {})
                && std::abs(soundState.parameter("cutoff") - 0.42f) < 0.00001f,
            "empty legacy slot state does not reset a live Acid instance");
    constexpr std::uint32_t magic = 0x44494341u;
    constexpr std::uint32_t legacyVersion = 1u;
    const std::array<float, acid::AcidEngine::legacyParameterCount> legacyValues {
        0.22f, 0.39f, 0.64f, 0.48f, 0.27f, 0.78f, 0.51f, 0.42f, 0.68f
    };
    std::array<std::byte, 8 + legacyValues.size() * sizeof(float)> oldState {};
    std::memcpy(oldState.data(), &magic, sizeof(magic));
    std::memcpy(oldState.data() + 4, &legacyVersion, sizeof(legacyVersion));
    std::memcpy(oldState.data() + 8, legacyValues.data(),
                legacyValues.size() * sizeof(float));
    REQUIRE(!soundState.loadState(legacyVersion, oldState),
            "Acid v1 sound state is not migrated");
    REQUIRE(std::abs(soundState.parameter("cutoff") - 0.42f) < 0.00001f,
            "rejected v1 payload leaves the live cutoff intact");

    REQUIRE(soundState.setParameter("cutoff", 0.42f), "mark current state before save");
    std::array<std::byte, acid::AcidEngine::encodedStateBytes> encoded {};
    std::uint32_t written {};
    REQUIRE(soundState.saveState(encoded, written)
                && written == encoded.size(), "current Acid state saves");
    const float originalCutoff = soundState.parameter("cutoff");
    auto malformed = encoded;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    std::memcpy(malformed.data() + 8 + 25 * sizeof(float),
                &nan, sizeof(nan));
    REQUIRE(!soundState.loadState(acid::AcidEngine::stateVersion, malformed),
            "current Acid state rejects NaN without applying any parameters");
    REQUIRE(std::abs(soundState.parameter("cutoff") - originalCutoff) < 0.00001f,
            "rejected current payload cannot partially alter cutoff");

    acid::AcidEngine restoredSound;
    REQUIRE(restoredSound.loadState(acid::AcidEngine::stateVersion, encoded),
            "current Acid state round-trips");
    for (std::size_t i = 0; i < acid::AcidEngine::parameterCount; ++i)
        REQUIRE(std::abs(restoredSound.parameter(acid::AcidEngine::parameterIds[i])
                         - soundState.parameter(acid::AcidEngine::parameterIds[i]))
                         < 0.00001f, "current state restores every semantic parameter");

    // Host automation bridge is per plugin and slot, never global.
    // Only extended Acid controls use the bridge; frozen macro IDs do not.
    acid::AcidParameterBridge hostBridge;
    std::atomic<float> outputA { 0.13f }, outputB { 0.79f };
    std::atomic<float> fineA { 0.67f }, fineB { 0.38f };
    std::atomic<float> legato { 1.0f }, retrigger { 0.0f };
    std::atomic<float> velAccent { 1.0f }, overlap { 0.0f };
    hostBridge.setSlotId(0, instrument::initialSlotId(0));
    hostBridge.setSlotId(1, instrument::initialSlotId(1));
    hostBridge.bind(0, acid::AcidEngine::output, &outputA);
    hostBridge.bind(1, acid::AcidEngine::output, &outputB);
    hostBridge.bind(0, acid::AcidEngine::oscFine, &fineA);
    hostBridge.bind(1, acid::AcidEngine::oscFine, &fineB);
    for (std::size_t flag = 0; flag < 4; ++flag) {
        std::array<std::atomic<float>*, 4> values {
            &legato, &retrigger, &velAccent, &overlap
        };
        hostBridge.bindFlag(0, flag, values[flag]);
    }
    acid::AcidEngine acidA, acidB;
    hostBridge.apply(instrument::initialSlotId(0), acidA);
    hostBridge.apply(instrument::initialSlotId(1), acidB);
    REQUIRE(std::abs(acidA.parameter("output") - 0.13f) < 0.00001f
            && std::abs(acidB.parameter("output") - 0.79f) < 0.00001f,
            "one Acid host parameter maps to exactly one slot");
    REQUIRE(std::abs(acidA.parameter("osc_fine") - 0.67f) < 0.00001f
            && std::abs(acidB.parameter("osc_fine") - 0.38f) < 0.00001f,
            "Acid fine tuning never bleeds across slots");
    REQUIRE(acidA.isLegatoEnabled() && !acidA.isRetriggerEnabled()
            && acidA.isVelToAccentEnabled() && !acidA.isSlideOnOverlapEnabled(),
            "four Acid play-mode switches remain independent");
    outputA.store(0.41f);
    hostBridge.apply(instrument::initialSlotId(0), acidA);
    REQUIRE(std::abs(acidA.parameter("output") - 0.41f) < 0.00001f
            && std::abs(acidB.parameter("output") - 0.79f) < 0.00001f,
            "automating one Acid slot preserves another slot");
    hostBridge.setSlotId(0, instrument::initialSlotId(3));
    outputA.store(0.55f);
    hostBridge.apply(instrument::initialSlotId(0), acidA);
    REQUIRE(std::abs(acidA.parameter("output") - 0.41f) < 0.00001f,
            "obsolete slot ID cannot change a replaced Acid engine");
    std::set<std::string> uniqueHostIds;
    for (std::size_t slot = 0; slot < instrument::maxSlots; ++slot)
        for (std::size_t parameter = instrument::macrosPerSlot;
             parameter < acid::AcidEngine::parameterCount; ++parameter)
            if (parameter != acid::AcidEngine::playModeFlags)
                REQUIRE(uniqueHostIds.insert(acid::hostParameterId(
                    slot, acid::AcidEngine::parameterIds[parameter])).second,
                "Acid host automation identity unique across slots and controls");
    REQUIRE(uniqueHostIds.size() == instrument::maxSlots
                                  * (acid::AcidEngine::parameterCount
                                     - instrument::macrosPerSlot - 1),
            "all independent Acid host automation IDs are unique");

    // The separate Slide and Portamento controls must not change each
    // other's timing. Fresh voices, equal MIDI and all other parameters.
    auto renderGlideGesture = [](float slide, float portamento, bool overlap) {
        acid::AcidEngine voice;
        if (!voice.setParameter("slide", slide)
            || !voice.setParameter("portamento", portamento)
            || !voice.prepare(48000.0, 4096, 2))
            return std::vector<float> {};
        std::vector<float> left(4096), right(4096);
        std::array<float*, 2> outputs { left.data(), right.data() };
        // MIDI events remain sample-offset sorted for either gesture.
        if (overlap) {
            const std::array<VoxMidiEventV1, 4> legato {{
                { 0, 3, { 0x90u, 48u, 100u } },
                { 1024, 3, { 0x90u, 60u, 100u } },
                { 2048, 3, { 0x80u, 48u, 0u } },
                { 3072, 3, { 0x80u, 60u, 0u } }
            }};
            voice.process(outputs, 4096, legato);
        } else {
            const std::array<VoxMidiEventV1, 4> detached {{
                { 0, 3, { 0x90u, 48u, 100u } },
                { 512, 3, { 0x80u, 48u, 0u } },
                { 1024, 3, { 0x90u, 60u, 100u } },
                { 3072, 3, { 0x80u, 60u, 0u } }
            }};
            voice.process(outputs, 4096, detached);
        }
        return left;
    };
    auto audioDiff = [](const std::vector<float>& a,
                        const std::vector<float>& b) {
        return std::inner_product(a.begin(), a.end(), b.begin(), 0.0,
            std::plus<> {}, [](float x, float y) {
                return std::abs(static_cast<double>(x) - static_cast<double>(y));
            });
    };
    const auto disconnectedSlideLow = renderGlideGesture(0.05f, 0.75f, false);
    const auto disconnectedSlideHigh = renderGlideGesture(0.95f, 0.75f, false);
    REQUIRE(disconnectedSlideLow.size() == 4096
            && audioDiff(disconnectedSlideLow, disconnectedSlideHigh) < 1.0e-5,
            "Slide Time never alters disconnected Portamento timing");
    const auto overlappingPortaLow = renderGlideGesture(0.75f, 0.05f, true);
    const auto overlappingPortaHigh = renderGlideGesture(0.75f, 0.95f, true);
    REQUIRE(audioDiff(overlappingPortaLow, overlappingPortaHigh) < 1.0e-5,
            "Portamento never alters overlapping Slide timing");


    // Measured LFO1 DSP proof: four controls belong to LFO1, with no alias to
    // the original ENV1 settings. This tests rendered audio, not UI labels.
    auto renderLfo1 = [](float depth, float rate, float waveform, float target) {
        acid::AcidEngine synth;
        const bool configured =
            synth.setParameter("mod_amount", 0.0f)
            && synth.setParameter("lfo1_depth", depth)
            && synth.setParameter("lfo1_rate", rate)
            && synth.setParameter("lfo1_wave", waveform)
            && synth.setParameter("lfo1_target", target)
            && synth.prepare(48000.0, 16384, 2);
        if (!configured) return std::vector<float> {};
        std::vector<float> left(16384), right(16384);
        std::array<float*, 2> outs { left.data(), right.data() };
        const std::array midi { VoxMidiEventV1 { 0, 3, { 0x90, 48, 120 } } };
        synth.process(outs, 16384, midi);
        return left;
    };
    auto sumAbsoluteDifference = [](const auto& a, const auto& b) {
        return std::inner_product(a.begin(), a.end(), b.begin(), 0.0,
            std::plus<> {}, [](float x, float y) {
                return std::abs(static_cast<double>(x) - static_cast<double>(y));
            });
    };
    const auto noLfo = renderLfo1(0.0f, 0.68f, 0.0f, 0.0f);
    const auto cutoffLfo = renderLfo1(0.85f, 0.68f, 0.0f, 0.0f);
    const auto pitchLfo = renderLfo1(0.85f, 0.68f, 0.0f, 0.2f);
    const auto squareLfo = renderLfo1(0.85f, 0.68f, 0.68f, 0.0f);
    REQUIRE(noLfo.size() == 16384 && cutoffLfo.size() == noLfo.size(),
            "LFO1 DSP prepares and renders");
    REQUIRE(sumAbsoluteDifference(noLfo, cutoffLfo) > 1.0,
            "LFO1 depth changes the real audible cutoff");
    REQUIRE(sumAbsoluteDifference(cutoffLfo, pitchLfo) > 1.0,
            "LFO1 target routes independently to pitch or cutoff");
    REQUIRE(sumAbsoluteDifference(cutoffLfo, squareLfo) > 1.0,
            "LFO1 waveform changes the modulation's audible waveform");
    for (float sample : squareLfo)
        REQUIRE(std::isfinite(sample) && std::abs(sample) <= 8.0f,
                "LFO1 high depth stays finite and bounded");
    const auto slowLfo = renderLfo1(0.85f, 0.08f, 0.0f, 0.0f);
    const auto fastLfo = renderLfo1(0.85f, 0.92f, 0.0f, 0.0f);
    REQUIRE(sumAbsoluteDifference(slowLfo, fastLfo) > 1.0,
            "LFO1 rate changes modulation frequency audibly");

    acid::AcidEngine lfoSaved;
    REQUIRE(lfoSaved.setParameter("lfo1_rate", 0.81f)
            && lfoSaved.setParameter("lfo1_depth", 0.64f)
            && lfoSaved.setParameter("lfo1_wave", 0.66f)
            && lfoSaved.setParameter("lfo1_target", 0.80f),
            "LFO1 four parameters writable independently");
    std::array<std::byte, acid::AcidEngine::encodedStateBytes> lfoBlob {};
    std::uint32_t lfoBytes {};
    REQUIRE(lfoSaved.saveState(lfoBlob, lfoBytes)
                && lfoBytes == lfoBlob.size(),
            "LFO1 v4 Acid preset serialized");
    acid::AcidEngine lfoReloaded;
    REQUIRE(lfoReloaded.loadState(acid::AcidEngine::stateVersion, lfoBlob),
            "LFO1 v4 Acid preset restored");
    for (auto parameter : { "lfo1_rate", "lfo1_depth",
                            "lfo1_wave", "lfo1_target" })
        REQUIRE(std::abs(lfoReloaded.parameter(parameter)
                         - lfoSaved.parameter(parameter)) < 0.00001f,
                "all four LFO1 controls survive module state round-trip");

    // LFO2 and LFO3 must be separately audible and must not alias LFO1
    // or each other. Preserve sample-continuous phase across host blocks.
    auto renderAddedLfo = [](int which, float rate, float depth, float wave,
                              float target) {
        acid::AcidEngine synth;
        if (!synth.setParameter("mod_amount", 0.0f)
            || !synth.setParameter("env2_amount", 0.0f)
            || !synth.setParameter("lfo1_depth", 0.0f))
            return std::vector<float> {};
        const auto prefix = which == 2 ? "lfo2_" : "lfo3_";
        if (!synth.setParameter(std::string(prefix) + "rate", rate)
            || !synth.setParameter(std::string(prefix) + "depth", depth)
            || !synth.setParameter(std::string(prefix) + "wave", wave)
            || !synth.setParameter(std::string(prefix) + "target", target)
            || !synth.prepare(48000.0, 4096, 2))
            return std::vector<float> {};
        std::vector<float> left(16384), right(16384);
        std::array<float*, 2> outputs { left.data(), right.data() };
        const std::array firstNote { VoxMidiEventV1 { 0, 3, { 0x90, 48, 120 } } };
        synth.process(outputs, 4096, firstNote);
        for (int block = 1; block < 4; ++block) {
            std::array<float*, 2> subOutputs {
                left.data() + block * 4096, right.data() + block * 4096
            };
            synth.process(subOutputs, 4096, {});
        }
        return left;
    };
    const auto lfo2Off = renderAddedLfo(2, 0.56f, 0.0f, 0.0f, 0.0f);
    const auto lfo2On = renderAddedLfo(2, 0.56f, 0.90f, 0.0f, 0.0f);
    const auto lfo3Off = renderAddedLfo(3, 0.56f, 0.0f, 0.0f, 0.0f);
    const auto lfo3On = renderAddedLfo(3, 0.56f, 0.90f, 0.0f, 0.0f);
    REQUIRE(lfo2Off.size() == 16384 && lfo2Off.size() == lfo3On.size(),
            "independent LFO2/LFO3 voices render all host blocks");
    REQUIRE(sumAbsoluteDifference(lfo2Off, lfo2On) > 1.0
            && sumAbsoluteDifference(lfo3Off, lfo3On) > 1.0,
            "LFO2 and LFO3 depth individually change real Acid audio");
    REQUIRE(sumAbsoluteDifference(lfo2On,
            renderAddedLfo(2, 0.93f, 0.90f, 0.0f, 0.0f)) > 1.0,
            "LFO2 rate changes audible modulation frequency");
    REQUIRE(sumAbsoluteDifference(lfo3On,
            renderAddedLfo(3, 0.56f, 0.90f, 0.75f, 0.0f)) > 1.0,
            "LFO3 waveform changes audible result");
    REQUIRE(sumAbsoluteDifference(lfo3On,
            renderAddedLfo(3, 0.56f, 0.90f, 0.0f, 0.2f)) > 1.0,
            "LFO3 destination switches between cutoff and pitch");
    for (const float sample : lfo3On)
        REQUIRE(std::isfinite(sample) && std::abs(sample) <= 8.0f,
                "LFO3 high depth remains finite and bounded");

    acid::AcidEngine savedWithLfo2Lfo3;
    REQUIRE(savedWithLfo2Lfo3.setParameter("lfo2_rate", 0.82f)
            && savedWithLfo2Lfo3.setParameter("lfo2_depth", 0.64f)
            && savedWithLfo2Lfo3.setParameter("lfo2_wave", 0.68f)
            && savedWithLfo2Lfo3.setParameter("lfo2_target", 0.4f)
            && savedWithLfo2Lfo3.setParameter("lfo3_rate", 0.28f)
            && savedWithLfo2Lfo3.setParameter("lfo3_depth", 0.74f)
            && savedWithLfo2Lfo3.setParameter("lfo3_wave", 0.32f)
            && savedWithLfo2Lfo3.setParameter("lfo3_target", 0.8f),
            "all eight LFO2/LFO3 controls are writable");
    std::array<std::byte, acid::AcidEngine::encodedStateBytes> lfo23Bytes {};
    std::uint32_t lfo23Size = 0;
    REQUIRE(savedWithLfo2Lfo3.saveState(lfo23Bytes, lfo23Size)
            && lfo23Size == lfo23Bytes.size(), "v6 LFO2/3 serialized");
    acid::AcidEngine restoredWithLfo2Lfo3;
    REQUIRE(restoredWithLfo2Lfo3.loadState(
                acid::AcidEngine::stateVersion, lfo23Bytes),
            "v6 LFO2/3 restored");
    for (const char* id : { "lfo2_rate", "lfo2_depth", "lfo2_wave", "lfo2_target",
                            "lfo3_rate", "lfo3_depth", "lfo3_wave", "lfo3_target" })
        REQUIRE(std::abs(restoredWithLfo2Lfo3.parameter(id)
                         - savedWithLfo2Lfo3.parameter(id)) < 0.00001f,
                "separate LFO2/LFO3 controls survive v6 preset roundtrip");

    // All LFOs support a free-running Hz mode and an independent musical
    // PPQ mode. At the same musical position a synced LFO must be block-size
    // invariant; seeks and song loops must restart its phase on the grid.
    auto renderSync = [](int which, bool sync, float rate,
                         double ppqStart, std::uint32_t blockSize,
                         bool playing = true) {
        acid::AcidEngine synth;
        if (!synth.setParameter("mod_amount", 0.0f)
            || !synth.setParameter("env2_amount", 0.0f)
            || !synth.setParameter("step_amount", 0.0f))
            return std::vector<float> {};
        for (int n = 1; n <= 3; ++n) {
            const auto prefix = std::string("lfo") + std::to_string(n) + "_";
            if (!synth.setParameter(prefix + "depth", n == which ? 0.87f : 0.0f)
                || !synth.setParameter(prefix + "sync", n == which && sync ? 1.0f : 0.0f)
                || !synth.setParameter(prefix + "rate", rate)
                || !synth.setParameter(prefix + "target", 0.0f)
                || !synth.setParameter(prefix + "wave", 0.0f))
                return std::vector<float> {};
        }
        if (!synth.prepare(48000.0, blockSize, 2)) return std::vector<float> {};
        constexpr int total = 8192;
        std::vector<float> left(total), right(total);
        const std::array note { VoxMidiEventV1 { 0, 3, { 0x90, 48, 112 } } };
        for (int offset = 0; offset < total; offset += static_cast<int>(blockSize)) {
            const double ppq = ppqStart + (playing
                ? static_cast<double>(offset) * 120.0 / (48000.0 * 60.0)
                : 0.0);
            synth.setTransport(120.0, ppq, playing);
            std::array<float*, 2> outputs {
                left.data() + offset, right.data() + offset
            };
            synth.process(outputs, blockSize, offset == 0
                ? std::span<const VoxMidiEventV1>(note)
                : std::span<const VoxMidiEventV1> {});
        }
        return left;
    };
    constexpr float quarterNoteRate = 2.0f / 7.0f; // 1/4, one cycle per beat
    for (int lfo = 1; lfo <= 3; ++lfo) {
        const auto base = renderSync(lfo, true, quarterNoteRate, 0.125, 512);
        const auto otherBlocks = renderSync(lfo, true, quarterNoteRate, 0.125, 128);
        const auto afterOneBeat = renderSync(lfo, true, quarterNoteRate, 1.125, 512);
        const auto offGrid = renderSync(lfo, true, quarterNoteRate, 0.625, 512);
        const auto freeRun = renderSync(lfo, false, quarterNoteRate, 0.125, 512);
        REQUIRE(base.size() == 8192 && otherBlocks.size() == base.size(),
                "host-synced LFO produces all requested audio samples");
        REQUIRE(sumAbsoluteDifference(base, otherBlocks) < 0.1,
                "host PPQ LFO phase is invariant to 128/512 sample-block partition");
        REQUIRE(sumAbsoluteDifference(base, afterOneBeat) < 0.1,
                "host PPQ LFO phase repeats identically after one musical beat");
        REQUIRE(sumAbsoluteDifference(base, offGrid) > 1.0,
                "host DAW seek to another musical position changes LFO phase");
        REQUIRE(sumAbsoluteDifference(base, freeRun) > 1.0,
                "SYNC off keeps original free-running Hz oscillator behavior");
    }
    const auto stationary = renderSync(2, true, quarterNoteRate, 0.125, 512, false);
    const auto advancing = renderSync(2, true, quarterNoteRate, 0.125, 512, true);
    REQUIRE(sumAbsoluteDifference(stationary, advancing) > 1.0,
            "when transport stops the musical LFO phase holds instead of drifting");

    acid::AcidEngine savedSynced;
    REQUIRE(savedSynced.setParameter("lfo1_sync", 1.0f)
            && savedSynced.setParameter("lfo2_sync", 0.0f)
            && savedSynced.setParameter("lfo3_sync", 1.0f),
            "three musical sync mode flags write independently");
    std::array<std::byte, acid::AcidEngine::encodedStateBytes> syncBytes {};
    std::uint32_t syncWritten {};
    REQUIRE(savedSynced.saveState(syncBytes, syncWritten)
            && syncWritten == syncBytes.size(),
            "v8 sync state saves all previous sound and three sync switches");
    acid::AcidEngine restoredSynced;
    REQUIRE(restoredSynced.loadState(acid::AcidEngine::stateVersion, syncBytes)
            && restoredSynced.parameter("lfo1_sync") == 1.0f
            && restoredSynced.parameter("lfo2_sync") == 0.0f
            && restoredSynced.parameter("lfo3_sync") == 1.0f,
            "all three independent sync settings survive v8 state roundtrip");

    // UB/transport contract: a note received at sampleOffset N after
    // silence must hear the *same* synced LFO + STEP phase as a note played
    // immediately at the corresponding absolute host PPQ position. This
    // also exercises the DSP early-return path while the envelope is idle.
    auto renderAcidPpqOnset = [](std::uint32_t midiOffset,
                                 double hostPpq, std::uint32_t samples) {
        acid::AcidEngine engine;
        if (!engine.setParameter("mod_amount", 0.0f)
            || !engine.setParameter("env2_amount", 0.0f)
            || !engine.setParameter("cutoff", 0.35f)
            || !engine.setParameter("lfo1_depth", 0.8f)
            || !engine.setParameter("lfo1_sync", 1.0f)
            || !engine.setParameter("lfo1_rate", 2.0f / 7.0f)
            || !engine.setParameter("lfo2_depth", 0.0f)
            || !engine.setParameter("lfo3_depth", 0.0f)
            || !engine.setParameter("step_amount", 0.85f)
            || !engine.setParameter("step_rate", 1.0f / 3.0f)
            || !engine.setParameter("step_smooth", 1.0f)
            || !engine.setParameter("step_01", 0.0f)
            || !engine.setParameter("step_16", 1.0f)
            || !engine.prepare(48000.0, 4096, 2))
            return std::vector<float> {};
        std::vector<float> left(samples), right(samples);
        std::array<float*, 2> output { left.data(), right.data() };
        const std::array notes { VoxMidiEventV1 {
            midiOffset, 3, { 0x90, 48, 110 } } };
        engine.setTransport(120.0, hostPpq, true);
        engine.process(output, samples, notes);
        return left;
    };
    constexpr std::uint32_t delayedOnset = 384;
    const double delayedPpq = static_cast<double>(delayedOnset) * 120.0
                              / (48000.0 * 60.0);
    const auto delayedAudio = renderAcidPpqOnset(delayedOnset, 0.0, 4096);
    const auto immediateAudio = renderAcidPpqOnset(
        0u, delayedPpq, 4096 - delayedOnset);
    REQUIRE(delayedAudio.size() == 4096
            && immediateAudio.size() == 4096 - delayedOnset,
            "sample-offset Acid note and absolute PPQ reference both render");
    double delayedMaxError = 0.0;
    for (std::size_t i = 0; i < immediateAudio.size(); ++i)
        delayedMaxError = std::max(delayedMaxError,
            std::abs(static_cast<double>(delayedAudio[i + delayedOnset])
                   - static_cast<double>(immediateAudio[i])));
    REQUIRE(delayedMaxError < 0.005,
            "synced LFO/STEP musical phase must advance through silent samples");

    // Finite but enormous host PPQ positions must not overflow intermediate
    // step-rate multiplication or generate a NaN->int conversion for the
    // 16-step array index. The host ABI permits finite double PPQ values.
    for (double extremePpq : {
            std::numeric_limits<double>::max(),
            -std::numeric_limits<double>::max() }) {
        const auto extreme = renderAcidPpqOnset(0u, extremePpq, 4096);
        REQUIRE(extreme.size() == 4096,
                "Acid renders at extreme but finite musical positions");
        for (const float sample : extreme)
            REQUIRE(std::isfinite(sample) && std::abs(sample) <= 8.0f,
                    "extreme host PPQ cannot overflow STEP index or poison audio");
    }

    // STEP is a musical-time modulation source, not the note sequencer.
    // Sample-accurate PPQ and BPM are supplied through the Acid provider.
    auto renderStep = [](float amount, float target, float rate, float smooth,
                          float firstStep, float secondStep, bool transport,
                          std::uint32_t blockSize) {
        acid::AcidEngine engine;
        if (!engine.setParameter("mod_amount", 0.0f)
            || !engine.setParameter("env2_amount", 0.0f)
            || !engine.setParameter("lfo1_depth", 0.0f)
            || !engine.setParameter("lfo2_depth", 0.0f)
            || !engine.setParameter("lfo3_depth", 0.0f)
            || !engine.setParameter("step_amount", amount)
            || !engine.setParameter("step_target", target)
            || !engine.setParameter("step_rate", rate)
            || !engine.setParameter("step_smooth", smooth)
            || !engine.setParameter("step_01", firstStep)
            || !engine.setParameter("step_02", secondStep)
            || !engine.prepare(48000.0, blockSize, 2))
            return std::vector<float> {};
        constexpr int total = 16384;
        std::vector<float> left(total), right(total);
        const std::array note { VoxMidiEventV1 { 0, 3, { 0x90, 48, 110 } } };
        double ppq = 0.0;
        for (int pos = 0; pos < total; pos += static_cast<int>(blockSize)) {
            engine.setTransport(120.0, ppq, transport);
            std::array<float*, 2> outputs {
                left.data() + pos, right.data() + pos
            };
            engine.process(outputs, blockSize, pos == 0
                ? std::span<const VoxMidiEventV1>(note)
                : std::span<const VoxMidiEventV1> {});
            ppq += static_cast<double>(blockSize) * 120.0 /
                   (48000.0 * 60.0);
        }
        return left;
    };
    const auto inactiveStep = renderStep(0.0f, 0.0f, 1.0f/3.0f,
        0.0f, 1.0f, 0.0f, true, 512);
    const auto activeStep = renderStep(0.92f, 0.0f, 1.0f/3.0f,
        0.0f, 1.0f, 0.0f, true, 512);
    REQUIRE(inactiveStep.size() == 16384 && activeStep.size() == inactiveStep.size(),
            "STEP modulation renders at actual PPQ and fixed host block size");
    REQUIRE(sumAbsoluteDifference(inactiveStep, activeStep) > 1.0,
            "STEP Amount audibly modulates its target");
    REQUIRE(sumAbsoluteDifference(activeStep,
            renderStep(0.92f, 0.2f, 1.0f/3.0f, 0.0f, 1.0f, 0.0f, true, 512)) > 1.0,
            "STEP Target changes routed audible destination");
    REQUIRE(sumAbsoluteDifference(activeStep,
            renderStep(0.92f, 0.0f, 2.0f/3.0f, 0.0f, 1.0f, 0.0f, true, 512)) > 1.0,
            "STEP rhythm changes the number of levels per quarter note");
    REQUIRE(sumAbsoluteDifference(activeStep,
            renderStep(0.92f, 0.0f, 1.0f/3.0f, 0.85f, 1.0f, 0.0f, true, 512)) > 1.0,
            "STEP smoothing changes interpolated modulation shape");
    REQUIRE(sumAbsoluteDifference(activeStep,
            renderStep(0.92f, 0.0f, 1.0f/3.0f, 0.0f, 0.0f, 0.0f, true, 512)) > 1.0,
            "STEP first editable level independently changes rendered sound");
    REQUIRE(sumAbsoluteDifference(inactiveStep,
            renderStep(0.92f, 0.0f, 1.0f/3.0f, 0.0f, 1.0f, 0.0f, false, 512))
            < 1.0e-5,
            "STEP transport stop produces no modulation");
    for (float sample : activeStep)
        REQUIRE(std::isfinite(sample) && std::abs(sample) <= 8.0f,
                "active STEP remains finite under audio render");

    acid::AcidEngine savedStep;
    REQUIRE(savedStep.setParameter("step_amount", 0.77f)
            && savedStep.setParameter("step_target", 0.4f)
            && savedStep.setParameter("step_rate", 1.0f)
            && savedStep.setParameter("step_smooth", 0.69f)
            && savedStep.setParameter("step_01", 0.97f)
            && savedStep.setParameter("step_16", 0.02f),
            "all STEP source controls and individual levels are writable");
    std::array<std::byte, acid::AcidEngine::encodedStateBytes> stepPayload {};
    std::uint32_t stepWritten {};
    REQUIRE(savedStep.saveState(stepPayload, stepWritten)
            && stepWritten == stepPayload.size(), "v7 STEP state serialized");
    acid::AcidEngine restoredStep;
    REQUIRE(restoredStep.loadState(acid::AcidEngine::stateVersion, stepPayload),
            "v7 STEP state deserializes");
    for (auto semantic : { "step_amount", "step_target", "step_rate", "step_smooth",
                           "step_01", "step_16" })
        REQUIRE(std::abs(restoredStep.parameter(semantic)
                         - savedStep.parameter(semantic)) < 0.00001f,
                "STEP independent parameters and 16-level curve preserve their state");

    // Real ENV2 DSP must have an independent ADSR and target. Its depth
    // should change rendered audio with ENV1 + LFO1 explicitly disabled.
    auto renderEnv2 = [](float depth, float target, float attack) {
        acid::AcidEngine engine;
        if (!engine.setParameter("mod_amount", 0.0f)
            || !engine.setParameter("lfo1_depth", 0.0f)
            || !engine.setParameter("env2_amount", depth)
            || !engine.setParameter("env2_target", target)
            || !engine.setParameter("env2_attack", attack)
            || !engine.prepare(48000.0, 16384, 2))
            return std::vector<float> {};
        std::vector<float> left(16384), right(16384);
        std::array<float*, 2> outputs { left.data(), right.data() };
        const std::array notes { VoxMidiEventV1 { 0, 3, { 0x90, 48, 110 } } };
        engine.process(outputs, 16384, notes);
        return left;
    };
    const auto env2Off = renderEnv2(0.0f, 0.0f, 0.0f);
    const auto env2Cutoff = renderEnv2(0.92f, 0.0f, 0.0f);
    const auto env2Pitch = renderEnv2(0.92f, 0.2f, 0.0f);
    const auto env2SlowAttack = renderEnv2(0.92f, 0.0f, 0.85f);
    REQUIRE(env2Off.size() == 16384 && env2Pitch.size() == env2Off.size(),
            "ENV2 prepares and renders its own MIDI-triggered ADSR");
    REQUIRE(sumAbsoluteDifference(env2Off, env2Cutoff) > 1.0,
            "ENV2 depth changes audible filter cutoff");
    REQUIRE(sumAbsoluteDifference(env2Cutoff, env2Pitch) > 1.0,
            "ENV2 target routes independently between cutoff and pitch");
    REQUIRE(sumAbsoluteDifference(env2Cutoff, env2SlowAttack) > 1.0,
            "ENV2 Attack changes its own audible envelope timing");
    for (float sample : env2SlowAttack)
        REQUIRE(std::isfinite(sample) && std::abs(sample) <= 8.0f,
                "ENV2 high depth remains finite and bounded");

    // Production DSP gate: user-visible Acid knob changes must alter audio,
    // not just move a normalized property. Render both variants from fresh,
    // identically prepared voices so no envelope/RNG history can mask a bug.
    auto renderControl = [&](std::string_view semantic, float value) {
        auto voice = provider->create(acid::instrumentId,
            { instrument::initialSlotId(9), nullptr });
        if (!voice || !voice->prepare({ 48000.0, 4096, 2 })
            || !voice->setParameter(semantic, value))
            return std::vector<float> {};
        return render(*voice, 48, 120);
    };
    constexpr std::array<std::string_view, 12> independentlyAudible {
        "osc_tune", "osc_fine", "waveform", "cutoff",
        "resonance", "filter_mode", "drive", "dist_mode",
        "dist_mix", "output", "pan", "amp_attack"
    };
    for (const auto id : independentlyAudible) {
        const auto low = renderControl(id, 0.07f);
        const auto high = renderControl(id, 0.93f);
        REQUIRE(!low.empty() && low.size() == high.size(),
                "independently automated Acid control renders both variants");
        const double difference = std::inner_product(
            low.begin(), low.end(), high.begin(), 0.0,
            std::plus<> {}, [](float a, float b) {
                return std::abs(static_cast<double>(a) - static_cast<double>(b));
            });
        REQUIRE(difference > 0.01,
                "each Acid oscillator/filter/drive/output control has audible DSP effect");
    }

    auto instance = provider->create(acid::instrumentId,
        { instrument::initialSlotId(1), nullptr });
    REQUIRE(instance && instance->prepare({ 48000.0, 4096, 2 }),
            "Acid instance prepares");
    // Short 512-sample blocks match the actual host routing workload. A
    // freshly created Acid module must make audible sound on the first block.
    auto shortBlock = provider->create(acid::instrumentId,
        { instrument::initialSlotId(7), nullptr });
    REQUIRE(shortBlock != nullptr
            && shortBlock->loadState(acid::AcidEngine::stateVersion, {})
            && shortBlock->prepare({ 48000.0, 512, 2 }),
            "fresh Acid instance loads empty Rack state and prepares");
    std::array<float, 512> shortLeft {}, shortRight {};
    std::array<float*, 2> shortOutput { shortLeft.data(), shortRight.data() };
    const std::array firstNote {
        VoxMidiEventV1 { 0, 3, { 0x90u, 48u, 110u } }
    };
    const std::array lastNote {
        VoxMidiEventV1 { 0, 3, { 0x80u, 48u, 0u } }
    };
    shortBlock->process({ shortOutput, 512, firstNote,
                          48000.0, 138.0, 0.0, true });
    REQUIRE(std::all_of(shortLeft.begin(), shortLeft.end(), [](float x) {
                return std::isfinite(x);
            }), "Acid first host block contains finite audio");
    const double firstBlockEnergy = std::inner_product(
        shortLeft.begin(), shortLeft.end(), shortLeft.begin(), 0.0);
    REQUIRE(firstBlockEnergy > 1.0e-7,
            "first 512-sample Acid block has audible energy");

    shortBlock->process({ shortOutput, 512, lastNote,
                          48000.0, 138.0, 0.0, true });
    // 400 blocks cover 4.27 s, longer than Acid's maximum amp release.
    for (int i = 0; i < 400; ++i)
        shortBlock->process({ shortOutput, 512, {},
                              48000.0, 138.0, 0.0, true });
    const double releasedEnergy = std::inner_product(
        shortLeft.begin(), shortLeft.end(), shortLeft.begin(), 0.0);
    REQUIRE(releasedEnergy < 1.0e-9,
            "Acid NoteOff releases fully, never leaves a continuous beep");

    // Stress Acid's own resonant filter at Nyquist-proximal cutoff and the
    // loudest saturated voice. Do not loosen Rack or host tests to hide NaNs.
    shortBlock->reset();
    REQUIRE(shortBlock->setParameter("cutoff", 1.0f)
            && shortBlock->setParameter("resonance", 1.0f)
            && shortBlock->setParameter("drive", 1.0f)
            && shortBlock->setParameter("dist_mix", 1.0f),
            "Acid high-resonance stress parameters accepted");
    shortBlock->process({ shortOutput, 512, firstNote,
                          48000.0, 138.0, 0.0, true });
    for (int i = 0; i < 200; ++i) {
        shortBlock->process({ shortOutput, 512, {},
                              48000.0, 138.0, 0.0, true });
        REQUIRE(std::all_of(shortLeft.begin(), shortLeft.end(), [](float x) {
                    return std::isfinite(x) && std::abs(x) <= 8.0f;
                }), "Acid maximum resonance and drive remain bounded");
    }
    shortBlock->reset();

    std::vector<double> presetEnergy;
    for (const auto& content : provider->contentDescriptors(acid::instrumentId)) {
        if (content.kind != instrument::ContentKind::soundPreset) continue;
        REQUIRE(provider->applySoundPreset(acid::instrumentId, content.id,
                    *instance) == instrument::ContentStatus::ok,
                "factory Acid Sound preset applies");
        instance->reset();
        const auto audio = render(*instance);
        REQUIRE(std::all_of(audio.begin(), audio.end(), [](float sample) {
                    return std::isfinite(sample) && std::abs(sample) <= 1.01f;
                }), "Acid factory render bounded and finite");
        presetEnergy.push_back(energy(audio));
    }
    REQUIRE(presetEnergy.size() == 6, "six Acid Sound presets render");
    const auto [low, high] = std::minmax_element(presetEnergy.begin(),
                                                 presetEnergy.end());
    REQUIRE(*high > *low * 1.08, "Acid Sound presets materially differ");

    VoxGenerationContextV1 context {};
    context.structSize = sizeof(context);
    context.globalSeed = 99;
    context.slotId = instrument::initialSlotId(4);
    context.rootNote = 48;
    context.bpm = 138.0;
    VoxPatternV1 output {};
    output.structSize = sizeof(output);
    REQUIRE(provider->generatePattern(acid::instrumentId, "forest-acid",
                context, nullptr, output) == instrument::ContentStatus::ok,
            "provider generates caller-owned Acid pattern");
    REQUIRE(output.schemaVersion == VOX_PATTERN_SCHEMA_V1
            && output.stepCount > 0 && output.stepCount <= VOX_PATTERN_MAX_STEPS,
            "generated POD pattern bounded and versioned");
    // Generation offsets are relative to the host's actual root. The Acid
    // grammar confines absolute notes to its declared 24..96 musical range.
    for (const auto *profileId :
         {"classic-303", "psy-acid", "dark-acid", "forest-acid", "hi-tech-acid",
          "hypnotic-acid"}) {
      for (int root : {0, 12, 24, 36, 48, 60, 96, 127}) {
        context.rootNote = root;
        VoxPatternV1 rootPattern{sizeof(VoxPatternV1)};
        REQUIRE(provider->generatePattern(acid::instrumentId, profileId,
                                          context, nullptr, rootPattern) ==
                    instrument::ContentStatus::ok,
                "Acid profile accepts each valid host MIDI root");
        REQUIRE(
            sequence::validPattern(rootPattern),
            "generated root-specific pattern satisfies the shared ABI bounds");
        REQUIRE(std::all_of(rootPattern.steps,
                            rootPattern.steps + rootPattern.stepCount,
                            [root](const auto &step) {
                              const int absolute = root + step.noteOffset;
                              return absolute >= 24 && absolute <= 96;
                            }),
                "generated pitches use selected root and stay in Acid range");
      }
    }
    std::array<std::byte, sizeof(VoxPatternV1)> previousOutput{};
    std::memcpy(previousOutput.data(), &output, sizeof(output));
    for (int root : {-1, 128}) {
      context.rootNote = root;
      REQUIRE(provider->generatePattern(acid::instrumentId, "forest-acid",
                                        context, nullptr, output) ==
                  instrument::ContentStatus::invalidArgument,
              "invalid host root is rejected");
      REQUIRE(std::memcmp(&output, previousOutput.data(), sizeof(output)) == 0,
              "invalid root preserves caller-owned output");
    }
    context.rootNote = 48;
    REQUIRE(provider->generatePattern("com.ultimavox.psy-bass", "forest-acid",
                context, nullptr, output) == instrument::ContentStatus::notFound,
            "wrong-instrument generation rejected");

    std::cout << "Acid tests passed (" << checks
              << ", compliance " << compliance.checks << ")\n";
    return EXIT_SUCCESS;
}
