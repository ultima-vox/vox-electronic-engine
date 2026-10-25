#pragma once
#include "bass/PsyBassVoice.h"
#include "kick/KickSynth.h"
#include <vector>

namespace vstengine::match {

// Kick/Bass low-end matching (issue #11 PHASE 6).
//
// The module analyzes the real synthesized kick and a real synthesized bass
// note (the same DSP used in the plugin) and produces BOUNDED matching
// adjustments. Everything here runs on the message thread (editor callback /
// tests), never inside the realtime audio callback.
//
// Forbidden behaviors honored: no random parameter movement, no preset
// switching, no fake "matched" state - the report is computed from actual
// DSP output and every adjustment is clamped to an explicit bound.

// Bounded recommended adjustments (PHASE 6 "allowed automatic adjustments").
// Every field stays inside its documented bound by construction.
struct MatchAdjustments {
    int bassTimingOffsetMs { 0 };      // 0..16 ms (bass later vs kick attack)
    double kickTailMultiplier { 1.0 }; // 0.6..1.0 (tail trim <= 40%)
    double kickPhaseDeg { 0.0 };       // snapped to 0 or 180 degrees
    double bassLevelDb { 0.0 };        // -6..+6 dB
};

// Full analysis report surfaced to the UI (all units SI).
struct MatchReport {
    double kickTailSeconds { 0.0 };
    double kickDominantHz { 0.0 };
    double kickPeak { 0.0 };
    double bassOnsetSeconds { 0.0 };
    double bassDominantHz { 0.0 };
    double bassPeak { 0.0 };
    // Spectral overlap in the overlap band, 0 (none) .. 1 (identical energy).
    double spectralOverlap { 0.0 };
    // Same metric after the bounded adjustments are applied to the buffers
    // (used by the exit gate and surfaced to the UI).
    double spectralOverlapAfter { 0.0 };
    // Zero-lag cross-correlation in the overlap window, -1 .. +1.
    double phaseCorrelation { 0.0 };
    // bassPeak / kickPeak.
    double peakRatio { 0.0 };
    MatchAdjustments adjustments {};
    // True when applying the bounded adjustments measurably reduces overlap.
    bool overlapImproves { false };
};

// Parameter snapshot needed to render the virtual bass note with the exact
// plugin voice. Mirrors the bass APVTS parameters 1:1.
struct BassRenderParams {
    float drive { 1.8f };
    float release { 0.035f };
    float ampAttack { 0.001f };
    float ampDecay { 0.055f };
    float ampSustain { 0.72f };
    float filterCutoff { 0.55f };
    float filterResonance { 0.7f };
    float filterDrive { 1.0f };
    float keyTracking { 0.2f };
    float pitchEnvAmount { 12.0f };
    float pitchEnvTime { 0.018f };
    float pitchEnvCurve { 2.0f };
    float outputLevel { 1.0f };
    int midiNote { 36 };        // bass root under test (default C2)
    double seconds { 1.5 };     // rendering length (analysis only)
};

class KickBassMatch final {
public:
    // Bounds (contract; also used by tests).
    static constexpr double maxBassTimingOffsetMs = 16.0;
    static constexpr double minKickTailMultiplier = 0.6;
    static constexpr double maxBassLevelDb = 6.0;
    static constexpr double overlapBandHalfWidthHz = 30.0;
    static constexpr double overlapThreshold = 0.20;
    static constexpr double phaseSnapThreshold = -0.30;

    // Renders real kick + real bass DSP and returns the report.
    [[nodiscard]] static MatchReport analyze(
        const vstengine::kick::KickParams& kick,
        const BassRenderParams& bass,
        double sampleRate);

    // Spectral overlap (0..1) between two mono buffers inside the overlap
    // band around their dominant frequencies. Public so tests can prove
    // overlap decreases after applying the bounded adjustments.
    [[nodiscard]] static double computeOverlap(
        const std::vector<float>& kick, const std::vector<float>& bass,
        double sampleRate);

    // Deterministic known-tone estimator, public for strict regression tests.
    [[nodiscard]] static double dominantHz(const std::vector<float>& v,
                                           int from, int to,
                                           double sampleRate);

private:
    [[nodiscard]] static std::vector<float> renderKick(
        const vstengine::kick::KickParams& kick, double sampleRate);
    [[nodiscard]] static std::vector<float> renderBass(
        const BassRenderParams& bass, double sampleRate);
    [[nodiscard]] static double peak(const std::vector<float>& v);
    [[nodiscard]] static double tailSeconds(const std::vector<float>& v,
                                            double sampleRate);
    [[nodiscard]] static double onsetSeconds(const std::vector<float>& v,
                                             double sampleRate);
    [[nodiscard]] static double phaseCorrelation(
        const std::vector<float>& a, const std::vector<float>& b,
        int from, int to);
};

} // namespace vstengine::match
