#include "acid/AcidProvider.h"
#include "acid/AcidEngine.h"
#include "acid/AcidGenerator.h"
#include <algorithm>
#include <array>
#include <cstring>

namespace vstengine::acid {
namespace {
class AcidInstance final : public instrument::InstrumentInstance {
public:
    bool prepare(const instrument::PrepareSpec& spec) override
    {
        return engine_.prepare(spec.sampleRate, spec.maximumBlockSize,
                               spec.outputChannels);
    }
    void reset() noexcept override { engine_.reset(); }
    void suspend() noexcept override { suspended_ = true; engine_.reset(); }
    void resume() noexcept override { suspended_ = false; }
    void setBypassed(bool bypassed) noexcept override
    {
        if (bypassed_ != bypassed && bypassed) engine_.reset();
        bypassed_ = bypassed;
    }
    void process(const instrument::ProcessBlock& block) noexcept override
    {
        if (!bypassed_ && !suspended_)
            engine_.process(block.outputs, block.sampleCount, block.midi);
    }
    bool setParameter(std::string_view id, float value) noexcept override
    {
        return engine_.setParameter(id, value);
    }
    bool loadState(std::uint32_t schema,
                   std::span<const std::byte> payload) noexcept override
    {
        return engine_.loadState(schema, payload);
    }
    bool saveState(std::span<std::byte> destination,
                   std::uint32_t& written) const noexcept override
    {
        return engine_.saveState(destination, written);
    }
    std::uint32_t latencySamples() const noexcept override
    {
        return engine_.latencySamples();
    }
    std::uint32_t tailSamples() const noexcept override
    {
        return engine_.tailSamples();
    }
private:
    AcidEngine engine_;
    bool bypassed_ {};
    bool suspended_ {};
};

struct SoundPreset {
    const char* id;
    const char* name;
    std::array<float, AcidEngine::parameterCount> values;
};

constexpr std::array soundPresets {
    SoundPreset { "classic-303", "Classic 303",
        { 0.05f, 0.46f, 0.72f, 0.70f, 0.48f, 0.58f, 0.30f, 0.28f, 0.72f } },
    SoundPreset { "psy-acid", "Psy Acid",
        { 0.18f, 0.55f, 0.68f, 0.84f, 0.34f, 0.76f, 0.22f, 0.48f, 0.68f } },
    SoundPreset { "dark-acid", "Dark Acid",
        { 0.42f, 0.29f, 0.82f, 0.62f, 0.60f, 0.66f, 0.42f, 0.55f, 0.70f } },
    SoundPreset { "forest-acid", "Forest Acid",
        { 0.30f, 0.39f, 0.78f, 0.76f, 0.52f, 0.80f, 0.36f, 0.62f, 0.66f } },
    SoundPreset { "hi-tech-acid", "Hi-Tech Acid",
        { 0.58f, 0.62f, 0.61f, 0.90f, 0.22f, 0.88f, 0.14f, 0.72f, 0.60f } },
    SoundPreset { "hypnotic-acid", "Hypnotic Acid",
        { 0.12f, 0.34f, 0.86f, 0.58f, 0.78f, 0.52f, 0.66f, 0.38f, 0.69f } }
};

AcidProfile profile(std::string_view id) noexcept
{
    if (id == "psy-acid") return AcidProfile::psyAcid;
    if (id == "dark-acid") return AcidProfile::darkAcid;
    if (id == "forest-acid") return AcidProfile::forestAcid;
    if (id == "hi-tech-acid") return AcidProfile::hiTechAcid;
    if (id == "hypnotic-acid") return AcidProfile::hypnoticAcid;
    return AcidProfile::classic303;
}

class AcidProvider final : public instrument::InstrumentProvider {
public:
    AcidProvider()
    {
        descriptor_.id = std::string(instrumentId);
        descriptor_.providerId = "com.ultimavox.builtin";
        descriptor_.name = "Acid";
        descriptor_.vendor = "Ultima Vox";
        descriptor_.instrumentVersion = 1;
        descriptor_.stateVersion = AcidEngine::stateVersion;
        descriptor_.contentVersion = 1;
        descriptor_.capabilities = instrument::Capability::notes
            | instrument::Capability::sequence
            | instrument::Capability::patternGenerator
            | instrument::Capability::modulation
            | instrument::Capability::standardEditor;
        descriptor_.budget = { 1, 2048, 4096, 0, 0, 153600,
                               64, 32, 65536 };
        constexpr std::array ids { "waveform", "cutoff", "resonance",
            "envelope", "decay", "accent", "slide", "drive", "output" };
        constexpr std::array names { "Waveform", "Cutoff", "Resonance",
            "Envelope", "Decay", "Accent", "Slide", "Drive", "Output" };
        for (std::size_t i = 0; i < ids.size(); ++i)
            descriptor_.parameters.push_back({ ids[i], names[i], "", 0.0f,
                1.0f, soundPresets[0].values[i], 0.0001f,
                instrument::ParameterType::floating,
                i == 8 ? "Output" : "Acid", {}, true, true,
                i < instrument::macrosPerSlot ? static_cast<std::int8_t>(i) : -1 });

        for (const auto& preset : soundPresets) {
            instrument::ContentDescriptor sound { std::string(instrumentId),
                preset.id, preset.name, instrument::ContentKind::soundPreset,
                1, 1, VOX_SEQUENCE_ALL, VOX_PRESET_CLASS_SOUND, 0xffu };
            std::copy_n(preset.values.begin(), instrument::macrosPerSlot,
                        sound.macroValues.begin());
            content_.push_back(std::move(sound));
            content_.push_back({ std::string(instrumentId), preset.id,
                preset.name, instrument::ContentKind::patternPreset, 1, 1,
                VOX_SEQUENCE_ALL, VOX_PRESET_CLASS_PATTERN });
            content_.push_back({ std::string(instrumentId), preset.id,
                preset.name, instrument::ContentKind::generatorProfile, 1, 1,
                VOX_SEQUENCE_ALL, VOX_PRESET_CLASS_GENERATOR });
        }
    }

    std::span<const instrument::InstrumentDescriptor>
        descriptors() const noexcept override { return { &descriptor_, 1 }; }

    std::unique_ptr<instrument::InstrumentInstance> create(
        std::string_view id, const instrument::CreateContext&) override
    {
        return id == instrumentId ? std::make_unique<AcidInstance>() : nullptr;
    }

    std::span<const instrument::ContentDescriptor> contentDescriptors(
        std::string_view id) const noexcept override
    {
        return id == instrumentId
            ? std::span<const instrument::ContentDescriptor>(content_) : std::span<const instrument::ContentDescriptor> {};
    }

    instrument::ContentStatus applySoundPreset(
        std::string_view id, std::string_view presetId,
        instrument::InstrumentInstance& instance) const noexcept override
    {
        if (id != instrumentId) return instrument::ContentStatus::notFound;
        for (const auto& preset : soundPresets)
            if (presetId == preset.id) {
                AcidEngine candidate;
                for (std::size_t i = 0; i < preset.values.size(); ++i)
                    if (!candidate.setParameter(descriptor_.parameters[i].id,
                                                preset.values[i]))
                        return instrument::ContentStatus::invalidArgument;
                std::array<std::byte, AcidEngine::encodedStateBytes> payload {};
                std::uint32_t written {};
                if (!candidate.saveState(payload, written)
                    || written != payload.size())
                    return instrument::ContentStatus::invalidArgument;
                return instance.loadState(AcidEngine::stateVersion, payload)
                    ? instrument::ContentStatus::ok
                    : instrument::ContentStatus::incompatible;
            }
        return instrument::ContentStatus::notFound;
    }

    instrument::ContentStatus generatePattern(
        std::string_view id, std::string_view profileId,
        const VoxGenerationContextV1& context, const VoxPatternV1*,
        VoxPatternV1& output) const noexcept override
    {
        if (id != instrumentId) return instrument::ContentStatus::notFound;
        if (context.structSize != sizeof(VoxGenerationContextV1)
            || output.structSize != sizeof(VoxPatternV1))
            return instrument::ContentStatus::invalidArgument;
        const auto found = std::find_if(soundPresets.begin(), soundPresets.end(),
            [profileId](const auto& preset) { return profileId == preset.id; });
        if (found == soundPresets.end()) return instrument::ContentStatus::notFound;
        const auto generated = AcidGenerator::generate(profile(profileId),
            context.globalSeed, context.slotId, instrumentId);
        VoxPatternV1 candidate {};
        candidate.structSize = sizeof(candidate);
        candidate.schemaVersion = VOX_PATTERN_SCHEMA_V1;
        candidate.stepCount = static_cast<std::uint32_t>(generated.size());
        candidate.timingMode = static_cast<std::uint32_t>(generated.getTimingMode());
        for (std::uint32_t i = 0; i < candidate.stepCount; ++i) {
            const auto& source = generated[static_cast<int>(i)];
            auto& target = candidate.steps[i];
            target.noteOffset = static_cast<std::int16_t>(source.noteOffset);
            target.gate = source.gate ? 1 : 0;
            target.accent = source.accent ? 1 : 0;
            target.velocity = source.velocity;
            target.probability = source.probability;
            target.ratchetCount = static_cast<std::uint8_t>(source.ratchetCount);
            target.slideDuration = source.slideDuration;
            target.gateWidth = source.gateWidth;
        }
        output = candidate;
        return instrument::ContentStatus::ok;
    }
private:
    instrument::InstrumentDescriptor descriptor_;
    std::vector<instrument::ContentDescriptor> content_;
};
} // namespace

std::unique_ptr<instrument::InstrumentProvider> createAcidProvider()
{
    return std::make_unique<AcidProvider>();
}
} // namespace vstengine::acid
