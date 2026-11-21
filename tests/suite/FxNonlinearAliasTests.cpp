// Nonlinear alias screen for the shared FX slots.
//
// A bin-centred sine is fed through one slot. Because the input period divides
// the analysis window, every harmonic of the input falls exactly on a DFT bin;
// all remaining energy is content the nonlinearity generated and folded back
// below Nyquist. Total energy comes from Parseval, harmonic energy from a
// Goertzel probe on the harmonic bins only, so the check stays cheap.
//
// Drive uses first-order antiderivative antialiasing (ADAA) and keeps the
// thresholds introduced with that change. Wavefold now uses second-order ADAA
// on its unchanged triangle-fold shape. It is measured against an independent
// point-sampled reference of the old transfer function: the assertion is on
// absolute folded power, because ADAA also attenuates the extreme probes and a
// ratio-only check would hide that.
#include "engine/Effects.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <vector>

namespace {
constexpr int window = 16384;
constexpr double pi = 3.14159265358979323846;

struct AliasResult {
    double harmonic = 0;
    double aliased = 0;
    double ratio = 0;
    // Power of a unit-amplitude sine over the analysis window.
    double inputPower = .25 * window * window;
};

// Positive-side energy of a buffer split into harmonic and generated energy.
AliasResult analyse(const std::vector<float>& left, int fundamentalBin) {
    AliasResult result;
    double timeEnergy = 0;
    for (int i = 0; i < window; ++i) {
        const double x = left[i];
        timeEnergy += x * x;
    }
    const double total = .5 * window * timeEnergy;
    for (int bin = fundamentalBin; bin <= window / 2; bin += fundamentalBin) {
        double coeff = 2 * std::cos(2 * pi * bin / window);
        double s1 = 0, s2 = 0;
        for (int i = 0; i < window; ++i) {
            const double s = left[i] + coeff * s1 - s2;
            s2 = s1;
            s1 = s;
        }
        result.harmonic += s1 * s1 + s2 * s2 - coeff * s1 * s2;
    }
    result.aliased = std::max(0.0, total - result.harmonic);
    result.ratio = total > 0 ? result.aliased / total : 0;
    return result;
}

double foldedDb(const AliasResult& r) {
    return 10 * std::log10(r.aliased / r.inputPower + 1e-30);
}
double ratioDb(const AliasResult& r) {
    return -10 * std::log10(r.ratio + 1e-12);
}

// Point-sampled reference: the pre-ADAA Drive transfer function, evaluated
// directly on the same input. Used only to show the measured improvement.
AliasResult measureDirect(float amount, double rate, int fundamentalBin) {
    const double f0 = fundamentalBin * rate / window;
    const double gain = 1.0 + 7.0 * amount;
    const double level = 1.0 / std::sqrt(gain);
    std::vector<float> left(window);
    for (int i = 0; i < window; ++i) {
        const double x = std::sin(2 * pi * f0 * i / rate);
        left[i] = static_cast<float>(x + amount * (std::tanh(x * gain) * level - x));
    }
    return analyse(left, fundamentalBin);
}

// Point-sampled reference for Wavefold: the literal pre-change expression,
// independent of the arithmetic triangle and ADAA helpers in the slot.
AliasResult measureDirectWavefold(float amount, double rate, int fundamentalBin) {
    const double f0 = fundamentalBin * rate / window;
    const double gain = 1.0 + 7.0 * amount;
    std::vector<float> left(window);
    for (int i = 0; i < window; ++i) {
        const double x = std::sin(2 * pi * f0 * i / rate);
        const double u = std::clamp(x * gain, -8.0, 8.0);
        left[i] = static_cast<float>(2.0 / pi * std::asin(std::sin(pi * u * .5)));
    }
    return analyse(left, fundamentalBin);
}

AliasResult measure(vox::fx::effects::Type type, float amount, double rate, int fundamentalBin) {
    vox::fx::effects::Chain chain;
    AliasResult result;
    if (!chain.prepare(rate) || !chain.set(0, {type, 1.0f, amount})) return result;
    const std::size_t settle = static_cast<std::size_t>(rate * .2);
    std::vector<float> left(settle + window), right(left.size());
    const double f0 = fundamentalBin * rate / window;
    for (std::size_t i = 0; i < left.size(); ++i)
        left[i] = static_cast<float>(std::sin(2 * pi * f0 * i / rate));
    right = left;
    chain.process(left.data(), right.data(), left.size());
    std::vector<float> settled(left.begin() + static_cast<std::ptrdiff_t>(settle), left.end());
    return analyse(settled, fundamentalBin);
}
} // namespace

int main() {
    using vox::fx::effects::Type;
    using vox::fx::effects::Settings;
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        if (!ok) { std::printf("FAIL: %s\n", what); ++failures; }
    };

    // Drive ADAA must keep the behaviour and limits introduced with it.
    for (const double rate : {44100.0, 48000.0, 96000.0}) {
        for (const int bin : {683, 1707, 2731}) {
            for (const float amount : {.5f, 1.0f}) {
                const auto drive = measure(Type::drive, amount, rate, bin);
                const auto direct = measureDirect(amount, rate, bin);
                check(std::isfinite(drive.ratio) && drive.ratio >= 0 && drive.ratio <= 1,
                      "drive alias measure in range");
                // Measured against the same transfer function evaluated
                // point-sampled, ADAA must remove a clear share of the folded
                // energy at every probe.
                check(drive.ratio <= .55 * direct.ratio + 1e-6,
                      "antialiased Drive folds less energy than direct tanh");
                check(drive.ratio < .06, "antialiased Drive keeps folded energy low");
            }
        }
        // Amount 0 is a linear slot; nothing may be generated.
        const auto bypassed = measure(Type::drive, 0.0f, rate, 1707);
        check(bypassed.ratio < 1e-12, "zero Drive amount is alias-free");
    }

    // Wavefold: second-order ADAA against the old point-sampled shape.
    for (const double rate : {44100.0, 48000.0, 96000.0}) {
        for (const int bin : {683, 1707, 2731}) {
            for (const float amount : {.28f, .5f, 1.0f}) {
                const auto fold = measure(Type::wavefold, amount, rate, bin);
                const auto direct = measureDirectWavefold(amount, rate, bin);
                const auto drive = measure(Type::drive, amount, rate, bin);
                std::printf("%5.0f Hz f0 %7.1f amt %.2f: Drive ADAA %.4f (%5.1f dB) | "
                            "Wavefold point %.4f (%5.1f dB, %6.1f dB abs) -> "
                            "Wavefold ADAA2 %.4f (%5.1f dB, %6.1f dB abs)\n",
                            rate, bin * rate / window, amount, drive.ratio, ratioDb(drive),
                            direct.ratio, ratioDb(direct), foldedDb(direct),
                            fold.ratio, ratioDb(fold), foldedDb(fold));
                check(std::isfinite(fold.ratio) && fold.ratio >= 0 && fold.ratio <= 1,
                      "wavefold alias measure in range");
                // Absolute folded power must stay a small fraction of the old
                // point-sampled folded power and of the input power itself.
                // Measured worst case is 8.3% of the old power and -14.2 dB
                // relative to a unit sine; the limits keep a clear margin while
                // still catching a silent fall back to point sampling.
                check(fold.aliased <= .20 * direct.aliased + 1e-12,
                      "antialiased Wavefold folds far less energy than point sampling");
                check(fold.aliased <= .10 * fold.inputPower,
                      "antialiased Wavefold keeps absolute folded energy low");
            }
        }
    }

    // The ADAA must not alter the static transfer function: a constant input
    // settles on exactly the old fold value, so presets keep their low-
    // frequency character.
    {
        vox::fx::effects::Chain chain;
        check(chain.prepare(48000) && chain.set(0, {Type::wavefold, 1, .5f}),
              "wavefold slot accepts settings");
        std::vector<float> left(512, .37f), right(left.size(), -.63f);
        chain.process(left.data(), right.data(), left.size());
        const auto expected = [](double x) {
            const double u = std::clamp(x * (1.0 + 7.0 * .5), -8.0, 8.0);
            return 2.0 / pi * std::asin(std::sin(pi * u * .5));
        };
        check(std::abs(left.back() - expected(.37)) < 1e-6,
              "wavefold DC transfer matches the old static shape");
        check(std::abs(right.back() - expected(-.63)) < 1e-6,
              "wavefold DC transfer matches on the second channel");
    }

    // Quasi-static sweep across the whole fold domain (both signs) must track
    // the old expression. ADAA introduces at most a sub-sample delay, so a
    // slowly rising ramp differs only by the local slope times the delay.
    for (const float amount : {.28f, 1.0f}) {
        vox::fx::effects::Chain chain;
        check(chain.prepare(48000) && chain.set(0, {Type::wavefold, 1, amount}),
              "wavefold static sweep prepare");
        constexpr int samples = 65536;
        std::vector<float> left(samples), right(samples);
        for (int i = 0; i < samples; ++i) {
            const float x = -2.0f + 4.0f * static_cast<float>(i) / (samples - 1);
            left[i] = x;
            right[i] = -x;
        }
        auto reference = left;
        chain.process(left.data(), right.data(), left.size());
        const auto expected = [amount](double x) {
            const double u = std::clamp(x * (1.0 + 7.0 * amount), -8.0, 8.0);
            return 2.0 / pi * std::asin(std::sin(pi * u * .5));
        };
        double error = 0;
        for (int i = 4096; i < samples; ++i) { // skip crossfade and amount smoothing
            error = std::max(error, std::abs(left[i] - expected(reference[i])));
            error = std::max(error, std::abs(right[i] - expected(-reference[i])));
        }
        check(error < 2e-3, "wavefold quasi-static sweep follows the old static shape");
    }

    // Latency impact. Second-order ADAA has no integer pre-delay and no
    // buffer, but the method itself carries p/2 = one sample of group delay.
    // Measure it in the wavefold's linear region (low level keeps the fold
    // inside |u|<=1) against the input phase. Drive keeps the smaller
    // effective delay of its parallel dry/nonlinear mix.
    {
        const auto groupDelay = [](Type type, float amount, double rate, int bin) {
            constexpr int samples = 32768;
            const auto settle = static_cast<std::size_t>(rate * .25);
            const double f0 = bin * rate / samples;
            std::vector<float> input(settle + samples), left, right;
            for (std::size_t i = 0; i < input.size(); ++i)
                input[i] = static_cast<float>(.1 * std::sin(2 * pi * f0 * i / rate));
            left = input;
            right = input;
            vox::fx::effects::Chain chain;
            chain.prepare(rate);
            chain.set(0, {type, 1, amount});
            chain.process(left.data(), right.data(), left.size());
            const auto amplitude = [&](const std::vector<float>& v) {
                double re = 0, im = 0;
                for (int i = 0; i < samples; ++i) {
                    const double w = 2 * pi * f0 * i / rate;
                    re += v[settle + static_cast<std::size_t>(i)] * std::cos(w);
                    im -= v[settle + static_cast<std::size_t>(i)] * std::sin(w);
                }
                return std::pair(re, im);
            };
            const auto in = amplitude(input), out = amplitude(left);
            double phase = std::atan2(out.second, out.first)
                         - std::atan2(in.second, in.first);
            while (phase > pi) phase -= 2 * pi;
            while (phase < -pi) phase += 2 * pi;
            return -phase / (2 * pi * f0 / rate);
        };
        for (const double rate : {44100.0, 48000.0, 96000.0}) {
            const auto wavefoldDelay = groupDelay(Type::wavefold, .28f, rate, 683);
            const auto driveDelay = groupDelay(Type::drive, .28f, rate, 683);
            std::printf("%5.0f Hz group delay: Wavefold %.3f samples, Drive %.3f samples\n",
                        rate, wavefoldDelay, driveDelay);
            check(wavefoldDelay > .9 && wavefoldDelay < 1.1,
                  "Wavefold adds one sample of ADAA group delay, not more");
            check(driveDelay >= 0 && driveDelay < .5,
                  "Drive keeps its parallel-path group delay");
        }
    }

    // Silence remains exactly silent and finite.
    {
        vox::fx::effects::Chain chain;
        check(chain.prepare(44100) && chain.set(0, {Type::wavefold, 1, .8f}),
              "wavefold silence prepare");
        std::vector<float> left(512), right(512);
        chain.process(left.data(), right.data(), left.size());
        double energy = 0;
        for (float value : left) { check(std::isfinite(value), "finite silent output"); energy += std::abs(value); }
        check(energy == 0, "wavefold silence stays exactly silent");
    }

    // Preset/codec compatibility: the type id, 6-bit state packing and the
    // FXSL v1 legacy decode are unchanged by the DSP change.
    {
        const Settings wavefold { Type::wavefold, .28f, .44f };
        check(static_cast<unsigned>(Type::wavefold) == 3,
              "wavefold type id is stable for FXSL");
        check(wavefold.valid(), "wavefold settings valid");
        const auto packed = vox::fx::effects::pack(wavefold);
        const auto restored = vox::fx::effects::unpack(packed);
        check(restored.type == Type::wavefold, "packed wavefold keeps its type");
        check(std::abs(restored.wet - .28f) <= 1.0f / 126.0f,
              "packed wavefold keeps wet within the 6-bit step");
        check(std::abs(restored.amount - .44f) <= 1.0f / 126.0f,
              "packed wavefold keeps amount within the 6-bit step");
        const auto legacy = vox::fx::effects::unpackLegacy(static_cast<std::uint16_t>(
            3u | (static_cast<unsigned>(std::lround(.28f * 63)) << 3)
               | (static_cast<unsigned>(std::lround(.44f * 127)) << 9)));
        check(legacy.type == Type::wavefold, "FXSL v1 wavefold still decodes");
        check(std::abs(legacy.wet - .28f) <= 1.0f / 126.0f
                  && std::abs(legacy.amount - .44f) <= 1.0f / 252.0f,
              "FXSL v1 wavefold values still decode");
    }

    std::printf("FX nonlinear alias: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
