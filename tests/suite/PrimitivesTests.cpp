// Measured behaviour of the ported vox_dsp primitives (dspcore/Primitives.h,
// byte-identical to f9c8bc6). The legacy tree had no direct tests for them;
// these pin frequency, aliasing, filter response and stability, envelope
// timing and smoothing so later changes are measured, not assumed.
#include "dspcore/Primitives.h"
#include "GoldenRender.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace vstengine::dspcore;

namespace {
int failures = 0;

void check (bool condition, const std::string& what)
{
    if (! condition)
    {
        std::printf ("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

std::vector<float> oscillator (int wave, double hz, double rate, std::size_t frames)
{
    BlepOscillator osc;
    std::vector<float> out (frames);
    for (auto& s : out)
        s = static_cast<float> (osc.next (wave, hz / rate));
    return out;
}

std::vector<float> naiveSaw (double hz, double rate, std::size_t frames)
{
    std::vector<float> out (frames);
    double phase = 0.0;
    for (auto& s : out)
    {
        s = static_cast<float> (2.0 * phase - 1.0);
        phase += hz / rate;
        phase -= std::floor (phase);
    }
    return out;
}

// Fraction of spectral energy (dB) that is not within +-3 bins of a true
// harmonic of f0 below Nyquist: aliasing plus leakage.
double inharmonicDb (const std::vector<float>& signal, double f0, double rate)
{
    constexpr std::size_t n = 1u << 15;
    const auto spectrum = vox::testkit::magnitudeSpectrum (signal, n);
    const double binHz = rate / static_cast<double> (n);
    double harmonic = 0.0, other = 0.0;
    for (std::size_t k = 1; k < spectrum.size(); ++k)
    {
        const double hz = static_cast<double> (k) * binHz;
        const double nearest = std::round (hz / f0) * f0;
        const double power = spectrum[k] * spectrum[k];
        if (nearest > 0.0 && std::fabs (hz - nearest) <= 3.0 * binHz)
            harmonic += power;
        else
            other += power;
    }
    return 10.0 * std::log10 (other / harmonic);
}

std::size_t risingCrossings (const std::vector<float>& signal)
{
    std::size_t count = 0;
    for (std::size_t i = 1; i < signal.size(); ++i)
        count += signal[i - 1] < 0.0f && signal[i] >= 0.0f;
    return count;
}

void testOscillatorFrequency()
{
    for (double rate : { 44100.0, 48000.0, 96000.0 })
        for (int wave : { BlepOscillator::sine, BlepOscillator::saw, BlepOscillator::square })
        {
            const auto signal = oscillator (wave, 440.0, rate, static_cast<std::size_t> (rate));
            const auto crossings = static_cast<double> (risingCrossings (signal));
            check (std::fabs (crossings - 440.0) <= 1.0,
                   "wave " + std::to_string (wave) + " at " + std::to_string (rate)
                       + " Hz: 440 cycles per second, got " + std::to_string (crossings));
        }
}

void testAliasing()
{
    // A 5 kHz saw at 44.1 kHz folds many harmonics. PolyBLEP must remove a
    // substantial part of that energy relative to a naive saw.
    constexpr double rate = 44100.0, f0 = 4987.0;
    const auto blep = inharmonicDb (oscillator (BlepOscillator::saw, f0, rate, 1u << 15), f0, rate);
    const auto naive = inharmonicDb (naiveSaw (f0, rate, 1u << 15), f0, rate);
    std::printf ("saw %.0f Hz @ %.0f: inharmonic energy BLEP %.1f dB, naive %.1f dB\n", f0, rate, blep, naive);
    check (blep <= naive - 6.0, "polyBLEP saw has at least 6 dB less aliasing than a naive saw");
    check (blep <= -20.0, "polyBLEP saw aliasing at most -20 dB relative to harmonics");
}

double sineGain (double hz, double cutoff, double resonance, double rate)
{
    SvfLowpass filter;
    const auto frames = static_cast<std::size_t> (rate);
    double inPower = 0.0, outPower = 0.0;
    for (std::size_t i = 0; i < frames; ++i)
    {
        const double x = std::sin (2.0 * pi * hz * static_cast<double> (i) / rate);
        const double y = filter.next (x, cutoff, resonance, rate);
        if (i > frames / 2)
        {
            inPower += x * x;
            outPower += y * y;
        }
    }
    return std::sqrt (outPower / inPower);
}

void testSvf()
{
    for (double rate : { 44100.0, 96000.0 })
    {
        check (std::fabs (sineGain (50.0, 1000.0, 0.0, rate) - 1.0) < 0.01, "SVF passband gain ~1");
        // resonance 0 -> k = 2 (Q = 0.5): -6 dB at the cutoff.
        const double atCutoff = sineGain (1000.0, 1000.0, 0.0, rate);
        check (atCutoff > 0.45 && atCutoff < 0.55, "SVF gain at cutoff ~0.5 for resonance 0, got " + std::to_string (atCutoff));
        check (sineGain (8000.0, 1000.0, 0.0, rate) < 0.03, "SVF stopband attenuates 3 octaves up");
        check (sineGain (1000.0, 1000.0, 1.0, rate) > 5.0, "SVF resonance 1 peaks at the cutoff");

        // Stability: impulse response bounded for every resonance and for
        // cutoffs up to and above the 0.45 * rate clamp.
        for (double resonance : { 0.0, 0.5, 0.9, 1.0 })
            for (double cutoff : { 20.0, 1000.0, 0.4 * rate, 0.45 * rate, rate })
            {
                SvfLowpass filter;
                double worst = 0.0;
                for (int i = 0; i < 48000; ++i)
                {
                    const double y = filter.next (i == 0 ? 1.0 : 0.0, cutoff, resonance, rate);
                    worst = std::isfinite (y) ? std::max (worst, std::fabs (y)) : 1e9;
                }
                check (worst < 100.0, "SVF impulse response bounded");
            }
    }
}

void testAdsr()
{
    constexpr double rate = 48000.0;
    LinearAdsr env;
    check (env.idle(), "envelope starts idle");
    env.gate();
    int attack = 0;
    while (env.next (0.010, 0.020, 0.5, rate) < 1.0 && attack < 100000)
        ++attack;
    check (std::abs (attack - 479) <= 1, "10 ms attack reaches 1 after 480 samples, got " + std::to_string (attack + 1));
    int decay = 0;
    while (env.next (0.010, 0.020, 0.5, rate) > 0.5 && decay < 100000)
        ++decay;
    check (std::abs (decay - 959) <= 2, "20 ms decay to 0.5 takes 960 samples, got " + std::to_string (decay + 1));
    check (env.next (0.010, 0.020, 0.5, rate) == 0.5 && env.stage == LinearAdsr::sustain, "sustain holds");
    env.startRelease (0.005, rate);
    int release = 0;
    while (! env.idle() && release < 100000)
    {
        env.next (0.010, 0.020, 0.5, rate);
        ++release;
    }
    check (std::abs (release - 240) <= 1, "5 ms release from 0.5 ends after 240 samples, got " + std::to_string (release));
    check (env.level == 0.0, "release ends at 0");
}

void testSmoothed()
{
    Smoothed s;
    s.prepare (48000.0, 0.010, 0.0);
    double value = 0.0;
    int steps = 0;
    while (value != 1.0 && steps < 48000)
    {
        value = s.next (1.0);
        ++steps;
    }
    check (value == 1.0, "smoothing lands exactly on the target");
    check (steps > 480 && steps < 48000, "smoothing takes several time constants");
    check (std::fabs (midiNoteHz (69) - 440.0) < 1e-9 && std::fabs (midiNoteHz (81) - 880.0) < 1e-9, "midiNoteHz");
}
} // namespace

int main()
{
    testOscillatorFrequency();
    testAliasing();
    testSvf();
    testAdsr();
    testSmoothed();
    if (failures == 0)
        std::printf ("primitives tests passed\n");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
