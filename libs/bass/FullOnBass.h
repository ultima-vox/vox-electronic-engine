#pragma once
#include "bass/PsyBassVoice.h"
#include "instrument/InstrumentContract.h"

namespace vstengine::bass {

inline constexpr std::string_view fullOnInstrumentId = "com.ultimavox.fullon-bass";
struct SoundParameter {
    std::string_view id, name, unit, group;
    float minimum, maximum, initial;
};
// Immutable payload order. Do not reorder or reinterpret version-1 entries.
inline constexpr std::array<SoundParameter, 13> fullOnParameters {{
    { "filter.cutoff", "Cutoff", "Hz", "Filter", 35, 12000, 550 },
    { "filter.resonance", "Resonance", "", "Filter", 0, 1, .55f },
    { "pitch.amount", "Pitch amount", "st", "Pitch", 0, 24, 9.6f },
    { "pitch.time", "Pitch time", "ms", "Pitch", 1, 100, 18 },
    { "amp.attack", "Attack", "ms", "Amplitude", .1f, 100, .8f },
    { "amp.decay", "Decay", "ms", "Amplitude", 1, 500, 60 },
    { "amp.sustain", "Sustain", "", "Amplitude", 0, 1, .5f },
    { "amp.release", "Release", "ms", "Amplitude", 1, 250, 28 },
    { "osc.drive", "Oscillator drive", "x", "Character", 1, 6, 2.6f },
    { "filter.drive", "Filter drive", "x", "Filter", 1, 5, 2.28f },
    { "filter.keytracking", "Key tracking", "", "Filter", 0, 1, .205f },
    { "output.gain", "Output gain", "dB", "Output", -60, 6, -2.85f },
    { "pitch.curve", "Pitch curve", "", "Pitch", .25f, 8, 2 }
}};

// Catalog registration is intentionally absent until the generic
// direct host automation/state path is implemented. This is real DSP, not a UI
// stand-in. All live methods require one audio-thread owner.
class FullOnBassInstance final : public instrument::InstrumentInstance {
public:
    FullOnBassInstance() noexcept;
    static instrument::InstrumentDescriptor descriptor();
    bool prepare(const instrument::PrepareSpec&) override;
    void reset() noexcept override;
    void releaseAllNotes() noexcept;
    void suspend() noexcept override;
    void resume() noexcept override;
    void setBypassed(bool) noexcept override;
    void process(const instrument::ProcessBlock&) noexcept override;
    bool setParameter(std::string_view, float) noexcept override;
    bool setParameterByIndex(std::size_t, float) noexcept;
    bool loadState(std::uint32_t, std::span<const std::byte>) noexcept override;
    bool saveState(std::span<std::byte>, std::uint32_t&) const noexcept override;
    std::uint32_t latencySamples() const noexcept override { return 0; }
    std::uint32_t tailSamples() const noexcept override;
private:
    void updateVoice() noexcept;
    std::array<float, fullOnParameters.size()> values {};
    PsyBassVoice voice;
    instrument::PrepareSpec spec;
    int currentNote { -1 }, currentChannel { -1 };
    std::array<bool, 16> sustainDown {};
    bool keyHeld {};
    std::uint32_t dirtyParameters { (1u << fullOnParameters.size()) - 1u };
    bool bypassed {}, suspended {};
};
} // namespace vstengine::bass
