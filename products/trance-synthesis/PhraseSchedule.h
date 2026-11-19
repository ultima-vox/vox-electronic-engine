#pragma once

// Canonical phrase schedule. Live audition and the SMF writer both start here.
//
// SMF type 0 carries tick, note, velocity, duration, tempo and end-of-track.
// It does not carry slide or accent. Importing an SMF drops those flags.
// That is a format limit, not a claim of parity.
//
// Native audition keeps the flags:
//   slide  Mono Acid and Psy tie the gate into the next note and glide,
//          even when the slide knob is 0. VA stays polyphonic and ignores slide.
//   accent Acid opens the filter, Psy raises the level, VA raises the voice
//          gain. The stored velocity is not rewritten. Host MIDI has no flag:
//          Acid still treats velocity >= 110 as an accent, Psy and VA do not.

#include "FactoryBank.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace vox::syn {

inline constexpr int phraseTempoBpm = 120;
inline constexpr int phrasePulsesPerQuarter = 960;
inline constexpr char phraseSmfLimit[] = "slide/accent are not in this SMF";

struct ScheduledEvent {
    int tick = 0;
    bool on = false;
    int note = 0;
    int velocity = 0;
    bool slide = false;
    bool accent = false;
};

struct AuditionEvent {
    int sample = 0;
    bool on = false;
    int note = 0;
    int velocity = 0;
    bool slide = false;
    bool accent = false;
};

// Half-away-from-zero. The same tick lands on the same sample at a given rate
// no matter which block size later slices the buffer.
[[nodiscard]] inline int phraseSample (int tick, double sampleRate) noexcept
{
    const double seconds = static_cast<double> (tick) * 60.0
                         / (static_cast<double> (phraseTempoBpm) * phrasePulsesPerQuarter);
    return static_cast<int> (std::llround (seconds * sampleRate));
}

[[nodiscard]] inline std::vector<ScheduledEvent> schedulePhrase (const PhraseNote* notes, int count)
{
    std::vector<int> off (static_cast<std::size_t> (std::max (0, count)));
    std::vector<char> into (static_cast<std::size_t> (std::max (0, count)));
    for (int i = 0; i < count; ++i)
        off[static_cast<std::size_t> (i)] = notes[i].tick + notes[i].duration;
    for (int i = 0; i < count; ++i)
    {
        if (! notes[i].slide)
            continue;
        int destTick = -1;
        for (int j = 0; j < count; ++j)
            if (notes[j].tick > notes[i].tick && (destTick < 0 || notes[j].tick < destTick))
                destTick = notes[j].tick;
        if (destTick < 0)
            continue;
        off[static_cast<std::size_t> (i)] = destTick;
        for (int j = 0; j < count; ++j)
            if (notes[j].tick == destTick)
                into[static_cast<std::size_t> (j)] = 1;
    }

    std::vector<ScheduledEvent> events;
    events.reserve (static_cast<std::size_t> (count) * 2u);
    for (int i = 0; i < count; ++i)
    {
        ScheduledEvent on;
        on.tick = notes[i].tick;
        on.on = true;
        on.note = notes[i].note;
        on.velocity = notes[i].velocity;
        on.slide = into[static_cast<std::size_t> (i)] != 0;
        on.accent = notes[i].accent;
        events.push_back (on);
        ScheduledEvent done;
        done.tick = off[static_cast<std::size_t> (i)];
        done.on = false;
        done.note = notes[i].note;
        events.push_back (done);
    }
    std::sort (events.begin(), events.end(), [] (const ScheduledEvent& a, const ScheduledEvent& b) {
        if (a.tick != b.tick)
            return a.tick < b.tick;
        if (a.on != b.on)
            return a.on && ! b.on;
        return a.note < b.note;
    });
    return events;
}

} // namespace vox::syn
