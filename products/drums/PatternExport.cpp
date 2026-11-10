#include "PatternExport.h"

#include <algorithm>
#include <cmath>

namespace vox::drums {

std::vector<ExportedNote> exportNotes (const kit::Pattern& pattern, const kit::StepContext& context,
                                      const std::array<std::uint8_t, kit::padCount>& notes, const ExportOptions& options)
{
    std::vector<ExportedNote> out;
    if (pattern.cells == nullptr || pattern.steps == 0 || pattern.bars == 0 || options.passes < 1)
        return out;
    const double step = kit::stepLength (pattern);
    const auto total = static_cast<std::int64_t> (pattern.steps) * pattern.bars * options.passes;
    const double tpq = options.ticksPerQuarter;
    std::array<kit::ScheduledHit, kit::padCount * kit::maxRatchet> hits;
    for (std::int64_t g = 0; g < total; ++g)
    {
        const int n = kit::stepHits (pattern, context, g, hits.data());
        // The ratchet spacing of each hit's cell, for its gate.
        for (int k = 0; k < n; ++k)
        {
            const auto& h = hits[static_cast<std::size_t> (k)];
            if (h.ppq < 0.0)
                continue;
            const auto st = static_cast<std::uint32_t> (g % pattern.steps);
            const auto bar = static_cast<std::uint32_t> ((g / pattern.steps) % pattern.bars);
            const auto& cell = pattern.cells[(h.pad * pattern.bars + bar) * pattern.steps + st];
            const int ratchet = std::clamp<int> (cell.ratchet, 1, kit::maxRatchet);
            ExportedNote e;
            e.tick = static_cast<std::int64_t> (std::llround (h.ppq * tpq));
            e.offTick = e.tick + std::max<std::int64_t> (1, std::llround (0.5 * step / ratchet * tpq));
            e.note = notes[h.pad];
            e.velocity = std::max (1, static_cast<int> (h.velocity));
            e.pad = h.pad;
            out.push_back (e);
        }
    }
    std::stable_sort (out.begin(), out.end(), [] (const ExportedNote& a, const ExportedNote& b) { return a.tick < b.tick; });
    // Two lanes on one note at one tick are one MIDI note (the louder).
    std::vector<ExportedNote> merged;
    for (const auto& e : out)
    {
        auto it = std::find_if (merged.rbegin(), merged.rend(), [&] (const ExportedNote& m) { return m.tick == e.tick && m.note == e.note; });
        if (it != merged.rend() && it->tick == e.tick)
        {
            it->velocity = std::max (it->velocity, e.velocity);
            it->offTick = std::max (it->offTick, e.offTick);
        }
        else
            merged.push_back (e);
    }
    out = std::move (merged);
    // A note sounding again ends the previous one first.
    for (std::size_t i = 0; i < out.size(); ++i)
        for (std::size_t j = i + 1; j < out.size() && out[j].tick < out[i].offTick; ++j)
            if (out[j].note == out[i].note)
                out[i].offTick = std::max (out[i].tick, out[j].tick);
    return out;
}

namespace {

void putVarLen (std::vector<std::uint8_t>& t, std::uint32_t v)
{
    std::uint8_t buf[5];
    int n = 0;
    buf[n++] = static_cast<std::uint8_t> (v & 0x7f);
    while ((v >>= 7) != 0)
        buf[n++] = static_cast<std::uint8_t> ((v & 0x7f) | 0x80);
    while (n > 0)
        t.push_back (buf[--n]);
}

void putBE (std::vector<std::uint8_t>& t, std::uint32_t v, int bytes)
{
    for (int i = bytes - 1; i >= 0; --i)
        t.push_back (static_cast<std::uint8_t> ((v >> (8 * i)) & 0xff));
}

} // namespace

juce::MemoryBlock exportMidi (const kit::Pattern& pattern, const kit::StepContext& context,
                              const std::array<std::uint8_t, kit::padCount>& notes, const ExportOptions& options,
                              const juce::String& trackName)
{
    const auto list = exportNotes (pattern, context, notes, options);
    struct Ev {
        std::int64_t tick;
        int order;   // at one tick: offs (0) before ons (1)
        std::uint8_t status, data1, data2;
    };
    const auto channel = static_cast<std::uint8_t> (std::clamp (options.channel, 1, 16) - 1);
    std::vector<Ev> events;
    for (const auto& n : list)
    {
        events.push_back ({ n.tick, 1, static_cast<std::uint8_t> (0x90 | channel), static_cast<std::uint8_t> (n.note),
                            static_cast<std::uint8_t> (n.velocity) });
        events.push_back ({ n.offTick, 0, static_cast<std::uint8_t> (0x80 | channel), static_cast<std::uint8_t> (n.note), 0 });
    }
    std::stable_sort (events.begin(), events.end(),
                      [] (const Ev& a, const Ev& b) { return a.tick != b.tick ? a.tick < b.tick : a.order < b.order; });

    std::vector<std::uint8_t> track;
    // Track name, tempo, 4/4.
    const auto name = trackName.toStdString();
    track.insert (track.end(), { 0x00, 0xFF, 0x03 });
    putVarLen (track, static_cast<std::uint32_t> (name.size()));
    track.insert (track.end(), name.begin(), name.end());
    const auto usPerQuarter = static_cast<std::uint32_t> (std::llround (60000000.0 / std::clamp (options.bpm, 20.0, 999.0)));
    track.insert (track.end(), { 0x00, 0xFF, 0x51, 0x03 });
    putBE (track, usPerQuarter, 3);
    track.insert (track.end(), { 0x00, 0xFF, 0x58, 0x04, 0x04, 0x02, 0x18, 0x08 });
    std::int64_t last = 0;
    for (const auto& e : events)
    {
        putVarLen (track, static_cast<std::uint32_t> (e.tick - last));
        last = e.tick;
        track.insert (track.end(), { e.status, e.data1, e.data2 });
    }
    // End of track at the pattern's end (or the last event).
    const double step = kit::stepLength (pattern);
    const auto end = std::max<std::int64_t> (
        last, std::llround (static_cast<double> (pattern.steps) * pattern.bars * options.passes * step * options.ticksPerQuarter));
    putVarLen (track, static_cast<std::uint32_t> (end - last));
    track.insert (track.end(), { 0xFF, 0x2F, 0x00 });

    std::vector<std::uint8_t> file { 'M', 'T', 'h', 'd' };
    putBE (file, 6, 4);
    putBE (file, 0, 2);   // format 0
    putBE (file, 1, 2);   // one track
    putBE (file, static_cast<std::uint32_t> (options.ticksPerQuarter), 2);
    file.insert (file.end(), { 'M', 'T', 'r', 'k' });
    putBE (file, static_cast<std::uint32_t> (track.size()), 4);
    file.insert (file.end(), track.begin(), track.end());
    return juce::MemoryBlock (file.data(), file.size());
}

} // namespace vox::drums
