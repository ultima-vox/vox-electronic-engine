#include "modules/BuiltInProvider.h"
#include "acid/AcidProvider.h"
#include "lead/LeadProvider.h"
#include "semanticfx/SemanticFxProvider.h"
#include "atmos/AtmosProvider.h"
#include "bass/BassGenerator.h"
#include "bass/PsyBassVoice.h"
#include "sequence/PatternAdapter.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace vstengine::modules {
namespace {
class BassInstance final : public instrument::InstrumentInstance {
public:
    bool prepare(const instrument::PrepareSpec& value) override
    {
        if (value.sampleRate <= 0.0 || value.maximumBlockSize == 0
            || value.outputChannels == 0 || value.outputChannels > 2)
            return false;
        spec = value;
        voice.prepareDirect(value.sampleRate);
        reset();
        return true;
    }
    void reset() noexcept override
    {
        voice.resetDirect();
    }
    void suspend() noexcept override { suspended = true; reset(); }
    void resume() noexcept override { suspended = false; }
    void setBypassed(bool value) noexcept override
    {
        if (bypassed == value) return;
        bypassed = value;
        if (bypassed) reset();
    }
    void process(const instrument::ProcessBlock& block) noexcept override
    {
        if (bypassed || suspended || block.sampleCount > spec.maximumBlockSize
            || block.outputs.empty()) return;
        updateVoice();
        juce::AudioBuffer<float> audio(block.outputs.data(),
            static_cast<int>(block.outputs.size()),
            static_cast<int>(block.sampleCount));
        std::uint32_t cursor = 0;
        for (const auto& event : block.midi) {
            if (event.sampleOffset >= block.sampleCount || event.size < 1) continue;
            if (event.sampleOffset > cursor)
                voice.renderNextBlock(audio, static_cast<int>(cursor),
                    static_cast<int>(event.sampleOffset - cursor));
            cursor = event.sampleOffset;
            const auto type = event.data[0] & 0xf0;
            if (event.size >= 3 && type == 0x90 && event.data[2] != 0)
                voice.noteOnDirect(event.data[1], event.data[2] / 127.0f);
            else if (event.size >= 3 && (type == 0x80 || type == 0x90))
                voice.noteOffDirect(true);
            else if (event.size >= 3 && type == 0xb0
                     && (event.data[1] == 120 || event.data[1] == 123))
                voice.resetDirect();
        }
        if (cursor < block.sampleCount)
            voice.renderNextBlock(audio, static_cast<int>(cursor),
                static_cast<int>(block.sampleCount - cursor));
    }
    bool setParameter(std::string_view id, float value) noexcept override
    {
        if (!std::isfinite(value)) return false;
        value = std::clamp(value, 0.0f, 1.0f);
        for (std::size_t i = 0; i < values.size(); ++i)
            if (ids[i] == id) { values[i] = value; return true; }
        return false;
    }
    bool loadState(std::uint32_t schema, std::span<const std::byte> payload) noexcept override
    {
        if (schema != instrument::stateSchemaVersion) return false;
        if (payload.empty()) return true;
        if (payload.size() != sizeof(values)) return false;
        std::memcpy(values.data(), payload.data(), sizeof(values));
        return std::all_of(values.begin(), values.end(), [](float value) {
            return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
        });
    }
    bool saveState(std::span<std::byte> destination,
                   std::uint32_t& written) const noexcept override
    {
        written = sizeof(values);
        if (destination.size() < written) return false;
        std::memcpy(destination.data(), values.data(), sizeof(values));
        return true;
    }
    std::uint32_t latencySamples() const noexcept override { return 0; }
    std::uint32_t tailSamples() const noexcept override
    {
        return static_cast<std::uint32_t>(spec.sampleRate * 0.25);
    }
private:
    void updateVoice() noexcept
    {
        voice.setFilterCutoff(0.05f + values[0] * 0.85f);
        voice.setFilterResonance(values[1]);
        voice.setPitchEnvelopeAmount(values[2] * 24.0f);
        voice.setAmpAttack(0.0002f + values[5] * 0.0012f);
        voice.setAmpDecay(0.015f + values[3] * 0.18f);
        voice.setAmpRelease(0.008f + values[3] * 0.08f);
        voice.setDrive(1.0f + values[4] * 5.0f);
        voice.setFilterDrive(1.0f + values[4] * 4.0f);
        voice.setAmpSustain(0.28f + values[5] * 0.45f);
        voice.setKeyTracking(0.1f + values[7] * 0.7f);
        voice.setOutputLevel(0.72f);
    }
    static constexpr std::array<std::string_view, 8> ids {
        "cutoff", "resonance", "envelope", "decay",
        "drive", "accent", "space", "motion"
    };
    std::array<float, 8> values { 0.48f, 0.55f, 0.4f, 0.25f,
                                  0.32f, 0.5f, 0.0f, 0.15f };
    bass::PsyBassVoice voice;
    instrument::PrepareSpec spec;
    bool bypassed {};
    bool suspended {};
};

class BuiltInProvider final : public instrument::InstrumentProvider {
public:
    explicit BuiltInProvider(bool includeAcid)
    {
        instrument::InstrumentDescriptor bassDescriptor;
        bassDescriptor.id = bassInstrumentId;
        bassDescriptor.providerId = "com.ultimavox.builtin";
        bassDescriptor.name = "Psy Bass";
        bassDescriptor.vendor = "Ultima Vox";
        bassDescriptor.capabilities = instrument::Capability::notes
            | instrument::Capability::sequence
            | instrument::Capability::patternGenerator
            | instrument::Capability::modulation
            | instrument::Capability::standardEditor;
        bassDescriptor.budget = { 1, 2048, 65536, 0, 0, 24000,
                                  2048, 64, 131072 };
        for (const auto id : { "cutoff", "resonance", "envelope", "decay",
                               "drive", "accent", "space", "motion" })
        {
            const auto macro = static_cast<std::int8_t>(
                bassDescriptor.parameters.size());
            bassDescriptor.parameters.push_back({ id, id, "", 0.0f, 1.0f,
                0.5f, 0.0001f, instrument::ParameterType::floating, "Sound",
                {}, true, true, macro });
        }
        descriptors_.push_back(std::move(bassDescriptor));
        for (const auto& [id, name] : std::array {
                 std::pair { "psy-bass", "Psy Bass" },
                 std::pair { "forest-bass", "Forest Bass" },
                 std::pair { "dark-bass", "Dark Bass" } })
            bassContent_.push_back({ std::string(bassInstrumentId), id, name,
                instrument::ContentKind::generatorProfile, 1, 1,
                VOX_SEQUENCE_ALL, VOX_PRESET_CLASS_GENERATOR });
        if (includeAcid) {
        acidProvider_ = acid::createAcidProvider();
        for (const auto& descriptor : acidProvider_->descriptors())
            descriptors_.push_back(descriptor);
        }
        leadProvider_ = lead::createLeadProvider();
        for (const auto& descriptor : leadProvider_->descriptors())
            descriptors_.push_back(descriptor);
        semanticFxProvider_ = semanticfx::createSemanticFxProvider();
        for (const auto& descriptor : semanticFxProvider_->descriptors())
            descriptors_.push_back(descriptor);
        atmosProvider_ = atmos::createAtmosProvider();
        for (const auto& descriptor : atmosProvider_->descriptors())
            descriptors_.push_back(descriptor);
    }
    std::span<const instrument::InstrumentDescriptor>
        descriptors() const noexcept override { return descriptors_; }
    std::unique_ptr<instrument::InstrumentInstance> create(
        std::string_view id, const instrument::CreateContext& context) override
    {
        if (id == bassInstrumentId) return std::make_unique<BassInstance>();
        if (id == acid::instrumentId && acidProvider_)
            return acidProvider_->create(id, context);
        if (id == lead::instrumentId)
            return leadProvider_->create(id, context);
        if (id == semanticfx::instrumentId)
            return semanticFxProvider_->create(id, context);
        if (id == atmos::instrumentId)
            return atmosProvider_->create(id, context);
        return {};
    }
    std::span<const instrument::ContentDescriptor> contentDescriptors(
        std::string_view id) const noexcept override
    {
        if (id == bassInstrumentId) return bassContent_;
        if (id == acid::instrumentId && acidProvider_)
            return acidProvider_->contentDescriptors(id);
        if (id == lead::instrumentId)
            return leadProvider_->contentDescriptors(id);
        if (id == semanticfx::instrumentId)
            return semanticFxProvider_->contentDescriptors(id);
        if (id == atmos::instrumentId)
            return atmosProvider_->contentDescriptors(id);
        return {};
    }
    instrument::ContentStatus applySoundPreset(
        std::string_view id, std::string_view preset,
        instrument::InstrumentInstance& instance) const noexcept override
    {
        if (id == acid::instrumentId && acidProvider_)
            return acidProvider_->applySoundPreset(id, preset, instance);
        if (id == lead::instrumentId)
            return leadProvider_->applySoundPreset(id, preset, instance);
        if (id == semanticfx::instrumentId)
            return semanticFxProvider_->applySoundPreset(id, preset, instance);
        if (id == atmos::instrumentId)
            return atmosProvider_->applySoundPreset(id, preset, instance);
        return instrument::ContentStatus::notFound;
    }
    instrument::ContentStatus generatePattern(
        std::string_view id, std::string_view profile,
        const VoxGenerationContextV1& context, const VoxPatternV1* input,
        VoxPatternV1& output) const noexcept override
    {
        if (id == bassInstrumentId) {
            if (context.structSize != sizeof(VoxGenerationContextV1)
                || output.structSize != sizeof(VoxPatternV1))
                return instrument::ContentStatus::invalidArgument;
            bass::BassProfile bassProfile;
            if (profile == "psy-bass") bassProfile = bass::BassProfile::psy;
            else if (profile == "forest-bass")
                bassProfile = bass::BassProfile::forest;
            else if (profile == "dark-bass")
                bassProfile = bass::BassProfile::dark;
            else return instrument::ContentStatus::notFound;
            output = sequence::toPattern(bass::BassGenerator::generate(
                bassProfile, context.globalSeed, context.slotId,
                bassInstrumentId));
            return sequence::validPattern(output)
                ? instrument::ContentStatus::ok
                : instrument::ContentStatus::invalidArgument;
        }
        if (id == acid::instrumentId && acidProvider_)
            return acidProvider_->generatePattern(id, profile, context, input,
                                                   output);
        if (id == lead::instrumentId)
            return leadProvider_->generatePattern(id, profile, context, input,
                                                   output);
        if (id == semanticfx::instrumentId)
            return semanticFxProvider_->generatePattern(id, profile, context,
                                                         input, output);
        if (id == atmos::instrumentId)
            return atmosProvider_->generatePattern(id, profile, context, input,
                                                    output);
        return instrument::ContentStatus::notFound;
    }
private:
    std::vector<instrument::InstrumentDescriptor> descriptors_;
    std::vector<instrument::ContentDescriptor> bassContent_;
    std::unique_ptr<instrument::InstrumentProvider> acidProvider_;
    std::unique_ptr<instrument::InstrumentProvider> leadProvider_;
    std::unique_ptr<instrument::InstrumentProvider> semanticFxProvider_;
    std::unique_ptr<instrument::InstrumentProvider> atmosProvider_;
};
} // namespace

std::unique_ptr<instrument::InstrumentProvider> createBuiltInProvider(bool includeAcid)
{
    return std::make_unique<BuiltInProvider>(includeAcid);
}
} // namespace vstengine::modules
