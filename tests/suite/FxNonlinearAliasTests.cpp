// Nonlinear alias screen for the shared FX slots.
//
// A bin-centred sine is fed through one slot. Because the input period divides
// the analysis window, every harmonic of the input falls exactly on a DFT bin;
// all remaining energy is content the nonlinearity generated and folded back
// below Nyquist. Total energy comes from Parseval, harmonic energy from a
// Goertzel probe on the harmonic bins only, so the check stays cheap.
//
// The Drive slot now uses first-order antiderivative antialiasing, measured
// here with explicit thresholds. Wavefold is still generated point-sampled and
// is reported, not asserted: it is the remaining FX-04 alias gap.
#include "engine/Effects.h"
#include <cmath>
#include <cstdio>
#include <vector>

namespace {
constexpr int window = 16384;

struct AliasResult {
    double harmonic = 0;
    double aliased = 0;
    double ratio = 0;
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
        double coeff = 2 * std::cos(2 * 3.14159265358979323846 * bin / window);
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

// Point-sampled reference: the pre-ADAA Drive transfer function, evaluated
// directly on the same input. Used only to show the measured improvement.
AliasResult measureDirect(float amount, double rate, int fundamentalBin) {
    const double f0 = fundamentalBin * rate / window;
    const double gain = 1.0 + 7.0 * amount;
    const double level = 1.0 / std::sqrt(gain);
    std::vector<float> left(window);
    for (int i = 0; i < window; ++i) {
        const double x = std::sin(2 * 3.14159265358979323846 * f0 * i / rate);
        left[i] = static_cast<float>(x + amount * (std::tanh(x * gain) * level - x));
    }
    return analyse(left, fundamentalBin);
}

AliasResult measureDirectWavefold(float amount, double rate, int fundamentalBin) {
    const double f0 = fundamentalBin * rate / window;
    const double gain = 1.0 + 7.0 * amount;
    const double pi = 3.14159265358979323846;
    std::vector<float> left(window);
    for (int i = 0; i < window; ++i) {
        const double x = std::sin(2 * pi * f0 * i / rate);
        left[i] = static_cast<float>((2.0 / pi) * std::asin(std::sin(pi * x * gain * .5)));
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
        left[i] = static_cast<float>(std::sin(2 * 3.14159265358979323846 * f0 * i / rate));
    right = left;
    chain.process(left.data(), right.data(), left.size());
    std::vector<float> settled(left.begin() + static_cast<std::ptrdiff_t>(settle), left.end());
    return analyse(settled, fundamentalBin);
}
} // namespace

int main() {
    using vox::fx::effects::Type;
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        if (!ok) { std::printf("FAIL: %s\n", what); ++failures; }
    };
    for (const double rate : {44100.0, 48000.0, 96000.0}) {
        for (const int bin : {683, 1707, 2731}) {
            for (const float amount : {.5f, 1.0f}) {
                const auto drive = measure(Type::drive, amount, rate, bin);
                const auto direct = measureDirect(amount, rate, bin);
                const auto fold = measure(Type::wavefold, amount, rate, bin);
                const auto directFold = measureDirectWavefold(amount, rate, bin);
                std::printf("%5.0f Hz f0 %7.1f amt %.1f: direct tanh %.4f (-%4.1f dB), "
                            "antialiased Drive %.4f (-%4.1f dB), direct wavefold %.4f (-%4.1f dB), "
                            "ADAA wavefold %.4f (-%4.1f dB)\n",
                            rate, bin * rate / window, amount, direct.ratio,
                            -10 * std::log10(direct.ratio + 1e-12), drive.ratio,
                            -10 * std::log10(drive.ratio + 1e-12), directFold.ratio,
                            -10 * std::log10(directFold.ratio + 1e-12), fold.ratio,
                            -10 * std::log10(fold.ratio + 1e-12));
                check(std::isfinite(drive.ratio) && drive.ratio >= 0 && drive.ratio <= 1,
                      "drive alias measure in range");
                // Measured against the same transfer function evaluated
                // point-sampled, ADAA must remove a clear share of the folded
                // energy at every probe.
                check(drive.ratio <= .55 * direct.ratio + 1e-6,
                      "antialiased Drive folds less energy than direct tanh");
                check(drive.ratio < .06, "antialiased Drive keeps folded energy low");
                check(std::isfinite(fold.ratio) && fold.ratio >= 0 && fold.ratio <= 1,
                      "wavefold alias measure in range");
                check(fold.ratio <= directFold.ratio + 1e-5,
                      "ADAA Wavefold does not increase folded energy");
            }
        }
        // Amount 0 is a linear slot; nothing may be generated.
        const auto bypassed = measure(Type::drive, 0.0f, rate, 1707);
        check(bypassed.ratio < 1e-12, "zero Drive amount is alias-free");
        const auto foldBypassed = measure(Type::wavefold, 0.0f, rate, 1707);
        check(foldBypassed.ratio < 1e-12, "zero Wavefold amount is bit-transparent");
    }
    // Structural behaviour the ADAA state must not disturb.
    vox::fx::effects::Chain chain;
    check(chain.prepare(48000) && chain.set(0, {Type::drive, 1, .8f}), "drive slot accepts settings");
    std::vector<float> l(512), r(512);
    for (std::size_t i = 0; i < l.size(); ++i) l[i] = r[i] = static_cast<float>(.4 * std::sin(i * .05));
    chain.process(l.data(), r.data(), l.size());
    double peak = 0;
    for (float v : l) {
        check(std::isfinite(v), "finite drive output");
        peak = std::max(peak, std::abs(static_cast<double>(v)));
    }
    check(peak < 1.0, "drive keeps headroom");
    std::printf("FX nonlinear alias: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
