#pragma once

// Factory capacity is the contract. Ready counts are curated programs and
// phrases that actually exist. Zero means the slot is reserved, not filled.

#include <array>
#include <cstddef>
#include <cstdint>

namespace vox::syn {

inline constexpr int factoryProgramCapacity = 600;
inline constexpr int factoryPhraseCapacity = 120;
inline constexpr int factoryProgramsReady = 600;
inline constexpr int factoryPhrasesReady = 120;

enum class ProgramCategory : std::uint8_t {
    bass, acid, lead, pluck, keys, pad, atmos, fx, user, count
};

inline constexpr std::array<const char*, static_cast<std::size_t> (ProgramCategory::count)> categoryNames {{
    "Bass", "Acid", "Lead", "Pluck", "Keys", "Pad", "Atmos", "FX", "User"
}};

// Phrase schema for the later scheduler. No notes are approved yet.
struct PhraseNote {
    int tick = 0;
    int note = 0;
    int velocity = 0;
    int duration = 0;
    bool slide = false;
    bool accent = false;
};

struct Phrase {
    const char* name = "";
    ProgramCategory category = ProgramCategory::bass;
    int ticksPerQuarter = 960;
    const PhraseNote* notes = nullptr;
    int noteCount = 0;
};

} // namespace vox::syn
