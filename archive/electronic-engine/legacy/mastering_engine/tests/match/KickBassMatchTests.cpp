#include "match/KickBassMatch.h"
#include <cmath>
#include <cstdlib>
#include <iostream>

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

vstengine::kick::KickParams overlappingKick()
{
    vstengine::kick::KickParams k;
    k.pitchStart = 0.0f;   // stable tone: dominant == Tune
    k.pitchEnd = 0.0f;
    k.pitchDecay = 0.05f;
    k.pitchCurve = 2.0f;
    k.bodyDecay = 0.2f;
    k.tail = 0.4f;         // long tail on top of the bass region
    k.click = 0.3f;
    k.clickTone = 0.5f;
    k.drive = 1.5f;
    k.clip = 1.0f;
    k.transient = 0.5f;
    k.sub = 0.5f;          // sub layer sits right on the bass
    k.tune = 36.0f;        // C2 ~65 Hz
    k.phase = 0.0f;
    k.outputLevel = 1.0f;
    return k;
}

vstengine::match::BassRenderParams overlappingBass()
{
    vstengine::match::BassRenderParams b;
    b.drive = 1.8f;
    b.release = 0.1f;
    b.ampAttack = 0.001f;
    b.ampDecay = 0.1f;
    b.ampSustain = 0.6f;
    b.filterCutoff = 0.6f;
    b.filterResonance = 0.6f;
    b.filterDrive = 1.2f;
    b.keyTracking = 0.2f;
    b.pitchEnvAmount = 0.0f;
    b.pitchEnvTime = 0.018f;
    b.pitchEnvCurve = 2.0f;
    b.outputLevel = 1.0f;
    b.midiNote = 36;       // same octave as the kick -> strong overlap
    return b;
}

} // namespace

int main()
{
    using vstengine::kick::KickParams;
    using vstengine::match::BassRenderParams;
    using vstengine::match::KickBassMatch;

    // Known-tone regression: count full periods (rising crossings only).
    for (const double hz : { 50.0, 65.406, 80.0, 100.0 }) {
        std::vector<float> sine(static_cast<std::size_t>(sr * 2.0));
        for (std::size_t i = 0; i < sine.size(); ++i)
            sine[i] = static_cast<float>(std::sin(
                juce::MathConstants<double>::twoPi * hz
                * static_cast<double>(i) / sr));
        const auto measured = KickBassMatch::dominantHz(
            sine, 0, static_cast<int>(sine.size()), sr);
        require(std::abs(measured - hz) / hz < 0.025,
                "dominant: known sine within 2.5 percent");
    }

    // 1: Determinism - identical inputs, identical report.
    {
        const auto k = overlappingKick();
        const auto b = overlappingBass();
        const auto a = KickBassMatch::analyze(k, b, sr);
        const auto c = KickBassMatch::analyze(k, b, sr);
        require(a.spectralOverlap == c.spectralOverlap,
                "determinism: identical overlap");
        require(a.kickTailSeconds == c.kickTailSeconds,
                "determinism: identical tail");
        require(a.adjustments.bassTimingOffsetMs
                    == c.adjustments.bassTimingOffsetMs,
                "determinism: identical timing suggestion");
    }

    // 2: Kick tail follows the Tail parameter (longer tail -> longer tail).
    {
        auto k = overlappingKick();
        k.tail = 0.05f;
        const auto shortReport =
            KickBassMatch::analyze(k, overlappingBass(), sr);
        k.tail = 0.9f;
        const auto longReport =
            KickBassMatch::analyze(k, overlappingBass(), sr);
        require(longReport.kickTailSeconds > shortReport.kickTailSeconds,
                "tail: longer Tail parameter -> longer measured tail");
    }

    // 3: Kick dominant region tracks Tune (C2 ~65 Hz).
    {
        auto k = overlappingKick();
        const auto report =
            KickBassMatch::analyze(k, overlappingBass(), sr);
        require(report.kickDominantHz > 20.0 && report.kickDominantHz < 140.0,
                "dominant: low sub/bass band for C2 kick");
        k.tune = 48.0f;
        const auto highReport =
            KickBassMatch::analyze(k, overlappingBass(), sr);
        require(highReport.kickDominantHz > report.kickDominantHz,
                "dominant: higher Tune raises dominant region");
    }

    // 4: Bass onset is early (attack ~1 ms) and bounded.
    {
        const auto report =
            KickBassMatch::analyze(overlappingKick(), overlappingBass(), sr);
        require(report.bassOnsetSeconds > 0.0
                    && report.bassOnsetSeconds < 0.12,
                "onset: bass attack is early and bounded");
    }

    // 5: High-overlap material triggers bounded matching adjustments and the
    //    exit gate proves the overlap measurably decreases (no muting).
    {
        const auto report =
            KickBassMatch::analyze(overlappingKick(), overlappingBass(), sr);
        require(report.spectralOverlap > 0.15,
                "exit gate: test material has meaningful overlap");
        require(report.spectralOverlapAfter < report.spectralOverlap - 0.02,
                "exit gate: bounded match measurably reduces overlap");
        require(report.overlapImproves,
                "exit gate: overlapImproves flag agrees");
        require(report.spectralOverlapAfter >= 0.0,
                "exit gate: overlap never negative");
    }

    // 6: Adjustments never leave their bounds, even for extreme inputs.
    {
        KickParams k;
        k.pitchStart = 36.0f;
        k.pitchEnd = 24.0f;
        k.pitchDecay = 0.5f;
        k.pitchCurve = 8.0f;
        k.bodyDecay = 2.0f;
        k.tail = 4.0f;
        k.click = 1.0f;
        k.clickTone = 1.0f;
        k.drive = 10.0f;
        k.clip = 0.1f;
        k.transient = 1.0f;
        k.sub = 1.0f;
        k.tune = 48.0f;
        k.phase = 360.0f;
        k.outputLevel = 2.0f;

        BassRenderParams b;
        b.drive = 6.0f;
        b.release = 0.25f;
        b.ampAttack = 0.1f;
        b.ampDecay = 1.0f;
        b.ampSustain = 1.0f;
        b.filterCutoff = 0.95f;
        b.filterResonance = 1.0f;
        b.filterDrive = 4.0f;
        b.keyTracking = 1.0f;
        b.pitchEnvAmount = 36.0f;
        b.pitchEnvTime = 0.5f;
        b.pitchEnvCurve = 0.5f;
        b.outputLevel = 2.0f;
        b.midiNote = 60;

        const auto report = KickBassMatch::analyze(k, b, sr);
        require(report.adjustments.bassTimingOffsetMs >= 0
                    && report.adjustments.bassTimingOffsetMs <= 16,
                "bounds: timing 0..16 ms");
        require(report.adjustments.kickTailMultiplier >= 0.6
                    && report.adjustments.kickTailMultiplier <= 1.0,
                "bounds: kick tail multiplier 0.6..1.0");
        require(std::abs(report.adjustments.bassLevelDb) <= 6.0001,
                "bounds: bass level +/-6 dB");
        require(report.adjustments.kickPhaseDeg == 0.0
                    || report.adjustments.kickPhaseDeg == 180.0,
                "bounds: phase snaps to 0 or 180 degrees");
    }

    // 7: Phase snap rule - negative correlation snaps to 180 degrees.
    {
        const auto report =
            KickBassMatch::analyze(overlappingKick(), overlappingBass(), sr);
        if (report.phaseCorrelation < -0.30)
            require(report.adjustments.kickPhaseDeg == 180.0,
                    "phase: strongly negative corr snaps to 180");
        else
            require(report.adjustments.kickPhaseDeg == 0.0,
                    "phase: non-negative corr keeps 0 degrees");
    }

    std::cout << "KickBassMatch tests passed (" << testsPassed << "/"
              << testsRun << ")\n";
    return EXIT_SUCCESS;
}
