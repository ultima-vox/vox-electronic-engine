// SYN-01 engine tests. No JUCE, no Rack, no product shell.
// Both parts sound, engines switch, glide does not snap the phase, acid
// resumes the held note, the 16-voice cap holds, MIDI CC behaves, and
// process() allocates nothing.
#include "engine/SynthEngine.h"
#include "GoldenRender.h"

#ifdef VOX_TEST_INTERPOSE_MALLOC
#include "AllocationProbe.h"
#endif

#include <algorithm>
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
    if (failures == 0 && skips == 0)
        std::printf ("synth engine tests passed (%d checks, 0 skipped)\n", checks);
    else
        std::printf ("synth engine tests failed (%d checks, %d failed, %d skipped)\n", checks, failures, skips);
    return failures == 0 && skips == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
