#include "kick/KickSynth.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

static int testsRun = 0;
static int testsPassed = 0;

#define require(cond, msg)                                                   \
    do {                                                                     \
        ++testsRun;                                                          \
        if (!(cond)) {                                                       \
            std::cerr << "FAIL: " << msg << " at line " << __LINE__ << "\n"; \
            std::exit(EXIT_FAILURE);                                         \
        }                                                                    \
        ++testsPassed;                                                       \
    } while (0)

namespace {

constexpr double sr = 44100.0;

// Minimal dry signal: pure body oscillator, no click/sub/transient shaping,
// no saturation, no tail. Individual engine stages are re-enabled per test.
vstengine::kick::KickParams cleanParams()
{
    vstengine::kick::KickParams p;
    p.click = 0.0f;
    p.sub = 0.0f;
    p.transient = 0.0f;
    p.drive = 1.0f;
    p.clip = 1.0f;
    p.tail = 0.0f;
    p.outputLevel = 1.0f;
    return p;
}

std::vector<float> renderTrigger(const vstengine::kick::KickParams& params,
                                 float velocity = 1.0f, int note = 60,
                                 int samples = 44100)
{
    vstengine::kick::KickSynth kick;
    kick.prepare(sr);
    kick.setParameters(params);
    juce::AudioBuffer<float> buffer(1, samples);
    buffer.clear();
    kick.trigger(velocity, note, 0);
    kick.render(buffer, samples);
    return { buffer.getReadPointer(0),
             buffer.getReadPointer(0) + samples };
}

double energyBetween(const std::vector<float>& v, int from, int to)
{
    double sum = 0.0;
    const int last = static_cast<int>(v.size());
    for (int i = from; i < to && i < last; ++i)
        sum += static_cast<double>(v[i]) * static_cast<double>(v[i]);
    return sum;
}

int zeroCrossingsBetween(const std::vector<float>& v, int from, int to)
{
    int crossings = 0;
    const int last = static_cast<int>(v.size()) - 1;
    for (int i = from; i < to && i < last; ++i)
        if ((v[i] <= 0.0f && v[i + 1] > 0.0f)
            || (v[i] >= 0.0f && v[i + 1] < 0.0f))
            ++crossings;
    return crossings;
}

} // namespace

int main()
{
    using vstengine::kick::KickParams;
    using vstengine::kick::KickSynth;

    // 1: Deterministic render - identical triggers produce identical output.
    {
        const auto p = cleanParams();
        const auto a = renderTrigger(p);
        const auto b = renderTrigger(p);
        require(a.size() == b.size(), "determinism: same length");
        bool identical = true;
        for (std::size_t i = 0; i < a.size(); ++i)
            if (std::abs(a[i] - b[i]) > 1e-6f) {
                identical = false;
                break;
            }
        require(identical, "determinism: sample-identical output");
    }

    // 2: Deterministic phase reset - Phase 90 deg starts positive, 270 deg
    //    starts negative (first body sample is the oscillator start phase).
    {
        auto p = cleanParams();
        p.pitchStart = 0.0f;
        p.phase = 90.0f;
        const auto up = renderTrigger(p, 1.0f, 60, 64);
        p.phase = 270.0f;
        const auto down = renderTrigger(p, 1.0f, 60, 64);
        require(up[0] > 0.5f, "phase 90 starts positive");
        require(down[0] < -0.5f, "phase 270 starts negative");
    }

    // 3: Pitch sweep - the tone falls from Pitch Start to Pitch End.
    {
        auto p = cleanParams();
        p.pitchStart = 24.0f;
        p.pitchDecay = 0.05f;
        p.bodyDecay = 0.5f;
        const auto out = renderTrigger(p, 1.0f, 60, 22050);
        const int early = zeroCrossingsBetween(out, 0, 4410);
        const int late = zeroCrossingsBetween(out, 19845, 22050);
        require(early > late * 2, "pitch sweep: early tone clearly higher");
    }

    // 4: Body decay - amplitude ~exp(-t/bodyDecay); after 3 time constants
    //    the body is below 12% of its initial peak.
    {
        auto p = cleanParams();
        p.pitchStart = 0.0f;
        p.bodyDecay = 0.1f;
        const auto out = renderTrigger(p, 1.0f, 60, 22050);
        float peak = 0.0f;
        for (int i = 0; i < 220; ++i)
            peak = std::max(peak, std::abs(out[i]));
        float late = 0.0f;
        for (int i = 12778; i < 13671; ++i) // 0.29 .. 0.31 s
            late = std::max(late, std::abs(out[i]));
        require(peak > 0.2f, "body decay: audible initial body");
        require(late < 0.12f * peak, "body decay: exp decay reached");
    }

    // 5: Tail - the sub layer rings with the Tail time constant.
    {
        auto p = cleanParams();
        p.pitchStart = 0.0f;
        p.bodyDecay = 0.05f;
        p.sub = 1.0f;
        p.tail = 0.01f;
        const auto shortTail = renderTrigger(p);
        p.tail = 1.0f;
        const auto longTail = renderTrigger(p);
        const double eShort = energyBetween(shortTail, 17640, 26460);
        const double eLong = energyBetween(longTail, 17640, 26460);
        require(eLong > 1e-4, "tail: audible sub tail");
        require(eLong > 10.0 * eShort, "tail: longer tail rings longer");
    }

    // 6: Click - the attack transient adds early energy and stays bounded.
    {
        auto p = cleanParams();
        p.pitchStart = 0.0f;
        p.bodyDecay = 0.3f;
        p.click = 0.0f;
        const auto noClick = renderTrigger(p);
        p.click = 1.0f;
        const auto click = renderTrigger(p);
        require(energyBetween(click, 0, 882) > energyBetween(noClick, 0, 882),
                "click: adds early energy");
        bool bounded = true;
        for (const auto sample : click)
            if (std::abs(sample) > 2.0f) {
                bounded = false;
                break;
            }
        require(bounded, "click: bounded output");
    }

    // 7: Clip - hard ceiling is respected.
    {
        auto p = cleanParams();
        p.pitchStart = 0.0f;
        p.bodyDecay = 0.2f;
        p.click = 1.0f;
        p.drive = 8.0f;
        p.clip = 0.2f;
        const auto clipped = renderTrigger(p);
        bool bounded = true;
        for (const auto sample : clipped)
            if (std::abs(sample) > 0.2f + 1e-3f) {
                bounded = false;
                break;
            }
        require(bounded, "clip: ceiling respected");
    }

    // 8: Drive - saturation changes the waveform but stays finite.
    {
        auto p = cleanParams();
        p.pitchStart = 0.0f;
        p.bodyDecay = 0.3f;
        p.click = 0.3f;
        const auto clean = renderTrigger(p);
        p.drive = 8.0f;
        const auto driven = renderTrigger(p);
        bool differ = false;
        bool finite = true;
        for (std::size_t i = 0; i < clean.size(); ++i) {
            if (std::abs(clean[i] - driven[i]) > 1e-4f)
                differ = true;
            if (!std::isfinite(clean[i]) || !std::isfinite(driven[i]))
                finite = false;
        }
        require(differ, "drive: saturates the signal");
        require(finite, "drive: finite output");
    }

    // 9: Transient - soft attack ramps in, instant attack is immediate.
    {
        auto p = cleanParams();
        p.pitchStart = 0.0f;
        p.bodyDecay = 0.3f;
        p.phase = 90.0f; // start at waveform crest so amplitude is direct
        p.transient = 0.0f;
        const auto hard = renderTrigger(p);
        p.transient = 1.0f;
        const auto soft = renderTrigger(p);
        require(std::abs(hard[4]) > 0.3f, "transient 0: immediate attack");
        require(std::abs(soft[4]) < 0.05f, "transient 1: ramped attack");
    }

    // 10: Sub - the sub-octave layer exists and follows the tail envelope.
    {
        auto p = cleanParams();
        p.pitchStart = 0.0f;
        p.bodyDecay = 0.02f;
        p.tail = 1.0f;
        p.sub = 0.0f;
        const auto noSub = renderTrigger(p);
        p.sub = 1.0f;
        const auto withSub = renderTrigger(p);
        const double eNoSub = energyBetween(noSub, 17640, 22050);
        const double eSub = energyBetween(withSub, 17640, 22050);
        require(eSub > 10.0 * eNoSub,
                "sub: late energy only present with sub layer");
    }

    // 11: Tune - +12 semitones roughly doubles the zero-crossing rate.
    {
        auto p = cleanParams();
        p.pitchStart = 0.0f;
        p.bodyDecay = 0.6f;
        p.tune = 36.0f;
        const auto low = renderTrigger(p);
        p.tune = 48.0f;
        const auto high = renderTrigger(p);
        const int lowCross = zeroCrossingsBetween(low, 22050, 33075);
        const int highCross = zeroCrossingsBetween(high, 22050, 33075);
        require(lowCross > 0, "tune: measurable low tone");
        require(highCross > lowCross * 3 / 2 && highCross < lowCross * 5 / 2,
                "tune: +12 st ~ doubles frequency");
    }

    // 12: Velocity - output scales with note velocity.
    {
        const auto p = cleanParams();
        const auto loud = renderTrigger(p, 1.0f);
        const auto quiet = renderTrigger(p, 0.5f);
        const float ratio =
            std::abs(loud[8]) / std::max(1e-6f, std::abs(quiet[8]));
        require(ratio > 1.7f && ratio < 2.3f,
                "velocity: amplitude scales with velocity");
    }

    // 13: Extreme parameters never produce NaN/Inf or runaway levels.
    {
        KickParams p;
        p.pitchStart = 36.0f;
        p.pitchEnd = 24.0f;
        p.pitchDecay = 0.5f;
        p.pitchCurve = 8.0f;
        p.bodyDecay = 2.0f;
        p.tail = 4.0f;
        p.click = 1.0f;
        p.clickTone = 1.0f;
        p.drive = 10.0f;
        p.clip = 0.1f;
        p.transient = 1.0f;
        p.sub = 1.0f;
        p.tune = 48.0f;
        p.phase = 360.0f;
        p.outputLevel = 2.0f;

        KickSynth kick;
        kick.prepare(sr);
        kick.setParameters(p);
        juce::AudioBuffer<float> buffer(2, 512);
        for (int block = 0; block < 400; ++block) { // ~4.6 s
            buffer.clear();
            if (block == 0)
                kick.trigger(1.0f, 60, 0);
            kick.render(buffer, 512);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i) {
                    const float s = buffer.getReadPointer(ch)[i];
                    require(std::isfinite(s), "extreme params: finite");
                    require(std::abs(s) <= 0.21f,
                            "extreme params: clip + level bounded");
                }
        }
        // Voice length for these params is capped at 30 s by design
        // ((body+tail)*8 = 48 s -> 30 s clamp), so it is still ringing after
        // 4.6 s; the hard-stop itself is proven by test 15.
        require(kick.isActive(), "extreme params: long capped tail rings");
    }

    // 14: Release - panic fast-fades the voice to silence (no stuck tail).
    {
        auto p = cleanParams();
        p.pitchStart = 0.0f;
        p.bodyDecay = 2.0f; // long body: release must still kill it

        KickSynth kick;
        kick.prepare(sr);
        kick.setParameters(p);
        juce::AudioBuffer<float> buffer(1, 2000);
        buffer.clear();
        kick.trigger(1.0f, 60, 0);
        kick.render(buffer, 100);
        require(kick.isActive(), "release: voice sounding before panic");
        kick.release(0);
        kick.render(buffer, 2000);
        const auto* data = buffer.getReadPointer(0);
        double lateEnergy = 0.0;
        for (int i = 300; i < 2000; ++i)
            lateEnergy += static_cast<double>(data[i]) * data[i];
        require(lateEnergy < 1e-9, "release: silence after fade");
        require(!kick.isActive(), "release: voice inactive");
    }

    // 15: Voice auto-stop - the one-shot ends by itself (no stuck notes).
    {
        auto p = cleanParams();
        p.pitchStart = 0.0f;
        p.bodyDecay = 0.05f;

        KickSynth kick;
        kick.prepare(sr);
        kick.setParameters(p);
        juce::AudioBuffer<float> buffer(1, 22050);
        buffer.clear();
        kick.trigger(1.0f, 60, 0);
        kick.render(buffer, 22050);
        require(!kick.isActive(), "voice hard-stop: inactive after cap");
    }

    // Panic queued after trigger in same block must fade new one-shot.
    {
        auto p = cleanParams();
        p.bodyDecay = 2.0f;
        KickSynth kick;
        kick.prepare(sr);
        kick.setParameters(p);
        juce::AudioBuffer<float> buffer(1, 2000);
        buffer.clear();
        kick.trigger(1.0f, 60, 0);
        kick.release(100);
        kick.render(buffer, 2000);
        require(!kick.isActive(), "same-block panic stops queued trigger");
    }

    // 16: Retrigger - a second note-on restarts the deterministic attack.
    {
        auto p = cleanParams();
        p.pitchStart = 0.0f;
        p.phase = 90.0f;

        const auto fresh = renderTrigger(p, 1.0f, 60, 256);

        KickSynth kick;
        kick.prepare(sr);
        kick.setParameters(p);
        juce::AudioBuffer<float> buffer(1, 1024);
        buffer.clear();
        kick.trigger(1.0f, 60, 0);
        kick.render(buffer, 512);
        require(kick.isActive(), "retrigger: voice sounding");
        kick.trigger(1.0f, 60, 0);
        // render() always writes from buffer index 0 (one call per block in
        // the plugin) and MIXES into the buffer, so clear the previous block
        // first; the restarted voice must then reproduce the fresh voice's
        // very first sample exactly.
        buffer.clear();
        kick.render(buffer, 512);
        require(std::abs(buffer.getReadPointer(0)[0] - fresh[0]) < 1e-5f,
                "retrigger: deterministic phase restart");
    }

    // 17: Sample-accurate trigger offset.
    {
        auto p = cleanParams();
        p.pitchStart = 0.0f;
        p.phase = 90.0f;

        KickSynth kick;
        kick.prepare(sr);
        kick.setParameters(p);
        juce::AudioBuffer<float> buffer(1, 256);
        buffer.clear();
        kick.trigger(1.0f, 60, 100);
        kick.render(buffer, 256);
        const auto* data = buffer.getReadPointer(0);
        bool silentBefore = true;
        for (int i = 0; i < 100; ++i)
            if (std::abs(data[i]) > 1e-9f)
                silentBefore = false;
        require(silentBefore, "trigger offset: silent before onset");
        require(std::abs(data[100]) > 0.3f,
                "trigger offset: audible exactly at onset");
    }

    std::cout << "KickSynth tests passed (" << testsPassed << "/" << testsRun
              << ")\n";
    return EXIT_SUCCESS;
}
