// SYN-01 engine tests. No JUCE, no Rack, no product shell.
// Both parts sound, engines switch, glide does not snap the phase, acid
// resumes the held note, the 16-voice cap holds, MIDI CC behaves, and
// process() allocates nothing.
#include "engine/SynthEngine.h"
#include "GoldenRender.h"

#ifdef VOX_TEST_INTERPOSE_MALLOC
#include "AllocationProbe.h"
#endif

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

namespace eng = vox::syn::engine;
int failures = 0;
int checks = 0;
int skips = 0;

void check (bool ok, const std::string& what)
{
    ++checks;
    if (! ok)
    {
        std::printf ("FAIL [synth engine]: %s\n", what.c_str());
        ++failures;
    }
}

eng::BlockParams defaults()
{
    eng::BlockParams params;
    params.part[0] = { eng::EngineKind::psyBass, 0.75f, 0.55f, 0.35f, 0.30f, 0.0f, 0.25f };
    params.part[1] = { eng::EngineKind::acid, 0.55f, 0.48f, 0.62f, 0.35f, 0.0f, 0.30f };
    params.output = 0.80f;
    return params;
}

eng::Event midi (std::uint32_t offset, std::uint8_t status, std::uint8_t data1, std::uint8_t data2)
{
    return { offset, status, data1, data2 };
}

struct Render {
    std::vector<float> left;
    float peak = 0.0f;
    float side = 0.0f;
    bool finite = true;
};

Render render (eng::SynthEngine& engine, int frames, const eng::Event* events, int eventCount,
               const eng::BlockParams& params, int blockSize = 512)
{
    Render out;
    out.left.assign (static_cast<std::size_t> (std::max (0, frames)), 0.0f);
    std::vector<float> right (out.left.size(), 0.0f);
    const int chunk = std::max (1, blockSize);
    int done = 0;
    int consumed = 0;
    while (done < frames)
    {
        const int n = std::min (chunk, frames - done);
        std::vector<eng::Event> local;
        while (consumed < eventCount && events[consumed].offset < static_cast<std::uint32_t> (done + n))
        {
            auto event = events[consumed++];
            event.offset -= static_cast<std::uint32_t> (done);
            local.push_back (event);
        }
        engine.process (out.left.data() + done, right.data() + done, n,
                        local.empty() ? nullptr : local.data(), static_cast<int> (local.size()), params);
        for (int i = 0; i < n; ++i)
        {
            const float sample = out.left[static_cast<std::size_t> (done + i)];
            const float other = right[static_cast<std::size_t> (done + i)];
            out.finite = out.finite && std::isfinite (sample) && std::isfinite (other);
            out.peak = std::max (out.peak, std::fabs (sample));
            out.side = std::max (out.side, std::fabs (sample - other));
        }
        done += n;
    }
    return out;
}

void hold (eng::SynthEngine& engine, const eng::BlockParams& params, int note, int frames)
{
    const eng::Event on = midi (0, 0x90, static_cast<std::uint8_t> (note), 110);
    render (engine, frames, &on, 1, params);
}

void testBothParts()
{
    eng::SynthEngine engine;
    engine.prepare (48000.0, 512);
    auto bass = defaults();
    bass.part[1].level = 0.0f;
    const auto a = render (engine, 2000, nullptr, 0, bass);
    hold (engine, bass, 40, 4000);
    const auto bassPeak = render (engine, 2000, nullptr, 0, bass).peak;

    eng::SynthEngine other;
    other.prepare (48000.0, 512);
    auto acid = defaults();
    acid.part[0].level = 0.0f;
    hold (other, acid, 40, 4000);
    const auto acidPeak = render (other, 2000, nullptr, 0, acid).peak;
    check (bassPeak > 1.0e-3f, "part A psy bass sounds");
    check (acidPeak > 1.0e-3f, "part B acid sounds");

    eng::SynthEngine both;
    both.prepare (48000.0, 512);
    const auto together = defaults();
    hold (both, together, 40, 4000);
    check (render (both, 1000, nullptr, 0, together).peak > 1.0e-3f, "both parts sound on one note");
    check (a.finite, "silence before a note is finite");
    (void) a;
}

void testGlidePhase()
{
    const auto continuous = [] (float slide, float porta) {
        eng::SynthEngine engine;
        engine.prepare (48000.0, 512);
        auto params = defaults();
        params.part[1].level = 0.0f;
        params.part[0].slide = slide;
        params.part[0].portamento = porta;
        hold (engine, params, 36, 2000);
        const double before = engine.monoPhase (0);
        const eng::Event next = midi (0, 0x90, 48, 100);
        render (engine, 1, &next, 1, params);
        const double after = engine.monoPhase (0);
        double expected = before + engine.monoFrequency (0) / 48000.0;
        expected -= std::floor (expected);
        const double error = std::fabs (after - expected);
        return error < 0.02 || error > 0.98;
    };
    check (continuous (1.0f, 0.0f), "slide keeps the oscillator phase");
    check (continuous (0.0f, 1.0f), "portamento keeps the oscillator phase");
    check (! continuous (0.0f, 0.0f), "snap resets the oscillator phase");
}

bool phaseStep (int part, eng::EngineKind kind, int osc, double ratio)
{
    eng::SynthEngine engine;
    engine.prepare (48000.0, 512);
    auto params = defaults();
    params.part[0].level = part == 0 ? 0.75f : 0.0f;
    params.part[1].level = part == 1 ? 0.55f : 0.0f;
    params.part[part].engine = kind;
    params.part[part].slide = 1.0f;
    params.part[part].portamento = 0.0f;
    hold (engine, params, 36, 2000);
    const double before = engine.monoOscPhase (part, osc);
    const eng::Event next = midi (0, 0x90, 48, 100);
    render (engine, 1, &next, 1, params);
    const double after = engine.monoOscPhase (part, osc);
    double expected = before + ratio * engine.monoFrequency (part) / 48000.0;
    expected -= std::floor (expected);
    const double error = std::fabs (after - expected);
    return error < 0.02 || error > 0.98;
}

void testSlidePhaseAndClick()
{
    check (phaseStep (0, eng::EngineKind::psyBass, 0, 1.0), "psy bass saw phase continues across slide");
    check (phaseStep (0, eng::EngineKind::psyBass, 2, 0.5), "psy bass sub phase continues across slide");
    check (phaseStep (1, eng::EngineKind::acid, 0, 1.0), "acid saw phase continues across slide");
    check (phaseStep (1, eng::EngineKind::acid, 1, 1.0), "acid square phase continues across slide");

    const auto transition = [] (eng::EngineKind kind, int part, int velocity) {
        eng::SynthEngine engine;
        engine.prepare (48000.0, 128);
        auto params = defaults();
        params.part[0].level = part == 0 ? 0.75f : 0.0f;
        params.part[1].level = part == 1 ? 0.70f : 0.0f;
        params.part[part].engine = kind;
        params.part[part].slide = 1.0f;
        params.part[part].portamento = 0.0f;
        hold (engine, params, 36, 4000);
        const auto steady = render (engine, 64, nullptr, 0, params, 128);
        double steadyDelta = 0.0;
        for (std::size_t i = 1; i < steady.left.size(); ++i)
            steadyDelta = std::max (steadyDelta, static_cast<double> (std::fabs (steady.left[i] - steady.left[i - 1])));
        const eng::Event next = midi (0, 0x90, 48, static_cast<std::uint8_t> (velocity));
        const auto hopped = render (engine, 32, &next, 1, params, 128);
        double hopDelta = std::fabs (hopped.left.front() - steady.left.back());
        for (std::size_t i = 1; i < hopped.left.size(); ++i)
            hopDelta = std::max (hopDelta, static_cast<double> (std::fabs (hopped.left[i] - hopped.left[i - 1])));
        const double limit = std::max (0.05, steadyDelta * 8.0 + 0.02);
        return hopDelta < limit && hopped.finite;
    };
    check (transition (eng::EngineKind::psyBass, 0, 80), "psy bass slide does not click");
    check (transition (eng::EngineKind::acid, 1, 80), "acid slide does not click");
    check (transition (eng::EngineKind::acid, 1, 120), "acid accent slide does not click");
}

void testSlideTimeDiffersFromPortamento()
{
    const auto freqAfter = [] (float slide, float porta) {
        eng::SynthEngine engine;
        engine.prepare (48000.0, 512);
        auto params = defaults();
        params.part[1].level = 0.0f;
        params.part[0].slide = slide;
        params.part[0].portamento = porta;
        hold (engine, params, 36, 1500);
        const eng::Event next = midi (0, 0x90, 60, 110);
        render (engine, 2500, &next, 1, params);
        return engine.monoFrequency (0);
    };
    const double slid = freqAfter (1.0f, 0.0f);
    const double porta = freqAfter (0.0f, 1.0f);
    check (std::fabs (slid - porta) > 1.0, "slide time and portamento time are not the same");
}

void testAcidResume()
{
    eng::SynthEngine engine;
    engine.prepare (48000.0, 512);
    auto params = defaults();
    params.part[0].level = 0.0f;
    params.part[1].slide = 0.85f;
    params.part[1].portamento = 0.0f;
    hold (engine, params, 36, 2000);
    check (engine.monoPhase (1) > 0.2, "acid phase is running");
    const eng::Event higher = midi (0, 0x90, 48, 120);
    render (engine, 400, &higher, 1, params);
    check (engine.monoNote (1) == 48 && engine.monoGate (1), "acid moves to the new note");
    const double before = engine.monoPhase (1);
    const double frequency = engine.monoFrequency (1);
    const eng::Event off = midi (0, 0x80, 48, 0);
    render (engine, 1, &off, 1, params);
    const double after = engine.monoPhase (1);
    double expected = before + frequency / 48000.0;
    expected -= std::floor (expected);
    const double error = std::fabs (after - expected);
    check (engine.monoNote (1) == 36 && engine.monoGate (1), "acid resumes the held note");
    check (error < 0.02 || error > 0.98, "acid resume does not restart the oscillator phase");
}

void testVoiceCap()
{
    eng::SynthEngine engine;
    engine.prepare (48000.0, 512);
    auto params = defaults();
    params.part[0].engine = eng::EngineKind::va;
    params.part[1].engine = eng::EngineKind::va;
    for (int note = 0; note < 8; ++note)
    {
        const eng::Event on = midi (0, 0x90, static_cast<std::uint8_t> (40 + note), 100);
        const auto heard = render (engine, 64, &on, 1, params);
        check (heard.finite && engine.activeVoices() <= eng::maxVoices, "voice count stays <= 16 while filling");
    }
    check (engine.activeVoices() == eng::maxVoices, "eight notes on both parts fill 16 voices");
    const eng::Event extra = midi (0, 0x90, 70, 110);
    render (engine, 32, &extra, 1, params);
    check (engine.activeVoices() <= eng::maxVoices, "the 17th request does not grow the allocator");
    check (engine.vaFadeLeft() > 0, "the stolen voice fades instead of a hard cut");

    eng::SynthEngine mixed;
    mixed.prepare (48000.0, 512);
    auto mix = defaults();
    mix.part[0].engine = eng::EngineKind::psyBass;
    mix.part[1].engine = eng::EngineKind::va;
    for (int note = 0; note < 20; ++note)
    {
        const eng::Event on = midi (0, 0x90, static_cast<std::uint8_t> (36 + (note % 40)), 90);
        render (mixed, 48, &on, 1, mix);
        check (mixed.activeVoices() <= eng::maxVoices, "mono plus VA stay inside 16");
    }
}

void testMidi()
{
    eng::SynthEngine engine;
    engine.prepare (48000.0, 512);
    auto params = defaults();
    const eng::Event on = midi (0, 0x90, 45, 100);
    render (engine, 200, &on, 1, params);
    check (engine.monoGate (0) && engine.monoGate (1), "a note opens both parts");

    const eng::Event pedal = midi (0, 0xb0, 64, 127);
    render (engine, 16, &pedal, 1, params);
    const eng::Event off = midi (0, 0x80, 45, 0);
    render (engine, 32, &off, 1, params);
    check (engine.monoGate (0) && engine.monoGate (1), "CC64 holds the note after note-off");
    const eng::Event lift = midi (0, 0xb0, 64, 0);
    render (engine, 32, &lift, 1, params);
    check (! engine.monoGate (0) && ! engine.monoGate (1), "releasing CC64 releases the note");

    const eng::Event again = midi (0, 0x90, 47, 110);
    render (engine, 300, &again, 1, params);
    const eng::Event kill = midi (0, 0xb0, 120, 0);
    render (engine, 16, &kill, 1, params);
    check (engine.activeVoices() == 0, "CC120 kills every sounding voice");

    render (engine, 400, &again, 1, params);
    const eng::Event release = midi (0, 0xb0, 123, 0);
    render (engine, 8, &release, 1, params);
    check (! engine.monoGate (0) && engine.activeVoices() > 0, "CC123 releases and leaves the tail");
    render (engine, 48000, nullptr, 0, params);
    check (engine.activeVoices() == 0, "the release tail ends");
}

void testSwitchAndRates()
{
    eng::SynthEngine engine;
    engine.prepare (48000.0, 512);
    auto params = defaults();
    hold (engine, params, 40, 500);
    params.part[0].engine = eng::EngineKind::acid;
    auto heard = render (engine, 400, nullptr, 0, params);
    check (heard.finite && heard.peak > 1.0e-4f && engine.activeVoices() <= 16, "switch to acid keeps the held note");
    params.part[0].engine = eng::EngineKind::va;
    eng::Event chord[4];
    for (int i = 0; i < 4; ++i)
        chord[i] = midi (static_cast<std::uint32_t> (i), 0x90, static_cast<std::uint8_t> (60 + i * 2), 100);
    heard = render (engine, 800, chord, 4, params);
    check (heard.finite && engine.activeVoices() <= 16 && engine.activeVoices() > 2, "switch to VA plays a chord inside the cap");
    params.part[0].engine = eng::EngineKind::psyBass;
    heard = render (engine, 400, nullptr, 0, params);
    check (heard.finite && engine.monoGate (0) && engine.activeVoices() <= 16, "switch back to psy bass is finite");

    for (double rate : { 44100.0, 48000.0, 96000.0 })
    {
        for (int block : { 32, 64, 128, 512 })
        {
            eng::SynthEngine local;
            local.prepare (rate, block);
            const eng::Event on = midi (0, 0x90, 36, 100);
            const auto tone = render (local, block, &on, 1, defaults());
            check (tone.finite && tone.peak > 1.0e-4f, "finite sound at " + std::to_string (rate) + " / " + std::to_string (block));
            float oneL = 0.0f, oneR = 0.0f;
            local.process (&oneL, &oneR, 1, nullptr, 0, defaults());
            check (std::isfinite (oneL) && std::isfinite (oneR), "block size 1 stays finite");
            local.process (&oneL, &oneR, 0, nullptr, 0, defaults());
        }
    }

    auto silent = defaults();
    silent.output = 0.0f;
    eng::SynthEngine quiet;
    quiet.prepare (48000.0, 128);
    const eng::Event on = midi (0, 0x90, 36, 127);
    check (render (quiet, 1000, &on, 1, silent).peak < 1.0e-6f, "output 0 is silent");
}

void testAllocation()
{
#ifndef VOX_TEST_INTERPOSE_MALLOC
#error "suite_synth_engine_tests requires the allocation probe. A missing probe used to pass."
#endif
    if (! voxProbeAvailable())
    {
        ++skips;
        check (false, "allocation probe did not attach");
        return;
    }
    eng::SynthEngine engine;
    engine.prepare (48000.0, 512);
    const auto params = defaults();
    unsigned long calls = 0;
    float left[128], right[128];
    eng::Event events[8];
    events[0] = midi (0, 0x90, 40, 100);
    events[1] = midi (10, 0x90, 47, 90);
    events[2] = midi (20, 0xb0, 64, 127);
    events[3] = midi (30, 0x80, 40, 0);
    events[4] = midi (40, 0xe0, 0, 64);
    events[5] = midi (50, 0xb0, 123, 0);
    voxProbeBegin();
    for (int i = 0; i < 300; ++i)
        engine.process (left, right, 128, events, 6, params);
    calls = voxProbeEnd();
    check (calls == 0, "process allocates nothing (" + std::to_string (calls) + ")");
}

// Golden contract, one row per engine. Not bit-identical across compilers.
//   rate 48000, block 128, 4096 frames, output 0.80
//   psy-bass: part A Psy Bass 0.75/0.55/0.35/0.30/0/0.25, part B level 0
//             note 36 velocity 100 at frame 0
//   acid:     part B Acid 0.55/0.48/0.62/0.40/0/0.30, part A level 0
//             note 40 velocity 120 at 0, note 52 velocity 100 at 2048
//   va:       part A VA 0.70/0.60/0.20/0/0/0.10, part B Psy Bass level 0
//             notes 60, 64, 67 velocity 100 at frames 0, 10, 20
// Tolerances: peak and RMS 2% relative (floor 1e-4), centroid 5% relative.
// Catches silence, a wrong pitch, or a dead oscillator. Clicks are a separate test.
void testGolden()
{
#ifndef VOX_SYNTH_GOLDEN_MANIFEST
#error "VOX_SYNTH_GOLDEN_MANIFEST was not configured"
#endif
    constexpr double rate = 48000.0;
    constexpr int frames = 4096;
    constexpr int block = 128;

    const auto rowFor = [&] (const std::string& key, const eng::BlockParams& params,
                             const eng::Event* events, int eventCount) {
        eng::SynthEngine engine;
        engine.prepare (rate, block);
        const auto audio = render (engine, frames, events, eventCount, params, block);
        check (audio.finite, key + " is finite");
        check (audio.side < 1.0e-6f, key + " is dual mono");
        check (audio.peak > 1.0e-3f, key + " is not silent");
        const auto metrics = vox::testkit::measure (audio.left, rate);
        return vox::testkit::ManifestRow { metrics, metrics };
    };

    std::map<std::string, vox::testkit::ManifestRow> rows;
    {
        auto params = defaults();
        params.part[1].level = 0.0f;
        const eng::Event on = midi (0, 0x90, 36, 100);
        rows["psy-bass@48000"] = rowFor ("psy-bass@48000", params, &on, 1);
    }
    {
        auto params = defaults();
        params.part[0].level = 0.0f;
        params.part[1].slide = 0.40f;
        const eng::Event events[2] = { midi (0, 0x90, 40, 120), midi (2048, 0x90, 52, 100) };
        rows["acid@48000"] = rowFor ("acid@48000", params, events, 2);
    }
    {
        auto params = defaults();
        params.part[0].engine = eng::EngineKind::va;
        params.part[0].level = 0.70f;
        params.part[0].cutoff = 0.60f;
        params.part[0].resonance = 0.20f;
        params.part[0].slide = 0.0f;
        params.part[0].drive = 0.10f;
        params.part[1].level = 0.0f;
        params.part[1].engine = eng::EngineKind::psyBass;
        const eng::Event chord[3] = {
            midi (0, 0x90, 60, 100), midi (10, 0x90, 64, 100), midi (20, 0x90, 67, 100)
        };
        rows["va@48000"] = rowFor ("va@48000", params, chord, 3);
    }

    const std::string path = VOX_SYNTH_GOLDEN_MANIFEST;
    if (std::getenv ("VOX_GOLDEN_UPDATE") != nullptr)
    {
        check (vox::testkit::writeManifest (path, rows), "write golden manifest");
        std::printf ("wrote %zu golden rows to %s\n", rows.size(), path.c_str());
        return;
    }

    vox::testkit::Tolerance tolerance;
    tolerance.relative = 0.02;
    tolerance.absoluteFloor = 1.0e-4;
    tolerance.centroidRelative = 0.05;
    const auto expected = vox::testkit::readManifest (path);
    check (expected.size() == rows.size(), "golden manifest has 3 rows");
    for (const auto& [key, row] : rows)
    {
        const auto it = expected.find (key);
        if (it == expected.end())
        {
            check (false, key + " missing from golden manifest");
            continue;
        }
        if (auto mismatch = vox::testkit::compare (it->second.left, row.left, tolerance))
            check (false, key + " L: " + *mismatch);
        else
            check (true, key + " matches golden");
        if (auto mismatch = vox::testkit::compare (it->second.right, row.right, tolerance))
            check (false, key + " R: " + *mismatch);
        else
            check (true, key + " right matches golden");
    }
}

void testSameKeyStack()
{
    eng::SynthEngine engine;
    engine.prepare (48000.0, 256);
    auto params = defaults();
    params.part[0].engine = eng::EngineKind::acid;
    params.part[0].slide = 0.0f;
    params.part[0].portamento = 0.0f;
    params.part[1].level = 0.0f;
    const eng::Event first = midi (0, 0x90, 48, 70);
    render (engine, 256, &first, 1, params);
    check (engine.monoGate (0), "first note is held");

    eng::Event join[2];
    join[0] = midi (0, 0x90, 48, 64);
    join[0].articulation = static_cast<std::uint8_t> (eng::articulateSlide | eng::articulateAccent);
    join[1] = midi (1, 0x80, 48, 0);
    render (engine, 256, join, 2, params);
    check (engine.monoGate (0), "same-note slide join keeps the new gate");
    check (engine.monoNote (0) == 48, "same-note join stays on that pitch");
    check (engine.glideSource (0) == 1, "same-note slide still glides");
    check (engine.monoAccent (0) > 0.2f, "accent on the new note is not cleared by the old note-off");

    const eng::Event release = midi (0, 0x80, 48, 0);
    render (engine, 64, &release, 1, params);
    check (! engine.monoGate (0), "the new note still releases on its own note-off");

    eng::SynthEngine overlap;
    overlap.prepare (48000.0, 128);
    const eng::Event on = midi (0, 0x90, 40, 80);
    render (overlap, 64, &on, 1, params);
    eng::Event again[2] = { midi (0, 0x90, 40, 100), midi (10, 0x80, 40, 0) };
    render (overlap, 64, again, 2, params);
    check (overlap.monoGate (0), "an older note-off does not kill a stacked note");
    const eng::Event done = midi (20, 0x80, 40, 0);
    render (overlap, 64, &done, 1, params);
    check (! overlap.monoGate (0), "the stacked note releases on the later note-off");

    eng::SynthEngine chord;
    chord.prepare (48000.0, 128);
    const eng::Event low = midi (0, 0x90, 36, 90);
    render (chord, 128, &low, 1, params);
    eng::Event move[2] = { midi (0, 0x90, 43, 90), midi (1, 0x80, 36, 0) };
    move[0].articulation = eng::articulateSlide;
    render (chord, 64, move, 2, params);
    check (chord.monoGate (0) && chord.monoNote (0) == 43, "NoteOn before NoteOff keeps the new pitch");
    check (chord.glideSource (0) == 1, "different-pitch slide still glides");
}

void testTailBound()
{
    const double derivedTail = -std::log (1.0e-4) * 0.06;
    check (eng::voiceTailSeconds() == derivedTail, "voice tail bound is -ln(1e-4) * 0.06 s");
}

float energy (const Render& audio)
{
    double sum = 0.0;
    for (float sample : audio.left)
        sum += static_cast<double> (sample) * static_cast<double> (sample);
    return static_cast<float> (std::sqrt (sum / static_cast<double> (std::max<std::size_t> (1, audio.left.size()))));
}

Render play (const eng::BlockParams& params, int note, int frames = 2048)
{
    eng::SynthEngine engine;
    engine.prepare (48000.0, 128);
    const eng::Event on = midi (0, 0x90, static_cast<std::uint8_t> (note), 100);
    return render (engine, frames, &on, 1, params, 128);
}

eng::BlockParams vaBase()
{
    auto params = defaults();
    params.part[0].engine = eng::EngineKind::va;
    params.part[0].level = 0.70f;
    params.part[1].level = 0.0f;
    return params;
}

void testVa()
{
    const auto classic = play (vaBase(), 60);
    auto sine = vaBase();
    sine.part[0].oscWave[0] = 0;
    sine.part[0].oscWave[1] = 0;
    check (std::fabs (energy (classic) - energy (play (sine, 60))) > 1.0e-4f, "sine is not the default saw mix");

    auto triangle = vaBase();
    triangle.part[0].oscWave[0] = 3;
    triangle.part[0].oscLevel[1] = 0.0f;
    triangle.part[0].vaSub = 0.0f;
    triangle.part[0].noise = 0.0f;
    const auto tri = play (triangle, 72);
    check (tri.finite && tri.peak > 1.0e-4f, "triangle speaks");

    auto narrow = triangle;
    narrow.part[0].oscWave[0] = 4;
    narrow.part[0].oscPulse[0] = 0.15f;
    auto wide = narrow;
    wide.part[0].oscPulse[0] = 0.85f;
    check (std::fabs (energy (play (narrow, 60)) - energy (play (wide, 60))) > 1.0e-4f, "pulse width changes the tone");

    auto hp = vaBase();
    hp.part[0].oscWave[0] = 0;
    hp.part[0].oscLevel[1] = 0.0f;
    hp.part[0].vaSub = 0.0f;
    hp.part[0].noise = 0.0f;
    hp.part[0].cutoff = 0.85f;
    hp.part[0].filterType = 2;
    auto lp = hp;
    lp.part[0].filterType = 0;
    check (energy (play (hp, 36)) < energy (play (lp, 36)) * 0.5f, "HP12 removes a low sine");

    auto lp24 = vaBase();
    lp24.part[0].filterType = 1;
    lp24.part[0].cutoff = 0.15f;
    auto lp12 = lp24;
    lp12.part[0].filterType = 0;
    check (std::fabs (energy (play (lp24, 60)) - energy (play (lp12, 60))) > 1.0e-5f, "LP24 is not LP12");

    auto notch = vaBase();
    notch.part[0].filterType = 5;
    notch.part[0].cutoff = 0.40f;
    notch.part[0].resonance = 0.90f;
    const auto notched = play (notch, 64);
    check (notched.finite && notched.peak > 1.0e-4f && notched.peak < 2.0f, "notch stays finite");

    auto band = vaBase();
    band.part[0].filterType = 4;
    band.part[0].resonance = 1.0f;
    const auto banded = play (band, 64);
    check (banded.finite && banded.peak < 2.0f, "BP12 stays finite at full resonance");

    auto serial = vaBase();
    serial.part[0].filterType = 2;
    serial.part[0].cutoff = 0.35f;
    serial.part[0].filter2Mix = 1.0f;
    serial.part[0].filter2Cutoff = 0.25f;
    serial.part[0].filter2Type = 0;
    serial.part[0].filterRoute = 0;
    check (std::fabs (energy (play (serial, 60)) - energy (classic)) > 1.0e-4f, "serial filter 2 changes the mix");
    auto parallel = serial;
    parallel.part[0].filterRoute = 1;
    const float serialEnergy = energy (play (serial, 60));
    const float parallelEnergy = energy (play (parallel, 60));
    check (std::fabs (serialEnergy - parallelEnergy) > 1.0e-4f,
           "parallel is not serial (" + std::to_string (serialEnergy) + " vs " + std::to_string (parallelEnergy) + ")");

    auto enveloped = vaBase();
    enveloped.part[0].filterEnv = 1.0f;
    enveloped.part[0].cutoff = 0.20f;
    enveloped.part[0].attack = 0.001;
    enveloped.part[0].decay = 0.40;
    enveloped.part[0].sustain = 0.0;
    const float moved = energy (play (enveloped, 60, 8000));
    const float still = energy (play (vaBase(), 60, 8000));
    check (std::fabs (moved - still) > 1.0e-4f,
           "filter envelope moves the cutoff (" + std::to_string (moved) + " vs " + std::to_string (still) + ")");

    auto fm = vaBase();
    fm.part[0].fm = 0.80f;
    fm.part[0].osc3Semi = 12.0f;
    fm.part[0].oscWave[2] = 0;
    check (std::fabs (energy (play (fm, 60)) - energy (classic)) > 1.0e-4f, "FM changes the tone");

    auto ring = vaBase();
    ring.part[0].ring = 1.0f;
    check (std::fabs (energy (play (ring, 60)) - energy (classic)) > 1.0e-4f, "ring changes the tone");

    auto tracked = vaBase();
    tracked.part[0].keyTrack = 1.0f;
    tracked.part[0].cutoff = 0.25f;
    check (tracked.part[0].oscWave[0] == 1, "default osc1 stays saw");
    check (std::fabs (energy (play (tracked, 36)) - energy (play (tracked, 84))) > 1.0e-4f, "key track follows the note");

    auto third = vaBase();
    third.part[0].oscLevel[2] = 0.50f;
    third.part[0].oscWave[2] = 1;
    third.part[0].osc3Semi = -12.0f;
    check (std::fabs (energy (play (third, 67)) - energy (classic)) > 1.0e-4f, "osc3 is in the mix");

    auto lead = vaBase();
    lead.part[0].oscLevel[1] = 0.0f;
    lead.part[0].vaSub = 0.0f;
    lead.part[0].noise = 0.0f;
    lead.part[0].cutoff = 0.90f;
    const auto high = play (lead, 96, 4096);
    check (high.finite && high.peak > 1.0e-3f && high.peak < 2.0f, "high saw lead is finite and bounded");

    eng::SynthEngine engine;
    engine.prepare (48000.0, 128);
    auto heavy = vaBase();
    heavy.part[0].oscLevel[2] = 0.20f;
    heavy.part[0].filter2Mix = 1.0f;
    heavy.part[0].filterType = 1;
    heavy.part[0].fm = 0.20f;
    heavy.part[0].ring = 0.10f;
    eng::Event notes[16];
    for (int i = 0; i < 16; ++i)
        notes[i] = midi (static_cast<std::uint32_t> (i), 0x90, static_cast<std::uint8_t> (48 + i), 100);
    float left[128] {};
    float right[128] {};
    engine.process (left, right, 128, notes, 16, heavy);
    for (int i = 0; i < 4; ++i)
        engine.process (left, right, 128, nullptr, 0, heavy);
    voxProbeBegin();
    for (int i = 0; i < 8; ++i)
        engine.process (left, right, 128, nullptr, 0, heavy);
    const auto calls = voxProbeEnd();
    check (calls == 0, "full VA allocates nothing (" + std::to_string (calls) + ")");
    bool finite = true;
    for (int i = 0; i < 128; ++i)
        finite = finite && std::isfinite (left[i]) && std::isfinite (right[i]);
    check (finite, "16-voice 3-osc 2-filter VA is finite");
    check (engine.activeVoices() == 16, "16 VA voices stay inside the cap");
}

void testVaLifecycle()
{
    auto params = vaBase();
    params.part[0].filterEnv = 1.0f;
    params.part[0].cutoff = 0.15f;
    eng::SynthEngine engine;
    engine.prepare (48000.0, 256);
    const eng::Event on = midi (0, 0x90, 60, 100);
    render (engine, 64, &on, 1, params, 64);
    check (engine.vaFilterStage (0, 60) == 1, "filter envelope attacks with the note");

    auto fast = params;
    fast.part[0].filterAttack = 0.001;
    fast.part[0].filterDecay = 0.02;
    fast.part[0].filterSustain = 1.0;
    auto slow = fast;
    slow.part[0].filterAttack = 0.80;
    slow.part[0].filterDecay = 0.80;
    slow.part[0].filterSustain = 0.0;
    const float fastEnergy = energy (play (fast, 60, 3000));
    const float slowEnergy = energy (play (slow, 60, 3000));
    check (std::fabs (fastEnergy - slowEnergy) > 1.0e-4f,
           "filter ADSR is not the amp ADSR (" + std::to_string (fastEnergy) + " vs " + std::to_string (slowEnergy) + ")");

    const eng::Event off = midi (0, 0x80, 60, 0);
    render (engine, 64, &off, 1, params, 64);
    check (engine.vaFilterStage (0, 60) == 4, "note-off releases the filter envelope");

    eng::SynthEngine panic;
    panic.prepare (48000.0, 128);
    render (panic, 64, &on, 1, params, 64);
    const eng::Event soft = midi (0, 0xb0, 123, 0);
    render (panic, 32, &soft, 1, params, 32);
    check (panic.vaFilterStage (0, 60) == 4, "CC123 releases the filter envelope");
    const eng::Event hard = midi (0, 0xb0, 120, 0);
    render (panic, 32, &hard, 1, params, 32);
    check (panic.vaFilterStage (0, 60) == -1, "CC120 clears the voice");

    auto paramsSteal = vaBase();
    eng::Event chord[16];
    for (int i = 0; i < 16; ++i)
        chord[i] = midi (0, 0x90, static_cast<std::uint8_t> (36 + i), 70);
    eng::SynthEngine accented;
    eng::SynthEngine plain;
    accented.prepare (48000.0, 512);
    plain.prepare (48000.0, 512);
    render (accented, 128, chord, 16, paramsSteal, 128);
    render (plain, 128, chord, 16, paramsSteal, 128);
    eng::Event loudOn = midi (0, 0x90, 96, 120);
    loudOn.articulation = eng::articulateAccent;
    const eng::Event softOn = midi (0, 0x90, 96, 120);
    const auto loud = render (accented, 512, &loudOn, 1, paramsSteal, 128);
    const auto quiet = render (plain, 512, &softOn, 1, paramsSteal, 128);
    check (std::fabs (accented.vaAccentGain (0, 96) - 1.35f) < 1.0e-6f, "stolen accent survives the fade");
    check (std::fabs (plain.vaAccentGain (0, 96) - 1.0f) < 1.0e-6f, "unaccented steal stays at unity");
    paramsSteal.part[0].release = 0.02;
    eng::Event rest[15];
    for (int i = 0; i < 15; ++i)
        rest[i] = midi (0, 0x80, static_cast<std::uint8_t> (37 + i), 0);
    render (accented, 4000, rest, 15, paramsSteal, 128);
    render (plain, 4000, rest, 15, paramsSteal, 128);
    const auto loudLeft = render (accented, 512, nullptr, 0, paramsSteal, 128);
    const auto quietLeft = render (plain, 512, nullptr, 0, paramsSteal, 128);
    double loudEnergy = 0.0;
    double quietEnergy = 0.0;
    for (float sample : loudLeft.left)
        loudEnergy += static_cast<double> (sample) * sample;
    for (float sample : quietLeft.left)
        quietEnergy += static_cast<double> (sample) * sample;
    check (loudEnergy > quietEnergy * 1.2,
           "accented stolen note is louder (" + std::to_string (loudEnergy) + " vs " + std::to_string (quietEnergy) + ")");
    const eng::Event release = midi (0, 0x80, 96, 0);
    render (accented, 64, &release, 1, paramsSteal, 64);
    check (accented.vaFilterStage (0, 96) == 4, "stolen note still releases");
}

void testMod()
{
    auto plain = vaBase();
    plain.part[0].cutoff = 0.20f;
    auto moved = plain;
    moved.part[0].lfoRate[0] = 12.0f;
    moved.part[0].lfoShape[0] = 0;
    moved.part[0].routeSource[0] = 1;
    moved.part[0].routeDest[0] = 1;
    moved.part[0].routeAmount[0] = 1.0f;
    check (std::fabs (energy (play (moved, 48, 4096)) - energy (play (plain, 48, 4096))) > 1.0e-4f,
           "LFO route moves the cutoff");

    auto silent = moved;
    silent.part[0].routeAmount[0] = 0.0f;
    const auto left = play (plain, 48, 1024);
    const auto right = play (silent, 48, 1024);
    float worst = 0.0f;
    for (std::size_t i = 0; i < left.left.size(); ++i)
        worst = std::max (worst, std::fabs (left.left[i] - right.left[i]));
    check (worst < 1.0e-6f, "a zero route does not change the sound");

    auto macro = plain;
    macro.macro[0] = 1.0f;
    macro.part[0].routeSource[0] = 5;
    macro.part[0].routeDest[0] = 1;
    macro.part[0].routeAmount[0] = 1.0f;
    check (std::fabs (energy (play (macro, 48, 2048)) - energy (play (plain, 48, 2048))) > 1.0e-4f,
           "macro route moves the cutoff");
}

void testVaQuality()
{
    auto heavy = vaBase();
    heavy.part[0].oscLevel[2] = 0.25f;
    heavy.part[0].fm = 0.35f;
    heavy.part[0].ring = 0.20f;
    heavy.part[0].filter2Mix = 1.0f;
    heavy.part[0].filter2Cutoff = 0.35f;
    heavy.part[0].filter2Resonance = 0.75f;
    heavy.part[0].resonance = 0.65f;
    heavy.part[0].cutoff = 0.55f;
    const double rates[] = { 44100.0, 48000.0, 96000.0 };
    for (double rate : rates)
    {
        eng::SynthEngine engine;
        engine.prepare (rate, 128);
        eng::Event notes[16];
        for (int i = 0; i < 16; ++i)
            notes[i] = midi (static_cast<std::uint32_t> (i), 0x90, static_cast<std::uint8_t> (36 + i * 3), 100);
        float left[32] {};
        float right[32] {};
        engine.process (left, right, 32, notes, 16, heavy);
        const int blocks = std::max (8, static_cast<int> (rate * 0.20 / 32.0));
        bool finite = true;
        float peak = 0.0f;
        voxProbeBegin();
        const auto started = std::chrono::steady_clock::now();
        for (int block = 0; block < blocks; ++block)
        {
            if (block == blocks / 2)
            {
                heavy.part[0].filterType = 4;
                heavy.part[0].oscWave[0] = 4;
                heavy.part[0].oscPulse[0] = 0.2f;
            }
            engine.process (left, right, 32, nullptr, 0, heavy);
            for (int i = 0; i < 32; ++i)
            {
                finite = finite && std::isfinite (left[i]) && std::isfinite (right[i]);
                peak = std::max (peak, std::fabs (left[i]));
            }
        }
        const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - started).count();
        const auto calls = voxProbeEnd();
        const double audioMs = 1000.0 * 32.0 * static_cast<double> (blocks) / rate;
        std::printf ("va load %.0f Hz: %.2f ms for %.0f ms audio, mallocs %d\n", rate, ms, audioMs, calls);
        check (calls == 0, "loaded VA allocates nothing at " + std::to_string (rate));
        check (finite && peak > 1.0e-4f && peak < 1.2f, "loaded VA stays finite at " + std::to_string (rate));
        check (ms < audioMs, "loaded VA is faster than realtime at " + std::to_string (rate));
    }

    auto lead = vaBase();
    lead.part[0].oscLevel[1] = 0.0f;
    lead.part[0].vaSub = 0.0f;
    lead.part[0].noise = 0.0f;
    lead.part[0].cutoff = 1.0f;
    lead.part[0].resonance = 0.0f;
    eng::SynthEngine pitch;
    pitch.prepare (48000.0, 256);
    const eng::Event a440 = midi (0, 0x90, 69, 100);
    const auto tone = render (pitch, 4800, &a440, 1, lead, 128);
    int crossings = 0;
    for (std::size_t i = 1; i < tone.left.size(); ++i)
        if ((tone.left[i - 1] <= 0.0f && tone.left[i] > 0.0f) || (tone.left[i - 1] >= 0.0f && tone.left[i] < 0.0f))
            ++crossings;
    check (crossings > 70 && crossings < 120, "A440 saw stays near 440 Hz (" + std::to_string (crossings) + " crossings)");

    auto highpass = lead;
    highpass.part[0].oscWave[0] = 0;
    highpass.part[0].filterType = 2;
    highpass.part[0].cutoff = 0.80f;
    const auto hp = play (highpass, 48, 2048);
    double mean = 0.0;
    for (float sample : hp.left)
        mean += sample;
    mean /= static_cast<double> (hp.left.size());
    check (hp.finite && std::fabs (mean) < 0.02, "high-pass VA does not sit on a DC offset");

    eng::SynthEngine whole;
    eng::SynthEngine split;
    whole.prepare (48000.0, 256);
    split.prepare (48000.0, 256);
    float wholeL[256] {};
    float wholeR[256] {};
    float splitL[128] {};
    float splitR[128] {};
    float splitL2[128] {};
    float splitR2[128] {};
    const eng::Event edge = midi (0, 0x90, 76, 110);
    whole.process (wholeL, wholeR, 256, &edge, 1, heavy);
    split.process (splitL, splitR, 128, &edge, 1, heavy);
    split.process (splitL2, splitR2, 128, nullptr, 0, heavy);
    float splitWorst = 0.0f;
    for (int i = 0; i < 128; ++i)
    {
        splitWorst = std::max (splitWorst, std::fabs (wholeL[i] - splitL[i]));
        splitWorst = std::max (splitWorst, std::fabs (wholeL[128 + i] - splitL2[i]));
    }
    check (splitWorst < 1.0e-5f, "block splits do not move the phase");
}

void testLfoPolicy()
{
    auto params = vaBase();
    params.part[0].lfoRate[0] = 4.0f;
    params.part[0].routeSource[0] = 1;
    params.part[0].routeDest[0] = 1;
    params.part[0].routeAmount[0] = 0.5f;
    params.part[0].cutoff = 0.40f;
    eng::SynthEngine alone;
    eng::SynthEngine extra;
    alone.prepare (48000.0, 256);
    extra.prepare (48000.0, 256);
    const eng::Event first = midi (0, 0x90, 60, 100);
    const eng::Event second = midi (0, 0x90, 67, 100);
    render (alone, 2000, &first, 1, params, 128);
    render (extra, 2000, &first, 1, params, 128);
    render (extra, 2000, &second, 1, params, 128);
    render (alone, 2000, nullptr, 0, params, 128);
    check (std::fabs (alone.lfoPhase (0, 0) - extra.lfoPhase (0, 0)) < 1.0e-6,
           "a second note does not restart the free-running LFO");
    check (std::fabs (alone.modCutoff (0) - extra.modCutoff (0)) < 1.0e-5f,
           "the cutoff trajectory survives the second note");

    auto retrig = params;
    retrig.part[0].lfoRetrig[0] = true;
    eng::SynthEngine perNote;
    perNote.prepare (48000.0, 256);
    render (perNote, 4000, &first, 1, retrig, 128);
    const double moved = perNote.vaRetrigPhase (0, 60, 0);
    render (perNote, 64, &second, 1, retrig, 128);
    check (moved > 0.05, "the held voice has its own phase");
    check (perNote.vaRetrigPhase (0, 60, 0) + 1.0e-4 > moved, "the held voice is not restarted");
    check (perNote.vaRetrigPhase (0, 67, 0) < 0.02, "the new voice starts at its own phase");

    eng::Event chord[15];
    chord[0] = midi (0, 0x90, 36, 80);
    chord[1] = midi (1, 0x90, 60, 100);
    for (int i = 2; i < 15; ++i)
        chord[i] = midi (static_cast<std::uint32_t> (i), 0x90, static_cast<std::uint8_t> (40 + i), 80);
    eng::SynthEngine stolen;
    eng::SynthEngine steady;
    stolen.prepare (48000.0, 512);
    steady.prepare (48000.0, 512);
    render (stolen, 256, chord, 15, params, 128);
    render (steady, 256, chord, 15, params, 128);
    const eng::Event thief = midi (0, 0x90, 96, 110);
    render (stolen, 512, &thief, 1, params, 128);
    render (steady, 512, nullptr, 0, params, 128);
    check (std::fabs (stolen.lfoPhase (0, 0) - steady.lfoPhase (0, 0)) < 1.0e-6, "voice steal does not restart the part LFO");
    check (stolen.vaNoteHeld (0, 60), "the held note is not the stolen one");
    check (! stolen.vaNoteHeld (0, 36), "the oldest note was the one stolen");

    auto psy = defaults();
    psy.part[0].lfoRate[0] = 3.0f;
    eng::SynthEngine psyOn;
    eng::SynthEngine psyOff;
    psyOn.prepare (48000.0, 128);
    psyOff.prepare (48000.0, 128);
    render (psyOn, 1000, &first, 1, psy, 128);
    render (psyOff, 1000, nullptr, 0, psy, 128);
    check (std::fabs (psyOn.lfoPhase (0, 0) - psyOff.lfoPhase (0, 0)) < 1.0e-6, "Psy notes do not reset the LFO");
    auto acid = psy;
    acid.part[0].engine = eng::EngineKind::acid;
    eng::SynthEngine acidOn;
    eng::SynthEngine acidOff;
    acidOn.prepare (48000.0, 128);
    acidOff.prepare (48000.0, 128);
    render (acidOn, 1000, &first, 1, acid, 128);
    render (acidOff, 1000, nullptr, 0, acid, 128);
    check (std::fabs (acidOn.lfoPhase (0, 0) - acidOff.lfoPhase (0, 0)) < 1.0e-6, "Acid notes do not reset the LFO");

    auto clock = vaBase();
    clock.part[0].lfoSync[0] = true;
    clock.part[0].lfoDiv[0] = 2;
    clock.bpm = 120.0;
    clock.bpmValid = true;
    eng::SynthEngine synced;
    synced.prepare (48000.0, 256);
    render (synced, 12000, nullptr, 0, clock, 256);
    check (std::fabs (synced.lfoPhase (0, 0) - 0.5) < 1.0e-4, "quarter-note sync at 120 bpm is 2 Hz");
    clock.bpmValid = false;
    eng::SynthEngine fallback;
    fallback.prepare (48000.0, 256);
    render (fallback, 12000, nullptr, 0, clock, 256);
    check (std::fabs (fallback.lfoPhase (0, 0) - 0.5) < 1.0e-4, "a missing clock falls back to 120 bpm");
    clock.bpm = 60.0;
    clock.bpmValid = true;
    eng::SynthEngine half;
    half.prepare (48000.0, 256);
    render (half, 12000, nullptr, 0, clock, 256);
    check (std::fabs (half.lfoPhase (0, 0) - 0.25) < 1.0e-4, "quarter-note sync follows the host bpm");

    eng::SynthEngine whole;
    eng::SynthEngine split;
    whole.prepare (48000.0, 256);
    split.prepare (48000.0, 256);
    float wholeL[256] {};
    float splitL[128] {};
    float splitL2[128] {};
    float ignored[256] {};
    whole.process (wholeL, ignored, 256, &first, 1, params);
    split.process (splitL, ignored, 128, &first, 1, params);
    split.process (splitL2, ignored, 128, nullptr, 0, params);
    float worst = 0.0f;
    for (int i = 0; i < 128; ++i)
    {
        worst = std::max (worst, std::fabs (wholeL[i] - splitL[i]));
        worst = std::max (worst, std::fabs (wholeL[128 + i] - splitL2[i]));
    }
    check (worst < 1.0e-5f, "LFO modulation is independent of the block size");
    check (std::fabs (whole.lfoPhase (0, 0) - split.lfoPhase (0, 0)) < 1.0e-6, "LFO phase is independent of the block size");
}

void testUnisonTable()
{
    check (vox::syn::engine::wavetableBytes() == 256 * 2048 * 4, "wavetable is 256 x 2048");
    auto plain = vaBase();
    auto table = plain;
    table.part[0].wtLevel = 1.0f;
    table.part[0].wtPos = 0.8f;
    check (std::fabs (energy (play (plain, 52, 2048)) - energy (play (table, 52, 2048))) > 1.0e-4f, "wavetable changes the sound");
    auto other = table;
    other.part[0].wtPos = 0.05f;
    check (std::fabs (energy (play (table, 52, 2048)) - energy (play (other, 52, 2048))) > 1.0e-4f, "wavetable frames are different");
    auto high = table;
    const auto top = play (high, 108, 2048);
    check (top.finite && top.peak < 1.5f && top.peak > 1.0e-4f, "a high wavetable note stays bounded");

    auto wide = vaBase();
    wide.part[0].unison = 4;
    wide.part[0].unisonDetune = 12.0f;
    wide.part[0].unisonSpread = 1.0f;
    wide.part[0].wtLevel = 0.4f;
    eng::SynthEngine engine;
    engine.prepare (48000.0, 256);
    const eng::Event on = midi (0, 0x90, 64, 110);
    const auto stereo = render (engine, 512, &on, 1, wide, 128);
    check (engine.vaActive (0) == 4, "one unison note costs four voices");
    check (stereo.side > 1.0e-4f, "unison spread is stereo");
    auto center = wide;
    center.part[0].unisonSpread = 0.0f;
    const auto mono = play (center, 64, 512);
    check (mono.side < 1.0e-6f, "spread zero stays mono");

    const eng::Event off = midi (0, 0x80, 64, 0);
    render (engine, 64, &off, 1, wide, 64);
    check (engine.vaFilterStage (0, 64) == 4, "unison note-off releases every copy");
    eng::SynthEngine panic;
    panic.prepare (48000.0, 128);
    render (panic, 64, &on, 1, wide, 64);
    const eng::Event hard = midi (0, 0xb0, 120, 0);
    render (panic, 32, &hard, 1, wide, 32);
    check (panic.vaActive (0) == 0, "CC120 clears unison voices");

    eng::SynthEngine full;
    full.prepare (48000.0, 128);
    eng::Event stack[5];
    for (int i = 0; i < 5; ++i)
        stack[i] = midi (static_cast<std::uint32_t> (i), 0x90, static_cast<std::uint8_t> (48 + i), 100);
    render (full, 256, stack, 5, wide, 128);
    check (full.activeVoices() == 16, "unison stays inside the shared 16-voice pool");
    float scratch[128] {};
    full.process (scratch, scratch, 128, nullptr, 0, wide);
    voxProbeBegin();
    full.process (scratch, scratch, 128, nullptr, 0, wide);
    const auto calls = voxProbeEnd();
    check (calls == 0, "wavetable and unison allocate nothing in the callback (" + std::to_string (calls) + ")");

    auto routed = vaBase();
    routed.part[0].cutoff = 0.15f;
    routed.macro[7] = 1.0f;
    routed.part[0].routeSource[31] = 12;
    routed.part[0].routeDest[31] = 1;
    routed.part[0].routeAmount[31] = 1.0f;
    auto quiet = routed;
    quiet.part[0].routeAmount[31] = 0.0f;
    check (std::fabs (energy (play (routed, 48, 1024)) - energy (play (quiet, 48, 1024))) > 1.0e-4f,
           "route 32 reaches the cutoff");
}

} // namespace

int main()
{
    testTailBound();
    testBothParts();
    testGlidePhase();
    testSlidePhaseAndClick();
    testSlideTimeDiffersFromPortamento();
    testAcidResume();
    testVoiceCap();
    testMidi();
    testSwitchAndRates();
    testSameKeyStack();
    testAllocation();
    testGolden();
    testVa();
    testVaLifecycle();
    testMod();
    testVaQuality();
    testLfoPolicy();
    testUnisonTable();
    if (failures == 0 && skips == 0)
        std::printf ("synth engine tests passed (%d checks, 0 skipped)\n", checks);
    else
        std::printf ("synth engine tests failed (%d checks, %d failed, %d skipped)\n", checks, failures, skips);
    return failures == 0 && skips == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
