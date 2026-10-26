#include "acid/AcidEngine.h"
#include "acid/AcidGenerator.h"
#include "acid/AcidProvider.h"
#include "instrument/InstrumentComplianceHarness.h"
#include "support/ReferenceInstrument.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
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

    auto instance = provider->create(acid::instrumentId,
        { instrument::initialSlotId(1), nullptr });
    REQUIRE(instance && instance->prepare({ 48000.0, 4096, 2 }),
            "Acid instance prepares");
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
