#pragma once
#include <array>
#include <string_view>

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

} // namespace vstengine::bass
