// VOX Drums production-candidate tests (DRM-05, DRM-06, DRM-07, presets):
//
//  - song: a 64-bar song (mixed A/B, unique bars, three grids, probability,
//    ratchets, microtiming, unique events in the final bar) plays exactly
//    what "Entire Song" exports, within one SMF tick, at 44.1/48/96 kHz and
//    blocks of 64/333/512, with tempo changes; seek, loop, stop/start and
//    Pattern <-> Song switching follow the canonical rule;
//  - export: click and drag write the same bytes from one frozen snapshot;
//    channel 10, Note On/Off pairs without overlap, tempo, 4/4 and End of
//    Track at the end of bar 16/32/64 (and of a 1-4 bar pattern);
//  - outputs: 9 host buses (Main + Out 2..9), every destination alone, no
//    copy on Main, a disabled bus plays on Main bit-identically, sends and
//    returns, tails, All Sound Off, mute, solo, meters, and blocks larger
//    than the prepared maximum (buffer boundaries);
//  - generator: seven distinct styles, deterministic, every action changes
//    data, step and lane locks, undo;
//  - presets: kit, pad, pattern, song, Undo Load, malformed files leave the
//    state unchanged, missing samples, portable kit round trip, factory
//    content against its committed hashes;
//  - state: the new sections round-trip; malformed ones are rejected with
//    the live state unchanged; older states load with defaults;
//  - editor: eight pages without overlapping controls at the minimum and a
//    large size and at 100/125/150/200 %, bound controls on every page;
//  - realtime: processBlock allocates nothing in Song mode on nine buses.
#include "DrumsEditor.h"
#include "DrumsParameters.h"
#include "FactoryContent.h"
#include "PatternEditor.h"
#include "PatternExport.h"
#include "Presets.h"
#include "Processor.h"
#include "common/SuiteEditor.h"

#include <juce_audio_formats/juce_audio_formats.h>

#ifdef VOX_TEST_INTERPOSE_MALLOC
#include "AllocationProbe.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>
#include <vector>

namespace {

using namespace vox::drums;

int failures = 0;
std::string manifestPath;
juce::File workFolder;

void check (bool condition, const juce::String& what)
{
    if (! condition)
    {
        std::printf ("FAIL [VOX Drums candidate]: %s\n", what.toRawUTF8());
        ++failures;
    }
}

void setEnvironment (const char* name, const juce::String& value)
{
#if JUCE_WINDOWS
    _putenv_s (name, value.toRawUTF8());
#else
    setenv (name, value.toRawUTF8(), 1);
#endif
}

class TestPlayHead final : public juce::AudioPlayHead {
public:
    juce::Optional<PositionInfo> getPosition() const override { return info; }
    void set (bool playing, double bpm, double ppq)
    {
        info = {};
        info.setIsPlaying (playing);
        info.setBpm (bpm);
        info.setPpqPosition (ppq);
        info.setTimeSignature (TimeSignature { 4, 4 });
    }
    PositionInfo info;
};

// `outputs`: bit n enables output bus n (bit 0, Main, is always on).
std::unique_ptr<DrumsProcessor> makeProcessor (double rate = 48000.0, int block = 512, std::uint32_t outputs = 1)
{
    auto p = std::make_unique<DrumsProcessor>();
    auto layout = p->getBusesLayout();
    for (int bus = 1; bus < layout.outputBuses.size(); ++bus)
        layout.outputBuses.getReference (bus) = (outputs & (1u << bus)) != 0 ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled();
    if (! p->setBusesLayout (layout))
        check (false, "the bus layout is accepted");
    p->setRateAndBufferSizeDetails (rate, block);
    p->prepareToPlay (rate, block);
    return p;
}

void setPlain (DrumsProcessor& p, const juce::String& id, float plain)
{
    auto* param = dynamic_cast<juce::RangedAudioParameter*> (p.state().getParameter (id));
    if (param == nullptr)
    {
        std::printf ("FAIL [VOX Drums candidate]: no parameter %s\n", id.toRawUTF8());
        std::exit (EXIT_FAILURE);
    }
    param->setValueNotifyingHost (param->convertTo0to1 (plain));
}

float plainOf (DrumsProcessor& p, const juce::String& id)
{
    return p.state().getRawParameterValue (id)->load();
}

juce::MemoryBlock stateOf (DrumsProcessor& p)
{
    juce::MemoryBlock block;
    p.getStateInformation (block);
    return block;
}

float energy (const juce::AudioBuffer<float>& buffer, int channel)
{
    return channel < buffer.getNumChannels() ? buffer.getMagnitude (channel, 0, buffer.getNumSamples()) : 0.0f;
}

//==============================================================================
struct MidiNote {
    std::int64_t tick = 0, off = -1;
    int note = 0, velocity = 0, channel = 0;
};

struct ParsedMidi {
    bool ok = false;
    int format = -1, tracks = 0, division = 0;
    std::vector<MidiNote> notes;
    std::int64_t endTick = -1;
    double bpm = 0.0;
    int numerator = 0, denominator = 0;
    bool overlap = false, unmatchedOff = false;
};

ParsedMidi parseMidi (const juce::MemoryBlock& block)
{
    ParsedMidi out;
    const auto* d = static_cast<const std::uint8_t*> (block.getData());
    const std::size_t size = block.getSize();
    const auto be = [&] (std::size_t at, int bytes) {
        std::uint32_t v = 0;
        for (int i = 0; i < bytes; ++i)
            v = (v << 8) | d[at + static_cast<std::size_t> (i)];
        return v;
    };
    if (size < 22 || std::memcmp (d, "MThd", 4) != 0 || be (4, 4) != 6 || std::memcmp (d + 14, "MTrk", 4) != 0)
        return out;
    out.format = static_cast<int> (be (8, 2));
    out.tracks = static_cast<int> (be (10, 2));
    out.division = static_cast<int> (be (12, 2));
    const std::size_t length = be (18, 4);
    if (22 + length != size)
        return out;
    std::size_t at = 22;
    std::int64_t tick = 0;
    std::array<std::ptrdiff_t, 128> open;
    open.fill (-1);
    while (at < size)
    {
        std::uint32_t delta = 0;
        for (;;)
        {
            if (at >= size)
                return out;
            const auto b = d[at++];
            delta = (delta << 7) | (b & 0x7fu);
            if ((b & 0x80u) == 0)
                break;
        }
        tick += delta;
        if (at >= size)
            return out;
        const auto status = d[at++];
        if (status == 0xff)
        {
            if (at + 1 >= size)
                return out;
            const auto type = d[at++];
            const std::size_t len = d[at++];   // the lengths written here are below 128
            if (at + len > size)
                return out;
            if (type == 0x51 && len == 3)
                out.bpm = 60000000.0 / static_cast<double> (be (at, 3));
            if (type == 0x58 && len == 4)
            {
                out.numerator = d[at];
                out.denominator = 1 << d[at + 1];
            }
            if (type == 0x2f)
            {
                out.endTick = tick;
                out.ok = at + len == size;
                return out;
            }
            at += len;
        }
        else if ((status & 0xf0) == 0x90 || (status & 0xf0) == 0x80)
        {
            if (at + 2 > size)
                return out;
            const int note = d[at], velocity = d[at + 1];
            at += 2;
            if ((status & 0xf0) == 0x90 && velocity > 0)
            {
                if (open[static_cast<std::size_t> (note)] >= 0)
                    out.overlap = true;   // a Note On while the same note is still held
                open[static_cast<std::size_t> (note)] = static_cast<std::ptrdiff_t> (out.notes.size());
                out.notes.push_back ({ tick, -1, note, velocity, (status & 0x0f) + 1 });
            }
            else if (open[static_cast<std::size_t> (note)] >= 0)
            {
                out.notes[static_cast<std::size_t> (open[static_cast<std::size_t> (note)])].off = tick;
                open[static_cast<std::size_t> (note)] = -1;
            }
            else
                out.unmatchedOff = true;
        }
        else
            return out;
    }
    return out;
}

struct Hit {
    double tick = 0.0;
    int note = 0, velocity = 0;
};

bool sameHits (std::vector<Hit> a, std::vector<Hit> b, juce::String& why)
{
    const auto order = [] (const Hit& x, const Hit& y) { return x.note != y.note ? x.note < y.note : x.tick < y.tick; };
    std::sort (a.begin(), a.end(), order);
    std::sort (b.begin(), b.end(), order);
    for (std::size_t i = 0; i < std::max (a.size(), b.size()); ++i)
        if (i >= a.size() || i >= b.size())
        {
            const auto& h = i >= a.size() ? b[i] : a[i];
            why = "counts differ: " + juce::String (static_cast<int> (a.size())) + " vs " + juce::String (static_cast<int> (b.size()))
                + "; first extra: note " + juce::String (h.note) + " velocity " + juce::String (h.velocity) + " tick " + juce::String (h.tick, 2);
            return false;
        }
        else if (a[i].note != b[i].note || a[i].velocity != b[i].velocity || std::abs (a[i].tick - b[i].tick) > 1.0)
        {
            why = "hit " + juce::String (static_cast<int> (i)) + ": note " + juce::String (a[i].note) + "/" + juce::String (b[i].note)
                + " velocity " + juce::String (a[i].velocity) + "/" + juce::String (b[i].velocity) + " tick " + juce::String (a[i].tick, 2)
                + "/" + juce::String (b[i].tick, 2);
            return false;
        }
    return true;
}

// The 64-bar song of the parity test.
void buildSong (DrumsProcessor& p)
{
    const auto all = std::make_unique<PatternStore::Everything>();
    p.patterns().copyEverything (*all);
    static constexpr int steps[5] = { 16, 32, 64, 16, 32 };
    static constexpr int bars[5] = { 4, 2, 1, 3, 4 };
    for (int slot = 0; slot < 5; ++slot)
    {
        GeneratorSettings g;
        g.style = static_cast<Style> (slot == 0 ? 0 : slot + 1);   // full-on, house, techno, trap, breaks
        g.density = 0.7f;
        g.complexity = 0.9f;      // ratchets, ghost notes, probability
        g.syncopation = 0.6f;
        g.swing = 0.2f;
        g.humanize = slot == 0 ? 0.0f : 0.6f;   // slot 1 opens the song: nothing before position 0
        g.fills = 0.8f;
        g.variation = 0.7f;
        g.seed = 100u + static_cast<std::uint32_t> (slot);
        auto& a = all->slots[static_cast<std::size_t> (slot)];
        a = {};
        a.resize (steps[slot], bars[slot]);
        generatePattern (g, a);
        auto& b = all->alternates[static_cast<std::size_t> (slot)];
        b = {};
        b.resize (steps[slot], bars[slot]);
        generateVariation (g, a, b);
        // Probability on real steps of every pattern.
        for (int lane = 3; lane < 6; ++lane)
            for (int st = 1; st < a.steps; st += 3)
                if (a.cell (lane, 0, st).on)
                    a.cell (lane, 0, st).probability = 55;
    }
    auto& song = all->song;
    song = {};
    song.songMode = true;
    song.length = 64;
    song.loopOn = false;
    song.loopStart = 0;
    song.loopLength = 4;
    for (int bar = 0; bar < 64; bar += 4)
        song.place (bar, (bar / 4) % 5, (bar / 8) % 2, 4);   // mixed A and B
    song.place (0, 0, 0, 4);
    // Unique bars: 21 (32 steps), 40 (16 steps) and the final bar (64 steps)
    // with events that exist nowhere else.
    const auto unique = [] (int barSteps, std::uint32_t seed, const std::function<void (BarCells&)>& fill) {
        auto cells = std::make_shared<BarCells>();
        cells->id = BarCells::nextId();
        cells->steps = static_cast<std::uint8_t> (barSteps);
        cells->seed = seed;
        fill (*cells);
        SongBar bar;
        bar.kind = SongBar::Kind::unique;
        bar.cells = cells;
        return bar;
    };
    const auto on = [] (BarCells& cells, int lane, int st, int velocity, int ratchet = 1, int micro = 0, int probability = 100,
                        bool accent = false) {
        auto& c = cells.cell (lane, st);
        c.on = true;
        c.velocity = static_cast<std::uint8_t> (velocity);
        c.ratchet = static_cast<std::uint8_t> (ratchet);
        c.micro = static_cast<std::int8_t> (micro);
        c.probability = static_cast<std::uint8_t> (probability);
        c.accent = accent;
    };
    song.bars[20] = unique (32, 777u, [&] (BarCells& cells) {
        for (int st = 0; st < 32; st += 3)
            on (cells, 8 + st % 4, st, 60 + st, 1 + st % 3, (st % 5) * 9 - 18, st % 2 == 0 ? 100 : 40);
    });
    song.bars[39] = unique (16, 31337u, [&] (BarCells& cells) {
        for (int st = 0; st < 16; ++st)
            on (cells, st % 16, st, 50 + st * 4, st == 15 ? 8 : 1, st == 0 ? -35 : 0, 100, st % 4 == 0);
    });
    song.bars[63] = unique (64, 4242u, [&] (BarCells& cells) {
        on (cells, 15, 63, 77, 1, 30);          // the very last step, late
        on (cells, 9, 62, 99, 8);               // an 8x ratchet at the end
        on (cells, 3, 0, 88, 1, -40);           // sounds before its bar line
        on (cells, 12, 31, 66, 3, 12, 50);      // probability and ratchet
        for (int st = 4; st < 60; st += 7)
            on (cells, 10, st, 40 + st, 2, st % 2 == 0 ? -20 : 25, 70, st % 3 == 0);
    });
    p.patterns().setEverything (*all);
    // Host-note remaps: pads 10, 12 and 16 on notes far from their defaults.
    for (const auto& [pad, note] : { std::pair { 9, 71 }, std::pair { 11, 28 }, std::pair { 15, 101 } })
    {
        auto settings = p.kitStore().pad (pad);
        settings.note = static_cast<std::uint8_t> (note);
        p.kitStore().setPad (pad, settings);
    }
}

std::vector<Hit> exportHits (const juce::MemoryBlock& midi, ParsedMidi* parsed = nullptr)
{
    const auto m = parseMidi (midi);
    if (parsed != nullptr)
        *parsed = m;
    std::vector<Hit> hits;
    for (const auto& n : m.notes)
        hits.push_back ({ static_cast<double> (n.tick), n.note, n.velocity });
    return hits;
}

// Plays [fromPpq, toPpq) in blocks and returns the sequencer's hits in SMF
// ticks. `tempo (ppq)` gives the tempo of the block starting at ppq.
std::vector<Hit> playHits (DrumsProcessor& p, double rate, int block, double fromPpq, double toPpq,
                           const std::function<double (double)>& tempo, const std::array<std::uint8_t, kit::padCount>& notes)
{
    TestPlayHead head;
    p.setPlayHead (&head);
    juce::AudioBuffer<float> buffer (p.getTotalNumOutputChannels(), block);
    juce::MidiBuffer midi;
    std::vector<Hit> hits;
    double ppq = fromPpq;
    while (ppq < toPpq)
    {
        const double bpm = tempo (ppq);
        const double perFrame = bpm / (60.0 * rate);
        const int frames = static_cast<int> (std::min<double> (block, std::ceil ((toPpq - ppq) / perFrame)));
        if (frames <= 0)
            break;
        head.set (true, bpm, ppq);
        buffer.setSize (buffer.getNumChannels(), frames, false, false, true);
        midi.clear();
        p.processBlock (buffer, midi);
        const auto& engine = p.engineForTesting();
        for (int i = 0; i < engine.scheduledCount(); ++i)
        {
            std::uint32_t offset = 0;
            std::uint8_t pad = 0, velocity = 0;
            engine.scheduled (i, offset, pad, velocity);
            // The last block may end a sample past toPpq: hits there belong to what follows.
            if ((ppq + offset * perFrame) * 960.0 < toPpq * 960.0 - 0.5)
                hits.push_back ({ (ppq + offset * perFrame) * 960.0, notes[pad], velocity });
        }
        ppq += frames * perFrame;
    }
    p.setPlayHead (nullptr);
    return hits;
}

void testSongParity()
{
    std::array<std::uint8_t, kit::padCount> notes {};
    juce::MemoryBlock exported;
    std::vector<Hit> expected;
    {
        auto p = makeProcessor();
        buildSong (*p);
        setPlain (*p, "drm.seq.swing", 0.2f);
        setPlain (*p, "drm.seq.accent", 0.8f);
        const auto snapshot = p->captureExport (ExportSnapshot::Scope::song);
        notes = snapshot->notes;
        exported = DrumsProcessor::renderExport (*snapshot);
        ParsedMidi parsed;
        expected = exportHits (exported, &parsed);
        check (parsed.ok && parsed.format == 0 && parsed.tracks == 1 && parsed.division == 960, "song export: SMF format 0, one track, 960 tpq");
        check (parsed.endTick == 64 * 4 * 960, "song export: End of Track at the end of bar 64 (" + juce::String (parsed.endTick) + ")");
        check (parsed.numerator == 4 && parsed.denominator == 4 && parsed.bpm > 0.0, "song export: tempo and 4/4");
        check (! parsed.overlap && ! parsed.unmatchedOff, "song export: no overlapping notes, no stray Note Off");
        bool paired = true, channel10 = true, inside = true;
        for (const auto& n : parsed.notes)
        {
            paired = paired && n.off >= n.tick;
            channel10 = channel10 && n.channel == 10;
            inside = inside && n.off <= parsed.endTick;
        }
        check (paired && channel10 && inside && expected.size() > 1000,
               "song export: every Note On has its Note Off, on channel 10, inside the file (" + juce::String (static_cast<int> (expected.size()))
                   + " notes)");
        // The final bar is not truncated: its unique events are there.
        const auto has = [&] (int note, int velocity, double fromTick) {
            return std::any_of (expected.begin(), expected.end(),
                                [&] (const Hit& h) { return h.note == note && h.velocity == velocity && h.tick >= fromTick; });
        };
        check (has (notes[15], 77, 255.9 * 960.0), "song export: the last step of bar 64 is exported");
        check (notes[9] == 71 && notes[11] == 28 && notes[15] == 101, "song export: remapped pad notes are used");
        {
            // A write that cannot succeed is reported, not silent.
            std::unique_ptr<juce::AudioProcessorEditor> editor (p->createEditorAndMakeActive());
            auto* frame = dynamic_cast<vox::suite::SuiteEditor*> (editor.get());
            auto* panel = frame != nullptr ? dynamic_cast<DrumsPanel*> (frame->content()) : nullptr;
            if (panel != nullptr)
            {
                auto& ed = panel->patternEditor();
                const auto impossible = workFolder.getChildFile ("no-such-folder").getChildFile ("deeper").getChildFile ("x.mid");
                check (! ed.writeMidi (impossible) && ed.exportMessage().contains ("failed"), "export: a failed write is reported (" + ed.exportMessage() + ")");
                check (ed.writeMidi (workFolder.getChildFile ("ok.mid")) && ed.exportMessage().isEmpty(), "export: a later success clears the message");
            }
            p->editorBeingDeleted (editor.get());
        }
        int ratchets = 0;
        for (const auto& h : expected)
            if (h.note == notes[9] && h.velocity == 99 && h.tick >= 255.8 * 960.0)
                ++ratchets;
        check (ratchets == 8, "song export: the 8x ratchet at the end of bar 64 is complete (" + juce::String (ratchets) + ")");
        check (has (notes[3], 88, 251.9 * 960.0) && ! has (notes[3], 88, 252.0 * 960.0),
               "song export: negative microtiming on the first step of bar 64 lands before its bar line");
    }

    // Playback against the export at every rate and block size, with tempo changes.
    for (double rate : { 44100.0, 48000.0, 96000.0 })
        for (int block : { 64, 333, 512 })
        {
            auto p = makeProcessor (rate, block);
            buildSong (*p);
            setPlain (*p, "drm.seq.swing", 0.2f);
            setPlain (*p, "drm.seq.accent", 0.8f);
            setPlain (*p, "drm.seq.mode", 1.0f);   // Internal
            const bool varying = block == 333;
            const auto played = playHits (*p, rate, block, 0.0, 256.0,
                                          [varying] (double ppq) { return varying ? 420.0 + 180.0 * std::sin (ppq * 0.37) : 480.0; }, notes);
            juce::String why;
            const bool same = sameHits (played, expected, why);
            check (same,
                   "song playback matches the exported MIDI within one tick at " + juce::String (rate, 0) + " Hz, block "
                       + juce::String (block) + (varying ? " with tempo changes" : "") + ": " + why);
        }

    // Seek, loop, stop/start, switching.
    {
        auto p = makeProcessor (48000.0, 256);
        buildSong (*p);
        setPlain (*p, "drm.seq.swing", 0.2f);
        setPlain (*p, "drm.seq.accent", 0.8f);
        setPlain (*p, "drm.seq.mode", 1.0f);
        const auto constant = [] (double) { return 300.0; };
        playHits (*p, 48000.0, 256, 36.0, 44.0, constant, notes);
        // Seek to bar 51: what plays there is what the export holds there.
        const auto after = playHits (*p, 48000.0, 256, 200.0, 208.0, constant, notes);
        const auto window = [] (const std::vector<Hit>& hits, double from, double to) {
            std::vector<Hit> out;
            for (const auto& h : hits)
                if (h.tick >= from * 960.0 && h.tick < to * 960.0)
                    out.push_back (h);
            return out;
        };
        juce::String why;
        bool same = sameHits (window (after, 200.05, 207.9), window (expected, 200.05, 207.9), why);
        check (same, "song: after a seek the bars at the new position play: " + why);
        const auto unlooped = DrumsProcessor::renderExport (*p->captureExport (ExportSnapshot::Scope::song));

        // Loop region bars 9-12: the absolute bar maps into the region (the canonical rule).
        p->editSong ([] (SongData& s) {
            s.loopOn = true;
            s.loopStart = 8;
            s.loopLength = 4;
        });
        const auto snapshot = p->captureExport (ExportSnapshot::Scope::song);
        std::array<kit::SongBarView, kit::maxSongBars> views {};
        for (int bar = 0; bar < 64; ++bar)
            if (const auto& cells = snapshot->bars[static_cast<std::size_t> (bar)])
                views[static_cast<std::size_t> (bar)] = { cells->cells.data(), BarCells::stride, cells->steps, cells->seed };
        kit::Song song;
        song.bars = 64;
        song.views = views.data();
        song.loopStart = 8;
        song.loopLength = 4;
        std::vector<Hit> looped;
        std::array<kit::ScheduledHit, kit::padCount * kit::maxRatchet> buffer;
        bool mapped = true;
        for (std::int64_t bar = 0; bar < 24; ++bar)
        {
            // Bars 1-8 play once, then bars 9-12 repeat from bar 9 (review 6795902654).
            mapped = mapped && kit::songBarAt (song, bar) == static_cast<std::uint32_t> (bar < 8 ? bar : 8 + (bar - 8) % 4);
            const auto& view = views[kit::songBarAt (song, bar)];
            for (std::uint32_t st = 0; st < view.steps; ++st)
            {
                const int n = kit::songHits (song, snapshot->context, bar, st, buffer.data());
                for (int k = 0; k < n; ++k)
                    looped.push_back ({ buffer[static_cast<std::size_t> (k)].ppq * 960.0, notes[buffer[static_cast<std::size_t> (k)].pad],
                                        buffer[static_cast<std::size_t> (k)].velocity });
            }
        }
        check (mapped, "song loop: bars before the region play once, then the region repeats from its first bar");
        // The playhead the editor shows uses the same mapping as the audio.
        {
            TestPlayHead at;
            p->setPlayHead (&at);
            juce::AudioBuffer<float> one (2, 64);
            juce::MidiBuffer none;
            bool playhead = true;
            for (const auto& [absolute, bar] : { std::pair { 3, 3 }, std::pair { 8, 8 }, std::pair { 11, 11 }, std::pair { 12, 8 }, std::pair { 17, 9 } })
            {
                at.set (true, 300.0, 4.0 * absolute + 0.5);
                p->processBlock (one, none);
                playhead = playhead && p->songBarPlaying() == bar;
            }
            p->editSong ([] (SongData& s) { s.loopStart = 4; s.loopLength = 3; });
            for (const auto& [absolute, bar] : { std::pair { 0, 0 }, std::pair { 4, 4 }, std::pair { 6, 6 }, std::pair { 7, 4 }, std::pair { 9, 6 } })
            {
                at.set (true, 300.0, 4.0 * absolute + 0.5);
                p->processBlock (one, none);
                playhead = playhead && p->songBarPlaying() == bar;
            }
            p->editSong ([] (SongData& s) { s.loopStart = 8; s.loopLength = 4; });
            p->setPlayHead (nullptr);
            check (playhead, "song loop: the playhead bar follows the same mapping as playback");
        }
        const auto loopPlayed = playHits (*p, 48000.0, 256, 20.0, 80.0, constant, notes);
        same = sameHits (window (loopPlayed, 20.05, 79.9), window (looped, 20.05, 79.9), why);
        check (same, "song loop: playback follows the loop region: " + why);
        // "Entire Song" ignores the loop region.
        check (DrumsProcessor::renderExport (*snapshot) == unlooped && parseMidi (unlooped).notes.size() == expected.size(),
               "song export: the loop region does not change Entire Song");

        // Stop: no hits while the host is stopped; voices and tails end.
        TestPlayHead head;
        p->setPlayHead (&head);
        juce::AudioBuffer<float> audio (2, 256);
        juce::MidiBuffer midi;
        int whileStopped = 0;
        for (int b = 0; b < 40; ++b)
        {
            head.set (false, 300.0, 80.0);
            p->processBlock (audio, midi);
            whileStopped += p->engineForTesting().scheduledCount();
        }
        check (whileStopped == 0, "song: nothing is scheduled while the host is stopped");
        // Host MIDI mode: the song does not play (a clip exported from it,
        // played back into the plugin, is then the only source of hits).
        setPlain (*p, "drm.seq.mode", 0.0f);
        p->takeHitMask();   // hits of the playback above
        int inHostMode = 0;
        std::uint32_t lit = 0;
        for (int b = 0; b < 60; ++b)
        {
            head.set (true, 300.0, 80.0 + b * 256.0 * 300.0 / (60.0 * 48000.0));
            p->processBlock (audio, midi);
            inHostMode += p->engineForTesting().scheduledCount();
            lit |= p->takeHitMask();
        }
        check (inHostMode == 0 && lit == 0, "song: Host MIDI mode schedules nothing from the song (no doubled hits with an imported clip)");
        // Internal mode: host notes do not play, only the song does.
        setPlain (*p, "drm.seq.mode", 1.0f);
        p->reset();
        p->processBlock (audio, midi);
        p->takeHitMask();
        head.set (false, 300.0, 80.0);
        midi.addEvent (juce::MidiMessage::noteOn (10, 36, static_cast<juce::uint8> (100)), 0);
        p->processBlock (audio, midi);
        midi.clear();
        check (p->takeHitMask() == 0 && p->activeVoices() == 0, "song: Internal mode ignores host notes");
        setPlain (*p, "drm.seq.mode", 1.0f);
        // Pattern <-> Song while playing: both directions keep playing, finite output.
        bool finite = true;
        int hitsPattern = 0, hitsSong = 0;
        double ppq = 80.0;
        for (int b = 0; b < 400; ++b)
        {
            if (b == 100)
                p->editSong ([] (SongData& s) { s.songMode = false; });
            if (b == 250)
                p->editSong ([] (SongData& s) { s.songMode = true; });
            head.set (true, 300.0, ppq);
            p->processBlock (audio, midi);
            (b >= 110 && b < 250 ? hitsPattern : hitsSong) += p->engineForTesting().scheduledCount();
            check (b < 110 || b >= 250 || p->songBarPlaying() == -1, "pattern mode reports no song bar");
            for (int i = 0; i < 256; ++i)
                finite = finite && std::isfinite (audio.getSample (0, i));
            ppq += 256.0 * 300.0 / (60.0 * 48000.0);
        }
        check (finite && hitsPattern > 0 && hitsSong > 0, "song: switching Pattern and Song while playing keeps both playing");
        p->setPlayHead (nullptr);
    }
}

//==============================================================================
void testExport()
{
    auto p = makeProcessor();
    buildSong (*p);
    std::unique_ptr<juce::AudioProcessorEditor> editor (p->createEditorAndMakeActive());
    auto* frame = dynamic_cast<vox::suite::SuiteEditor*> (editor.get());
    auto* panel = frame != nullptr ? dynamic_cast<DrumsPanel*> (frame->content()) : nullptr;
    check (panel != nullptr, "export: Drums panel");
    if (panel == nullptr)
        return;
    panel->showPage (2);
    auto& ed = panel->patternEditor();
    const auto root = workFolder.getChildFile ("drags");
    PatternEditor::setDragRootForTesting (root);
    for (int scope = 0; scope < 2; ++scope)
    {
        auto* combo = panel->songStrip().combo ("scope");
        check (combo != nullptr, "export: scope selector");
        if (combo == nullptr)
            break;
        combo->setSelectedItemIndex (scope, juce::sendNotificationSync);
        check (p->uiState().exportScope == scope, "export: the scope selector sets the scope");
        const auto name = juce::String (scope == 0 ? "pattern" : "song");
        const auto clicked = workFolder.getChildFile ("click-" + name + ".mid");
        check (ed.writeMidi (clicked), "export " + name + ": click writes the file");
        const auto dragged = ed.exportForDrag();
        check (dragged.existsAsFile(), "export " + name + ": drag writes the file");
        juce::MemoryBlock a, b;
        clicked.loadFileAsData (a);
        dragged.loadFileAsData (b);
        check (a.getSize() > 0 && a == b, "export " + name + ": click and drag are byte-identical");
        // One frozen snapshot: edits after the capture do not reach it.
        const auto frozen = ed.snapshot();
        const auto before = DrumsProcessor::renderExport (*frozen);
        p->generate (DrumsProcessor::Generate::song);
        check (DrumsProcessor::renderExport (*frozen) == before, "export " + name + ": a snapshot is frozen at its capture");
        p->patternHistory().undo (p->patterns());
        check (DrumsProcessor::renderExport (*ed.snapshot()) == before, "export " + name + ": identical inputs give identical bytes");
        const auto parsed = parseMidi (a);
        check (parsed.ok && ! parsed.overlap && ! parsed.unmatchedOff, "export " + name + ": a valid file without overlaps");
        check (dragged.getFileName() == (scope == 0 ? "VOX Drums Pattern 1.mid" : "VOX Drums Song.mid"), "export " + name + ": file name");
        if (scope == 1)
            check (parsed.endTick == 64 * 3840, "export song: End of Track at bar 64");
    }
    // Song lengths 16 and 32, pattern lengths 1-4 bars.
    for (int length : { 16, 32 })
    {
        p->editSong ([length] (SongData& s) { s.length = static_cast<std::uint8_t> (length); });
        const auto parsed = parseMidi (DrumsProcessor::renderExport (*p->captureExport (ExportSnapshot::Scope::song)));
        check (parsed.ok && parsed.endTick == length * 3840, "export song: End of Track at bar " + juce::String (length));
        bool inside = true;
        for (const auto& n : parsed.notes)
            inside = inside && n.tick <= parsed.endTick;
        check (inside, "export song: a shorter song exports only its bars");
    }
    for (int bars = 1; bars <= 4; ++bars)
    {
        const auto pattern = std::make_unique<StoredPattern>();
        p->patterns().copySlot (0, *pattern);
        pattern->resize (16, bars);
        p->patterns().setSlot (0, *pattern);
        const auto parsed = parseMidi (DrumsProcessor::renderExport (*p->captureExport (ExportSnapshot::Scope::pattern)));
        check (parsed.ok && parsed.endTick == bars * 3840 && ! parsed.notes.empty(),
               "export pattern: " + juce::String (bars) + " bar(s) end at their last bar");
    }
    p->editorBeingDeleted (editor.get());
    editor.reset();
    if (root.getFileName() == "drags" && root.isAChildOf (workFolder))
        root.deleteRecursively();
    PatternEditor::setDragRootForTesting ({});
}

//==============================================================================
// Renders `blocks` blocks after auditioning `pad`; returns the buffer of the
// first block (and leaves the processor after the last).
juce::AudioBuffer<float> hitPad (DrumsProcessor& p, int pad, int frames = 512)
{
    juce::AudioBuffer<float> buffer (p.getTotalNumOutputChannels(), frames);
    juce::MidiBuffer midi;
    p.audition (pad, 110);
    p.processBlock (buffer, midi);
    return buffer;
}

void setOutput (DrumsProcessor& p, int pad, int output)
{
    auto settings = p.kitStore().pad (pad);
    settings.output = static_cast<std::uint8_t> (output);
    p.kitStore().setPad (pad, settings);
}

void testOutputs()
{
    // The contract: 9 host-visible stereo buses, 18 channels; the two send
    // pairs are internal and never appear as buses.
    {
        DrumsProcessor p;
        check (p.getBusCount (false) == 9 && p.getBusCount (true) == 0, "outputs: Main + 8 = 9 output buses, no input");
        check (kit::hostOutputBuses == 9 && kit::hostOutputChannels == 18 && kit::auxSends == 2 && kit::auxBuses == 10,
               "outputs: 9 host buses; 10 internal pairs (2 sends + 8 outputs)");
        bool named = p.getBus (false, 0)->getName() == "Main" && p.getBus (false, 0)->isEnabledByDefault();
        for (int bus = 1; bus < 9; ++bus)
            named = named && p.getBus (false, bus)->getName() == "Out " + juce::String (bus + 1) && ! p.getBus (false, bus)->isEnabledByDefault()
                 && p.getBus (false, bus)->getDefaultLayout() == juce::AudioChannelSet::stereo();
        check (named, "outputs: Main, Out 2..Out 9; stereo; the auxiliary outputs are off by default");
        auto layout = p.getBusesLayout();
        auto mono = layout;
        mono.outputBuses.getReference (2) = juce::AudioChannelSet::mono();
        auto extra = layout;
        extra.outputBuses.add (juce::AudioChannelSet::stereo());
        auto input = layout;
        input.inputBuses.add (juce::AudioChannelSet::stereo());
        auto noMain = layout;
        noMain.outputBuses.getReference (0) = juce::AudioChannelSet::disabled();
        check (p.isBusesLayoutSupported (layout) && ! p.isBusesLayoutSupported (mono) && ! p.isBusesLayoutSupported (extra)
                   && ! p.isBusesLayoutSupported (input) && ! p.isBusesLayoutSupported (noMain),
               "outputs: only stereo or disabled outputs, at most nine, stereo Main, no input");
    }
    // Every destination alone, with all buses enabled.
    {
        auto p = makeProcessor (48000.0, 512, 0x1ffu);
        check (p->getTotalNumOutputChannels() == 18, "outputs: 18 channels with every bus enabled");
        for (int output = 0; output <= 8; ++output)
        {
            const int pad = output % padCount;
            p->reset();
            setOutput (*p, pad, output);
            const auto buffer = hitPad (*p, pad);   // the first block also takes the panic of reset()
            const auto second = hitPad (*p, pad);
            bool alone = energy (second, output * 2) > 1.0e-3f || energy (second, output * 2 + 1) > 1.0e-3f;
            for (int ch = 0; ch < 18; ++ch)
                if (ch / 2 != output)
                    alone = alone && juce::exactlyEqual (energy (second, ch), 0.0f);
            check (alone, "outputs: a pad on " + juce::String (output == 0 ? "Main" : "Out " + juce::String (output + 1))
                              + " sounds there and nowhere else");
            check ((p->activeOutputs() & 0x1ffu) == 0x1ffu, "outputs: all nine buses reported active");
            setOutput (*p, pad, 0);
        }
    }
    // A disabled bus plays on Main, bit-identically to the enabled bus.
    {
        auto all = makeProcessor (48000.0, 512, 0x1ffu);
        auto mainOnly = makeProcessor (48000.0, 512, 1u);
        check (mainOnly->getTotalNumOutputChannels() == 2, "outputs: 2 channels with only Main enabled");
        for (auto* p : { all.get(), mainOnly.get() })
            setOutput (*p, 1, 5);
        const auto a = hitPad (*all, 1);
        const auto b = hitPad (*mainOnly, 1);
        bool same = energy (a, 10) > 1.0e-3f && juce::exactlyEqual (energy (a, 0), 0.0f);
        for (int i = 0; i < 512; ++i)
            same = same && juce::exactlyEqual (a.getSample (10, i), b.getSample (0, i)) && juce::exactlyEqual (a.getSample (11, i), b.getSample (1, i));
        check (same, "outputs: a pad on a bus the host disabled plays on Main, identical to the enabled bus");
        check (mainOnly->activeOutputs() == 1u, "outputs: only Main reported active");
    }
    // A sparse layout: Main and Out 4 only.
    {
        auto p = makeProcessor (48000.0, 512, 1u | (1u << 3));
        check (p->getTotalNumOutputChannels() == 4, "outputs: 4 channels with Main and Out 4");
        setOutput (*p, 0, 3);
        setOutput (*p, 1, 2);
        auto buffer = hitPad (*p, 0);
        check (energy (buffer, 2) > 1.0e-3f && juce::exactlyEqual (energy (buffer, 0), 0.0f), "outputs: Out 4 is the second enabled bus");
        p->reset();
        hitPad (*p, 15);
        buffer = hitPad (*p, 1);
        check (energy (buffer, 0) > 1.0e-3f && energy (buffer, 2) < 1.0e-6f, "outputs: a pad on disabled Out 3 falls back to Main, not to Out 4");
    }
    // Sends and returns: post-fader, return to Main only, tails, All Sound Off.
    {
        auto p = makeProcessor (48000.0, 512, 0x1ffu);
        setOutput (*p, 0, 2);
        setPlain (*p, "drm.p01.send1", 1.0f);
        setPlain (*p, "drm.fx.delay.time", 0.0f);       // 1/16 at 120 BPM: 125 ms
        setPlain (*p, "drm.fx.delay.feedback", 0.5f);
        juce::AudioBuffer<float> buffer (18, 512);
        juce::MidiBuffer midi;
        p->audition (0, 127);
        float mainEarly = 0.0f, mainLater = 0.0f, others = 0.0f, dry = 0.0f;
        for (int b = 0; b < 60; ++b)
        {
            p->processBlock (buffer, midi);
            (b < 10 ? mainEarly : mainLater) = std::max (b < 10 ? mainEarly : mainLater, std::max (energy (buffer, 0), energy (buffer, 1)));
            dry = std::max (dry, energy (buffer, 4));
            for (int ch = 2; ch < 18; ++ch)
                if (ch / 2 != 2)
                    others = std::max (others, energy (buffer, ch));
        }
        check (dry > 1.0e-3f && juce::exactlyEqual (mainEarly, 0.0f) && mainLater > 1.0e-4f,
               "sends: the dry pad stays on its bus; the delay returns to Main after the delay time");
        check (juce::exactlyEqual (others, 0.0f), "sends: the return does not leak into other outputs");
        // The tail continues after the voice ended, then All Sound Off clears it.
        p->processBlock (buffer, midi);
        check (energy (buffer, 0) > 1.0e-6f, "sends: the delay tail continues");
        midi.addEvent (juce::MidiMessage::allSoundOff (10), 0);
        p->processBlock (buffer, midi);
        midi.clear();
        p->processBlock (buffer, midi);
        check (juce::exactlyEqual (energy (buffer, 0), 0.0f) && juce::exactlyEqual (energy (buffer, 1), 0.0f), "sends: All Sound Off clears the return tails");
        // Reverb on send 2, with the send level measurable and the tail ending by itself.
        setPlain (*p, "drm.p01.send1", 0.0f);
        float returned[2] = { 0.0f, 0.0f };
        for (int k = 0; k < 2; ++k)
        {
            setPlain (*p, "drm.p01.send2", k == 0 ? 0.25f : 1.0f);
            p->reset();
            p->processBlock (buffer, midi);
            p->audition (0, 127);
            for (int b = 0; b < 30; ++b)
            {
                p->processBlock (buffer, midi);
                returned[k] = std::max (returned[k], energy (buffer, 0));
            }
        }
        check (returned[0] > 1.0e-5f && returned[1] > returned[0] * 2.0f, "sends: the reverb return follows the send level");
        float last = 1.0f;
        for (int b = 0; b < 2400; ++b)   // 25 s
        {
            p->processBlock (buffer, midi);
            last = energy (buffer, 0);
        }
        check (last < 1.0e-5f, "sends: the reverb tail ends (" + juce::String (last, 8) + ")");
        // Return level at zero: no return.
        setPlain (*p, "drm.fx.reverb.level", 0.0f);
        p->audition (0, 127);
        float silent = 0.0f;
        for (int b = 0; b < 20; ++b)
        {
            p->processBlock (buffer, midi);
            silent = std::max (silent, energy (buffer, 0));
        }
        check (juce::exactlyEqual (silent, 0.0f), "sends: a return level of zero silences the return");
    }
    // Mute, solo, meters.
    {
        auto p = makeProcessor();
        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        hitPad (*p, 0);
        check (p->takePadMeter (0) > 1.0e-3f && juce::exactlyEqual (p->takePadMeter (3), 0.0f) && p->takeMainMeter (0) > 1.0e-3f,
               "meters: the pad that sounds and Main show a level, a silent pad shows none");
        check (juce::exactlyEqual (p->takePadMeter (0), 0.0f), "meters: a meter is the peak since it was last read");
        p->reset();
        p->processBlock (buffer, midi);
        setPlain (*p, "drm.p01.mute", 1.0f);
        p->takeHitMask();
        auto out = hitPad (*p, 0);
        check (juce::exactlyEqual (energy (out, 0), 0.0f) && p->takeHitMask() == 0, "mute: a muted pad is silent and does not light");
        setPlain (*p, "drm.p01.mute", 0.0f);
        setPlain (*p, "drm.p04.solo", 1.0f);
        p->audition (0, 110);
        p->audition (3, 110);
        p->processBlock (buffer, midi);
        check (juce::exactlyEqual (p->takePadMeter (0), 0.0f) && p->takePadMeter (3) > 1.0e-3f, "solo: only the soloed pad sounds");
        setPlain (*p, "drm.p04.solo", 0.0f);
        // Level and pan are measurable.
        p->reset();
        p->processBlock (buffer, midi);
        setPlain (*p, "drm.p02.pan", -1.0f);
        out = hitPad (*p, 1);
        check (energy (out, 0) > 1.0e-3f && energy (out, 1) < energy (out, 0) * 0.05f, "mixer: pan left puts the pad on the left");
        p->reset();
        p->processBlock (buffer, midi);
        setPlain (*p, "drm.p02.level", 0.0f);
        out = hitPad (*p, 1);
        check (juce::exactlyEqual (energy (out, 0), 0.0f), "mixer: level zero silences the pad");
    }
    // Buffer boundaries: a block far larger than the prepared maximum is
    // rendered in chunks of the internal buses and equals the chunked render.
    {
        const int big = 5000;
        const auto render = [big] (bool whole) {
            auto p = makeProcessor (48000.0, 64, 0x1ffu);   // internal buses: 1024 frames
            buildSong (*p);
            setPlain (*p, "drm.seq.mode", 2.0f);
            for (int pad = 0; pad < padCount; ++pad)
            {
                setOutput (*p, pad, pad % 9);
                setPlain (*p, juce::String (padPrefix (pad)) + ".send1", 0.5f);
                setPlain (*p, juce::String (padPrefix (pad)) + ".send2", 0.5f);
            }
            TestPlayHead head;
            p->setPlayHead (&head);
            for (int pad = 0; pad < padCount; ++pad)
                p->audition (pad, 100);   // every bus carries a pad from the first frame
            juce::AudioBuffer<float> out (18, big);
            const double perFrame = 480.0 / (60.0 * 48000.0);
            if (whole)
            {
                juce::MidiBuffer midi;
                midi.addEvent (juce::MidiMessage::noteOn (10, 36, static_cast<juce::uint8> (100)), 0);
                midi.addEvent (juce::MidiMessage::noteOn (10, 38, static_cast<juce::uint8> (90)), 1023);
                midi.addEvent (juce::MidiMessage::noteOn (10, 42, static_cast<juce::uint8> (80)), 1024);
                midi.addEvent (juce::MidiMessage::noteOn (10, 46, static_cast<juce::uint8> (70)), big - 1);
                head.set (true, 480.0, 8.0);
                p->processBlock (out, midi);
            }
            else
                for (int start = 0; start < big; start += 1024)
                {
                    const int n = std::min (1024, big - start);
                    juce::AudioBuffer<float> part (18, n);
                    juce::MidiBuffer midi;
                    for (const auto& [at, note, velocity] : { std::tuple { 0, 36, 100 }, std::tuple { 1023, 38, 90 }, std::tuple { 1024, 42, 80 },
                                                             std::tuple { big - 1, 46, 70 } })
                        if (at >= start && at < start + n)
                            midi.addEvent (juce::MidiMessage::noteOn (10, note, static_cast<juce::uint8> (velocity)), at - start);
                    head.set (true, 480.0, 8.0 + start * perFrame);
                    p->processBlock (part, midi);
                    for (int ch = 0; ch < 18; ++ch)
                        out.copyFrom (ch, start, part, ch, 0, n);
                }
            p->setPlayHead (nullptr);
            return out;
        };
        const auto whole = render (true), chunked = render (false);
        bool same = true, finite = true;
        int soundingBuses = 0;
        for (int ch = 0; ch < 18; ++ch)
        {
            for (int i = 0; i < big; ++i)
            {
                same = same && juce::exactlyEqual (whole.getSample (ch, i), chunked.getSample (ch, i));
                finite = finite && std::isfinite (whole.getSample (ch, i));
            }
            if (ch % 2 == 0 && energy (whole, ch) > 1.0e-4f)
                ++soundingBuses;
        }
        check (finite && same, "buffers: a 5000-frame block on a 64-frame setup equals the same audio in 1024-frame blocks");
        check (soundingBuses == 9, "buffers: all nine buses carry signal in the oversized block (" + juce::String (soundingBuses) + ")");
        // Tiny and odd blocks through all buses.
        auto p = makeProcessor (44100.0, 1, 0x1ffu);
        for (int frames : { 1, 2, 3, 17, 333, 1023, 1024, 1025, 2049 })
        {
            juce::AudioBuffer<float> buffer (18, frames);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (10, 36, static_cast<juce::uint8> (100)), frames - 1);
            p->processBlock (buffer, midi);
            for (int ch = 0; ch < 18; ++ch)
                finite = finite && std::isfinite (buffer.getMagnitude (ch, 0, frames));
        }
        check (finite, "buffers: blocks of 1 to 2049 frames on nine buses stay finite");
        // A buffer with fewer channels than the layout (a host that passes less).
        juce::AudioBuffer<float> narrow (2, 256), mono (1, 256), empty (18, 0);
        juce::MidiBuffer midi;
        p->processBlock (narrow, midi);
        p->processBlock (mono, midi);
        p->processBlock (empty, midi);
        check (std::isfinite (narrow.getMagnitude (0, 0, 256)), "buffers: a buffer narrower than the layout is handled");
    }
}

//==============================================================================
void testGenerator()
{
    // Distinct styles: every pair of styles gives a different pattern, and
    // each has its idiom on the kick lane.
    std::vector<std::unique_ptr<StoredPattern>> byStyle;
    for (int style = 0; style < styleCount; ++style)
    {
        GeneratorSettings g;
        g.style = static_cast<Style> (style);
        g.seed = 5;
        auto p = std::make_unique<StoredPattern>();
        p->resize (16, 4);
        generatePattern (g, *p);
        auto again = std::make_unique<StoredPattern>();
        again->resize (16, 4);
        generatePattern (g, *again);
        check (*p == *again, juce::String ("generator: deterministic for ") + styleName (g.style));
        int hits = 0;
        for (const auto& cell : p->cells)
            hits += cell.on ? 1 : 0;
        check (hits >= 8, juce::String ("generator: ") + styleName (g.style) + " writes a pattern (" + juce::String (hits) + " hits)");
        byStyle.push_back (std::move (p));
    }
    bool distinct = true;
    for (std::size_t a = 0; a < byStyle.size(); ++a)
        for (std::size_t b = a + 1; b < byStyle.size(); ++b)
            distinct = distinct && ! (*byStyle[a] == *byStyle[b]);
    check (distinct, "generator: the seven styles give seven different patterns");
    const auto kickOn = [&] (int style, int step) { return byStyle[static_cast<std::size_t> (style)]->cell (0, 0, step).on; };
    check (kickOn (0, 0) && kickOn (0, 4) && kickOn (0, 8) && kickOn (0, 12), "generator: full-on has a four-on-the-floor kick");
    check (kickOn (2, 4) && kickOn (3, 12), "generator: house and techno have a four-on-the-floor kick");
    check (kickOn (4, 0) && ! kickOn (4, 4) && ! kickOn (4, 12) && byStyle[4]->cell (1, 0, 8).on, "generator: trap is half-time (snare on 3)");
    check (kickOn (5, 0) && kickOn (5, 10) && ! kickOn (5, 4) && byStyle[5]->cell (1, 0, 4).on, "generator: breaks has a broken kick and a back-beat");
    int ambientHits = 0, fullOnHits = 0;
    for (const auto& cell : byStyle[6]->cells)
        ambientHits += cell.on ? 1 : 0;
    for (const auto& cell : byStyle[0]->cells)
        fullOnHits += cell.on ? 1 : 0;
    check (ambientHits * 2 < fullOnHits, "generator: ambient is sparse");
    // Each amount changes the result.
    {
        GeneratorSettings base;
        base.style = Style::trap;
        base.seed = 9;
        auto reference = std::make_unique<StoredPattern>();
        reference->resize (16, 4);
        generatePattern (base, *reference);
        float GeneratorSettings::* amounts[] = { &GeneratorSettings::density, &GeneratorSettings::groove, &GeneratorSettings::syncopation,
                                                &GeneratorSettings::swing, &GeneratorSettings::humanize, &GeneratorSettings::complexity,
                                                &GeneratorSettings::fills, &GeneratorSettings::variation };
        const char* names[] = { "density", "groove", "syncopation", "swing", "humanize", "complexity", "fills", "variation" };
        for (int i = 0; i < 8; ++i)
        {
            auto g = base;
            g.*amounts[i] = (base.*amounts[i]) < 0.5f ? 1.0f : 0.0f;
            auto changed = std::make_unique<StoredPattern>();
            changed->resize (16, 4);
            generatePattern (g, *changed);
            check (! (*changed == *reference), juce::String ("generator: ") + names[i] + " changes the pattern");
        }
        auto g = base;
        g.seed = 10;
        auto seeded = std::make_unique<StoredPattern>();
        seeded->resize (16, 4);
        generatePattern (g, *seeded);
        check (! (*seeded == *reference), "generator: the seed changes the pattern");
        // Grids of 32 and 64 steps keep the idiom on the quarter notes.
        for (int steps : { 32, 64 })
        {
            auto fine = std::make_unique<StoredPattern>();
            fine->resize (steps, 2);
            GeneratorSettings f;
            generatePattern (f, *fine);
            check (fine->cell (0, 0, 0).on && fine->cell (0, 0, steps / 4).on && ! fine->cell (0, 0, 1).on,
                   "generator: " + juce::String (steps) + "-step grid");
        }
    }
    // Through the processor: every action changes data, locks hold, undo restores.
    auto p = makeProcessor();
    auto& store = p->patterns();
    const auto slot = [&] { auto s = std::make_unique<StoredPattern>(); store.copySlot (0, *s); return s; };
    const auto alternate = [&] { auto s = std::make_unique<StoredPattern>(); store.copyAlternate (0, *s); return s; };
    using G = DrumsProcessor::Generate;
    {
        const auto before = slot();
        const auto seed = p->generator().seed;
        check (p->generate (G::pattern) && ! (*slot() == *before) && p->generator().seed != seed, "generate pattern: new data and a new seed");
        const auto first = slot();
        check (p->generate (G::pattern) && ! (*slot() == *first), "generate pattern: pressing again gives new data again");
        check (p->patternHistory().undo (store) == 0 && *slot() == *first, "generate pattern: undo restores the previous pattern");
        p->patternHistory().undo (store);
        check (*slot() == *before, "generate pattern: a second undo restores the original");
    }
    {
        const auto before = slot();
        check (p->generate (G::mutate, 1u << 3) && ! (*slot() == *before), "mutate: changes the pattern");
        const auto after = slot();
        bool onlyLane = true;
        for (int lane = 0; lane < padCount; ++lane)
            for (int bar = 0; bar < after->bars; ++bar)
                for (int st = 0; st < after->steps; ++st)
                    if (lane != 3)
                        onlyLane = onlyLane && after->cell (lane, bar, st).on == before->cell (lane, bar, st).on
                                && after->cell (lane, bar, st).velocity == before->cell (lane, bar, st).velocity;
        check (onlyLane, "mutate: only the chosen lane changes");
    }
    {
        const auto before = slot();
        check (p->generate (G::fill) && ! (*slot() == *before), "fill: changes the pattern");
        const auto after = slot();
        bool lastBarOnly = true;
        for (int lane = 0; lane < padCount; ++lane)
            for (int bar = 0; bar + 1 < after->bars; ++bar)
                for (int st = 0; st < after->steps; ++st)
                    lastBarOnly = lastBarOnly && after->cell (lane, bar, st).on == before->cell (lane, bar, st).on;
        check (lastBarOnly, "fill: only the last bar changes");
    }
    {
        const auto a = slot();
        const auto before = alternate();
        check (p->generate (G::variation) && ! (*alternate() == *before) && *slot() == *a, "variation: B changes, A stays");
        check (! (*alternate() == *a), "variation: B differs from A");
        check (p->patternHistory().undo (store) == 0 && *alternate() == *before, "variation: undo restores B");
    }
    {
        const auto before = std::make_unique<PatternStore::Everything>();
        store.copyEverything (*before);
        for (int length : { 16, 32, 64 })
        {
            p->editSong ([length] (SongData& s) { s.length = static_cast<std::uint8_t> (length); });
            check (p->generate (G::song), "generate song: changes data at " + juce::String (length) + " bars");
            const auto song = store.song();
            int placed = 0;
            std::set<int> slots;
            for (int bar = 0; bar < length; ++bar)
                if (song.bars[static_cast<std::size_t> (bar)].kind == SongBar::Kind::pattern)
                {
                    ++placed;
                    slots.insert (song.bars[static_cast<std::size_t> (bar)].slot * 2 + song.bars[static_cast<std::size_t> (bar)].variation);
                }
            check (placed == length && song.length == length && slots.size() >= 4,
                   "generate song: fills all " + juce::String (length) + " bars with A and B of several slots");
        }
        const auto after = std::make_unique<PatternStore::Everything>();
        store.copyEverything (*after);
        bool allSlots = true;
        for (std::size_t i = 0; i < 5; ++i)
            allSlots = allSlots && ! (after->slots[i] == before->slots[i]) && ! (after->alternates[i] == after->slots[i]);
        check (allSlots, "generate song: all five slots and their variations are new");
        p->patternHistory().undo (store);
        const auto undone = std::make_unique<PatternStore::Everything>();
        store.copyEverything (*undone);
        check (undone->song.length == 64 && ! (undone->slots == after->slots), "generate song: undo restores the patterns before it");
    }
    // Locks.
    {
        auto pattern = slot();
        for (int st = 0; st < pattern->steps; ++st)
        {
            auto& cell = pattern->cell (0, 0, st);
            cell = {};
            cell.on = st % 5 == 0;
            cell.velocity = 33;
            cell.locked = true;
        }
        pattern->cell (6, 1, 2) = {};
        pattern->cell (6, 1, 2).locked = true;   // a locked empty step stays empty
        store.setSlot (0, *pattern);
        auto g = p->generator();
        g.laneLocks = 1u << 4;
        g.fills = 1.0f;
        p->setGenerator (g);
        const auto lockedLane = slot();
        for (auto what : { G::pattern, G::mutate, G::fill, G::song })
        {
            p->generate (what);
            const auto after = slot();
            bool steps = true, lane = true;
            for (int st = 0; st < after->steps; ++st)
            {
                const auto& c = after->cell (0, 0, st);
                steps = steps && c.locked && c.on == (st % 5 == 0) && c.velocity == 33;
            }
            steps = steps && ! after->cell (6, 1, 2).on && after->cell (6, 1, 2).locked;
            for (int bar = 0; bar < after->bars; ++bar)
                for (int st = 0; st < after->steps; ++st)
                    lane = lane && after->cell (4, bar, st).on == lockedLane->cell (4, bar, st).on
                        && after->cell (4, bar, st).velocity == lockedLane->cell (4, bar, st).velocity;
            check (steps, "locks: locked steps survive generator action " + juce::String (static_cast<int> (what)));
            check (lane, "locks: a locked lane survives generator action " + juce::String (static_cast<int> (what)));
        }
        // Everything locked: the action reports that nothing changed.
        g.laneLocks = 0xffffu;
        p->setGenerator (g);
        const auto frozen = slot();
        check (! p->generate (G::pattern) && *slot() == *frozen, "locks: with every lane locked Generate changes nothing and says so");
    }
}

//==============================================================================
juce::File writeWav (const juce::String& name, double frequency)
{
    const auto file = workFolder.getChildFile (name);
    file.deleteFile();
    juce::AudioBuffer<float> audio (1, 4800);
    for (int i = 0; i < 4800; ++i)
        audio.setSample (0, i, 0.5f * static_cast<float> (std::sin (2.0 * 3.141592653589793 * frequency * i / 48000.0)));
    juce::WavAudioFormat format;
    if (auto stream = file.createOutputStream())
    {
        std::unique_ptr<juce::AudioFormatWriter> writer (format.createWriterFor (stream.release(), 48000.0, 1, 16, {}, 0));
        if (writer != nullptr)
            writer->writeFromAudioSampleBuffer (audio, 0, 4800);
    }
    return file;
}

void testPresets()
{
    const auto presetRoot = workFolder.getChildFile ("presets");
    const auto library = workFolder.getChildFile ("library");
    setEnvironment ("VOX_DRUMS_PRESETS", presetRoot.getFullPathName());
    setEnvironment ("VOX_DRUMS_SAMPLE_LIBRARY", library.getFullPathName());
    check (Presets::userFolder (PresetType::kit) == presetRoot.getChildFile ("Kits"), "presets: the user folder follows VOX_DRUMS_PRESETS");

    auto p = makeProcessor();
    auto& presets = p->presets();
    // Kit: structure, kit-scope parameters; not the sequencer or mute.
    {
        auto pad = p->kitStore().pad (2);
        pad.model = kit::Model::tom;
        pad.character = 0.91f;
        pad.output = 4;
        p->kitStore().setPad (2, pad);
        setPlain (*p, "drm.p03.level", 0.31f);
        setPlain (*p, "drm.p03.send2", 0.44f);
        setPlain (*p, "drm.fx.reverb.size", 0.9f);
        setPlain (*p, "drm.kit.volume", 0.42f);
        const auto file = Presets::userFolder (PresetType::kit).getChildFile ("Test Kit.voxkitpreset");
        check (presets.save (PresetType::kit, "Test Kit", file).wasOk() && file.existsAsFile(), "kit preset: saved");
        PresetInfo info;
        juce::MemoryBlock bytes;
        file.loadFileAsData (bytes);
        check (Presets::describe (bytes.getData(), bytes.getSize(), *p, info) && info.type == PresetType::kit && info.name == "Test Kit",
               "kit preset: type and name are in the file");
        check (presets.userPresets (PresetType::kit).contains (file), "kit preset: listed");
        const auto saved = stateOf (*p);
        // Change everything a kit owns and something it does not.
        p->kitStore().setAll (defaultKit());
        setPlain (*p, "drm.p03.level", 0.9f);
        setPlain (*p, "drm.p03.send2", 0.0f);
        setPlain (*p, "drm.fx.reverb.size", 0.1f);
        setPlain (*p, "drm.kit.volume", 0.9f);
        setPlain (*p, "drm.seq.swing", 0.33f);
        setPlain (*p, "drm.p03.mute", 1.0f);
        const auto changed = stateOf (*p);
        check (presets.load (file).wasOk(), "kit preset: loaded");
        const auto loaded = p->kitStore().pad (2);
        check (loaded.model == kit::Model::tom && std::abs (loaded.character - 0.91f) < 1.0e-6f && loaded.output == 4,
               "kit preset: pad structure and output restored");
        check (std::abs (plainOf (*p, "drm.p03.level") - 0.31f) < 1.0e-5f && std::abs (plainOf (*p, "drm.p03.send2") - 0.44f) < 1.0e-5f
                   && std::abs (plainOf (*p, "drm.fx.reverb.size") - 0.9f) < 1.0e-5f && std::abs (plainOf (*p, "drm.kit.volume") - 0.42f) < 1.0e-5f,
               "kit preset: mix, sends and returns restored");
        check (std::abs (plainOf (*p, "drm.seq.swing") - 0.33f) < 1.0e-5f && plainOf (*p, "drm.p03.mute") >= 0.5f,
               "kit preset: the sequencer and mutes are not part of a kit");
        check (presets.canUndoLoad() && presets.undoLoad().wasOk() && stateOf (*p) == changed && ! presets.canUndoLoad(),
               "kit preset: Undo Load returns to the state before the load");
        juce::ignoreUnused (saved);
    }
    // Pad preset onto another pad: the target keeps its note and output.
    {
        auto pad = p->kitStore().pad (0);
        pad.drive = 5.5f;
        p->kitStore().setPad (0, pad);
        setPlain (*p, "drm.p01.tune", 7.0f);
        const auto blob = presets.capture (PresetType::pad, "Kick", 0);
        const auto targetBefore = p->kitStore().pad (9);
        check (presets.apply (blob.getData(), blob.getSize(), 9).wasOk(), "pad preset: applied to pad 10");
        const auto target = p->kitStore().pad (9);
        check (target.model == kit::Model::kick && std::abs (target.drive - 5.5f) < 1.0e-6f && target.note == targetBefore.note
                   && target.output == targetBefore.output && std::abs (plainOf (*p, "drm.p10.tune") - 7.0f) < 1.0e-5f,
               "pad preset: sound moves to the target pad, which keeps its MIDI note and output");
        presets.undoLoad();
        check (p->kitStore().pad (9).model == targetBefore.model, "pad preset: Undo Load");
    }
    // Pattern and song presets.
    {
        p->generate (DrumsProcessor::Generate::pattern);
        p->generate (DrumsProcessor::Generate::variation);
        const auto a = std::make_unique<StoredPattern>(), b = std::make_unique<StoredPattern>(), got = std::make_unique<StoredPattern>();
        p->patterns().copySlot (0, *a);
        p->patterns().copyAlternate (0, *b);
        const auto blob = presets.capture (PresetType::pattern, "Groove", 0);
        check (presets.apply (blob.getData(), blob.getSize(), 3).wasOk(), "pattern preset: applied to slot 4");
        p->patterns().copySlot (3, *got);
        const bool sameA = *got == *a;
        p->patterns().copyAlternate (3, *got);
        check (sameA && *got == *b, "pattern preset: A and B arrive in the target slot");

        buildSong (*p);
        const auto song = presets.capture (PresetType::song, "Song");
        const auto wanted = stateOf (*p);
        p->generate (DrumsProcessor::Generate::song);
        check (presets.apply (song.getData(), song.getSize()).wasOk(), "song preset: applied");
        const auto all = std::make_unique<PatternStore::Everything>();
        p->patterns().copyEverything (*all);
        check (all->song.length == 64 && all->song.bars[63].kind == SongBar::Kind::unique && all->song.bars[63].cells != nullptr
                   && all->song.bars[63].cells->cell (15, 63).velocity == 77,
               "song preset: the timeline with its unique bars is restored");
        juce::ignoreUnused (wanted);
    }
    // Malformed and foreign files: rejected, nothing changes.
    {
        const auto before = stateOf (*p);
        auto good = presets.capture (PresetType::kit, "Good");
        const auto rejects = [&] (juce::MemoryBlock blob, const juce::String& what) {
            const auto result = presets.apply (blob.getData(), blob.getSize());
            check (result.failed() && stateOf (*p) == before, "malformed preset (" + what + "): rejected, the instrument is unchanged");
        };
        rejects ({}, "empty");
        rejects (juce::MemoryBlock ("not a preset at all", 19), "text");
        auto truncated = good;
        truncated.setSize (good.getSize() / 2);
        rejects (truncated, "truncated");
        auto flipped = good;
        static_cast<char*> (flipped.getData())[good.getSize() / 2] ^= 0x55;
        rejects (flipped, "corrupt byte (checksum)");
        // A valid container that is not a preset (a whole project state).
        rejects (before, "a project state without a preset type");
        // A preset whose pad section is out of range.
        {
            juce::MemoryBlock block;
            vox::preset::StateDocument doc;
            const auto decoded = vox::preset::decode ({ static_cast<const std::byte*> (good.getData()), good.getSize() },
                                                      p->product().stateTag, p->product().stateSchema);
            doc = decoded.document;
            for (auto& s : doc.sections)
                if (s.tag == padSectionTag)
                    s.data[8] = std::byte { 200 };   // model of pad 1
            const auto bytes = vox::preset::encode (doc);
            rejects (juce::MemoryBlock (bytes->data(), bytes->size()), "invalid pad section");
        }
        const auto missing = workFolder.getChildFile ("does-not-exist.voxkitpreset");
        check (presets.load (missing).failed() && stateOf (*p) == before, "preset file missing: rejected");
        auto fresh = makeProcessor();
        check (fresh->presets().apply (truncated.getData(), truncated.getSize()).failed() && ! fresh->presets().canUndoLoad(),
               "a rejected load leaves nothing to undo");
    }
    // Samples: missing file keeps the reference; portable kit carries the file.
    {
        const auto wav = writeWav ("kick-sample.wav", 110.0);
        const auto decoded = decodeSampleFile (wav);
        check (decoded.buffer != nullptr, "portable kit: the test sample decodes");
        if (decoded.buffer != nullptr)
        {
            p->samples().assign (slotOf (0, 0), decoded.buffer, wav);
            auto pad = p->kitStore().pad (0);
            pad.source = kit::Source::sample;
            p->kitStore().setPad (0, pad);
            const auto hash = hexOf (p->samples().pad (slotOf (0, 0)).reference->sha256);
            const auto zip = workFolder.getChildFile ("Portable.voxkit");
            check (presets.exportPortableKit (zip, "Portable").wasOk() && zip.existsAsFile(), "portable kit: exported");
            const auto kitOnly = presets.capture (PresetType::kit, "References only");
            // The original file disappears (another computer).
            wav.deleteFile();
            {
                auto other = makeProcessor();
                check (other->presets().apply (kitOnly.getData(), kitOnly.getSize()).wasOk(), "missing sample: the kit still loads");
                const auto info = other->samples().pad (slotOf (0, 0));
                check (info.reference.has_value() && info.buffer == nullptr && info.status.isNotEmpty() && hexOf (info.reference->sha256) == hash,
                       "missing sample: the reference is kept and shown as missing (" + info.status + ")");
                check (other->samples().diagnostics().isNotEmpty(), "missing sample: reported in the diagnostics");
            }
            {
                auto other = makeProcessor();
                check (other->presets().importPortableKit (zip).wasOk(), "portable kit: imported on a machine without the file");
                const auto info = other->samples().pad (slotOf (0, 0));
                check (info.reference.has_value() && info.buffer != nullptr && hexOf (info.reference->sha256) == hash,
                       "portable kit: the sample plays after the import");
                check (library.getChildFile (hash + ".wav").existsAsFile(), "portable kit: the sample is in the user library under its hash");
                check (other->kitStore().pad (0).source == kit::Source::sample, "portable kit: the kit arrives");
            }
            // A tampered archive: a sample that does not match its name, or an unexpected file.
            {
                juce::ZipFile::Builder builder;
                builder.addEntry (std::make_unique<juce::MemoryInputStream> (kitOnly, true), 9, "kit.voxkitpreset", juce::Time());
                builder.addEntry (std::make_unique<juce::MemoryInputStream> ("not the audio", 13, true), 0, "samples/" + hash + ".wav", juce::Time());
                const auto bad = workFolder.getChildFile ("Tampered.voxkit");
                bad.deleteFile();
                {
                    juce::FileOutputStream out (bad);
                    builder.writeToStream (out, nullptr);
                }
                auto other = makeProcessor();
                const auto before = stateOf (*other);
                check (other->presets().importPortableKit (bad).failed() && stateOf (*other) == before, "portable kit: a sample that does not match its hash is refused");
                juce::ZipFile::Builder traversal;
                traversal.addEntry (std::make_unique<juce::MemoryInputStream> (kitOnly, true), 9, "kit.voxkitpreset", juce::Time());
                traversal.addEntry (std::make_unique<juce::MemoryInputStream> ("x", 1, true), 0, "samples/../../evil.wav", juce::Time());
                const auto evil = workFolder.getChildFile ("Traversal.voxkit");
                evil.deleteFile();
                {
                    juce::FileOutputStream out (evil);
                    traversal.writeToStream (out, nullptr);
                }
                check (other->presets().importPortableKit (evil).failed() && stateOf (*other) == before
                           && ! workFolder.getParentDirectory().getChildFile ("evil.wav").exists(),
                       "portable kit: a path in the archive is refused and nothing is written");
            }
        }
    }
    // Factory content: three families, distinct, with patterns; hashes as committed.
    {
        check (factoryKitCount >= 3, "factory: at least three kit families");
        std::set<juce::String> families;
        std::vector<juce::MemoryBlock> kitStates;
        for (int kit = 0; kit < factoryKitCount; ++kit)
        {
            auto q = makeProcessor();
            families.insert (factoryKitInfo (kit).family);
            check (q->presets().loadFactoryKit (kit).wasOk(), juce::String ("factory: kit loads: ") + factoryKitInfo (kit).name);
            check (q->presets().loadFactoryPatterns (kit).wasOk(), juce::String ("factory: patterns load: ") + factoryKitInfo (kit).name);
            kitStates.push_back (stateOf (*q));
            // Every pad of the kit sounds, and its patterns play.
            int sounding = 0;
            for (int pad = 0; pad < padCount; ++pad)
            {
                q->reset();
                hitPad (*q, 15 - pad);
                const auto out = hitPad (*q, pad);
                sounding += energy (out, 0) > 1.0e-4f || energy (out, 1) > 1.0e-4f ? 1 : 0;
            }
            check (sounding == padCount, juce::String ("factory: all 16 pads sound in ") + factoryKitInfo (kit).name + " (" + juce::String (sounding) + ")");
            const auto all = std::make_unique<PatternStore::Everything>();
            q->patterns().copyEverything (*all);
            bool useful = all->song.length == 16;
            for (std::size_t slot = 0; slot < 5; ++slot)
            {
                int hits = 0;
                for (const auto& cell : all->slots[slot].cells)
                    hits += cell.on ? 1 : 0;
                useful = useful && hits >= 16 && ! (all->alternates[slot] == all->slots[slot]);
            }
            check (useful, juce::String ("factory: five patterns with B variations and a song in ") + factoryKitInfo (kit).name);
            check (q->presets().undoLoad().wasOk(), "factory: Undo Load after a factory load");
        }
        check (families.size() >= 3, "factory: three distinct families");
        check (kitStates[0] != kitStates[1] && kitStates[1] != kitStates[2] && kitStates[0] != kitStates[2], "factory: the kits differ");
        const auto manifest = factoryManifest();
        if (std::getenv ("VOX_GOLDEN_UPDATE") != nullptr)
        {
            std::ofstream (manifestPath, std::ios::binary) << manifest;
            std::printf ("wrote %s\n", manifestPath.c_str());
        }
        std::ifstream in (manifestPath, std::ios::binary);
        std::stringstream committed;
        committed << in.rdbuf();
        auto expected = committed.str();
        expected.erase (std::remove (expected.begin(), expected.end(), '\r'), expected.end());
        check (expected == manifest, "factory: the content matches the committed hashes (products/drums/factory-content.csv)");
    }
}

//==============================================================================
void testState()
{
    auto p = makeProcessor();
    buildSong (*p);
    GeneratorSettings g;
    g.style = Style::breaks;
    g.density = 0.83f;
    g.seed = 987654321u;
    g.laneLocks = 0x8421u;
    p->setGenerator (g);
    p->setUiState ({ 3, 11, 42, 2, 1 });
    setOutput (*p, 5, 7);
    p->kitStore().setVoiceLimit (32);
    setPlain (*p, "drm.p07.send1", 0.66f);
    setPlain (*p, "drm.p07.solo", 1.0f);
    setPlain (*p, "drm.fx.delay.time", 5.0f);
    auto pattern = std::make_unique<StoredPattern>();
    p->patterns().copySlot (1, *pattern);
    pattern->cell (2, 0, 3).locked = true;
    p->patterns().setSlot (1, *pattern);
    const auto saved = stateOf (*p);
    check (saved.getSize() > 0 && saved.getSize() < vox::preset::maxStateBytes, "state: the candidate state fits the container ("
                                                                                   + juce::String (static_cast<int> (saved.getSize())) + " bytes)");
    {
        auto q = makeProcessor();
        q->setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
        check (q->lastStateError().isEmpty(), "state: restored (" + q->lastStateError() + ")");
        check (stateOf (*q) == saved, "state: saves back byte-identically");
        const auto song = q->patterns().song();
        check (song.songMode && song.length == 64 && song.bars[63].kind == SongBar::Kind::unique && song.bars[20].cells != nullptr
                   && song.bars[20].cells->steps == 32 && song.bars[4].kind == SongBar::Kind::pattern && song.bars[8].variation == 1,
               "state: the song (mode, length, references, unique bars) is restored");
        check (q->generator() == g, "state: generator settings and seed are restored");
        check (q->uiState() == UiState { 3, 11, 42, 2, 1 }, "state: the editor's page, pad and bars are restored");
        check (q->kitStore().pad (5).output == 7 && q->kitStore().snapshot().voiceLimit == 32, "state: pad outputs and the voice limit are restored");
        check (std::abs (plainOf (*q, "drm.p07.send1") - 0.66f) < 1.0e-5f && plainOf (*q, "drm.p07.solo") >= 0.5f
                   && static_cast<int> (plainOf (*q, "drm.fx.delay.time")) == 5,
               "state: the DRM-06 parameters are restored");
        auto restored = std::make_unique<StoredPattern>();
        q->patterns().copySlot (1, *restored);
        check (restored->cell (2, 0, 3).locked, "state: step locks are restored");
        // The restored instrument plays the song.
        juce::String why;
        const auto notes = q->captureExport (ExportSnapshot::Scope::song)->notes;
        check (DrumsProcessor::renderExport (*q->captureExport (ExportSnapshot::Scope::song))
                   == DrumsProcessor::renderExport (*p->captureExport (ExportSnapshot::Scope::song)),
               "state: the restored song exports the same MIDI");
        juce::ignoreUnused (why, notes);
    }
    // Malformed new sections: rejected, the live state unchanged.
    {
        auto q = makeProcessor();
        q->setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
        const auto before = stateOf (*q);
        const auto decoded = vox::preset::decode ({ static_cast<const std::byte*> (saved.getData()), saved.getSize() }, q->product().stateTag,
                                                  q->product().stateSchema);
        check (decoded.ok(), "state: the saved blob decodes");
        const auto rejects = [&] (vox::preset::Tag tag, const std::function<void (std::vector<std::byte>&)>& damage, const juce::String& what) {
            auto doc = decoded.document;
            for (auto& s : doc.sections)
                if (s.tag == tag)
                    damage (s.data);
            const auto bytes = vox::preset::encode (doc);
            q->setStateInformation (bytes->data(), static_cast<int> (bytes->size()));
            check (q->lastStateError().isNotEmpty() && stateOf (*q) == before, "state: " + what + " is rejected and the live state is unchanged");
        };
        const auto set = [] (std::size_t at, int value) { return [at, value] (std::vector<std::byte>& d) { d[at] = static_cast<std::byte> (value); }; };
        rejects (songSectionTag, set (0, 9), "DSNG unknown version");
        rejects (songSectionTag, set (5, 20), "DSNG invalid length");
        rejects (songSectionTag, set (7, 64), "DSNG loop start outside the song");
        rejects (songSectionTag, set (9, 3), "DSNG unknown bar kind");
        rejects (songSectionTag, set (10, 9), "DSNG pattern slot out of range");
        rejects (songSectionTag, [] (std::vector<std::byte>& d) { d.resize (d.size() - 3); }, "DSNG truncated");
        rejects (songSectionTag, [] (std::vector<std::byte>& d) { d.push_back (std::byte { 0 }); }, "DSNG trailing bytes");
        rejects (songSectionTag, [] (std::vector<std::byte>& d) { d.clear(); }, "DSNG empty");
        rejects (generatorSectionTag, set (0, 2), "DGEN unknown version");
        rejects (generatorSectionTag, set (4, 7), "DGEN unknown style");
        rejects (generatorSectionTag, [] (std::vector<std::byte>& d) { d[8] = std::byte { 0xc0 }; d[7] = std::byte { 0x7f }; }, "DGEN NaN amount");
        rejects (generatorSectionTag, [] (std::vector<std::byte>& d) { d.pop_back(); }, "DGEN truncated");
        rejects (uiStateSectionTag, set (4, 8), "DUIS page out of range");
        rejects (uiStateSectionTag, set (5, 16), "DUIS pad out of range");
        rejects (uiStateSectionTag, set (0, 2), "DUIS unknown version");
        rejects (padSectionTag, [] (std::vector<std::byte>& d) { d[d.size() - 1] = std::byte { 48 }; }, "DPAD invalid voice limit");
        rejects (padSectionTag, [] (std::vector<std::byte>& d) { d[d.size() - 2] = std::byte { 9 }; }, "DPAD output out of range");
        check (stateOf (*q) == before, "state: after every rejected blob the instrument is still the same");
    }
    // A state from before this release (no DSNG, DGEN, DUIS; DPAD v1): defaults.
    {
        auto q = makeProcessor();
        const auto decoded = vox::preset::decode ({ static_cast<const std::byte*> (saved.getData()), saved.getSize() }, q->product().stateTag,
                                                  q->product().stateSchema);
        auto doc = decoded.document;
        doc.sections.erase (std::remove_if (doc.sections.begin(), doc.sections.end(),
                                            [] (const vox::preset::Section& s) {
                                                return s.tag == songSectionTag || s.tag == generatorSectionTag || s.tag == uiStateSectionTag;
                                            }),
                            doc.sections.end());
        for (auto& s : doc.sections)
            if (s.tag == padSectionTag)
            {
                s.data.resize (s.data.size() - 17);   // v2 appended 16 outputs and the voice limit
                s.data[0] = std::byte { 1 };
            }
        const auto bytes = vox::preset::encode (doc);
        q->setStateInformation (bytes->data(), static_cast<int> (bytes->size()));
        check (q->lastStateError().isEmpty(), "older state: accepted (" + q->lastStateError() + ")");
        const auto song = q->patterns().song();
        check (! song.songMode && song == SongData::initial(), "older state: Pattern mode and the initial song");
        check (q->generator() == GeneratorSettings {} && q->uiState() == UiState {}, "older state: default generator and editor state");
        check (q->kitStore().pad (5).output == 0 && q->kitStore().snapshot().voiceLimit == 64, "older state: every pad on Main, 64 voices");
    }
    // Song timeline edits (the functions behind the timeline buttons).
    {
        auto q = makeProcessor();
        const auto base = q->patterns().song();
        const auto kindAt = [] (const SongData& s, int bar) { return s.bars[static_cast<std::size_t> (bar)].kind; };
        auto s = songedit::insertEmpty (base, 2, 2);
        check (kindAt (s, 2) == SongBar::Kind::empty && kindAt (s, 3) == SongBar::Kind::empty && s.bars[4] == base.bars[2] && s.bars[15] == base.bars[13],
               "timeline: insert shifts the rest right");
        s = songedit::removeBars (base, 2, 2);
        check (s.bars[2] == base.bars[4] && kindAt (s, 15) == SongBar::Kind::empty && kindAt (s, 14) == SongBar::Kind::empty, "timeline: delete shifts the rest left");
        s = songedit::duplicate (base, 1, 2);
        check (s.bars[3] == base.bars[1] && s.bars[4] == base.bars[2], "timeline: duplicate copies the range after itself");
        s = songedit::clear (base, 0, 3);
        check (kindAt (s, 0) == SongBar::Kind::empty && kindAt (s, 2) == SongBar::Kind::empty && s.bars[3] == base.bars[3], "timeline: clear empties the range");
        s = songedit::paste (base, 14, songedit::copy (base, 0, 4));
        check (s.bars[14] == base.bars[0] && s.bars[15] == base.bars[1], "timeline: paste replaces and stops at the song end");
        const auto all = std::make_unique<PatternStore::Everything>();
        q->patterns().copyEverything (*all);
        s = songedit::makeUnique (base, 5, 1, all->slots, all->alternates);
        check (kindAt (s, 5) == SongBar::Kind::unique && s.bars[5].cells != nullptr && kindAt (s, 4) == SongBar::Kind::pattern, "timeline: Make Unique");
        // A unique bar is independent of its pattern.
        q->patterns().setSong (s);
        const auto before = DrumsProcessor::renderExport (*q->captureExport (ExportSnapshot::Scope::song));
        auto edited = std::make_unique<StoredPattern> (all->slots[static_cast<std::size_t> (base.bars[5].slot)]);
        for (int lane = 0; lane < padCount; ++lane)
            for (int st = 0; st < edited->steps; ++st)
                edited->cell (lane, base.bars[5].patternBar, st) = {};
        q->patterns().setSlot (base.bars[5].slot, *edited);
        const auto after = parseMidi (DrumsProcessor::renderExport (*q->captureExport (ExportSnapshot::Scope::song)));
        const auto reference = parseMidi (before);
        const auto inBar = [] (const ParsedMidi& m, int bar) {
            int n = 0;
            for (const auto& note : m.notes)
                n += note.tick >= bar * 3840 && note.tick < (bar + 1) * 3840 ? 1 : 0;
            return n;
        };
        check (inBar (after, 5) == inBar (reference, 5) && inBar (reference, 5) > 0 && inBar (after, 1) == 0 && inBar (reference, 1) > 0,
               "timeline: a unique bar keeps its events when its pattern is emptied; references follow the pattern");
        // Undo and redo of a song edit.
        q->patterns().setSong (base);
        check (q->editSong ([] (SongData& song) { song.length = 32; song.place (16, 2, 1, 4); }), "timeline: an edit is recorded");
        check (q->patterns().song().length == 32, "timeline: the edit applies");
        q->patternHistory().undo (q->patterns());
        check (q->patterns().song() == base, "timeline: undo restores the song");
        q->patternHistory().redo (q->patterns());
        check (q->patterns().song().length == 32 && q->patterns().song().bars[16].slot == 2, "timeline: redo");
        check (! q->editSong ([] (SongData&) {}), "timeline: an edit that changes nothing is not recorded");
    }
}

//==============================================================================
void testEditor()
{
    auto p = makeProcessor();
    std::unique_ptr<juce::AudioProcessorEditor> editor (p->createEditorAndMakeActive());
    auto* frame = dynamic_cast<vox::suite::SuiteEditor*> (editor.get());
    auto* panel = frame != nullptr ? dynamic_cast<DrumsPanel*> (frame->content()) : nullptr;
    check (panel != nullptr, "editor: Drums panel");
    if (panel == nullptr)
        return;
    check (DrumsPanel::pageCount == 8, "editor: eight pages");
    const char* names[8] = { "PERFORMANCE", "SOUND", "PATTERN/SONG", "MIXER", "GENERATE", "ROUTING", "PRESETS", "ADVANCED" };
    const char* shots = std::getenv ("VOX_SCREENSHOT_OUT");
    for (auto size : { std::pair { vox::suite::EditorLimits::minWidth, vox::suite::EditorLimits::minHeight }, std::pair { 1500, 920 } })
    {
        editor->setSize (size.first, size.second);
        for (int length : { 16, 64 })
        {
            p->editSong ([length] (SongData& s) { s.length = static_cast<std::uint8_t> (length); });
            for (int page = 0; page < DrumsPanel::pageCount; ++page)
            {
                panel->showPage (page);
                panel->pollForTesting();
                check (panel->page() == page && juce::String (DrumsPanel::pageName (page)) == names[page], juce::String ("editor: page ") + names[page]);
                const auto overlaps = panel->overlappingControls();
                check (overlaps.isEmpty(), juce::String ("editor: no overlapping or clipped controls on ") + names[page] + " at "
                                               + juce::String (size.first) + "x" + juce::String (size.second) + " (" + juce::String (length)
                                               + " bars): " + overlaps.joinIntoString ("; ").substring (0, 400));
                // 100, 125, 150 and 200 %: the page renders at each scale.
                for (float scale : { 1.0f, 1.25f, 1.5f, 2.0f })
                {
                    juce::Image image (juce::Image::ARGB, juce::roundToInt (size.first * scale), juce::roundToInt (size.second * scale), true);
                    {
                        juce::Graphics g (image);
                        g.addTransform (juce::AffineTransform::scale (scale));
                        editor->paintEntireComponent (g, false);
                    }
                    if (shots != nullptr && juce::exactlyEqual (scale, 2.0f) && length == 16 && size.first == vox::suite::EditorLimits::minWidth)
                    {
                        const auto file = juce::File (juce::String (shots)).getChildFile ("vox-drums-page-" + juce::String (page + 1) + ".png");
                        file.deleteFile();
                        juce::FileOutputStream stream (file);
                        juce::PNGImageFormat().writeImageToStream (image, stream);
                    }
                }
            }
        }
    }
    p->editSong ([] (SongData& s) { s.length = 16; });

    // The page is part of the saved state.
    panel->showPage (5);
    check (p->uiState().page == 5, "editor: the shown page is saved");
    {
        auto ui = p->uiState();
        ui.page = 3;
        ui.pad = 9;
        p->setUiState (ui);
        panel->pollForTesting();
        check (panel->page() == 3 && panel->selectedPad() == 9, "editor: a restored state selects its page and pad");
    }

    // PERFORMANCE: the kit selector loads a factory kit; macros are bound.
    {
        panel->showPage (0);
        auto* kits = panel->kitSelector();
        check (kits != nullptr && kits->getNumItems() == factoryKitCount, "performance: factory kit selector");
        if (kits != nullptr)
        {
            const auto before = stateOf (*p);
            kits->setSelectedItemIndex (2, juce::sendNotificationSync);
            check (stateOf (*p) != before && p->kitStore().pad (11).model == factoryKit (2).pads[11].model
                       && std::abs (plainOf (*p, "drm.p01.decay") - factoryKit (2).pads[0].decay) < 1.0e-4f,
                   "performance: choosing a factory kit loads it");
            check (p->presets().undoLoad().wasOk() && stateOf (*p) == before, "performance: the kit load can be undone");
        }
        for (const auto* id : { "drm.kit.volume", "drm.kit.velocity", "drm.seq.swing", "drm.seq.accent", "drm.fx.delay.feedback",
                                "drm.fx.delay.level", "drm.fx.reverb.size", "drm.fx.reverb.level" })
            check (panel->sliderFor (id) != nullptr && panel->sliderFor (id)->getParentComponent()->isVisible(),
                   juce::String ("performance: macro ") + id);
        check (panel->comboFor ("drm.seq.mode") != nullptr && panel->comboFor ("drm.seq.pattern") != nullptr, "performance: mode and pattern");
    }
    // MIXER: meters follow the audio; routing labels follow the store and the host.
    {
        panel->showPage (3);
        auto* mixer = panel->pageComponent (3);
        hitPad (*p, 2);
        panel->pollForTesting();
        auto* meter = mixer->named ("meter.3");
        check (meter != nullptr && meter->getWidth() > 0, "mixer: pad meter");
        setOutput (*p, 2, 4);
        panel->pollForTesting();
        auto* out = dynamic_cast<juce::Label*> (mixer->named ("out.3"));
        check (out != nullptr && out->getText() == "Out 5>Main", "mixer: a pad on a disabled output shows that it plays on Main (" + (out != nullptr ? out->getText() : juce::String()) + ")");
        setOutput (*p, 2, 0);
        for (const auto* id : { "drm.p05.send1", "drm.p05.send2", "drm.p05.level", "drm.p05.pan", "drm.fx.delay.feedback", "drm.fx.reverb.size" })
            check (mixer->slider (id) != nullptr, juce::String ("mixer: knob for ") + id);
        check (mixer->button ("drm.p05.mute") != nullptr && mixer->button ("drm.p05.solo") != nullptr && mixer->combo ("drm.fx.delay.time") != nullptr,
               "mixer: mute, solo and delay time");
        // A gesture reaches the host (automation recording).
        struct Listener final : juce::AudioProcessorParameter::Listener {
            void parameterValueChanged (int, float) override { ++changes; }
            void parameterGestureChanged (int, bool starting) override { (starting ? begins : ends)++; }
            int changes = 0, begins = 0, ends = 0;
        } listener;
        auto* param = p->state().getParameter ("drm.p05.send1");
        param->addListener (&listener);
        auto* slider = mixer->slider ("drm.p05.send1");
        // A real mouse drag on the knob: drag start, value changes, drag end.
        const float before = plainOf (*p, "drm.p05.send1");
        auto source = juce::Desktop::getInstance().getMainMouseSource();
        const auto centre = slider->getLocalBounds().getCentre().toFloat();
        const auto event = [&] (juce::Point<float> at, bool down) {
            return juce::MouseEvent (source, at, down ? juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier) : juce::ModifierKeys(), 0.0f,
                                     0.0f, 0.0f, 0.0f, 0.0f, slider, slider, juce::Time::getCurrentTime(), centre, juce::Time::getCurrentTime(), 1,
                                     at != centre);
        };
        slider->mouseDown (event (centre, true));
        slider->mouseDrag (event (centre.translated (0.0f, -40.0f), true));
        slider->mouseDrag (event (centre.translated (0.0f, -80.0f), true));
        slider->mouseUp (event (centre.translated (0.0f, -80.0f), false));
        param->removeListener (&listener);
        check (listener.begins == 1 && listener.ends == 1 && listener.changes >= 1 && plainOf (*p, "drm.p05.send1") > before + 0.05f,
               "mixer: a knob drag is one host gesture with value changes (begins " + juce::String (listener.begins) + ", ends "
                   + juce::String (listener.ends) + ", changes " + juce::String (listener.changes) + ")");
    }
    // GENERATE: every button changes data; settings are stored.
    {
        panel->showPage (4);
        auto* page = panel->pageComponent (4);
        const auto press = [page] (const char* name) {
            auto* b = dynamic_cast<juce::Button*> (page->named (name));
            if (b != nullptr && b->onClick)
                b->onClick();
            return b != nullptr;
        };
        const auto everything = [&] {
            auto all = std::make_unique<PatternStore::Everything>();
            p->patterns().copyEverything (*all);
            return all;
        };
        for (const auto* name : { "pattern", "song", "mutate", "fill", "variation.ab" })
        {
            const auto before = everything();
            check (press (name), juce::String ("generate: button ") + name);
            const auto after = everything();
            check (! (after->slots == before->slots) || ! (after->alternates == before->alternates) || after->song != before->song,
                   juce::String ("generate: ") + name + " changes data");
        }
        const auto before = everything();
        press ("undo");
        check (! (everything()->alternates == before->alternates) || ! (everything()->slots == before->slots), "generate: Undo");
        press ("redo");
        check (everything()->alternates == before->alternates && everything()->slots == before->slots, "generate: Redo");
        auto* style = dynamic_cast<juce::ComboBox*> (page->named ("style"));
        auto* density = dynamic_cast<juce::Slider*> (page->named ("density"));
        auto* lock = dynamic_cast<juce::Button*> (page->named ("lock.3"));
        check (style != nullptr && density != nullptr && lock != nullptr, "generate: style, amounts and lane locks");
        if (style != nullptr && density != nullptr && lock != nullptr)
        {
            style->setSelectedItemIndex (5, juce::sendNotificationSync);
            density->setValue (0.9, juce::sendNotificationSync);
            lock->setToggleState (true, juce::sendNotificationSync);
            const auto g = p->generator();
            check (g.style == Style::breaks && std::abs (g.density - 0.9f) < 1.0e-6f && (g.laneLocks & 4u) != 0, "generate: controls set the stored settings");
            GeneratorSettings other;
            other.style = Style::goa;
            other.fills = 0.1f;
            p->setGenerator (other);
            panel->pollForTesting();
            check (style->getSelectedItemIndex() == 1 && ! lock->getToggleState(), "generate: restored settings move the controls");
        }
        for (const auto* name : { "groove", "syncopation", "swing", "humanize", "complexity", "fills", "variation" })
            check (dynamic_cast<juce::Slider*> (page->named (name)) != nullptr, juce::String ("generate: amount ") + name);
    }
    // ROUTING: note and output per pad.
    {
        panel->showPage (5);
        auto* page = panel->pageComponent (5);
        auto* output = dynamic_cast<juce::ComboBox*> (page->named ("output.7"));
        auto* note = dynamic_cast<juce::ComboBox*> (page->named ("note.7"));
        check (output != nullptr && note != nullptr && output->getNumItems() == 9 && note->getNumItems() == 128, "routing: note and output selectors");
        if (output != nullptr && note != nullptr)
        {
            output->setSelectedItemIndex (8, juce::sendNotificationSync);
            note->setSelectedItemIndex (72, juce::sendNotificationSync);
            check (p->kitStore().pad (6).output == 8 && p->kitStore().pad (6).note == 72, "routing: the selectors set the pad's output and note");
            setOutput (*p, 6, 2);
            panel->pollForTesting();
            check (output->getSelectedItemIndex() == 2, "routing: the store moves the selector");
            auto* state = dynamic_cast<juce::Label*> (page->named ("state.7"));
            check (state != nullptr && state->getText().contains ("Main"), "routing: a disabled output is shown as playing on Main");
            setOutput (*p, 6, 0);
        }
        check (page->combo ("drm.seq.mode") != nullptr && page->named ("sync") != nullptr, "routing: sequencer mode and sync state");
    }
    // PRESETS: factory and user entries; load and Undo Load from the page.
    {
        panel->showPage (6);
        auto* page = panel->pageComponent (6);
        auto* list = dynamic_cast<juce::ListBox*> (page->named ("list"));
        auto* load = dynamic_cast<juce::Button*> (page->named ("load"));
        auto* undo = dynamic_cast<juce::Button*> (page->named ("undo"));
        auto* status = dynamic_cast<juce::Label*> (page->named ("status"));
        check (list != nullptr && load != nullptr && undo != nullptr && status != nullptr, "presets: list, Load, Undo Load, status");
        if (list != nullptr && load != nullptr && undo != nullptr && status != nullptr)
        {
            check (list->getListBoxModel()->getNumRows() >= factoryKitCount, "presets: the factory kits are listed");
            const auto before = stateOf (*p);
            load->onClick();
            check (stateOf (*p) == before && status->getText().isNotEmpty(), "presets: Load without a selection changes nothing and says why");
            list->selectRow (1);
            load->onClick();
            check (stateOf (*p) != before && status->getText().startsWith ("Loaded"), "presets: Load applies the selected factory kit");
            panel->pollForTesting();
            check (undo->isEnabled(), "presets: Undo Load is offered after a load");
            undo->onClick();
            check (stateOf (*p) == before, "presets: Undo Load from the page");
            for (const auto* name : { "save", "import", "export", "type", "name" })
                check (page->named (name) != nullptr, juce::String ("presets: control ") + name);
        }
    }
    // ADVANCED: voice limit and diagnostics.
    {
        panel->showPage (7);
        auto* page = panel->pageComponent (7);
        auto* voices = dynamic_cast<juce::ComboBox*> (page->named ("voices"));
        auto* diagnostics = dynamic_cast<juce::Label*> (page->named ("diagnostics"));
        auto* panic = dynamic_cast<juce::Button*> (page->named ("panic"));
        check (voices != nullptr && diagnostics != nullptr && panic != nullptr, "advanced: voice limit, diagnostics, panic");
        if (voices != nullptr && diagnostics != nullptr && panic != nullptr)
        {
            voices->setSelectedItemIndex (0, juce::sendNotificationSync);
            check (p->kitStore().snapshot().voiceLimit == 16, "advanced: the voice limit is stored");
            // The limit is real: 40 overlapping hits leave at most 16 voices.
            juce::AudioBuffer<float> buffer (2, 64);
            juce::MidiBuffer midi;
            p->processBlock (buffer, midi);
            for (int i = 0; i < 40; ++i)
            {
                p->audition (7, 100);   // the cymbal: a long voice
                p->audition (5, 100);
                p->processBlock (buffer, midi);
            }
            check (p->activeVoices() > 0 && p->activeVoices() <= 16, "advanced: the voice limit bounds the voices (" + juce::String (p->activeVoices()) + ")");
            panel->pollForTesting();
            check (diagnostics->getText().contains ("Active voices") && diagnostics->getText().contains ("9 stereo buses"), "advanced: diagnostics");
            panic->onClick();
            p->processBlock (buffer, midi);
            p->processBlock (buffer, midi);
            check (p->activeVoices() == 0, "advanced: Panic stops every voice");
            voices->setSelectedItemIndex (2, juce::sendNotificationSync);
        }
        check (page->slider ("drm.kit.velocity") != nullptr && page->button ("drm.kit.hat_choke") != nullptr, "advanced: velocity and choke groups");
    }
    // PATTERN/SONG: the timeline's buttons and editing a song bar.
    {
        panel->showPage (2);
        auto& strip = panel->songStrip();
        auto& ed = panel->patternEditor();
        const auto song = [&] { return p->patterns().song(); };
        const auto click = [&strip] (const char* name) {
            auto* b = strip.button (name);
            if (b != nullptr)
                b->onClick();
            return b != nullptr;
        };
        const auto base = song();
        strip.select (2, 2);
        check (strip.selectionStart() == 2 && strip.selectionLength() == 2 && p->uiState().songBar == 2, "timeline: selection");
        check (click ("clear") && song().bars[2].kind == SongBar::Kind::empty && song().bars[3].kind == SongBar::Kind::empty, "timeline: Clear");
        strip.combo ("slot")->setSelectedItemIndex (3, juce::sendNotificationSync);
        strip.combo ("variation")->setSelectedItemIndex (1, juce::sendNotificationSync);
        check (click ("place") && song().bars[2].kind == SongBar::Kind::pattern && song().bars[2].slot == 3 && song().bars[2].variation == 1
                   && song().bars[5].patternBar == 3,
               "timeline: Place puts the chosen pattern and variation at the selection");
        check (click ("copy"), "timeline: Copy");
        strip.select (10, 1);
        check (click ("paste") && song().bars[10] == song().bars[2] && song().bars[11] == song().bars[3], "timeline: Paste");
        check (click ("insert") && song().bars[10].kind == SongBar::Kind::empty && song().bars[11] == song().bars[2], "timeline: Insert");
        check (click ("delete") && song().bars[10] == song().bars[2], "timeline: Delete");
        strip.select (0, 2);
        check (click ("dup") && song().bars[2] == song().bars[0] && song().bars[3] == song().bars[1], "timeline: Dup");
        check (click ("unique") && song().bars[0].kind == SongBar::Kind::unique && song().bars[1].kind == SongBar::Kind::unique, "timeline: Unique");
        strip.select (4, 3);
        check (click ("loopsel") && song().loopStart == 4 && song().loopLength == 3, "timeline: the loop region is the selection");
        strip.button ("loop")->setToggleState (true, juce::sendNotificationSync);
        strip.button ("mode")->setToggleState (true, juce::sendNotificationSync);
        check (song().loopOn && song().songMode, "timeline: Loop and Song mode");
        strip.combo ("length")->setSelectedItemIndex (2, juce::sendNotificationSync);
        check (song().length == 64, "timeline: length");
        // Every step back.
        int undone = 0;
        while (p->patternHistory().canUndo() && undone < 40)
        {
            p->patternHistory().undo (p->patterns());
            ++undone;
        }
        check (song() == base, "timeline: undo returns to the song before the edits (" + juce::String (undone) + " steps)");
        // The mouse selects a bar.
        panel->pollForTesting();
        const auto r = strip.barBounds (6);
        check (! r.isEmpty(), "timeline: bar bounds");
        auto source = juce::Desktop::getInstance().getMainMouseSource();
        const juce::MouseEvent e (source, r.getCentre().toFloat(), juce::ModifierKeys(), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &strip, &strip,
                                  juce::Time::getCurrentTime(), r.getCentre().toFloat(), juce::Time::getCurrentTime(), 1, false);
        strip.mouseDown (e);
        check (strip.selectionStart() == 6 && strip.selectionLength() == 1, "timeline: a click selects the bar");
        // Edit Bar: the step grid edits the song bar, which becomes unique;
        // the pattern it referenced does not change.
        const auto pattern = std::make_unique<StoredPattern>(), patternAfter = std::make_unique<StoredPattern>();
        const auto reference = song().bars[6];
        p->patterns().copySlot (reference.slot, *pattern);
        strip.button ("edit")->setToggleState (true, juce::sendNotificationSync);
        check (ed.songBar() == 6, "song bar: the step grid follows the selected bar");
        const bool wasOn = ed.shown().cell (12, 0, 5).on;
        ed.clickCell (12, 5);
        p->patterns().copySlot (reference.slot, *patternAfter);
        check (song().bars[6].kind == SongBar::Kind::unique && song().bars[6].cells != nullptr
                   && song().bars[6].cells->cell (12, 5).on != wasOn && *patternAfter == *pattern,
               "song bar: an edit makes the bar unique and leaves its pattern alone");
        check (ed.lockToggle() != nullptr, "song bar: step lock control");
        ed.lockToggle()->setToggleState (true, juce::sendNotificationSync);
        check (song().bars[6].cells->cell (12, 5).locked, "song bar: Lock sets the step lock");
        p->patternHistory().undo (p->patterns());
        p->patternHistory().undo (p->patterns());
        check (song().bars[6] == reference, "song bar: undo returns the bar to its pattern");
        strip.select (9, 1);
        check (ed.songBar() == 9, "song bar: selecting another bar edits that bar");
        strip.button ("edit")->setToggleState (false, juce::sendNotificationSync);
        check (ed.songBar() == -1, "song bar: leaving Edit Bar returns to the pattern");
    }
    // Closing and reopening the editor does not touch the audio.
    {
        juce::AudioBuffer<float> a (2, 256), b (2, 256);
        juce::MidiBuffer midi;
        auto q = makeProcessor (48000.0, 256);
        auto r = makeProcessor (48000.0, 256);
        std::unique_ptr<juce::AudioProcessorEditor> view;
        bool same = true;
        for (int block = 0; block < 60; ++block)
        {
            if (block % 20 == 3)
                view.reset (q->createEditorAndMakeActive());
            if (block % 20 == 13 && view != nullptr)
            {
                q->editorBeingDeleted (view.get());
                view.reset();
            }
            if (block % 7 == 0)
            {
                q->audition (block % padCount, 100);
                r->audition (block % padCount, 100);
            }
            q->processBlock (a, midi);
            r->processBlock (b, midi);
            for (int i = 0; i < 256; ++i)
                same = same && juce::exactlyEqual (a.getSample (0, i), b.getSample (0, i));
        }
        check (same, "editor: opening and closing it does not change the audio");
    }
    p->editorBeingDeleted (editor.get());
}

//==============================================================================
void testRealtime()
{
#ifdef VOX_TEST_INTERPOSE_MALLOC
    unsigned long allocations = 0;
    for (double rate : { 44100.0, 96000.0 })
        for (int block : { 64, 333, 512 })
        {
            auto p = makeProcessor (rate, block, 0x1ffu);
            buildSong (*p);
            setPlain (*p, "drm.seq.mode", 2.0f);
            for (int pad = 0; pad < padCount; ++pad)
            {
                setOutput (*p, pad, pad % 9);
                setPlain (*p, juce::String (padPrefix (pad)) + ".send1", 0.4f);
                setPlain (*p, juce::String (padPrefix (pad)) + ".send2", 0.4f);
            }
            TestPlayHead head;
            p->setPlayHead (&head);
            juce::AudioBuffer<float> buffer (18, block);
            juce::MidiBuffer midi;
            midi.ensureSize (4096);
            double ppq = 240.0;   // through the unique final bar and the wrap
            for (int b = 0; b < 300; ++b)
            {
                head.set (b % 97 != 50, 300.0, b == 200 ? 12.0 : ppq);   // a stop and a seek
                midi.clear();
                midi.addEvent (juce::MidiMessage::noteOn (10, 36 + b % 16, static_cast<juce::uint8> (100)), 0);
                if (b % 40 == 39)
                    midi.addEvent (juce::MidiMessage::allSoundOff (10), block / 2);
                if (b % 11 == 0)   // edits between blocks: song, mode, solo, voice limit, outputs
                {
                    p->editSong ([b] (SongData& s) {
                        s.loopOn = b % 22 == 0;
                        s.songMode = b % 33 != 0;
                    });
                    setPlain (*p, "drm.p03.solo", b % 22 == 0 ? 1.0f : 0.0f);
                    p->kitStore().setVoiceLimit (b % 44 == 0 ? 16 : 64);
                    setOutput (*p, b % padCount, (b / 11) % 9);
                }
                voxProbeBegin();
                p->processBlock (buffer, midi);
                allocations += voxProbeEnd();
                ppq += block * 300.0 / (60.0 * rate);
            }
            p->setPlayHead (nullptr);
        }
    if (voxProbeAvailable())
        check (allocations == 0, "realtime: processBlock allocates nothing in Song mode on nine buses (" + juce::String (static_cast<juce::int64> (allocations)) + " calls)");
    else
        std::printf ("note: allocation probe not available on this platform\n");
#else
    std::printf ("note: built without the allocation probe\n");
#endif
}

} // namespace

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf ("usage: %s <products/drums/factory-content.csv>\n", argv[0]);
        return EXIT_FAILURE;
    }
    manifestPath = argv[1];
    juce::ScopedJuceInitialiser_GUI juce;
    // A private folder for this run's files; removed at the end (only it).
    workFolder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("vox-drums-candidate-" + juce::Uuid().toString());
    workFolder.createDirectory();
    testSongParity();
    testExport();
    testOutputs();
    testGenerator();
    testPresets();
    testState();
    testEditor();
    testRealtime();
    if (workFolder.getFileName().startsWith ("vox-drums-candidate-") && workFolder.isDirectory())
        workFolder.deleteRecursively();
    if (failures == 0)
        std::printf ("VOX Drums candidate tests passed\n");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
