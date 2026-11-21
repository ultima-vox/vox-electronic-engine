#pragma once

// Factory capacity is the contract. Installed tables are generated candidates.
// human-approved stays 0 until a documented listening review exists.
// ProgramCategory ordinals are written into UBNK. Do not reorder or insert.

#include <array>
#include <cstddef>
#include <cstdint>

namespace vox::syn {

inline constexpr int factoryProgramCapacity = 600;
inline constexpr int factoryPhraseCapacity = 120;
inline constexpr int factoryProgramsInstalled = 600;
inline constexpr int factoryProgramsCandidate = 600;
inline constexpr int factoryProgramsHumanApproved = 0;
inline constexpr int factoryPhrasesInstalled = 120;
inline constexpr int factoryPhrasesHumanApproved = 0;

enum class ProgramCategory : std::uint8_t {
    bass, acid, lead, pluck, keys, pad, atmos, fx, user, count
};

static_assert (static_cast<int> (ProgramCategory::bass) == 0);
static_assert (static_cast<int> (ProgramCategory::acid) == 1);
static_assert (static_cast<int> (ProgramCategory::lead) == 2);
static_assert (static_cast<int> (ProgramCategory::pluck) == 3);
static_assert (static_cast<int> (ProgramCategory::keys) == 4);
static_assert (static_cast<int> (ProgramCategory::pad) == 5);
static_assert (static_cast<int> (ProgramCategory::atmos) == 6);
static_assert (static_cast<int> (ProgramCategory::fx) == 7);
static_assert (static_cast<int> (ProgramCategory::user) == 8);
static_assert (static_cast<int> (ProgramCategory::count) == 9);

inline constexpr std::array<const char*, static_cast<std::size_t> (ProgramCategory::count)> categoryNames {{
    "Bass", "Acid", "Lead", "Pluck", "Keys", "Pad", "Atmos", "FX", "User"
}};

// Primary factory role. Not stored in UBNK and not an alias of ProgramCategory.
enum class FactoryRole : std::uint8_t {
    bass, acid, lead, pluckKeys, padAtmos, arp, stab, count
};

inline constexpr std::array<const char*, static_cast<std::size_t> (FactoryRole::count)> factoryRoleNames {{
    "Bass", "Acid", "Lead", "Pluck/Keys", "Pad/Atmos", "Arp", "Stab"
}};

inline constexpr std::array<int, static_cast<std::size_t> (FactoryRole::count)> factoryRoleQuota {{
    120, 90, 110, 80, 90, 75, 35
}};

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
