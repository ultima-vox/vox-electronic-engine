// DRM-03: what the kit engine adds to the legacy kit (parity of the legacy
// part is suite_drums_kit_parity_tests):
//  - all 8 models on all 16 pads sound, stay finite and bounded across the
//    whole tune range and character range (Perc is new);
//  - any MIDI note per pad, the lowest pad wins a shared note;
//  - per-pad polyphony 1..8, the 64-voice budget and oldest-voice stealing;
//  - choke groups on any pads; on a coincident hit the lowest pad wins;
//  - sample players A and B: A only, velocity switch at the split, mix;
//    usage reports every held buffer;
//  - block-size invariance (bit-identical for 64 / 512 / 20000 frames);
//  - process() allocates nothing (allocation probe).
#include "engine/KitEngine.h"

#ifdef VOX_TEST_INTERPOSE_MALLOC
#include "AllocationProbe.h"
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <random>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

namespace kit = vox::drums::kit;
int failures = 0;

void check (bool ok, const std::string& what)
{
    if (! ok)
    {
        std::printf ("FAIL [kit engine]: %s\n", what.c_str());
        ++failures;
    }
}

kit::Event note (std::uint32_t offset, int noteNumber, int velocity)
{
    kit::Event e;
    e.offset = offset;
    e.size = 3;
    e.bytes[0] = 0x90;
    e.bytes[1] = static_cast<std::uint8_t> (noteNumber);
    e.bytes[2] = static_cast<std::uint8_t> (velocity);
    return e;
}

struct Render {
    std::vector<float> left, right;
    double peak = 0.0;
    bool finite = true;
};

// Renders `blocks` x `block` frames; events are given in absolute samples.
Render render (kit::KitEngine& engine, const kit::KitSettings& settings, std::uint32_t total, std::uint32_t block,
               std::vector<kit::Event> events, const kit::Sample* samples = nullptr)
{
    Render r;
    r.left.assign (total, 0.0f);
    r.right.assign (total, 0.0f);
    kit::Transport tr;
    std::size_t next = 0;
    std::vector<kit::Event> local;
    for (std::uint32_t done = 0; done < total; done += block)
    {
        const auto n = std::min (block, total - done);
        local.clear();
        while (next < events.size() && events[next].offset < done + n)
        {
            auto e = events[next++];
            e.offset -= done;
            local.push_back (e);
        }
        kit::Outputs out { r.left.data() + done, r.right.data() + done, nullptr };
        engine.process (settings, out, n, local.data(), static_cast<std::uint32_t> (local.size()), tr, nullptr, samples, false);
    }
    for (std::uint32_t i = 0; i < total; ++i)
    {
        r.finite = r.finite && std::isfinite (r.left[i]) && std::isfinite (r.right[i]);
        r.peak = std::max (r.peak, static_cast<double> (std::max (std::abs (r.left[i]), std::abs (r.right[i]))));
    }
    return r;
}

const char* modelName (kit::Model m)
{
    static const char* names[] = { "Kick", "Snare", "Clap", "Hat", "Tom", "Rim", "Cymbal", "Perc" };
    return names[static_cast<int> (m)];
}

void testModels()
{
    for (int model = 0; model < kit::modelCount; ++model)
        for (int pad = 0; pad < kit::padCount; ++pad)
        {
            auto settings = kit::legacyKit();
            for (int p = 0; p < kit::padCount; ++p)
                settings.pads[static_cast<std::size_t> (p)].note = static_cast<std::uint8_t> (60 + p);
            auto& ps = settings.pads[static_cast<std::size_t> (pad)];
            ps.model = static_cast<kit::Model> (model);
            ps.level = 1.0f;
            ps.decay = 0.4f;
            kit::KitEngine engine;
            engine.prepare (48000.0);
            const auto r = render (engine, settings, 24000, 256, { note (10, 60 + pad, 120) });
            check (r.finite && r.peak > 1.0e-3, std::string (modelName (ps.model)) + " sounds on pad " + std::to_string (pad + 1));
        }

    // Extremes: tune -12/+12, fine +-100, character 0/1, at 44.1 and 96 kHz:
    // finite and bounded (|x| < 8 at full level).
    for (double rate : { 44100.0, 96000.0 })
        for (int model = 0; model < kit::modelCount; ++model)
            for (float tune : { -12.0f, 0.0f, 12.0f })
                for (float character : { 0.0f, 1.0f })
                {
                    auto settings = kit::legacyKit();
                    auto& ps = settings.pads[0];
                    ps.model = static_cast<kit::Model> (model);
                    ps.tune = tune;
                    ps.fine = tune > 0.0f ? 100.0f : -100.0f;
                    ps.character = character;
                    ps.level = 1.0f;
                    settings.volume = 1.0f;
                    ps.decay = 1.5f;
                    kit::KitEngine engine;
                    engine.prepare (rate);
                    const auto r = render (engine, settings, static_cast<std::uint32_t> (rate), 512, { note (0, 36, 127) });
                    check (r.finite && r.peak < 8.0 && r.peak > 1.0e-4,
                           std::string (modelName (ps.model)) + " bounded at tune " + std::to_string (tune) + ", character "
                               + std::to_string (character) + ", " + std::to_string (static_cast<int> (rate)) + " Hz (peak "
                               + std::to_string (r.peak) + ")");
                }

    // Perc: character changes the timbre; tune changes the pitch.
    {
        auto settings = kit::legacyKit();
        settings.pads[8].level = 1.0f;
        kit::KitEngine a, b;
        a.prepare (48000.0);
        b.prepare (48000.0);
        const auto plain = render (a, settings, 12000, 256, { note (0, 50, 110) });
        settings.pads[8].character = 1.0f;
        const auto zap = render (b, settings, 12000, 256, { note (0, 50, 110) });
        check (plain.left != zap.left, "Perc character changes the sound");
    }
}

void testNotesAndChokes()
{
    auto settings = kit::legacyKit();
    settings.pads[9].note = 70;
    settings.pads[9].level = 1.0f;
    {
        kit::KitEngine engine;
        engine.prepare (48000.0);
        check (render (engine, settings, 6000, 256, { note (0, 70, 110) }).peak > 1.0e-3, "a pad plays on any assigned note");
    }
    {
        kit::KitEngine engine;
        engine.prepare (48000.0);
        check (render (engine, settings, 6000, 256, { note (0, 51, 110) }).peak < 1.0e-9, "the old note of a remapped pad is silent");
    }
    // Shared note: only the lowest pad plays.
    {
        auto shared = settings;
        shared.pads[12].note = 70;
        shared.pads[12].model = kit::Model::kick;
        kit::KitEngine a, b;
        a.prepare (48000.0);
        b.prepare (48000.0);
        check (render (a, shared, 6000, 256, { note (0, 70, 110) }).left == render (b, settings, 6000, 256, { note (0, 70, 110) }).left,
               "the lowest pad wins a shared note");
    }
    // Choke group on pads 10 and 15: a hit on 10 cuts a ringing 15.
    {
        auto choke = kit::legacyKit();
        for (int p : { 9, 14 })
        {
            choke.pads[static_cast<std::size_t> (p)].model = kit::Model::cymbal;
            choke.pads[static_cast<std::size_t> (p)].decay = 4.0f;
            choke.pads[static_cast<std::size_t> (p)].chokeGroup = 3;
            choke.pads[static_cast<std::size_t> (p)].level = 1.0f;
        }
        kit::KitEngine engine;
        engine.prepare (48000.0);
        engine.setMaxVoices (64);
        render (engine, choke, 4800, 256, { note (0, 56, 110) });   // pad 15 rings
        render (engine, choke, 480, 256, { note (0, 50 + 1, 110) });    // pad 10 cuts it
        auto silent = choke;
        silent.pads[9].level = 0.0f;   // pad 10 silent: only the cut is audible
        kit::KitEngine e2;
        e2.prepare (48000.0);
        render (e2, silent, 4800, 256, { note (0, 56, 110) });
        render (e2, silent, 480, 256, { note (0, 51, 110) });
        check (e2.activeVoices() <= 1 && render (e2, silent, 2400, 256, {}).peak < 1.0e-9,
               "a choke group cuts its other members (any pads)");
        auto open = silent;
        open.chokeEnabled = false;
        kit::KitEngine e3;
        e3.prepare (48000.0);
        render (e3, open, 4800, 256, { note (0, 56, 110) });
        render (e3, open, 480, 256, { note (0, 51, 110) });
        check (render (e3, open, 2400, 256, {}).peak > 1.0e-3, "with chokes disabled the members ring on");
    }
}

void testPolyphony()
{
    auto settings = kit::legacyKit();
    auto& ps = settings.pads[7];   // cymbal, long decay
    ps.decay = 6.0f;
    std::vector<kit::Event> hits;
    for (int k = 0; k < 6; ++k)
        hits.push_back (note (static_cast<std::uint32_t> (k * 2000), 49, 100));
    {
        kit::KitEngine engine;
        engine.prepare (48000.0);
        engine.setMaxVoices (64);
        render (engine, settings, 13000, 256, hits);
        check (engine.activeVoices() == 1, "mono pad: one hit rings (" + std::to_string (engine.activeVoices()) + ")");
    }
    for (int poly : { 2, 4, 8 })
    {
        settings.pads[7].polyphony = static_cast<std::uint8_t> (poly);
        kit::KitEngine engine;
        engine.prepare (48000.0);
        engine.setMaxVoices (64);
        render (engine, settings, 13000, 256, hits);
        check (engine.activeVoices() == std::min (poly, 6), "poly " + std::to_string (poly) + ": "
                                                              + std::to_string (engine.activeVoices()) + " hits ring");
    }
    // 64-voice budget: 16 pads x poly 8, 128 hits -> 64 voices, oldest stolen.
    {
        auto many = kit::legacyKit();
        std::vector<kit::Event> burst;
        for (int p = 0; p < kit::padCount; ++p)
        {
            auto& pad = many.pads[static_cast<std::size_t> (p)];
            pad.polyphony = 8;
            pad.decay = 3.0f;
            pad.model = kit::Model::cymbal;
            pad.chokeGroup = 0;
            pad.note = static_cast<std::uint8_t> (60 + p);
        }
        for (int k = 0; k < 8; ++k)
            for (int p = 0; p < kit::padCount; ++p)
                burst.push_back (note (static_cast<std::uint32_t> (k * 100 + p), 60 + p, 100));
        kit::KitEngine engine;
        engine.prepare (48000.0);
        engine.setMaxVoices (64);
        const auto r = render (engine, many, 4000, 512, burst);
        check (r.finite && engine.activeVoices() == kit::maxVoices, "128 hits fill exactly the 64-voice budget");
    }
}

void testSamplePlayers()
{
    const auto make = [] (std::uint64_t id, float value, std::vector<float>& storage, std::vector<const float*>& ptr) {
        storage.assign (4800, value);
        ptr = { storage.data() };
        kit::Sample s;
        s.id = id;
        s.channels = ptr.data();
        s.channelCount = 1;
        s.frames = storage.size();
        s.sampleRate = 48000.0;
        s.peak = value;
        return s;
    };
    std::vector<float> da, db;
    std::vector<const float*> pa, pb;
    std::array<kit::Sample, kit::sampleSlots> samples {};
    samples[0] = make (101, 0.25f, da, pa);   // pad 1 player A: DC 0.25
    samples[1] = make (102, 0.5f, db, pb);    // pad 1 player B: DC 0.5
    auto settings = kit::legacyKit();
    auto& ps = settings.pads[0];
    ps.source = kit::Source::sample;
    ps.pan = 0.0f;
    settings.velocitySensitivity = 0.0f;
    const auto firstLeft = [&] (int velocity) {
        kit::KitEngine engine;
        engine.prepare (48000.0);
        const auto r = render (engine, settings, 256, 256, { note (0, 36, velocity) }, samples.data());
        return std::pair { r.left[100], engine.usage().count };
    };
    ps.sampleMode = kit::SampleMode::aOnly;
    const auto [aOnly, usedA] = firstLeft (100);
    ps.sampleMode = kit::SampleMode::velocitySwitch;
    ps.velocitySplit = 64;
    const auto [soft, usedSoft] = firstLeft (40);
    const auto [hard, usedHard] = firstLeft (100);
    ps.sampleMode = kit::SampleMode::mix;
    const auto [both, usedBoth] = firstLeft (100);
    check (aOnly > 0.0f && usedA == 1, "A only plays player A");
    check (std::abs (soft - aOnly) < 1.0e-6f && usedSoft == 1, "velocity below the split plays A");
    check (std::abs (hard - 2.0f * aOnly) < 1.0e-5f && usedHard == 1, "velocity from the split plays B (twice the level)");
    check (std::abs (both - 3.0f * aOnly) < 1.0e-5f && usedBoth == 2, "mix plays A and B; both are reported in use");
}

void testBlockInvariance()
{
    auto settings = kit::legacyKit();
    for (int p = 8; p < kit::padCount; ++p)
        settings.pads[static_cast<std::size_t> (p)].model = static_cast<kit::Model> (p % kit::modelCount);
    constexpr int legacyNotes[8] = { 36, 38, 39, 42, 46, 45, 37, 49 };
    std::vector<kit::Event> events;
    for (int k = 0; k < 80; ++k)
        events.push_back (note (static_cast<std::uint32_t> (k * 457 + (k % 3)), k % 2 ? 50 + k % 8 : legacyNotes[k % 8], 30 + k % 90));
    std::sort (events.begin(), events.end(), [] (const auto& a, const auto& b) { return a.offset < b.offset; });
    std::vector<float> reference;
    for (std::uint32_t block : { 64u, 512u, 20000u, 37000u })
    {
        kit::KitEngine engine;
        engine.prepare (48000.0);
        engine.setMaxVoices (64);
        const auto r = render (engine, settings, 40000, block, events);
        if (reference.empty())
            reference = r.left;
        else
            check (r.left == reference, "bit-identical with " + std::to_string (block) + "-frame blocks");
    }
}

// Review 6737829467: invalid settings never index out of range. Output bus
// 7 plays on Out 8; 8 and 255 play on Main (exactly once); garbage in every
// field (NaN, infinities, huge, negative, enums out of range, sample regions
// outside 0..1) renders finite audio without touching memory outside the
// buses and buffers (run under ASan/UBSan in the PR evidence).
void testInvalidSettings()
{
    std::vector<std::vector<float>> bus (20, std::vector<float> (256));
    std::array<float*, kit::auxBuses * 2> aux {};
    for (int b = 0; b < kit::auxBuses * 2; ++b)
        aux[static_cast<std::size_t> (b)] = bus[static_cast<std::size_t> (b + 2)].data();
    const auto renderTo = [&] (const kit::KitSettings& settings, int noteNumber) {
        for (auto& b : bus)
            std::fill (b.begin(), b.end(), 0.0f);
        kit::KitEngine engine;
        engine.prepare (48000.0);
        const auto e = note (0, noteNumber, 120);
        kit::Transport tr;
        kit::Outputs out { bus[0].data(), bus[1].data(), &aux };
        engine.process (settings, out, 256, &e, 1, tr, nullptr, nullptr, false);
        std::array<double, 10> energy {};
        for (int b = 0; b < 10; ++b)
            for (int i = 0; i < 256; ++i)
                energy[static_cast<std::size_t> (b)] += std::abs (bus[static_cast<std::size_t> (b * 2)][static_cast<std::size_t> (i)]);
        return energy;   // [0] main, [1] send 1, [2] send 2, [3..9] Out 2..8
    };
    for (const int output : { 0, 7, 8, 255 })
    {
        auto settings = kit::legacyKit();
        settings.pads[0].output = static_cast<std::uint8_t> (output);
        const auto e = renderTo (settings, 36);
        const int expected = output == 7 ? 9 : 0;   // Out 8 is bus pair 9; invalid -> Main
        int carrying = 0;
        for (int b = 0; b < 10; ++b)
            if (b != 1 && b != 2 && e[static_cast<std::size_t> (b)] > 0.0)
                ++carrying;
        check (e[static_cast<std::size_t> (expected)] > 1.0e-3 && carrying == 1,
               "output " + std::to_string (output) + " plays exactly once on " + (expected == 0 ? "Main" : "Out 8"));
    }

    // Garbage in every field.
    std::mt19937 rng (42);
    const float specials[] = { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                               -std::numeric_limits<float>::infinity(), -1.0e30f, 1.0e30f, -5.0f, 0.0f, 7.0f };
    std::vector<float> sampleData (1000, 0.5f);
    std::vector<const float*> sampleChannels { sampleData.data() };
    std::array<kit::Sample, kit::sampleSlots> samples {};
    for (int i = 0; i < kit::sampleSlots; ++i)
        samples[static_cast<std::size_t> (i)] = { static_cast<std::uint64_t> (i + 1), sampleChannels.data(), 1, sampleData.size(), 44100.0, 0.5f };
    for (int round = 0; round < 200; ++round)
    {
        kit::KitSettings k = kit::legacyKit();
        const auto f = [&] { return specials[rng() % 8]; };
        const auto byte = [&] { return static_cast<std::uint8_t> (rng() % 256); };
        k.volume = f();
        k.velocitySensitivity = f();
        k.sequencerMode = byte();
        k.swing = f();
        k.accent = f();
        for (auto& p : k.pads)
        {
            p.model = static_cast<kit::Model> (byte());
            p.note = byte();
            p.chokeGroup = byte();
            p.polyphony = byte();
            p.level = f(); p.tune = f(); p.fine = f(); p.decay = f(); p.pan = f(); p.character = f();
            p.filter = static_cast<kit::Filter> (byte());
            p.cutoff = f(); p.resonance = f(); p.attack = f(); p.sustain = f();
            p.output = byte();
            p.send1 = f(); p.send2 = f();
            p.pitchAmount = f(); p.pitchDecay = f(); p.drive = f(); p.sub = f(); p.tail = f();
            p.source = static_cast<kit::Source> (byte());
            p.sampleMode = static_cast<kit::SampleMode> (byte());
            p.velocitySplit = byte();
            for (auto& pl : p.players)
            {
                pl.start = f();
                pl.end = f();
                pl.level = f();
                pl.loop = rng() % 2;
                pl.reverse = rng() % 2;
                pl.normalize = rng() % 2;
            }
        }
        kit::KitEngine engine;
        engine.prepare (48000.0);
        engine.setMaxVoices (64);
        std::vector<kit::Event> events;
        for (int n = 0; n < 128; n += 3)
            events.push_back (note (static_cast<std::uint32_t> (n), n, 1 + n % 127));
        kit::Transport tr;
        kit::Outputs out { bus[0].data(), bus[1].data(), &aux };
        bool finite = true;
        for (int b = 0; b < 4; ++b)
        {
            engine.process (k, out, 256, b == 0 ? events.data() : nullptr, b == 0 ? static_cast<std::uint32_t> (events.size()) : 0u,
                            tr, nullptr, samples.data(), false);
            for (const auto& ch : bus)
                for (auto x : ch)
                    finite = finite && std::isfinite (x);
        }
        if (! finite)
        {
            check (false, "garbage settings round " + std::to_string (round) + " renders finite audio");
            break;
        }
    }
}

void testRealtime()
{
#ifdef VOX_TEST_INTERPOSE_MALLOC
    auto settings = kit::legacyKit();
    for (auto& p : settings.pads)
        p.polyphony = 8;
    kit::KitEngine engine;
    engine.prepare (48000.0);
    engine.setMaxVoices (64);
    std::vector<float> l (512), r (512);
    std::vector<kit::Event> events;
    events.reserve (64);
    std::vector<kit::PatternStep> cells (16 * 16);
    for (std::size_t i = 0; i < cells.size(); i += 3)
        cells[i] = { true, i % 2 == 0, 100 };
    const kit::Pattern pattern { 16, 16, 1, 0, cells.data() };
    settings.sequencerMode = 2;
    kit::Transport tr;
    tr.playing = tr.hasTempo = tr.hasPpq = true;
    tr.bpm = 140.0;
    unsigned long allocations = 0;
    for (int b = 0; b < 400; ++b)
    {
        events.clear();
        for (int k = 0; k < 12; ++k)
            events.push_back (note (static_cast<std::uint32_t> (k * 40), 36 + (b + k) % 30, 100));
        kit::Outputs out { l.data(), r.data(), nullptr };
        voxProbeBegin();
        engine.process (settings, out, 512, events.data(), static_cast<std::uint32_t> (events.size()), tr, &pattern, nullptr,
                        b % 50 == 0);
        allocations += voxProbeEnd();
        tr.ppq += 512.0 / 48000.0 * 140.0 / 60.0;
    }
    if (voxProbeAvailable())
        check (allocations == 0, "process allocates nothing (" + std::to_string (allocations) + ")");
#endif
}

} // namespace

int main()
{
    testModels();
    testNotesAndChokes();
    testPolyphony();
    testSamplePlayers();
    testBlockInvariance();
    testInvalidSettings();
    testRealtime();
    if (failures == 0)
        std::printf ("VOX Drums kit engine tests passed\n");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
