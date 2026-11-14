#include "PatternExport.h"

#include <algorithm>
#include <cmath>

namespace vox::drums {

namespace {

// Every hit of one bar (all lanes, cells in order) by the canonical rule.
void barNotes (const kit::BarView& view, const kit::StepContext& context, std::int64_t absoluteBar, std::int64_t barStart,
               const std::array<std::uint8_t, kit::padCount>& notes, const ExportOptions& options, std::vector<ExportedNote>& out)
{
    if (view.cells == nullptr)
        return;
    const double tpq = options.ticksPerQuarter;
    std::array<kit::ScheduledHit, kit::maxRatchet> hits;
    for (int lane = 0; lane < kit::padCount; ++lane)
    {
        const auto t = view.timing (lane);
        if (! t.valid())
            continue;
        for (int beat = 0; beat < t.beats(); ++beat)
            for (int cell = 0; cell < t.cells (beat); ++cell)
            {
                const int n = kit::cellHits (view, context, absoluteBar, barStart, lane, beat, cell, hits.data());
                for (int k = 0; k < n; ++k)
                {
                    const auto& h = hits[static_cast<std::size_t> (k)];
                    if (h.ppq < 0.0)
                        continue;   // before position 0: it does not play from 0 either
                    ExportedNote e;
                    e.tick = static_cast<std::int64_t> (std::llround (h.ppq * tpq));
                    e.offTick = e.tick + std::max<std::int64_t> (1, std::llround (h.gateTicks * tpq / timing::ticksPerQuarter));
                    e.note = notes[h.pad];
                    e.velocity = std::max (1, static_cast<int> (h.velocity));
                    e.pad = h.pad;
                    out.push_back (e);
                }
            }
    }
}

} // namespace

std::vector<ExportedNote> exportNotes (const kit::Pattern& pattern, const kit::StepContext& context,
                                      const std::array<std::uint8_t, kit::padCount>& notes, const ExportOptions& options)
{
    std::vector<ExportedNote> out;
    if (! pattern.playable() || options.passes < 1)
        return out;
    const std::int64_t barTicks = pattern.barTicks();
    const auto total = static_cast<std::int64_t> (pattern.bars) * options.passes;
    for (std::int64_t bar = 0; bar < total; ++bar)
        barNotes (pattern.bar (static_cast<std::uint32_t> (bar % pattern.bars)), context, bar, bar * barTicks, notes, options, out);
    finishNotes (out);
    return out;
}

// Time order, one MIDI note per (tick, note), and no overlap: a note
// sounding again ends the previous one first. Linear in the note count.
void finishNotes (std::vector<ExportedNote>& out)
{
    std::stable_sort (out.begin(), out.end(), [] (const ExportedNote& a, const ExportedNote& b) { return a.tick < b.tick; });
    std::vector<ExportedNote> merged;
    merged.reserve (out.size());
    std::array<std::ptrdiff_t, 128> last;   // index in merged of the latest note on this pitch
    last.fill (-1);
    for (const auto& e : out)
    {
        const auto note = static_cast<std::size_t> (e.note & 0x7f);
        if (last[note] >= 0)
        {
            auto& previous = merged[static_cast<std::size_t> (last[note])];
            if (previous.tick == e.tick)
            {
                // Two lanes on one note at one tick are one MIDI note (the louder).
                previous.velocity = std::max (previous.velocity, e.velocity);
                previous.offTick = std::max (previous.offTick, e.offTick);
                continue;
            }
        }
        last[note] = static_cast<std::ptrdiff_t> (merged.size());
        merged.push_back (e);
    }
    last.fill (-1);
    for (std::size_t i = 0; i < merged.size(); ++i)
    {
        const auto note = static_cast<std::size_t> (merged[i].note & 0x7f);
        if (last[note] >= 0)
        {
            auto& previous = merged[static_cast<std::size_t> (last[note])];
            if (merged[i].tick < previous.offTick)
                previous.offTick = std::max (previous.tick, merged[i].tick);
        }
        last[note] = static_cast<std::ptrdiff_t> (i);
    }
    out = std::move (merged);
}

static std::int64_t songBarTicks (const kit::BarView& view) noexcept
{
    // An empty bar is a 4/4 bar.
    return view.cells != nullptr ? timing::BarTiming { view.signature, view.resolution, 0 }.barTicks()
                                 : 4 * timing::ticksPerQuarter;
}

std::vector<ExportedNote> exportSongNotes (const kit::Song& song, const kit::StepContext& context,
                                          const std::array<std::uint8_t, kit::padCount>& notes, const ExportOptions& options)
{
    std::vector<ExportedNote> out;
    if (song.views == nullptr || song.bars == 0)
        return out;
    // Entire Song: one pass from bar 1 (the loop region is not applied),
    // each bar in its own meter.
    std::int64_t start = 0;
    for (std::uint32_t bar = 0; bar < song.bars; ++bar)
    {
        const auto& view = song.views[bar];
        barNotes (view, context, bar, start, notes, options, out);
        start += songBarTicks (view);
    }
    finishNotes (out);
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

namespace {

struct MeterAt {
    std::int64_t tick = 0;   // 960 PPQ
    timing::TimeSignature signature {};
};

juce::MemoryBlock writeSmf (const std::vector<ExportedNote>& list, std::int64_t endTick, const std::vector<MeterAt>& meters,
                            const ExportOptions& options, const juce::String& trackName)
{
    struct Ev {
        std::int64_t tick;
        int order;   // at one tick: meter (-1), offs (0), ons (1)
        std::uint8_t status, data1, data2;
        timing::TimeSignature meter {};
    };
    const auto channel = static_cast<std::uint8_t> (std::clamp (options.channel, 1, 16) - 1);
    std::vector<Ev> events;
    for (const auto& n : list)
    {
        events.push_back ({ n.tick, 1, static_cast<std::uint8_t> (0x90 | channel), static_cast<std::uint8_t> (n.note),
                            static_cast<std::uint8_t> (n.velocity) });
        events.push_back ({ n.offTick, 0, static_cast<std::uint8_t> (0x80 | channel), static_cast<std::uint8_t> (n.note), 0 });
    }
    for (const auto& m : meters)
        events.push_back ({ std::llround (static_cast<double> (m.tick) * options.ticksPerQuarter / timing::ticksPerQuarter), -1,
                            0xFF, 0x58, 0, m.signature });
    std::stable_sort (events.begin(), events.end(),
                      [] (const Ev& a, const Ev& b) { return a.tick != b.tick ? a.tick < b.tick : a.order < b.order; });

    std::vector<std::uint8_t> track;
    // Track name, tempo; time signatures with the events.
    const auto name = trackName.toStdString();
    track.insert (track.end(), { 0x00, 0xFF, 0x03 });
    putVarLen (track, static_cast<std::uint32_t> (name.size()));
    track.insert (track.end(), name.begin(), name.end());
    const auto usPerQuarter = static_cast<std::uint32_t> (std::llround (60000000.0 / std::clamp (options.bpm, 20.0, 999.0)));
    track.insert (track.end(), { 0x00, 0xFF, 0x51, 0x03 });
    putBE (track, usPerQuarter, 3);
    std::int64_t last = 0;
    for (const auto& e : events)
    {
        putVarLen (track, static_cast<std::uint32_t> (e.tick - last));
        last = e.tick;
        if (e.order < 0)
        {
            // FF 58 04 nn dd cc bb: dd = log2 (denominator); cc = MIDI clocks
            // per beat (24 a quarter, 36 a dotted quarter, 12 an eighth).
            const auto kind = timing::beatKind (e.meter);
            const std::uint8_t clocks = kind == timing::BeatKind::quarter ? 24 : kind == timing::BeatKind::dottedQuarter ? 36 : 12;
            track.insert (track.end(), { 0xFF, 0x58, 0x04, static_cast<std::uint8_t> (e.meter.numerator),
                                         static_cast<std::uint8_t> (e.meter.denominator == 8 ? 3 : 2), clocks, 0x08 });
            continue;
        }
        track.insert (track.end(), { e.status, e.data1, e.data2 });
    }
    // End of track at the end of the last bar (or the last event).
    const auto end = std::max<std::int64_t> (last, endTick);
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

} // namespace

juce::MemoryBlock exportMidi (const kit::Pattern& pattern, const kit::StepContext& context,
                              const std::array<std::uint8_t, kit::padCount>& notes, const ExportOptions& options,
                              const juce::String& trackName)
{
    const std::int64_t ticks = pattern.playable() ? static_cast<std::int64_t> (pattern.barTicks()) * pattern.bars * std::max (0, options.passes) : 0;
    return writeSmf (exportNotes (pattern, context, notes, options),
                     std::llround (static_cast<double> (ticks) * options.ticksPerQuarter / timing::ticksPerQuarter),
                     { MeterAt { 0, pattern.signature } }, options, trackName);
}

juce::MemoryBlock exportSongMidi (const kit::Song& song, const kit::StepContext& context,
                                  const std::array<std::uint8_t, kit::padCount>& notes, const ExportOptions& options,
                                  const juce::String& trackName)
{
    std::vector<MeterAt> meters;
    std::int64_t start = 0;
    for (std::uint32_t bar = 0; song.views != nullptr && bar < song.bars; ++bar)
    {
        const auto& view = song.views[bar];
        const timing::TimeSignature meter = view.cells != nullptr ? view.signature : timing::TimeSignature {};
        if (meters.empty() || ! (meters.back().signature == meter))
            meters.push_back ({ start, meter });
        start += songBarTicks (view);
    }
    if (meters.empty())
        meters.push_back ({});
    return writeSmf (exportSongNotes (song, context, notes, options),
                     std::llround (static_cast<double> (start) * options.ticksPerQuarter / timing::ticksPerQuarter), meters,
                     options, trackName);
}

} // namespace vox::drums
