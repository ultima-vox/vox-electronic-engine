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

#ifdef VOX_TEST_INTERPOSE_MALLOC
#include "AllocationProbe.h"
#endif

#include <algorithm>
#include <array>
#include <cmath>
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


//==============================================================================
void testStepRules()
{
    std::vector<kit::PatternStep> cells (16 * 16);
    const auto at = [&] (int lane, int st) -> kit::PatternStep& { return cells[static_cast<std::size_t> (lane * 16 + st)]; };
    at (0, 0) = { true, false, 100 };
    at (0, 0).ratchet = 4;
    at (1, 1) = { true, false, 90 };
    at (1, 1).micro = 50;
    at (2, 1) = { true, false, 80 };
    at (3, 2) = { true, true, 60 };
    at (4, 2) = { true, false, 70 };
    at (5, 3) = { true, false, 70 };
    at (5, 3).micro = -50;
    kit::Pattern pattern { 16, 16, 1, 0, cells.data() };
    kit::StepContext ctx;
    ctx.swing = 0.0;
    ctx.accent = 0.5;
    std::array<kit::ScheduledHit, kit::padCount * kit::maxRatchet> out {};
    int n = kit::stepHits (pattern, ctx, 0, out.data());
    check (n == 4 && juce_exact (out[0].ppq, 0.0) && std::abs (out[1].ppq - 0.0625) < 1e-12 && std::abs (out[3].ppq - 0.1875) < 1e-12
               && out[3].pad == 0 && out[3].velocity == 100,
           "ratchet 4: four hits evenly in the step");
    n = kit::stepHits (pattern, ctx, 1, out.data());
    check (n == 2 && out[0].pad == 1 && std::abs (out[0].ppq - 0.375) < 1e-12 && out[1].pad == 2 && juce_exact (out[1].ppq, 0.25),
           "micro +50 % moves a hit half a step later");
    ctx.swing = 0.3;
    n = kit::stepHits (pattern, ctx, 1, out.data());
    check (n == 2 && std::abs (out[1].ppq - (0.25 + 0.3 * 0.25)) < 1e-12 && std::abs (out[0].ppq - (0.25 + 0.075 + 0.125)) < 1e-12,
           "swing delays odd steps, micro adds to it");
    n = kit::stepHits (pattern, ctx, 2, out.data());
    check (n == 2 && out[0].velocity == 94 && out[1].velocity == 70, "accent raises velocity towards 127 by the accent amount");
    ctx.laneMask = ~(1u << 3);
    n = kit::stepHits (pattern, ctx, 2, out.data());
    check (n == 1 && out[0].pad == 4, "a disabled lane plays nothing");
    ctx.laneMask = 0xffffu;
    n = kit::stepHits (pattern, ctx, 3, out.data());
    check (n == 1 && std::abs (out[0].ppq - (0.75 + 0.3 * 0.25 - 0.125)) < 1e-12, "micro -50 % moves a hit half a step earlier");
    check (kit::stepHits (pattern, ctx, -1, out.data()) == 0, "negative steps have no hits");

    // Probability: deterministic per seed and absolute position, about the
    // stated share, different seeds decide differently.
    std::vector<kit::PatternStep> pc (16);
    pc[0] = { true, false, 100 };
    pc[0].probability = 30;
    kit::Pattern prob { 1, 16, 1, 0, pc.data(), 7 };
    int played = 0, same = 0, differ = 0;
    kit::Pattern other = prob;
    other.seed = 8;
    for (std::int64_t pass = 0; pass < 4000; ++pass)
    {
        const std::int64_t g = pass * 16;
        const int a = kit::stepHits (prob, ctx, g, out.data());
        const int b = kit::stepHits (prob, ctx, g, out.data());
        const int c = kit::stepHits (other, ctx, g, out.data());
        played += a;
        same += a == b ? 1 : 0;
        differ += a != c ? 1 : 0;
    }
    check (same == 4000, "probability is deterministic for a seed and position");
    check (played > 1100 && played < 1300, "probability 30 % plays about 30 % of passes (" + std::to_string (played) + " / 4000)");
    check (differ > 500, "another seed decides differently");

    // 1/64 grid.
    kit::Pattern fine { 16, 64, 1, 4, cells.data() };
    check (juce_exact (kit::stepLength (fine), 0.0625) && juce_exact (kit::stepLength (pattern), 0.25), "step lengths 1/64 and 1/16");
}

// The sequencer plays exactly the canonical hit list: every hit of
// stepHits() once, at its sample (legacy rounding), none twice, across
// rates, block sizes (also odd) and a loop back.
void testSchedulerEqualsCanonical()
{
    std::mt19937 random (2024);
    std::vector<kit::PatternStep> cells (16 * 32 * 2);
    for (auto& c : cells)
        if (random() % 5 == 0)
        {
            c.on = true;
            c.velocity = static_cast<std::uint8_t> (1 + random() % 127);
            c.accent = random() % 4 == 0;
            c.probability = static_cast<std::uint8_t> (random() % 3 == 0 ? 1 + random() % 100 : 100);
            c.ratchet = static_cast<std::uint8_t> (random() % 4 == 0 ? 1 + random() % 8 : 1);
            c.micro = static_cast<std::int8_t> (random() % 3 == 0 ? static_cast<int> (random() % 101) - 50 : 0);
        }
    const kit::Pattern pattern { 16, 32, 2, 2, cells.data(), 1234 };
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
            std::array<kit::ScheduledHit, kit::padCount * kit::maxRatchet> out {};
            long long ambiguous = 0;
            const auto add = [&] (double from, double to, long long base, double origin) {
                for (std::int64_t g = 0; g * kit::stepLength (pattern) < to + 1.0; ++g)
                {
                    const int n = kit::stepHits (pattern, ctx, g, out.data());
                    for (int k = 0; k < n; ++k)
                    {
                        const auto& h = out[static_cast<std::size_t> (k)];
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
// songBarAt maps bars; the engine plays exactly the hits of songHits, once
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
        const std::uint32_t steps = b % 3 == 0 ? 64u : (b % 3 == 1 ? 32u : 16u);   // bar 64 (index 63): 64 steps
        cells[static_cast<std::size_t> (b)].resize (16 * steps);
        for (auto& c : cells[static_cast<std::size_t> (b)])
            if (random() % 7 == 0)
            {
                c.on = true;
                c.velocity = static_cast<std::uint8_t> (1 + random() % 127);
                c.accent = random() % 4 == 0;
                c.probability = static_cast<std::uint8_t> (random() % 3 == 0 ? 1 + random() % 100 : 100);
                c.ratchet = static_cast<std::uint8_t> (random() % 4 == 0 ? 1 + random() % 8 : 1);
                c.micro = static_cast<std::int8_t> (random() % 3 == 0 ? static_cast<int> (random() % 101) - 50 : 0);
            }
        views[static_cast<std::size_t> (b)] = { cells[static_cast<std::size_t> (b)].data(), steps, steps, static_cast<std::uint32_t> (1000 + b) };
    }
    // A unique hit on the last step of the last bar.
    cells[63][15 * 64 + 63] = { true, false, 99 };
    cells[2][0].on = true;          // bar 3 (16 steps), lane 1, step 0: half a step before its bar line
    cells[2][0].velocity = 77;
    cells[2][0].micro = -50;
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
        const int n = kit::songHits (song, ctx, 63, 63, out.data());
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
            for (std::int64_t bar = 0; bar <= bars; ++bar)
            {
                const auto& view = views[kit::songBarAt (song, bar)];
                for (std::uint32_t st = 0; st < view.steps && view.cells != nullptr; ++st)
                {
                    const int n = kit::songHits (song, ctx, bar, st, out.data());
                    for (int k = 0; k < n; ++k)
                    {
                        const auto& h = out[static_cast<std::size_t> (k)];
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
        std::vector<kit::PatternStep> pc (16 * 16);
        pc[0] = { true, false, 100 };
        const kit::Pattern pattern { 16, 16, 1, 0, pc.data(), 1 };
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
    std::vector<std::vector<kit::PatternStep>> cells (bars, std::vector<kit::PatternStep> (16 * 16));
    std::array<kit::SongBarView, bars> views {};
    for (int b = 0; b < bars; ++b)
    {
        auto& c = cells[static_cast<std::size_t> (b)][static_cast<std::size_t> ((b % 16) * 16)];   // lane b, step 0
        c.on = true;
        c.velocity = static_cast<std::uint8_t> (20 + b);
        views[static_cast<std::size_t> (b)] = { cells[static_cast<std::size_t> (b)].data(), 16, 16, 1 };
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
    testStepRules();
    testSchedulerEqualsCanonical();
    if (failures == 0)
        std::printf ("VOX Drums kit engine tests passed\n");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
