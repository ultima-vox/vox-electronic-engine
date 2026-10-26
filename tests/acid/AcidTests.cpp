#include "acid/AcidEngine.h"
#include "acid/AcidParameterBridge.h"
#include "acid/AcidGenerator.h"
#include "acid/AcidProvider.h"
#include "instrument/InstrumentComplianceHarness.h"
#include "support/ReferenceInstrument.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
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
    auto provider = acid::createAcidProvider();
    const auto compliance = tests::runInstrumentCompliance(
        *provider, acid::instrumentId);
    if (!compliance) {
        for (const auto& failure : compliance.failures)
            std::cerr << "COMPLIANCE: " << failure << '\n';
    }
    REQUIRE(compliance.passed(), "Acid passes common module compliance");

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
            "all 36 Acid parameters are advertised by the production descriptor");
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

    // Acid-only legacy migration and malformed-state transaction checks.
    acid::AcidEngine soundState;
    // Contract regression: a fresh Rack slot has no serialized module bytes.
    // This is not an error and must not leave Acid unresolved/silent.
    REQUIRE(soundState.loadState(acid::AcidEngine::legacyStateVersion, {}),
            "new Acid slot accepts empty legacy module state");
    REQUIRE(soundState.loadState(acid::AcidEngine::stateVersion, {}),
            "new Acid slot accepts empty current module state");
    REQUIRE(!soundState.loadState(acid::AcidEngine::stateVersion + 1u, {}),
            "unknown future Acid schema rejected even with empty bytes");
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
    REQUIRE(soundState.loadState(legacyVersion, oldState),
            "Acid v1 sound state migrates into current engine");
    for (std::size_t i = 0; i < legacyValues.size(); ++i)
        REQUIRE(std::abs(soundState.parameter(acid::AcidEngine::parameterIds[i])
                         - legacyValues[i]) < 0.00001f,
                "Acid v1 migration preserves all nine legacy parameters");

    std::array<std::byte, acid::AcidEngine::encodedStateBytes> encoded {};
    std::uint32_t written {};
    REQUIRE(soundState.saveState(encoded, written)
                && written == encoded.size(), "Acid v3 save succeeds");
    const float originalCutoff = soundState.parameter("cutoff");
    auto malformed = encoded;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    std::memcpy(malformed.data() + 8 + 25 * sizeof(float),
                &nan, sizeof(nan));
    REQUIRE(!soundState.loadState(acid::AcidEngine::stateVersion, malformed),
            "Acid v3 rejects NaN without applying any parameters");
    REQUIRE(std::abs(soundState.parameter("cutoff") - originalCutoff) < 0.00001f,
            "rejected Acid v3 payload cannot partially alter cutoff");

    auto invalidOld = oldState;
    std::memcpy(invalidOld.data() + 8 + 8 * sizeof(float),
                &nan, sizeof(nan));
    REQUIRE(!soundState.loadState(legacyVersion, invalidOld),
            "Acid v1 migration rejects malformed float");
    REQUIRE(std::abs(soundState.parameter("cutoff") - originalCutoff) < 0.00001f,
            "rejected v1 migration leaves pre-existing sound values intact");
    acid::AcidEngine restoredSound;
    REQUIRE(restoredSound.loadState(acid::AcidEngine::stateVersion, encoded),
            "Acid v3 save/load round-trips");
    for (std::size_t i = 0; i < acid::AcidEngine::parameterCount; ++i)
        REQUIRE(std::abs(restoredSound.parameter(acid::AcidEngine::parameterIds[i])
                         - soundState.parameter(acid::AcidEngine::parameterIds[i]))
                         < 0.00001f, "Acid v3 restores every semantic parameter");

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
             parameter < acid::AcidEngine::playModeFlags; ++parameter)
            REQUIRE(uniqueHostIds.insert(acid::hostParameterId(
                slot, acid::AcidEngine::parameterIds[parameter])).second,
                "Acid host automation identity unique across slots and controls");
    REQUIRE(uniqueHostIds.size() == instrument::maxSlots
                                  * (acid::AcidEngine::playModeFlags
                                     - instrument::macrosPerSlot),
            "all 432 extended Acid parameter IDs are unique");

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
            && shortBlock->loadState(acid::AcidEngine::legacyStateVersion, {})
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
    REQUIRE(provider->generatePattern("com.ultimavox.psy-bass", "forest-acid",
                context, nullptr, output) == instrument::ContentStatus::notFound,
            "wrong-instrument generation rejected");

    std::cout << "Acid tests passed (" << checks
              << ", compliance " << compliance.checks << ")\n";
    return EXIT_SUCCESS;
}
