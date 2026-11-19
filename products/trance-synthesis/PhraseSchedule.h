#pragma once

// One note list, two projections. Policy A.
//
// Authored: NoteOff stays at tick + duration. This is what SMF type 0 stores,
// together with note and velocity. Slide and accent are not in the file.
// At a shared tick the file plays NoteOff before NoteOn.
//
// Performance: a slide note holds its gate until the next note and that next
// note is marked slide. NoteOn is ordered before NoteOff at that tick so the
// mono gate does not drop before the glide. VA does not glide. Accent never
// rewrites velocity. A same-pitch NoteOff does not clear a newer NoteOn;
// the engine counts stacked ons.
//
// Host MIDI has no slide flag. Acid still treats velocity >= 110 as an accent.
// Psy and VA do not.

#include "FactoryBank.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace vox::syn {

inline constexpr int phraseTempoBpm = 120;
inline constexpr int phrasePulsesPerQuarter = 960;
inline constexpr char phraseSmfLimit[] = "slide/accent are not in this SMF";

enum class PhraseProjection { authored, performance };

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

inline constexpr int maxPhraseNotes = 64;
inline constexpr int maxPhraseEvents = maxPhraseNotes * 2;

// Fixed performance schedule. The control thread builds it. The audio thread only copies it.
struct PhraseSnapshot {
    int loopTicks = 1;
    int eventCount = 0;
    ScheduledEvent events[maxPhraseEvents] {};
};

[[nodiscard]] inline bool fillPerformanceSnapshot (const PhraseNote* notes, int count, int lengthTicks,
                                                   PhraseSnapshot& out)
{
    out = {};
    if (count < 0 || count > maxPhraseNotes || (notes == nullptr && count > 0))
        return false;
    int off[maxPhraseNotes];
    char into[maxPhraseNotes] {};
    for (int i = 0; i < count; ++i)
        off[i] = notes[i].tick + notes[i].duration;
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
        off[i] = destTick;
        for (int j = 0; j < count; ++j)
            if (notes[j].tick == destTick)
                into[j] = 1;
    }

    ScheduledEvent built[maxPhraseEvents];
    int builtCount = 0;
    for (int i = 0; i < count; ++i)
    {
        ScheduledEvent on;
        on.tick = notes[i].tick;
        on.on = true;
        on.note = notes[i].note;
        on.velocity = notes[i].velocity;
        on.slide = into[i] != 0;
        on.accent = notes[i].accent;
        built[builtCount++] = on;
        ScheduledEvent done;
        done.tick = off[i];
        done.on = false;
        done.note = notes[i].note;
        built[builtCount++] = done;
    }
    std::sort (built, built + builtCount, [] (const ScheduledEvent& a, const ScheduledEvent& b) {
        if (a.tick != b.tick)
            return a.tick < b.tick;
        if (a.on != b.on)
            return a.on && ! b.on;
        return a.note < b.note;
    });
    out.eventCount = builtCount;
    int loopTicks = std::max (1, lengthTicks);
    for (int i = 0; i < builtCount; ++i)
    {
        out.events[i] = built[i];
        loopTicks = std::max (loopTicks, built[i].tick + 1);
    }
    out.loopTicks = loopTicks;
    return true;
}

// Half-away-from-zero. Block size does not move the result.
[[nodiscard]] inline int phraseSample (int tick, double sampleRate, double bpm = phraseTempoBpm) noexcept
{
    const double tempo = bpm > 1.0 ? bpm : phraseTempoBpm;
    const double seconds = static_cast<double> (tick) * 60.0
                         / (tempo * phrasePulsesPerQuarter);
    return static_cast<int> (std::llround (seconds * sampleRate));
}

[[nodiscard]] inline std::vector<ScheduledEvent> schedulePhrase (const PhraseNote* notes, int count,
                                                                 PhraseProjection projection = PhraseProjection::performance)
{
    std::vector<int> off (static_cast<std::size_t> (std::max (0, count)));
    std::vector<char> into (static_cast<std::size_t> (std::max (0, count)));
    for (int i = 0; i < count; ++i)
        off[static_cast<std::size_t> (i)] = notes[i].tick + notes[i].duration;
    if (projection == PhraseProjection::performance)
    {
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
        on.slide = projection == PhraseProjection::performance && into[static_cast<std::size_t> (i)] != 0;
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

[[nodiscard]] inline std::vector<std::byte> exportPhraseSmf (const PhraseNote* notes, int count)
{
    auto events = schedulePhrase (notes, count, PhraseProjection::authored);
    std::sort (events.begin(), events.end(), [] (const ScheduledEvent& a, const ScheduledEvent& b) {
        if (a.tick != b.tick)
            return a.tick < b.tick;
        if (a.on != b.on)
            return ! a.on && b.on;
        return a.note < b.note;
    });

    std::vector<std::byte> track;
    const auto varlen = [&track] (int value)
    {
        unsigned char bytes[5];
        int used = 0;
        bytes[used++] = static_cast<unsigned char> (value & 0x7f);
        value >>= 7;
        while (value > 0)
        {
            bytes[used++] = static_cast<unsigned char> ((value & 0x7f) | 0x80);
            value >>= 7;
        }
        while (used > 0)
            track.push_back (static_cast<std::byte> (bytes[--used]));
    };
    const auto push = [&track] (unsigned char byte) { track.push_back (static_cast<std::byte> (byte)); };

    track.insert (track.end(), { std::byte { 0 }, std::byte { 0xff }, std::byte { 0x51 }, std::byte { 3 } });
    push (0x07);
    push (0xa1);
    push (0x20);
    const auto* notice = reinterpret_cast<const unsigned char*> (phraseSmfLimit);
    const int noticeLength = static_cast<int> (std::char_traits<char>::length (phraseSmfLimit));
    push (0x00);
    push (0xff);
    push (0x01);
    push (static_cast<unsigned char> (noticeLength));
    for (int i = 0; i < noticeLength; ++i)
        push (notice[i]);

    int last = 0;
    for (const auto& event : events)
    {
        varlen (event.tick - last);
        last = event.tick;
        push (event.on ? 0x90 : 0x80);
        push (static_cast<unsigned char> (event.note & 0x7f));
        push (event.on ? static_cast<unsigned char> (event.velocity & 0x7f) : 0);
    }
    varlen (0);
    push (0xff);
    push (0x2f);
    push (0);

    std::vector<std::byte> file;
    const auto be32 = [&file] (unsigned value)
    {
        file.push_back (static_cast<std::byte> ((value >> 24) & 0xffu));
        file.push_back (static_cast<std::byte> ((value >> 16) & 0xffu));
        file.push_back (static_cast<std::byte> ((value >> 8) & 0xffu));
        file.push_back (static_cast<std::byte> (value & 0xffu));
    };
    file.insert (file.end(), { std::byte { 'M' }, std::byte { 'T' }, std::byte { 'h' }, std::byte { 'd' } });
    be32 (6);
    file.push_back (std::byte { 0 });
    file.push_back (std::byte { 0 });
    file.push_back (std::byte { 0 });
    file.push_back (std::byte { 1 });
    file.push_back (std::byte { 0x03 });
    file.push_back (std::byte { 0xc0 });
    file.insert (file.end(), { std::byte { 'M' }, std::byte { 'T' }, std::byte { 'r' }, std::byte { 'k' } });
    be32 (static_cast<unsigned> (track.size()));
    file.insert (file.end(), track.begin(), track.end());
    return file;
}

} // namespace vox::syn
