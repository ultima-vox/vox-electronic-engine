#pragma once

// VOX Drums pattern and song generator (DRM-05). Message thread; pure
// functions of the settings: equal settings and input give equal output.
// It writes StoredPattern / SongData, the one event model the sequencer
// plays and the MIDI export writes; it has no scheduler of its own.
//
// Lane roles follow the pad layout of the default and factory kits: 1 kick,
// 2 snare, 3 clap, 4 closed hat, 5 open hat, 6 tom, 7 rim, 8 cymbal, 9-16
// percussion.
//
// Locks: a locked step (PatternStep::locked) and every step of a locked
// lane (laneLocks) are left exactly as they are by every function here.
//
// Roles (DROL, option B): every function takes a RoleMap. The style rules
// work on role slots; each slot's result goes to the pad lane that has that
// role. Lanes without a slot (role none, a duplicate role) are cleared like
// any lane the style leaves empty. Locks and the Mutate Lanes mask are pad
// lanes. With RoleMap::identity() the output is exactly as before roles.

#include "PadRoles.h"
#include "PatternStore.h"

namespace vox::drums {

inline constexpr preset::Tag generatorSectionTag = preset::makeTag ("DGEN");

enum class Style : std::uint8_t { fullOn, goa, house, techno, trap, breaks, ambient };
inline constexpr int styleCount = 7;
const char* styleName (Style) noexcept;

struct GeneratorSettings {
    Style style = Style::fullOn;
    float density = 0.5f;       // how many hits
    float groove = 0.5f;        // velocity shaping (accents against the beat)
    float syncopation = 0.3f;   // off-beat placement
    float swing = 0.0f;         // delays the off 16ths (written as microtiming)
    float humanize = 0.2f;      // velocity and timing variance
    float complexity = 0.4f;    // ghost notes, ratchets, probability
    float fills = 0.5f;         // fill length and intensity
    float variation = 0.4f;     // difference between bars, and of B from A
    std::uint32_t seed = 1;
    std::uint16_t laneLocks = 0;   // bit n: lane n is locked

    bool operator== (const GeneratorSettings&) const noexcept;
    bool operator!= (const GeneratorSettings& o) const noexcept { return ! (*this == o); }
};

preset::Section encodeGenerator (const GeneratorSettings&);
// Strict: unknown version, wrong size or a value out of range is rejected.
bool decodeGenerator (const preset::Section&, GeneratorSettings& out, juce::String& error);

// A new pattern in `pattern`'s grid (its steps and bars are kept).
void generatePattern (const GeneratorSettings&, StoredPattern& pattern, const RoleMap& roles = RoleMap::identity());
// Regenerates only the lanes in `laneMask` (bit n = lane n).
// The mask is a set of pad lanes; lanes outside it are not touched.
void mutateLanes (const GeneratorSettings&, std::uint32_t laneMask, StoredPattern& pattern, const RoleMap& roles = RoleMap::identity());
// Writes a fill into the end of bar `bar` (the last bar: -1).
void generateFill (const GeneratorSettings&, StoredPattern& pattern, int bar = -1, const RoleMap& roles = RoleMap::identity());
// B from A: A with `variation` applied and a fill in its last bar. B takes
// A's grid; B's locked steps and lanes are kept. If B has another grid than
// A and holds a locked step or a hit on a locked lane, B is not changed.
void generateVariation (const GeneratorSettings&, const StoredPattern& a, StoredPattern& b, const RoleMap& roles = RoleMap::identity());
// A whole song of the current length: the five slots (intro, main, lift,
// break, peak) with their B variations, arranged on the timeline in
// eight-bar phrases. Song length, mode and loop are kept; locked steps and
// lanes of the slots are kept; unique bars are replaced.
void generateSong (const GeneratorSettings&, PatternStore::Everything& all, const RoleMap& roles = RoleMap::identity());

} // namespace vox::drums
