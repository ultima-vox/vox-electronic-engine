#if __has_include("acid/AcidProvider.h")
#include "acid/AcidProvider.h"
#include "acid/AcidEngine.h"
#else
#include "AcidProvider.h"
#include "AcidEngine.h"
#endif

#include <array>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace vstengine::acid {

namespace {

struct ParameterMetadata {
    std::string_view id;
    std::string_view name;
    float defaultValue;
};

constexpr std::array<ParameterMetadata, AcidEngine::parameterCount> kAcidSchema {{
    // Core 9 parameters (V1 ABI compatible)
    { "waveform",        "Waveform",          0.50f },
    { "cutoff",          "Cutoff",            0.37f },
    { "resonance",       "Resonance",         0.75f },
    { "envelope",        "Filter Env Amt",    0.60f },
    { "decay",           "Filter Decay",      0.35f },
    { "accent",          "Accent Amount",     0.58f },
    { "slide",           "Slide Time",        0.30f },
    { "drive",           "Distortion Drive",  0.65f },
    { "output",          "Output Volume",     0.75f },

    // Oscillator Section
    { "osc_tune",        "Oscillator Tune",   0.50f },
    { "osc_fine",        "Oscillator Fine",   0.50f },
    { "osc_pw",          "Pulse Width",       0.50f },

    // Filter Section
    { "filter_mode",     "Filter Mode",       0.00f },
    { "key_tracking",    "Key Track",         0.20f },

    // Envelope Section (AMP & FILTER ADSR)
    { "amp_attack",      "Amp Attack",        0.00f },
    { "amp_decay",       "Amp Decay",         0.22f },
    { "amp_sustain",     "Amp Sustain",       0.80f },
    { "amp_release",     "Amp Release",       0.35f },
    { "flt_attack",      "Filter Attack",     0.00f },
    { "flt_sustain",     "Filter Sustain",    0.00f },
    { "flt_release",     "Filter Release",    0.25f },

    // Distortion / Drive Section
    { "dist_mode",       "Distortion Mode",   0.00f },
    { "dist_tone",       "Distortion Tone",   0.40f },
    { "dist_mix",        "Distortion Mix",    0.70f },

    // Output Pan
    { "pan",             "Output Pan",        0.50f },

    // Modulation Section
    { "mod_target",      "Mod Target",        0.00f },
    { "mod_amount",      "Mod Amount",        0.70f },
    { "mod_attack",      "Mod Attack",        0.00f },
    { "mod_decay",       "Mod Decay",         0.38f },
    { "mod_sustain",     "Mod Sustain",       0.80f },
    { "mod_release",     "Mod Release",       0.42f },

    // Performance & Play Mode Section
    { "portamento",      "Portamento",        0.00f },
    { "unison_voices",   "Unison Voices",     0.00f },
    { "unison_detune",   "Unison Detune",     0.10f },
    { "stereo_spread",   "Stereo Spread",     0.40f },
    { "play_mode_flags", "Play Mode Flags",   0.75f }
}};

class AcidInstrumentInstance final : public instrument::InstrumentInstance {
public:
    bool prepare(double sampleRate, std::uint32_t maximumBlockSize,
                 std::uint32_t outputChannels) noexcept override {
        return engine_.prepare(sampleRate, maximumBlockSize, outputChannels);
    }

    void reset() noexcept override {
        engine_.reset();
    }

    void process(std::span<float*> outputs, std::uint32_t sampleCount,
                 std::span<const VoxMidiEventV1> midi) noexcept override {
        engine_.process(outputs, sampleCount, midi);
    }

    bool setParameter(std::string_view id, float normalizedValue) noexcept override {
        return engine_.setParameter(id, normalizedValue);
    }

    [[nodiscard]] float parameter(std::string_view id) const noexcept override {
        return engine_.parameter(id);
    }

    bool loadState(std::uint32_t schemaVersion,
                   std::span<const std::byte> payload) noexcept override {
        return engine_.loadState(schemaVersion, payload);
    }

    bool saveState(std::span<std::byte> destination,
                   std::uint32_t& bytesWritten) const noexcept override {
        return engine_.saveState(destination, bytesWritten);
    }

    [[nodiscard]] std::uint32_t latencySamples() const noexcept override {
        return engine_.latencySamples();
    }

    [[nodiscard]] std::uint32_t tailSamples() const noexcept override {
        return engine_.tailSamples();
    }

private:
    AcidEngine engine_;
};

class AcidInstrumentProvider final : public instrument::InstrumentProvider {
public:
    [[nodiscard]] std::string_view id() const noexcept override {
        return instrumentId;
    }

    [[nodiscard]] std::string_view name() const noexcept override {
        return "Acid";
    }

    [[nodiscard]] std::uint32_t stateSchemaVersion() const noexcept override {
        return AcidEngine::stateVersion;
    }

    [[nodiscard]] std::vector<instrument::ParameterSpec> parameterSchema() const override {
        std::vector<instrument::ParameterSpec> specs;
        specs.reserve(kAcidSchema.size());
        for (const auto& item : kAcidSchema) {
            instrument::ParameterSpec spec {};
            spec.id = std::string(item.id);
            spec.name = std::string(item.name);
            spec.defaultValue = item.defaultValue;
            specs.push_back(std::move(spec));
        }
        return specs;
    }

    [[nodiscard]] std::unique_ptr<instrument::InstrumentInstance> createInstance() const override {
        return std::make_unique<AcidInstrumentInstance>();
    }
};

} // namespace

std::unique_ptr<instrument::InstrumentProvider> createAcidProvider() {
    return std::make_unique<AcidInstrumentProvider>();
}

} // namespace vstengine::acid