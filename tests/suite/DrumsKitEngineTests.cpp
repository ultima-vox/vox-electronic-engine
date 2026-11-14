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
//  - process() allocates nothing (allocation probe);
//  - DRM-04: the step rule (ratchet, microtiming, swing, accent, lanes,
//    probability by seed and position, 1/64 grid) and that the sequencer
//    plays exactly the canonical hit list (no miss, no double) across block
//    sizes, rates and a loop back.
#include "engine/KitEngine.h"
#include "DrumsTimingTestUtil.h"

#ifdef VOX_TEST_INTERPOSE_MALLOC
#include "AllocationProbe.h"
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <tuple>
#include <random>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

namespace kit = vox::drums::kit;
namespace timing = vox::drums::timing;
namespace testutil = vox::drums::testutil;
int failures = 0;

void check (bool ok, const std::string& what)
{
    if (! ok)
    {
        std::printf ("FAIL [kit engine]: %s\n", what.c_str());
        ++failures;
    }
}

bool juce_exact (double a, double b)
{
    return std::memcmp (&a, &b, sizeof a) == 0;
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
    std::vector<std::vector<float>> bus (2 + kit::auxBuses * 2, std::vector<float> (256));
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
        std::array<double, 11> energy {};
        for (int b = 0; b < 11; ++b)
            for (int i = 0; i < 256; ++i)
                energy[static_cast<std::size_t> (b)] += std::abs (bus[static_cast<std::size_t> (b * 2)][static_cast<std::size_t> (i)]);
        return energy;   // [0] main, [1] send 1, [2] send 2, [3..10] Out 2..9
    };
    // Every destination (Main, Out 2..9) carries the pad exactly once and
    // nothing else does; an invalid index plays on Main.
    for (const int output : { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 255 })
    {
        auto settings = kit::legacyKit();
        settings.pads[0].output = static_cast<std::uint8_t> (output);
        const auto e = renderTo (settings, 36);
        const int expected = output >= 1 && output <= 8 ? output + 2 : 0;   // Out N+1 is bus pair N+2
        int carrying = 0;
        for (int b = 0; b < 11; ++b)
            if (b != 1 && b != 2 && e[static_cast<std::size_t> (b)] > 0.0)
                ++carrying;
        check (e[static_cast<std::size_t> (expected)] > 1.0e-3 && carrying == 1,
               "output " + std::to_string (output) + " plays exactly once on "
                   + (expected == 0 ? std::string ("Main") : "Out " + std::to_string (output + 1)));
    }
    // Sends are post level and pan, proportional and independent of the output.
    {
        auto settings = kit::legacyKit();
        settings.pads[0].output = 3;
        settings.pads[0].send1 = 0.5f;
        settings.pads[0].send2 = 0.25f;
        const auto e = renderTo (settings, 36);
        check (e[0] <= 0.0 && e[5] > 1.0e-3 && std::abs (e[1] / e[5] - 0.5) < 1.0e-4 && std::abs (e[2] / e[5] - 0.25) < 1.0e-4,
               "sends carry level x send of the pad, next to its dry output");
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
    auto grid = std::make_unique<testutil::GridPattern> (16, 16, 1);
    for (int i = 0; i < 16 * 16; i += 3)
        grid->at (i / 16, 0, i % 16) = { true, i % 2 == 0, 100 };
    const kit::Pattern& pattern = grid->pattern;
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


//==============================================================================
void testStepRules()
{
    auto grid = std::make_unique<testutil::GridPattern> (16, 16, 1);
    const auto at = [&] (int lane, int st) -> kit::PatternStep& { return grid->at (lane, 0, st); };
    at (0, 0) = { true, false, 100 };
    at (0, 0).ratchet = 4;
    at (1, 1) = { true, false, 90 };
    at (1, 1).microTicks = static_cast<std::int16_t> (kit::percentToTicks (50, 240));
    at (2, 1) = { true, false, 80 };
    at (3, 2) = { true, true, 60 };
    at (4, 2) = { true, false, 70 };
    at (5, 3) = { true, false, 70 };
    at (5, 3).microTicks = static_cast<std::int16_t> (kit::percentToTicks (-50, 240));
    const kit::Pattern& pattern = grid->pattern;
    kit::StepContext ctx;
    ctx.swing = 0.0;
    ctx.accent = 0.5;
    std::array<kit::ScheduledHit, kit::padCount * kit::maxRatchet> out {};
    const auto stepHits = [&] (const kit::Pattern& p, std::int64_t g) { return testutil::stepHits (p, ctx, g, 16, out.data()); };
    int n = stepHits (pattern, 0);
    check (n == 4 && juce_exact (out[0].ppq, 0.0) && std::abs (out[1].ppq - 0.0625) < 1e-12 && std::abs (out[3].ppq - 0.1875) < 1e-12
               && out[3].pad == 0 && out[3].velocity == 100 && out[1].tick == 60 && out[3].tick == 180 && out[0].gateTicks == 30,
           "ratchet 4: four hits evenly in the step (ticks 0/60/120/180, gate half a subhit)");
    n = stepHits (pattern, 1);
    check (n == 2 && out[0].pad == 1 && std::abs (out[0].ppq - 0.375) < 1e-12 && out[0].tick == 360 && out[1].pad == 2 && juce_exact (out[1].ppq, 0.25),
           "micro +50 % (120 ticks) moves a hit half a step later");
    ctx.swing = 0.3;
    n = stepHits (pattern, 1);
    check (n == 2 && std::abs (out[1].ppq - (0.25 + 0.3 * 0.25)) < 1e-12 && std::abs (out[0].ppq - (0.25 + 0.075 + 0.125)) < 1e-12
               && out[1].tick == 240,
           "swing delays odd steps (unrounded, not in the tick), micro adds to it");
    n = stepHits (pattern, 2);
    check (n == 2 && out[0].velocity == 94 && out[1].velocity == 70, "accent raises velocity towards 127 by the accent amount");
    ctx.laneMask = ~(1u << 3);
    n = stepHits (pattern, 2);
    check (n == 1 && out[0].pad == 4, "a disabled lane plays nothing");
    ctx.laneMask = 0xffffu;
    n = stepHits (pattern, 3);
    check (n == 1 && std::abs (out[0].ppq - (0.75 + 0.3 * 0.25 - 0.125)) < 1e-12, "micro -50 % moves a hit half a step earlier");
    {
        const auto view = pattern.bar (0);
        check (kit::cellHits (view, ctx, 0, 0, 0, 4, 0, out.data()) == 0 && kit::cellHits (view, ctx, 0, 0, 0, 0, 4, out.data()) == 0
                   && kit::cellHits (view, ctx, 0, 0, 16, 0, 0, out.data()) == 0 && kit::cellHits (view, ctx, 0, 0, -1, 0, 0, out.data()) == 0,
               "a beat, cell or lane outside the bar has no hits");
    }

    // Probability: deterministic per seed and absolute position, about the
    // stated share, different seeds decide differently.
    auto pg = std::make_unique<testutil::GridPattern> (1, 16, 1, 7);
    pg->at (0, 0, 0) = { true, false, 100 };
    pg->at (0, 0, 0).probability = 30;
    const kit::Pattern& prob = pg->pattern;
    int played = 0, same = 0, differ = 0;
    kit::Pattern other = prob;
    other.seed = 8;
    for (std::int64_t pass = 0; pass < 4000; ++pass)
    {
        const std::int64_t g = pass * 16;
        const int a = stepHits (prob, g);
        const int b = stepHits (prob, g);
        const int c = stepHits (other, g);
        played += a;
        same += a == b ? 1 : 0;
        differ += a != c ? 1 : 0;
    }
    check (same == 4000, "probability is deterministic for a seed and position");
    check (played > 1100 && played < 1300, "probability 30 % plays about 30 % of passes (" + std::to_string (played) + " / 4000)");
    check (differ > 500, "another seed decides differently");

    // 1/64 grid: cells of 60 ticks.
    auto fine = std::make_unique<testutil::GridPattern> (16, 64, 1);
    fine->at (0, 0, 5) = { true, false, 100 };
    n = testutil::stepHits (fine->pattern, ctx, 5, 64, out.data());
    ctx.swing = 0.0;
    const int m = testutil::stepHits (fine->pattern, ctx, 5, 64, out.data());
    check (n == 1 && m == 1 && out[0].tick == 300 && juce_exact (out[0].ppq, 0.3125), "1/64 cells are 60 ticks (step 6 at tick 300)");

    // PR 137 §10: a triplet beat; fractional ratchets rounded once (ties
    // earlier); tuplet cells are not swung.
    {
        kit::PatternCells cells {};
        std::array<std::uint16_t, kit::padCount * kit::maxPatternBars> masks {};
        kit::Pattern tp;
        tp.lanes = 1;
        tp.bars = 1;
        tp.cells = cells.data();
        tp.resolution = timing::Resolution::thirtySecond;  // 4/4 32nd: 8 straight / 12 triplet cells, 12 slots per beat
        tp.masks = masks.data();
        masks[0] = 0b0010;                                 // lane 1, bar 1: beat 2 in triplets
        const timing::BarTiming t { {}, timing::Resolution::thirtySecond, 0b0010 };
        auto& c = cells[static_cast<std::size_t> (t.slot (1, 1))];   // triplet cell 2 of beat 2: tick 960 + 80
        c = { true, false, 100 };
        c.ratchet = 3;                                      // 80 / 3: subhits 0, 27 (26.67), 53 (53.33)
        auto& straightOdd = cells[static_cast<std::size_t> (t.slot (2, 1))];   // straight cell 2 of beat 3: tick 1920 + 120
        straightOdd = { true, false, 90 };
        kit::StepContext sw;
        sw.swing = 0.5;
        const auto view = tp.bar (0);
        n = kit::cellHits (view, sw, 0, 0, 0, 1, 1, out.data());
        check (n == 3 && out[0].tick == 1040 && out[1].tick == 1067 && out[2].tick == 1093 && juce_exact (out[0].ppq, 1040.0 / 960.0),
               "a triplet cell: tick 1040, ratchet subhits rounded once to 1067 / 1093, not swung");
        n = kit::cellHits (view, sw, 0, 0, 0, 2, 1, out.data());
        check (n == 1 && out[0].tick == 2040 && std::abs (out[0].ppq - (2040.0 / 960.0 + 0.5 * 120.0 / 960.0)) < 1e-12,
               "a straight odd cell next to it is swung by swing x cell");
        auto& tie = cells[static_cast<std::size_t> (t.slot (3, 0))];   // straight 32nd (120 ticks) x 8: 15 each, exact
        tie = { true, false, 80 };
        tie.ratchet = 8;
        n = kit::cellHits (view, sw, 0, 0, 0, 3, 0, out.data());
        bool even = n == 8;
        for (int k = 0; k < n; ++k)
            even = even && out[static_cast<std::size_t> (k)].tick == 2880 + 15 * k;
        check (even, "ratchet 8 in a 120-tick cell: every 15 ticks");
    }
}

// The sequencer plays exactly the canonical hit list: every hit of
// cellHits() once, at its sample (legacy rounding), none twice, across
// rates, block sizes (also odd) and a loop back.
void testSchedulerEqualsCanonical()
{
    std::mt19937 random (2024);
    // 4/4 at 32nds, 2 bars; each lane's beats randomly straight or triplet
    // (PR 137 §10.5b: mixed straight/tuplet notes), humanised in ticks.
    kit::PatternCells cells {};
    std::array<std::uint16_t, kit::padCount * kit::maxPatternBars> masks {};
    kit::Pattern pattern;
    pattern.lanes = 16;
    pattern.bars = 2;
    pattern.resolution = timing::Resolution::thirtySecond;
    pattern.cells = cells.data();
    pattern.masks = masks.data();
    pattern.seed = 1234;
    int tupletCells = 0;
    for (int lane = 0; lane < 16; ++lane)
        for (int bar = 0; bar < 2; ++bar)
        {
            auto& mask = masks[static_cast<std::size_t> (lane * kit::maxPatternBars + bar)];
            mask = static_cast<std::uint16_t> (random() % 16);
            const timing::BarTiming t { {}, pattern.resolution, mask };
            for (int beat = 0; beat < t.beats(); ++beat)
                for (int cell = 0; cell < t.cells (beat); ++cell)
                {
                    if (random() % 5 != 0)
                        continue;
                    auto& c = cells[static_cast<std::size_t> ((lane * kit::maxPatternBars + bar) * kit::slotStride + t.slot (beat, cell))];
                    c.on = true;
                    c.velocity = static_cast<std::uint8_t> (1 + random() % 127);
                    c.accent = random() % 4 == 0;
                    c.probability = static_cast<std::uint8_t> (random() % 3 == 0 ? 1 + random() % 100 : 100);
                    c.ratchet = static_cast<std::uint8_t> (random() % 4 == 0 ? 1 + random() % 8 : 1);
                    const int half = t.cellTicks (beat) / 2;
                    c.microTicks = static_cast<std::int16_t> (random() % 3 == 0 ? static_cast<int> (random() % static_cast<unsigned> (2 * half + 1)) - half : 0);
                    tupletCells += ((mask >> beat) & 1u) != 0 ? 1 : 0;
                }
        }
    check (tupletCells > 40, "the pattern mixes straight and triplet cells (" + std::to_string (tupletCells) + " triplet hits)");
    auto settings = kit::legacyKit();
    settings.sequencerMode = 1;
    settings.swing = 0.2f;
    settings.accent = 0.7f;
    settings.pads[6].laneOn = false;
    kit::StepContext ctx;
    ctx.swing = settings.swing;
    ctx.accent = settings.accent;
    ctx.laneMask = 0xffffu & ~(1u << 6);
    const double bpm = 138.0;
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
        for (const int block : { 64, 333, 512, 4096 })
        {
            kit::KitEngine engine;
            engine.prepare (rate);
            const double spp = 60.0 * rate / bpm;   // samples per quarter
            // Played: (pad, velocity) -> global sample indices.
            std::multiset<std::tuple<long long, int, int>> played;
            kit::Transport tr;
            tr.playing = tr.hasTempo = tr.hasPpq = true;
            tr.bpm = bpm;
            double ppq = 0.0;
            long long sample = 0;
            const double jumpAt = 9.3, jumpTo = 2.6, end = 14.0;
            long long jumpSample = -1;
            std::vector<float> l (static_cast<std::size_t> (block)), r (static_cast<std::size_t> (block));
            while (ppq < end)
            {
                if (jumpSample < 0 && ppq >= jumpAt)
                {
                    jumpSample = sample;
                    ppq = jumpTo;
                }
                tr.ppq = ppq;
                kit::Outputs o { l.data(), r.data(), nullptr };
                engine.process (settings, o, static_cast<std::uint32_t> (block), nullptr, 0, tr, &pattern, nullptr, false);
                for (int i = 0; i < engine.scheduledCount(); ++i)
                {
                    std::uint32_t off;
                    std::uint8_t pad, vel;
                    engine.scheduled (i, off, pad, vel);
                    played.insert ({ sample + off, pad, vel });
                }
                ppq += block / spp;
                sample += block;
            }
            // Canonical: segment 1 [0, ppqAtJump), segment 2 from jumpTo on.
            const double ppqAtJump = static_cast<double> (jumpSample) / spp;
            std::multiset<std::tuple<long long, int, int>> canonical;
            long long ambiguous = 0;
            std::vector<kit::ScheduledHit> barOut;
            const auto add = [&] (double from, double to, long long base, double origin) {
                for (std::int64_t bar = 0; static_cast<double> (bar * pattern.barTicks()) / 960.0 < to + 1.0; ++bar)
                {
                    barOut.clear();
                    testutil::barHits (pattern.bar (static_cast<std::uint32_t> (bar % pattern.bars)), ctx, bar, bar * pattern.barTicks(), barOut);
                    for (const auto& h : barOut)
                    {
                        if (h.ppq < from || h.ppq >= to)
                            continue;
                        const double exact = (h.ppq - origin) * spp;
                        // Block-wise rounding can move a hit right at a
                        // segment edge across it: such hits are skipped.
                        if (h.ppq - from < 2.0 / spp || to - h.ppq < 2.0 / spp)
                        {
                            ++ambiguous;
                            continue;
                        }
                        canonical.insert ({ base + static_cast<long long> (std::ceil (exact - 1e-6)), h.pad, h.velocity });
                    }
                }
            };
            add (0.0, ppqAtJump, 0, 0.0);
            add (jumpTo, jumpTo + (static_cast<double> (sample - jumpSample) / spp), jumpSample, jumpTo);
            // Compare, allowing 1 sample of accumulated position rounding.
            std::size_t matched = 0;
            auto pool = played;
            for (const auto& c : canonical)
            {
                bool found = false;
                for (long long d : { 0LL, -1LL, 1LL })
                {
                    auto it = pool.find ({ std::get<0> (c) + d, std::get<1> (c), std::get<2> (c) });
                    if (it != pool.end())
                    {
                        pool.erase (it);
                        found = true;
                        break;
                    }
                }
                matched += found ? 1 : 0;
            }
            const std::string label = std::to_string (static_cast<int> (rate)) + " Hz / " + std::to_string (block);
            check (canonical.size() > 300, label + ": the pattern has hits (" + std::to_string (canonical.size()) + ")");
            check (matched == canonical.size(), label + ": every canonical hit played at its sample (" + std::to_string (matched) + " / "
                                                    + std::to_string (canonical.size()) + ")");
            check (static_cast<long long> (pool.size()) <= ambiguous,
                   label + ": nothing played twice or outside the list (" + std::to_string (pool.size()) + " extra, "
                       + std::to_string (ambiguous) + " edge hits)");
        }
}

//==============================================================================
// DRM-06: mute gates a pad's running voices within ~3 ms and the gate opens
// again; the per-pad meter reports the post-fader peak; soundOffSeen.
void testMixerGateAndMeters()
{
    auto settings = kit::legacyKit();
    settings.pads[7].decay = 4.0f;   // cymbal, long
    kit::KitEngine engine;
    engine.prepare (48000.0);
    std::vector<float> l (480), r (480);
    kit::Transport tr;
    kit::Outputs out { l.data(), r.data(), nullptr };
    const auto e = note (0, 49, 120);
    engine.process (settings, out, 480, &e, 1, tr, nullptr, nullptr, false);
    float peak = 0.0f;
    for (int i = 0; i < 480; ++i)
        peak = std::max (peak, std::max (std::abs (l[static_cast<std::size_t> (i)]), std::abs (r[static_cast<std::size_t> (i)])));
    check (peak > 1.0e-3f && std::abs (engine.padPeak (7) - peak) < 1.0e-6f, "the pad meter equals the pad's output peak");
    bool others = true;
    for (int pad = 0; pad < kit::padCount; ++pad)
        others = others && (pad == 7 || engine.padPeak (pad) <= 0.0f);
    check (others, "silent pads meter zero");
    settings.pads[7].mute = true;
    engine.process (settings, out, 480, nullptr, 0, tr, nullptr, nullptr, false);   // 10 ms
    float tail = 0.0f;
    for (int i = 240; i < 480; ++i)
        tail = std::max (tail, std::abs (l[static_cast<std::size_t> (i)]));
    check (tail <= 0.0f && std::abs (l[0]) > 0.0f, "mute fades the running voice out within 5 ms");
    check (engine.activeVoices() == 1, "the muted voice keeps running silently");
    settings.pads[7].mute = false;
    engine.process (settings, out, 480, nullptr, 0, tr, nullptr, nullptr, false);
    check (std::abs (l[479]) > 0.0f || std::abs (r[479]) > 0.0f, "unmute opens the gate again");
    check (! engine.soundOffSeen(), "no sound-off seen");
    kit::Event cc;
    cc.size = 3;
    cc.bytes[0] = 0xB0;
    cc.bytes[1] = 120;
    engine.process (settings, out, 480, &cc, 1, tr, nullptr, nullptr, false);
    check (engine.soundOffSeen() && engine.activeVoices() == 0, "CC120 is reported and stops the voices");
}

//==============================================================================
// DRM-05: the song rule and the song scheduler. A 64-bar song whose bars
// have different grids (16/32/64), seeds and content, with a loop region:
// songBarAt maps bars; the engine plays exactly the hits of cellHits, once
// each, at their samples, across block sizes and a seek back.
void testSongScheduler()
{
    std::mt19937 random (77);
    constexpr int bars = 64;
    std::vector<std::vector<kit::PatternStep>> cells (bars);
    std::array<kit::SongBarView, bars> views {};
    for (int b = 0; b < bars; ++b)
    {
        if (b % 9 == 8)
            continue;   // empty bar
        const int steps = b % 3 == 0 ? 64 : (b % 3 == 1 ? 32 : 16);   // bar 64 (index 63): 64 steps
        auto& bc = cells[static_cast<std::size_t> (b)];
        bc.resize (16 * kit::slotStride);
        for (int lane = 0; lane < 16; ++lane)
            for (int st = 0; st < steps; ++st)
                if (random() % 7 == 0)
                {
                    auto& c = bc[static_cast<std::size_t> (lane * kit::slotStride + testutil::slotOfStep (steps, st))];
                    c.on = true;
                    c.velocity = static_cast<std::uint8_t> (1 + random() % 127);
                    c.accent = random() % 4 == 0;
                    c.probability = static_cast<std::uint8_t> (random() % 3 == 0 ? 1 + random() % 100 : 100);
                    c.ratchet = static_cast<std::uint8_t> (random() % 4 == 0 ? 1 + random() % 8 : 1);
                    c.microTicks = static_cast<std::int16_t> (
                        random() % 3 == 0 ? kit::percentToTicks (static_cast<int> (random() % 101) - 50, 3840 / steps) : 0);
                }
        auto& v = views[static_cast<std::size_t> (b)];
        v.cells = bc.data();
        v.laneStride = kit::slotStride;
        v.resolution = testutil::resolutionOfSteps (steps);
        v.seed = static_cast<std::uint32_t> (1000 + b);
    }
    // A unique hit on the last step of the last bar.
    cells[63][15 * 64 + 63] = { true, false, 99 };
    cells[2][0].on = true;          // bar 3 (16 steps), lane 1, step 0: half a step before its bar line
    cells[2][0].velocity = 77;
    cells[2][0].microTicks = -120;
    cells[2][0].probability = 100;
    cells[2][0].ratchet = 1;
    kit::Song song { bars, views.data(), 0, 0 };
    check (kit::songBarAt (song, 0) == 0 && kit::songBarAt (song, 63) == 63 && kit::songBarAt (song, 64) == 0 && kit::songBarAt (song, 130) == 2,
           "song bars wrap at the song length");
    kit::Song looped { bars, views.data(), 8, 4 };
    check (kit::songBarAt (looped, 0) == 0 && kit::songBarAt (looped, 7) == 7 && kit::songBarAt (looped, 8) == 8 && kit::songBarAt (looped, 11) == 11
               && kit::songBarAt (looped, 12) == 8 && kit::songBarAt (looped, 21) == 9,
           "a loop region plays the bars before it once, then repeats [start, start + length) from its first bar");

    auto settings = kit::legacyKit();
    settings.sequencerMode = 1;
    settings.swing = 0.22f;
    settings.accent = 0.6f;
    settings.pads[3].laneOn = false;
    kit::StepContext ctx;
    ctx.swing = settings.swing;
    ctx.accent = settings.accent;
    ctx.laneMask = 0xffffu & ~(1u << 3);
    std::array<kit::ScheduledHit, kit::padCount * kit::maxRatchet> out {};
    // The final-bar hit exists in the rule, at the last 1/64 of bar 64.
    {
        const int n = kit::cellHits (views[63], ctx, 63, 63 * 3840, 15, 3, 15, out.data());
        bool found = false;
        for (int k = 0; k < n; ++k)
            found = found || (out[static_cast<std::size_t> (k)].pad == 15 && out[static_cast<std::size_t> (k)].velocity == 99);
        check (found, "the unique hit of the final bar is in the song rule");
    }
    const double bpm = 174.0;
    for (const double rate : { 44100.0, 96000.0 })
        for (const int block : { 64, 333, 512 })
        {
            kit::KitEngine engine;
            engine.prepare (rate);
            const double spp = 60.0 * rate / bpm;
            std::multiset<std::tuple<long long, int, int>> played;
            kit::Transport tr;
            tr.playing = tr.hasTempo = tr.hasPpq = true;
            tr.bpm = bpm;
            std::vector<float> l (static_cast<std::size_t> (block)), r (static_cast<std::size_t> (block));
            long long sample = 0;
            const double end = 4.0 * bars + 2.0;   // the whole song and into the wrap
            for (double ppq = 0.0; ppq < end; ppq += block / spp, sample += block)
            {
                tr.ppq = ppq;
                kit::Outputs o { l.data(), r.data(), nullptr };
                engine.process (settings, o, static_cast<std::uint32_t> (block), nullptr, 0, tr, nullptr, nullptr, false,
                                kit::KitEngine::Release::none, &song);
                for (int i = 0; i < engine.scheduledCount(); ++i)
                {
                    std::uint32_t off;
                    std::uint8_t pad, vel;
                    engine.scheduled (i, off, pad, vel);
                    played.insert ({ sample + off, pad, vel });
                }
            }
            std::multiset<std::tuple<long long, int, int>> canonical;
            long long edge = 0;
            const double playedEnd = static_cast<double> (sample) / spp;
            std::vector<kit::ScheduledHit> barOut;
            for (std::int64_t bar = 0; bar <= bars; ++bar)
            {
                barOut.clear();
                testutil::barHits (views[kit::songBarAt (song, bar)], ctx, bar, bar * 3840, barOut);
                {
                    for (const auto& h : barOut)
                    {
                        if (h.ppq < 0.0 || h.ppq >= playedEnd)
                            continue;
                        if (h.ppq < 2.0 / spp || playedEnd - h.ppq < 2.0 / spp)
                        {
                            ++edge;
                            continue;
                        }
                        canonical.insert ({ static_cast<long long> (std::ceil (h.ppq * spp - 1e-6)), h.pad, h.velocity });
                    }
                }
            }
            std::size_t matched = 0;
            auto pool = played;
            for (const auto& c : canonical)
                for (long long d : { 0LL, -1LL, 1LL })
                {
                    auto it = pool.find ({ std::get<0> (c) + d, std::get<1> (c), std::get<2> (c) });
                    if (it != pool.end())
                    {
                        pool.erase (it);
                        ++matched;
                        break;
                    }
                }
            const std::string label = "song " + std::to_string (static_cast<int> (rate)) + " Hz / " + std::to_string (block);
            check (canonical.size() > 3000, label + ": the song has hits (" + std::to_string (canonical.size()) + ")");
            check (matched == canonical.size() && static_cast<long long> (pool.size()) <= edge,
                   label + ": every song hit played once at its sample (" + std::to_string (matched) + " / "
                       + std::to_string (canonical.size()) + ", " + std::to_string (pool.size()) + " extra)");
            bool lastBar = false;
            for (const auto& h : played)
                lastBar = lastBar || (std::get<1> (h) == 15 && std::get<2> (h) == 99
                                      && std::get<0> (h) > static_cast<long long> (255.9 * spp) && std::get<0> (h) < static_cast<long long> (256.0 * spp) + 2);
            check (lastBar, label + ": the final bar's unique hit plays in the last 1/64 of bar 64");
        }
    // A seek back replays; switching from the song to a pattern and back is safe.
    {
        kit::KitEngine engine;
        engine.prepare (48000.0);
        kit::Transport tr;
        tr.playing = tr.hasTempo = tr.hasPpq = true;
        tr.bpm = bpm;
        std::vector<float> l (512), r (512);
        kit::Outputs o { l.data(), r.data(), nullptr };
        const double spp = 60.0 * 48000.0 / bpm;
        const auto count = [&] (double from, double to, const kit::Song* sg, const kit::Pattern* pt) {
            int hits = 0;
            for (double ppq = from; ppq < to; ppq += 512 / spp)
            {
                tr.ppq = ppq;
                engine.process (settings, o, 512, nullptr, 0, tr, pt, nullptr, false, kit::KitEngine::Release::none, sg);
                hits += engine.scheduledCount();
            }
            return hits;
        };
        const int first = count (0.0, 16.0, &song, nullptr);
        const int again = count (0.0, 16.0, &song, nullptr);
        check (first > 50 && first == again, "a seek back to 0 replays the same hits (" + std::to_string (first) + " / " + std::to_string (again) + ")");
        auto pg = std::make_unique<testutil::GridPattern> (16, 16, 1, 1);
        pg->at (0, 0, 0) = { true, false, 100 };
        const kit::Pattern& pattern = pg->pattern;
        const int inPattern = count (16.0, 24.0, nullptr, &pattern);
        const int back = count (24.0, 32.0, &song, nullptr);
        check (inPattern == 2 && back > 0, "switching between song and pattern keeps playing without doubles");
    }
}

// Review 6795902654: the loop region is entered at its first bar when the
// host reaches it (bars before the region play once, in order), and repeats
// in order for any start and length. Every bar has one hit that names it.
void testSongLoopRegion()
{
    constexpr int bars = 16;
    std::vector<std::vector<kit::PatternStep>> cells (bars, std::vector<kit::PatternStep> (16 * kit::slotStride));
    std::array<kit::SongBarView, bars> views {};
    for (int b = 0; b < bars; ++b)
    {
        auto& c = cells[static_cast<std::size_t> (b)][static_cast<std::size_t> ((b % 16) * kit::slotStride)];   // lane b, step 0
        c.on = true;
        c.velocity = static_cast<std::uint8_t> (20 + b);
        auto& v = views[static_cast<std::size_t> (b)];
        v.cells = cells[static_cast<std::size_t> (b)].data();
        v.laneStride = kit::slotStride;
        v.seed = 1;
    }
    const auto expect = [&] (std::uint32_t start, std::uint32_t length, std::initializer_list<int> sequence, const std::string& what) {
        kit::Song song { bars, views.data(), start, length };
        std::int64_t absolute = 0;
        bool ok = true;
        std::string got;
        for (const int bar : sequence)
        {
            got += std::to_string (kit::songBarAt (song, absolute)) + " ";
            ok = ok && kit::songBarAt (song, absolute) == static_cast<std::uint32_t> (bar);
            ++absolute;
        }
        check (ok, "loop region " + what + ": bars " + got);
    };
    expect (4, 3, { 0, 1, 2, 3, 4, 5, 6, 4, 5, 6, 4, 5, 6, 4 }, "start 5, length 3");
    expect (4, 4, { 0, 1, 2, 3, 4, 5, 6, 7, 4, 5, 6, 7, 4 }, "start 5, length 4");
    expect (0, 4, { 0, 1, 2, 3, 0, 1, 2, 3, 0 }, "start 1, length 4");
    expect (13, 3, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 13, 14, 15, 13 }, "the last three bars");
    expect (15, 1, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15 }, "one bar");
    expect (0, 0, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 0, 1 }, "disabled: the song repeats");
    {
        kit::Song song { bars, views.data(), 4, 3 };
        check (kit::songBarAt (song, 30) == 6 && kit::songBarAt (song, 31) == 4 && kit::songBarAt (song, 1) == 1 && kit::songBarAt (song, 1000) == 4,
               "loop region: a seek far into the loop, and back before it, uses the same mapping");
    }

    // The engine plays that mapping: which bar's hit sounds at each absolute bar.
    auto settings = kit::legacyKit();
    settings.sequencerMode = 1;
    const double rate = 48000.0, bpm = 480.0;   // one bar = 0.5 s = 24000 frames
    const auto play = [&] (const kit::Song& song, std::int64_t fromBar, std::int64_t toBar) {
        kit::KitEngine engine;
        engine.prepare (rate);
        std::vector<float> left (512), right (512);
        std::vector<int> sequence;
        const double perFrame = bpm / (60.0 * rate);
        for (double ppq = 4.0 * static_cast<double> (fromBar); ppq < 4.0 * static_cast<double> (toBar); ppq += 512 * perFrame)
        {
            kit::Transport transport;
            transport.playing = transport.hasTempo = transport.hasPpq = true;
            transport.bpm = bpm;
            transport.ppq = ppq;
            engine.process (settings, { left.data(), right.data(), nullptr }, 512, nullptr, 0, transport, nullptr, nullptr, false,
                            kit::KitEngine::Release::none, &song);
            for (int i = 0; i < engine.scheduledCount(); ++i)
            {
                std::uint32_t offset = 0;
                std::uint8_t pad = 0, velocity = 0;
                engine.scheduled (i, offset, pad, velocity);
                if (ppq + offset * perFrame < 4.0 * static_cast<double> (toBar) - 1.0e-6)   // the last block ends past toBar
                    sequence.push_back (velocity - 20);   // the bar that hit belongs to
            }
        }
        return sequence;
    };
    const auto text = [] (const std::vector<int>& v) {
        std::string s;
        for (const int x : v)
            s += std::to_string (x) + " ";
        return s;
    };
    {
        const kit::Song song { bars, views.data(), 4, 3 };
        const auto played = play (song, 0, 12);
        check (played == std::vector<int> { 0, 1, 2, 3, 4, 5, 6, 4, 5, 6, 4, 5 }, "loop region: the engine plays the intro once, then bars 5-7 in order: " + text (played));
        const auto into = play (song, 9, 12);   // a seek into the loop
        check (into == std::vector<int> { 6, 4, 5 }, "loop region: playback started inside the loop is in phase: " + text (into));
        const auto before = play (song, 2, 6);  // a seek back before the loop
        check (before == std::vector<int> { 2, 3, 4, 5 }, "loop region: playback started before the loop enters it at its first bar: " + text (before));
    }
    {
        const kit::Song song { bars, views.data(), 4, 4 };
        const auto played = play (song, 0, 13);
        check (played == std::vector<int> { 0, 1, 2, 3, 4, 5, 6, 7, 4, 5, 6, 7, 4 }, "loop region: start 5, length 4 in the engine: " + text (played));
    }
    {
        const kit::Song song { bars, views.data(), 0, 0 };
        const auto played = play (song, 14, 18);
        check (played == std::vector<int> { 14, 15, 0, 1 }, "loop region: without a loop the song repeats after its last bar: " + text (played));
    }
}

// PR 137 §10.4 / §10.5b (host): the sequencer plays each pattern bar from
// the host's bar line; a longer bar is cut at the next line, a shorter one
// rests until it; without a bar position bars are counted from 0; a step
// back of less than a tick is jitter, more replays; a meter change between
// bars only changes the cut. The patterns are never changed, and the export
// (PatternExport) takes no host input at all.
void testHostBars()
{
    auto settings = kit::legacyKit();
    settings.sequencerMode = 1;
    const double rate = 48000.0, bpm = 120.0;   // one quarter = 24000 frames
    const double spq = 60.0 * rate / bpm;
    // Plays [fromPpq, toPpq) in 480-frame blocks with `setup` filling the
    // host fields per block; returns (ppq of each hit, pad) in order.
    using Hits = std::vector<std::pair<double, int>>;
    const auto play = [&] (const kit::Pattern& pattern, double fromPpq, double toPpq,
                           const std::function<void (kit::Transport&)>& setup) {
        kit::KitEngine engine;
        engine.prepare (rate);
        std::vector<float> l (480), r (480);
        Hits hits;
        for (double ppq = fromPpq; ppq < toPpq - 1.0e-9; ppq += 480.0 / spq)
        {
            kit::Transport tr;
            tr.playing = tr.hasTempo = tr.hasPpq = true;
            tr.bpm = bpm;
            tr.ppq = ppq;
            setup (tr);
            engine.process (settings, { l.data(), r.data(), nullptr }, 480, nullptr, 0, tr, &pattern, nullptr, false);
            for (int i = 0; i < engine.scheduledCount(); ++i)
            {
                std::uint32_t off;
                std::uint8_t pad, vel;
                engine.scheduled (i, off, pad, vel);
                hits.emplace_back (ppq + off / spq, pad);
            }
        }
        return hits;
    };
    const auto near = [] (const Hits& got, std::initializer_list<double> want) {
        if (got.size() != want.size())
            return false;
        std::size_t i = 0;
        for (const double w : want)
            if (std::abs (got[i++].first - w) > 2.0 / 24000.0)
                return false;
        return true;
    };
    const auto text = [] (const Hits& h) {
        std::string t;
        for (const auto& [ppq, pad] : h)
            t += std::to_string (ppq).substr (0, 6) + " ";
        return t;
    };
    // A 1-bar 4/4 pattern: lane 1 on every quarter.
    auto four = std::make_unique<testutil::GridPattern> (16, 16, 1);
    for (int b = 0; b < 4; ++b)
        four->at (0, 0, 4 * b) = { true, false, 100 };
    const auto host = [] (int numerator, int denominator, double barStart, std::int64_t barCount) {
        return [=] (kit::Transport& tr) {
            tr.hasSignature = tr.hasBarStart = tr.hasBarCount = true;
            tr.numerator = numerator;
            tr.denominator = denominator;
            tr.barStartPpq = barStart;
            tr.barCount = barCount;
        };
    };
    // Host 4/4 with bar positions = counted bars.
    {
        const auto counted = play (four->pattern, 0.0, 8.0, [] (kit::Transport&) {});
        const auto hosted = play (four->pattern, 0.0, 8.0, [&] (kit::Transport& tr) {
            const double bar = std::floor (tr.ppq / 4.0);
            host (4, 4, bar * 4.0, static_cast<std::int64_t> (bar)) (tr);
        });
        check (near (counted, { 0, 1, 2, 3, 4, 5, 6, 7 }) && counted == hosted, "host bars: 4/4 bar lines from the host equal the counted bars");
    }
    // Host 3/4: each 4/4 pattern bar starts at the host's bar line and is cut after 3 beats.
    {
        const auto got = play (four->pattern, 0.0, 9.0, [&] (kit::Transport& tr) {
            const double bar = std::floor (tr.ppq / 3.0);
            host (3, 4, bar * 3.0, static_cast<std::int64_t> (bar)) (tr);
        });
        check (near (got, { 0, 1, 2, 3, 4, 5, 6, 7, 8 }), "host 3/4: a 4/4 pattern bar is cut at the next bar line: " + text (got));
    }
    // A 3/4 pattern in a 4/4 host: plays 3 beats from each bar line, rests on beat 4.
    {
        auto three = std::make_unique<testutil::GridPattern> (16, 16, 1);
        three->pattern.signature = { 3, 4 };
        for (int b = 0; b < 3; ++b)
            three->cells[static_cast<std::size_t> (timing::BarTiming { { 3, 4 }, timing::Resolution::sixteenth, 0 }.slot (b, 0))] = { true, false, 100 };
        const auto got = play (three->pattern, 0.0, 8.0, [&] (kit::Transport& tr) {
            const double bar = std::floor (tr.ppq / 4.0);
            host (4, 4, bar * 4.0, static_cast<std::int64_t> (bar)) (tr);
        });
        check (near (got, { 0, 1, 2, 4, 5, 6 }), "host 4/4: a 3/4 pattern bar rests until the next bar line: " + text (got));
        const auto counted = play (three->pattern, 0.0, 6.0, [] (kit::Transport&) {});
        check (near (counted, { 0, 1, 2, 3, 4, 5 }), "no host bars: a 3/4 pattern counts its own 3/4 bars from 0: " + text (counted));
    }
    // Bar lines off the counted grid (the host's bar 5 starts at ppq 1.5);
    // the pattern bar is the host's bar index modulo the bar count.
    {
        auto two = std::make_unique<testutil::GridPattern> (16, 16, 2);
        two->at (0, 0, 0) = { true, false, 100 };   // bar A: lane 1
        two->at (1, 1, 0) = { true, false, 100 };   // bar B: lane 2
        const auto got = play (two->pattern, 1.5, 9.5, [&] (kit::Transport& tr) {
            const double bar = std::floor ((tr.ppq - 1.5) / 4.0);
            host (4, 4, 1.5 + bar * 4.0, 5 + static_cast<std::int64_t> (bar)) (tr);
        });
        check (near (got, { 1.5, 5.5 }) && got[0].second == 1 && got[1].second == 0,
               "host bars: a bar line at ppq 1.5 with the host's bar 5 plays pattern bar B there, then bar A: " + text (got));
    }
    // A meter change: 4/4, 4/4, then 3/4 bars; the third pattern bar is cut.
    {
        const auto got = play (four->pattern, 0.0, 11.0, [&] (kit::Transport& tr) {
            if (tr.ppq < 8.0)
                host (4, 4, std::floor (tr.ppq / 4.0) * 4.0, static_cast<std::int64_t> (tr.ppq / 4.0)) (tr);
            else
                host (3, 4, 8.0 + std::floor ((tr.ppq - 8.0) / 3.0) * 3.0, 2 + static_cast<std::int64_t> ((tr.ppq - 8.0) / 3.0)) (tr);
        });
        bool beat4 = false;
        for (const auto& h : got)
            beat4 = beat4 || std::abs (h.first - 11.0) < 1.0e-3;
        check (near (got, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 }) && ! beat4, "host meter change 4/4 -> 3/4: the next bar is cut: " + text (got));
    }
    // Jitter: block 51 reports a start a little before the previous block's
    // (ppq 1.0, where a hit played): half a tick back is jitter and replays
    // nothing; two ticks back is a jump and replays that hit.
    {
        const auto replay = [&] (double back) {
            kit::KitEngine engine;
            engine.prepare (rate);
            std::vector<float> l (480), r (480);
            int hits = 0;
            for (int b = 0; b < 120; ++b)
            {
                kit::Transport tr;
                tr.playing = tr.hasTempo = tr.hasPpq = true;
                tr.bpm = bpm;
                tr.ppq = b == 51 ? 1.0 - back : b * 480.0 / spq;
                engine.process (settings, { l.data(), r.data(), nullptr }, 480, nullptr, 0, tr, &four->pattern, nullptr, false);
                hits += engine.scheduledCount();
            }
            return hits;
        };
        const int clean = replay (-480.0 / spq);   // block 51 at its own position
        check (clean == 3, "jitter: three quarters in 2.4 quarters (" + std::to_string (clean) + ")");
        check (replay (0.5 / 960.0) == clean, "jitter: a step back of half a tick replays nothing");
        check (replay (2.0 / 960.0) == clean + 1, "a step back of two ticks is a jump: the hit there plays again");
    }
}

int main()
{
    testModels();
    testNotesAndChokes();
    testPolyphony();
    testSamplePlayers();
    testBlockInvariance();
    testInvalidSettings();
    testRealtime();
    testMixerGateAndMeters();
    testSongScheduler();
    testSongLoopRegion();
    testHostBars();
    testStepRules();
    testSchedulerEqualsCanonical();
    if (failures == 0)
        std::printf ("VOX Drums kit engine tests passed\n");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
