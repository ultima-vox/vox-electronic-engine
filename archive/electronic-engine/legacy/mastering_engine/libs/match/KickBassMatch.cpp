#include "match/KickBassMatch.h"

#include <algorithm>
#include <cstdint>
#include <cmath>

namespace vstengine::match {
namespace {

constexpr double twoPi = juce::MathConstants<double>::twoPi;
constexpr double analysisSeconds = 2.0;
constexpr double minHz = 20.0;
constexpr int envelopeBlock = 32;

double clampDb(double v) noexcept
{
    return juce::jlimit(-KickBassMatch::maxBassLevelDb,
                        KickBassMatch::maxBassLevelDb, v);
}

// Simple one-pole low-pass applied in place (analysis only).
void lowpass(float* data, int n, double fc, double sampleRate) noexcept
{
    const double w = twoPi * fc / sampleRate;
    const double k = w / (w + 1.0); // stable one-pole coefficient
    double y = 0.0;
    for (int i = 0; i < n; ++i) {
        y += k * (static_cast<double>(data[i]) - y);
        data[i] = static_cast<float>(y);
    }
}

// One-pole high-pass applied in place (analysis only, complements lowpass).
void highpass(float* data, int n, double fc, double sampleRate) noexcept
{
    const double w = twoPi * fc / sampleRate;
    const double k = w / (w + 1.0);
    double prev = 0.0;
    double lp = 0.0;
    for (int i = 0; i < n; ++i) {
        lp += k * (static_cast<double>(data[i]) - lp);
        const double hp = static_cast<double>(data[i]) - lp;
        data[i] = static_cast<float>(hp);
        prev = hp;
    }
}

std::vector<float> bandPass(const std::vector<float>& in, double lo,
                            double hi, double sampleRate)
{
    std::vector<float> out(in);
    highpass(out.data(), static_cast<int>(out.size()), lo, sampleRate);
    lowpass(out.data(), static_cast<int>(out.size()), hi, sampleRate);
    return out;
}

} // namespace

std::vector<float> KickBassMatch::renderKick(
    const vstengine::kick::KickParams& kick, double sampleRate)
{
    const int n = static_cast<int>(analysisSeconds * sampleRate);
    vstengine::kick::KickSynth k;
    k.prepare(sampleRate);
    k.setParameters(kick);
    std::vector<float> out(n, 0.0f);
    juce::AudioBuffer<float> buffer(1, n);
    buffer.clear();
    k.trigger(1.0f, 60, 0);
    k.render(buffer, n);
    const auto* src = buffer.getReadPointer(0);
    out.assign(src, src + n);
    return out;
}

std::vector<float> KickBassMatch::renderBass(const BassRenderParams& bass,
                                             double sampleRate)
{
    const int perBlock = 512;
    const int total = static_cast<int>(bass.seconds * sampleRate);
    std::vector<float> out(total, 0.0f);

    juce::Synthesiser synth;
    synth.setCurrentPlaybackSampleRate(sampleRate);
    synth.addSound(new vstengine::bass::PsyBassSound());
    auto* voice = new vstengine::bass::PsyBassVoice();
    synth.addVoice(voice);

    voice->setDrive(bass.drive);
    voice->setAmpAttack(bass.ampAttack);
    voice->setAmpDecay(bass.ampDecay);
    voice->setAmpSustain(bass.ampSustain);
    voice->setAmpRelease(bass.release);
    voice->setPitchEnvelopeAmount(bass.pitchEnvAmount);
    voice->setPitchEnvelopeTime(bass.pitchEnvTime);
    voice->setPitchEnvelopeCurve(bass.pitchEnvCurve);
    voice->setFilterCutoff(bass.filterCutoff);
    voice->setFilterResonance(bass.filterResonance);
    voice->setFilterDrive(bass.filterDrive);
    voice->setKeyTracking(bass.keyTracking);
    voice->setOutputLevel(bass.outputLevel);

    juce::MidiBuffer empty;
    synth.noteOn(1, bass.midiNote, 1.0f);

    int written = 0;
    const int noteLenSamples =
        juce::jmin(total, static_cast<int>(0.9 * sampleRate));
    while (written < noteLenSamples) {
        const int n = juce::jmin(perBlock, noteLenSamples - written);
        juce::AudioBuffer<float> block(1, n);
        block.clear();
        synth.renderNextBlock(block, empty, 0, n);
        const auto* src = block.getReadPointer(0);
        for (int i = 0; i < n; ++i)
            out[written + i] = src[i];
        written += n;
    }

    synth.noteOff(1, bass.midiNote, 0.0f, false);
    while (written < total) {
        const int n = juce::jmin(perBlock, total - written);
        juce::AudioBuffer<float> block(1, n);
        block.clear();
        synth.renderNextBlock(block, empty, 0, n);
        const auto* src = block.getReadPointer(0);
        for (int i = 0; i < n; ++i)
            out[written + i] = src[i];
        written += n;
    }

    return out;
}

double KickBassMatch::peak(const std::vector<float>& v)
{
    float p = 0.0f;
    for (const auto sample : v)
        p = std::max(p, std::abs(sample));
    return static_cast<double>(p);
}

double KickBassMatch::tailSeconds(const std::vector<float>& v,
                                  double sampleRate)
{
    const int n = static_cast<int>(v.size());
    const int blocks = n / envelopeBlock;
    if (blocks < 1)
        return 0.0;

    // Block peak-follower envelope, then the last block still above 1%.
    std::vector<double> env(static_cast<std::size_t>(blocks), 0.0);
    double maxEnv = 0.0;
    for (int b = 0; b < blocks; ++b) {
        double e = 0.0;
        for (int i = 0; i < envelopeBlock; ++i)
            e = std::max(e,
                         std::abs(static_cast<double>(v[b * envelopeBlock + i])));
        env[static_cast<std::size_t>(b)] = e;
        maxEnv = std::max(maxEnv, e);
    }
    if (maxEnv <= 1e-6)
        return 0.0;

    const double floorLevel = 0.01 * maxEnv;
    int lastAudible = 0;
    for (int b = blocks - 1; b >= 0; --b)
        if (env[static_cast<std::size_t>(b)] > floorLevel) {
            lastAudible = b;
            break;
        }
    return static_cast<double>(lastAudible + 1) * envelopeBlock / sampleRate;
}

double KickBassMatch::dominantHz(const std::vector<float>& v, int from,
                                 int to, double sampleRate)
{
    const int n = static_cast<int>(v.size());
    if (n < 64 || sampleRate <= 0.0)
        return 0.0;
    to = juce::jlimit(0, n, to);
    from = juce::jlimit(0, n - 1, from);
    if (to - from < 64)
        return 0.0;

    // Low-pass first so high-frequency click/transient artifacts do not
    // double the counted zero-crossing rate (kick body ~65 Hz, not 130).
    std::vector<float> filtered(v.begin() + from, v.begin() + to);
    lowpass(filtered.data(), static_cast<int>(filtered.size()), 200.0,
            sampleRate);

    int crossings = 0;
    std::int64_t totalSamples = 0;
    int lastCross = -1;
    for (int i = 1; i < static_cast<int>(filtered.size()); ++i) {
        const bool up = filtered[i - 1] <= 0.0f && filtered[i] > 0.0f;
        if (up) {
            if (lastCross >= 0)
                totalSamples += i - lastCross;
            lastCross = i;
            ++crossings;
        }
    }
    if (crossings < 4 || totalSamples <= 0)
        return 0.0;
    const double period = static_cast<double>(totalSamples)
                          / static_cast<double>(crossings - 1);
    return sampleRate / period;
}

double KickBassMatch::onsetSeconds(const std::vector<float>& v,
                                   double sampleRate)
{
    const double p = peak(v);
    if (p <= 1e-6)
        return 0.0;
    const double threshold = 0.5 * p;
    for (std::size_t i = 0; i < v.size(); ++i)
        if (std::abs(v[i]) >= threshold)
            return static_cast<double>(i) / sampleRate;
    return 0.0;
}

double KickBassMatch::phaseCorrelation(const std::vector<float>& a,
                                       const std::vector<float>& b,
                                       int from, int to)
{
    if (to - from < 32)
        return 0.0;
    double sum = 0.0, sumA = 0.0, sumB = 0.0;
    for (int i = from; i < to; ++i) {
        const double x = a[static_cast<std::size_t>(i)];
        const double y = b[static_cast<std::size_t>(i)];
        sum += x * y;
        sumA += x * x;
        sumB += y * y;
    }
    const double denom = std::sqrt(sumA * sumB);
    if (denom <= 1e-12)
        return 0.0;
    return juce::jlimit(-1.0, 1.0, sum / denom);
}

double KickBassMatch::computeOverlap(const std::vector<float>& kick,
                                     const std::vector<float>& bass,
                                     double sampleRate)
{
    if (kick.empty() || bass.empty())
        return 0.0;

    const double kickDom = dominantHz(
        kick, static_cast<int>(0.08 * sampleRate),
        static_cast<int>(0.4 * sampleRate), sampleRate);
    const double bassDom = dominantHz(
        bass, static_cast<int>(0.1 * sampleRate),
        static_cast<int>(0.5 * sampleRate), sampleRate);
    if (kickDom < minHz && bassDom < minHz)
        return 0.0;

    const bool bothDominant = kickDom > 0.0 && bassDom > 0.0;
    double lo = std::max(minHz, std::min(kickDom, bassDom)
                                    - (bothDominant ? overlapBandHalfWidthHz
                                                    : overlapBandHalfWidthHz * 2.0));
    double hi = std::max(kickDom, bassDom) + overlapBandHalfWidthHz;
    if (hi - lo < 12.0) {
        lo = std::max(minHz, lo - 6.0);
        hi += 6.0;
    }

    const int maxLen = juce::jmin(static_cast<int>(kick.size()),
                                  static_cast<int>(bass.size()));
    const std::vector<float> kBand = bandPass(kick, lo, hi, sampleRate);
    const std::vector<float> bBand = bandPass(bass, lo, hi, sampleRate);

    // Window: from either signal's onset over the kick tail plus a margin.
    const double startSec = juce::jmin(onsetSeconds(kick, sampleRate),
                                       onsetSeconds(bass, sampleRate));
    const double tail = tailSeconds(kick, sampleRate);
    const int from = juce::jlimit(0, maxLen - 1,
                                  static_cast<int>(startSec * sampleRate));
    const int to = juce::jlimit(from + 64, maxLen,
                                static_cast<int>(
                                    (startSec + tail + 0.12) * sampleRate));

    // Envelope coherence: normalized cross-correlation of the two band
    // envelopes inside the window. This is sensitive to timing offset and
    // to trimming either source, which is exactly what the match adjustments
    // manipulate. Range 0 (no shared energy in time) .. 1 (identical shape).
    constexpr int envBlock = 64;
    const int winLen = to - from;
    const int envBlocks = juce::jmax(1, winLen / envBlock);
    double sumK = 0.0, sumB = 0.0, sumKB = 0.0, sumK2 = 0.0, sumB2 = 0.0;
    int count = 0;
    for (int e = 0; e < envBlocks; ++e) {
        const int bStart = from + e * envBlock;
        const int bEnd = juce::jmin(to, bStart + envBlock);
        double ek = 0.0, eb = 0.0;
        for (int i = bStart; i < bEnd; ++i) {
            ek += std::abs(kBand[static_cast<std::size_t>(i)]);
            eb += std::abs(bBand[static_cast<std::size_t>(i)]);
        }
        const int n = bEnd - bStart;
        if (n > 0) {
            ek /= static_cast<double>(n);
            eb /= static_cast<double>(n);
        }
        sumK += ek;
        sumB += eb;
        sumKB += ek * eb;
        sumK2 += ek * ek;
        sumB2 += eb * eb;
        ++count;
    }
    if (count < 2)
        return 0.0;
    const double meanK = sumK / static_cast<double>(count);
    const double meanB = sumB / static_cast<double>(count);
    const double cov = sumKB - static_cast<double>(count) * meanK * meanB;
    const double varK = sumK2 - static_cast<double>(count) * meanK * meanK;
    const double varB = sumB2 - static_cast<double>(count) * meanB * meanB;
    const double denom = std::sqrt(std::max(0.0, varK) * std::max(0.0, varB));
    if (denom <= 1e-12)
        return 0.0;
    return juce::jlimit(0.0, 1.0, cov / denom);
}

MatchReport KickBassMatch::analyze(const vstengine::kick::KickParams& kick,
                                   const BassRenderParams& bassParams,
                                   double sampleRate)
{
    MatchReport report;
    const std::vector<float> kickBuf = renderKick(kick, sampleRate);
    const std::vector<float> bassBuf = renderBass(bassParams, sampleRate);

    report.kickPeak = peak(kickBuf);
    report.bassPeak = peak(bassBuf);
    report.peakRatio = report.kickPeak > 1e-6
                           ? report.bassPeak / report.kickPeak
                           : 0.0;

    report.kickTailSeconds = tailSeconds(kickBuf, sampleRate);
    report.bassOnsetSeconds = onsetSeconds(bassBuf, sampleRate);
    report.kickDominantHz = dominantHz(
        kickBuf, static_cast<int>(0.08 * sampleRate),
        static_cast<int>(0.4 * sampleRate), sampleRate);
    report.bassDominantHz = dominantHz(
        bassBuf, static_cast<int>(0.1 * sampleRate),
        static_cast<int>(0.5 * sampleRate), sampleRate);

    report.spectralOverlap = computeOverlap(kickBuf, bassBuf, sampleRate);

    // Phase interaction inside the overlap window (band-filtered signals).
    {
        const double kickDom = report.kickDominantHz;
        const double bassDom = report.bassDominantHz;
        const bool bothDominant = kickDom > 0.0 && bassDom > 0.0;
        const double lo = std::max(
            minHz, std::min(kickDom, bassDom)
                       - (bothDominant ? overlapBandHalfWidthHz
                                       : overlapBandHalfWidthHz * 2.0));
        const double hi = std::max(kickDom, bassDom) + overlapBandHalfWidthHz;
        const std::vector<float> kBand =
            bandPass(kickBuf, lo, hi, sampleRate);
        const std::vector<float> bBand =
            bandPass(bassBuf, lo, hi, sampleRate);
        const int maxLen = juce::jmin(static_cast<int>(kickBuf.size()),
                                      static_cast<int>(bassBuf.size()));
        const double startSec = juce::jmin(report.bassOnsetSeconds,
                                           juce::jmin(
                                               onsetSeconds(kickBuf, sampleRate),
                                               report.bassOnsetSeconds));
        const int from = juce::jlimit(0, maxLen - 1,
                                      static_cast<int>(startSec * sampleRate));
        const int to = juce::jlimit(
            from + 64, maxLen,
            static_cast<int>((startSec + report.kickTailSeconds + 0.12)
                             * sampleRate));
        report.phaseCorrelation =
            phaseCorrelation(kBand, bBand, from, to);
    }

    // ---- Bounded recommended adjustments --------------------------------
    MatchAdjustments adj;
    if (report.spectralOverlap > overlapThreshold)
        adj.kickTailMultiplier = juce::jlimit(
            minKickTailMultiplier, 1.0,
            1.0 - (report.spectralOverlap - overlapThreshold) * 1.25);
    else
        adj.kickTailMultiplier = 1.0;

    adj.kickPhaseDeg = report.phaseCorrelation < phaseSnapThreshold ? 180.0
                                                                    : 0.0;

    if (report.kickTailSeconds > 0.02
        && report.bassOnsetSeconds < report.kickTailSeconds * 0.30) {
        const double delta = report.kickTailSeconds * 0.30
                             - report.bassOnsetSeconds;
        adj.bassTimingOffsetMs = juce::jlimit(
            0, static_cast<int>(maxBassTimingOffsetMs),
            static_cast<int>(std::lround(delta * 1000.0)));
    }

    if (report.peakRatio > 0.85)
        adj.bassLevelDb = clampDb(
            20.0 * std::log10(0.6 / std::max(report.peakRatio, 1e-3)));
    else if (report.peakRatio < 0.35)
        adj.bassLevelDb = clampDb(
            20.0 * std::log10(0.8 / std::max(report.peakRatio, 1e-3)));

    report.adjustments = adj;

    // ---- Exit-gate proof: does applying the bounded adjustments reduce
    //      the overlap? (no source is muted) ------------------------------
    {
        const double tail = report.kickTailSeconds;
        const double trimmed = juce::jmin(tail * adj.kickTailMultiplier, tail);
        std::vector<float> kickAdj(kickBuf);
        for (std::size_t idx = 0; idx < kickAdj.size(); ++idx) {
            const double t = static_cast<double>(idx) / sampleRate;
            if (t > trimmed)
                kickAdj[idx] *= static_cast<float>(
                    std::exp(-(t - trimmed) * 6.0 / std::max(tail, 1e-3)));
        }
        // NOTE: phase snap is a real acoustic adjustment (constructive /
        // destructive interference) but does not change the envelope, so it is
        // intentionally not part of this envelope-coherence exit gate.

        std::vector<float> bassAdj(static_cast<std::size_t>(
                                       bassBuf.size()
                                           + static_cast<std::size_t>(std::ceil(
                                                 maxBassTimingOffsetMs / 1000.0
                                                 * sampleRate))
                                           + 16),
                                   0.0f);
        const int shift = juce::jlimit(
            0, static_cast<int>((maxBassTimingOffsetMs / 1000.0) * sampleRate),
            static_cast<int>(std::lround(
                adj.bassTimingOffsetMs / 1000.0 * sampleRate)));
        const double level = std::pow(10.0, adj.bassLevelDb / 20.0);
        for (std::size_t idx = 0; idx < bassBuf.size(); ++idx) {
            bassAdj[idx + static_cast<std::size_t>(shift)] =
                bassBuf[idx] * static_cast<float>(level);
        }

        const double adjustedOverlap =
            computeOverlap(kickAdj, bassAdj, sampleRate);
        report.spectralOverlapAfter = adjustedOverlap;
        report.overlapImproves =
            adjustedOverlap < report.spectralOverlap - 0.02;
    }

    return report;
}

} // namespace vstengine::match
