// Nonlinear DSP regression screen for the shared FX slots.
//
// Shipping contracts verified here (TL review 7211163434):
//  - Wavefold ships as the ORIGINAL memoryless triangle fold with the +/-8
//    input clamp (#156 5c2f122). Its rendered output is compared sample by
//    sample against that pre-change reference, including over-range input
//    where the clamp actually binds, and the measured lag between render and
//    reference must be zero samples (latency is measured, never inferred
//    from "no buffering/PDC").
//  - Drive keeps the gain-compensated parallel saturator with first-order
//    ADAA. It is compared against the legacy point-sampled transfer for
//    level/DC compatibility, and its folded energy must stay below the
//    direct-tanh reference.
//  - experimental::wavefoldAdaa is measured as a separately versioned
//    candidate only; Chain never wires it.
//
// Every measurement fails explicitly when prepare() or set() fails or when
// the output is silent: a zero alias ratio alone is never accepted as
// evidence, and zero-amount transparency is asserted sample by sample, not
// via an alias ratio. Coverage also includes passband level/phase, impulse
// and step transients, dry/wet mixing, host-style amount automation through
// the modulation array, and amount/wet/sample-rate boundaries.
#include "engine/Effects.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

namespace {
constexpr int window = 16384;
constexpr double pi = 3.14159265358979323846;
// The wet/amount smoothers converge to within about half an ulp of their
// targets, so any comparison against a target-valued reference is bounded by
// one fold slope (8x) times that residual — below 1.5e-4 across 44.1–96 kHz.
// A one-sample shift of the stimulus is orders of magnitude larger, so the
// lag discrimination stays meaningful.
constexpr double smoothedTolerance = 1.5e-4;

struct Measurement {
    bool ok = false;      // prepare+set succeeded and the output is non-silent
    double harmonic = 0;  // energy at the input harmonic bins
    double aliased = 0;   // generated energy folded back below Nyquist
    double ratio = 0;     // aliased / total
    double rms = 0;
    double peak = 0;
};

// Positive-side energy of a buffer split into harmonic and generated energy.
// ok is false when the buffer is silent or non-finite, so a zero ratio can
// never be reported as a measurement of nothing.
Measurement analyse(const std::vector<float>& left, int fundamentalBin) {
    Measurement result;
    double timeEnergy = 0;
    for (int i = 0; i < window; ++i) {
        const double x = left[i];
        if (!std::isfinite(x)) return result;
        timeEnergy += x * x;
        result.peak = std::max(result.peak, std::abs(x));
    }
    const double total = .5 * window * timeEnergy;
    if (!(result.peak > 1e-4) || !(total > 0)) return result; // silent/non-measurable
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
    result.ratio = result.aliased / total;
    result.rms = std::sqrt(timeEnergy / window);
    result.ok = std::isfinite(result.ratio) && result.ratio >= 0 && result.ratio <= 1;
    return result;
}

// The shared float stimulus: one bin-centred sine, generated once so the
// Chain render and every point-sampled reference consume identical samples.
std::vector<float> stimulus(double rate, int fundamentalBin, double amplitude,
                            std::size_t extraPrefix = 0) {
    const double f0 = fundamentalBin * rate / window;
    std::vector<float> input(extraPrefix + window);
    for (std::size_t i = 0; i < input.size(); ++i)
        input[i] = static_cast<float>(amplitude * std::sin(2 * pi * f0 * static_cast<double>(i) / rate));
    return input;
}

// Legacy point-sampled Drive transfer (#156 5c2f122), evaluated on the same
// float samples the Chain consumes. Reference for level/DC compatibility.
std::vector<float> directDrive(const std::vector<float>& input, float amount) {
    const double gain = 1.0 + 7.0 * amount;
    const double level = 1.0 / std::sqrt(gain);
    std::vector<float> out(input.size());
    for (std::size_t i = 0; i < input.size(); ++i) {
        const double x = input[i];
        out[i] = static_cast<float>(x + amount * (std::tanh(x * gain) * level - x));
    }
    return out;
}

// Pre-change Wavefold reference: the original memoryless triangle fold WITH
// the +/-8 clamp, evaluated on the same float samples the Chain consumes.
// `clampIt` exists only to prove that the clamp actually binds on over-range
// input; the shipping comparison always uses the clamped form.
std::vector<float> directWavefold(const std::vector<float>& input, float amount, bool clampIt = true) {
    const double gain = 1.0 + 7.0 * amount;
    std::vector<float> out(input.size());
    for (std::size_t i = 0; i < input.size(); ++i) {
        const double raw = static_cast<double>(input[i]) * gain;
        const double scaled = clampIt ? std::clamp(raw, -8.0, 8.0) : raw;
        out[i] = static_cast<float>((2.0 / pi) * std::asin(std::sin(pi * scaled * .5)));
    }
    return out;
}

Measurement analyseRender(const std::vector<float>& rendered, int fundamentalBin, std::size_t settle) {
    std::vector<float> settled(rendered.begin() + static_cast<std::ptrdiff_t>(settle), rendered.end());
    return analyse(settled, fundamentalBin);
}

// Renders `input` through one Chain slot after letting the wet/amount
// smoothers reach their targets. prepare/set failure yields ok=false.
Measurement measure(vox::fx::effects::Type type, float amount, float wet,
                    double rate, int fundamentalBin, double amplitude,
                    const std::vector<float>* modulation = nullptr) {
    Measurement result;
    vox::fx::effects::Chain chain;
    if (!chain.prepare(rate) || !chain.set(0, {type, wet, amount})) return result;
    const std::size_t settle = static_cast<std::size_t>(rate * .2);
    auto input = stimulus(rate, fundamentalBin, amplitude, settle);
    std::vector<float> right = input;
    chain.process(input.data(), right.data(), input.size(), modulation ? modulation->data() : nullptr);
    return analyseRender(input, fundamentalBin, settle);
}

double maxAbsDifference(const std::vector<float>& a, const std::vector<float>& b,
                        std::size_t begin = 0) {
    double worst = 0;
    for (std::size_t i = begin; i < a.size(); ++i)
        worst = std::max(worst, std::abs(static_cast<double>(a[i] - b[i])));
    return worst;
}

// Squared error between the render and the reference shifted by `lag`
// samples, over the settled window where both are defined.
double lagError(const std::vector<float>& render, const std::vector<float>& reference,
                std::size_t settle, int lag) {
    std::size_t lo = settle, hi = render.size();
    if (lag > 0) hi -= static_cast<std::size_t>(lag);
    if (lag < 0) lo += static_cast<std::size_t>(-lag);
    if (lo >= hi || hi > reference.size()) return 1e30;
    double error = 0;
    for (std::size_t i = lo; i < hi; ++i) {
        const double d = static_cast<double>(render[i]) -
                         reference[static_cast<std::ptrdiff_t>(i) + lag];
        error += d * d;
    }
    return error;
}

// Sample-lag scan: how far the reference must be shifted to best match the
// render. A real zero-lag match at lag 0 with much larger error at +/-1 is
// measured evidence of no added delay, not an inference from buffering.
int bestLag(const std::vector<float>& render, const std::vector<float>& reference,
            std::size_t settle, double& errorAtZero) {
    constexpr int span = 4;
    double best = 1e30; int bestLagValue = 99;
    for (int lag = -span; lag <= span; ++lag) {
        const double error = lagError(render, reference, settle, lag);
        if (lag == 0) errorAtZero = error;
        if (error < best) { best = error; bestLagValue = lag; }
    }
    return bestLagValue;
}
} // namespace

int main() {
    using vox::fx::effects::Chain;
    using vox::fx::effects::Type;
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        if (!ok) { std::printf("FAIL: %s\n", what); ++failures; }
    };

    // ---------------------------------------------------------------------
    // 1. Alias screen: Drive (shipping ADAA) and Wavefold (shipping
    //    memoryless) against their point-sampled references, plus the
    //    experimental ADAA fold measured separately. Every render must
    //    prepare, set and produce measurable non-silent output first.
    // ---------------------------------------------------------------------
    for (const double rate : {44100.0, 48000.0, 96000.0}) {
        for (const int bin : {683, 1707, 2731}) {
            for (const float amount : {.5f, 1.0f}) {
                const auto drive = measure(Type::drive, amount, 1.0f, rate, bin, 1.0);
                const auto fold = measure(Type::wavefold, amount, 1.0f, rate, bin, 1.0);
                check(drive.ok, "Drive render prepared, set and produced non-silent output");
                check(fold.ok, "Wavefold render prepared, set and produced non-silent output");
                if (!drive.ok || !fold.ok) continue;
                check(drive.rms > .05 && drive.peak > .05,
                      "Drive output has measurable level (not silence)");
                check(fold.rms > .05 && fold.peak > .05 && fold.peak <= 1.000001,
                      "Wavefold output has measurable level and stays within the +/-1 fold bound");

                const auto input = stimulus(rate, bin, 1.0);
                const auto direct = analyse(directDrive(input, amount), bin);
                const auto directFold = analyse(directWavefold(input, amount), bin);
                check(direct.ok && directFold.ok,
                      "point-sampled references produce measurable non-silent output");

                std::printf("%5.0f Hz f0 %7.1f amt %.1f: direct tanh %.4f (-%4.1f dB), "
                            "ADAA Drive %.4f (-%4.1f dB), pre-change fold %.4f (-%4.1f dB), "
                            "shipping fold %.4f (-%4.1f dB)\n",
                            rate, bin * rate / window, amount, direct.ratio,
                            -10 * std::log10(direct.ratio + 1e-12), drive.ratio,
                            -10 * std::log10(drive.ratio + 1e-12), directFold.ratio,
                            -10 * std::log10(directFold.ratio + 1e-12), fold.ratio,
                            -10 * std::log10(fold.ratio + 1e-12));

                // Drive: measured against the same transfer function
                // evaluated point-sampled, ADAA must remove a clear share of
                // the folded energy at every probe.
                check(drive.ratio <= .55 * direct.ratio + 1e-6,
                      "antialiased Drive folds less energy than direct tanh");
                check(drive.ratio < .06, "antialiased Drive keeps folded energy low");

                // Shipping Wavefold IS the pre-change point-sampled transfer;
                // its folded energy must match the reference, not merely be
                // non-increasing.
                check(std::abs(fold.ratio - directFold.ratio) <= 2e-3,
                      "shipping Wavefold matches the pre-change reference alias energy");

                // Experimental ADAA fold (not shipping): measured for the
                // record, must never be worse than the direct transfer.
                std::vector<float> adaaInput = input;
                double previous = 0;
                for (auto& x : adaaInput) {
                    x = static_cast<float>(
                        vox::fx::effects::experimental::wavefoldAdaa(x, previous, 1.0 + 7.0 * amount));
                }
                const auto adaa = analyse(adaaInput, bin);
                check(adaa.ok && adaa.rms > .05,
                      "experimental ADAA fold produces measurable non-silent output");
                check(adaa.ratio <= directFold.ratio + 1e-6,
                      "experimental ADAA fold never increases folded energy");
                if (directFold.ratio > .005)
                    check(adaa.ratio < directFold.ratio,
                          "experimental ADAA fold reduces folded energy where folding exists");
            }
        }
        // Zero amount: explicit transparency, asserted sample by sample with
        // measurable level. The alias ratio is only a secondary guard and is
        // meaningless without ok/level above.
        const auto foldBypassed = measure(Type::wavefold, 0.0f, 1.0f, rate, 1707, .4);
        check(foldBypassed.ok && foldBypassed.rms > .05,
              "zero Wavefold amount still renders measurable level");
        {
            const auto settle = static_cast<std::size_t>(rate * .2);
            auto input = stimulus(rate, 1707, .4, settle);
            Chain chain;
            check(chain.prepare(rate) && chain.set(0, {Type::wavefold, 1.0f, 0.0f}),
                  "zero-amount Wavefold slot accepts settings");
            auto rendered = input;
            chain.process(rendered.data(), rendered.data(), rendered.size());
            double worst = 0;
            for (std::size_t i = settle; i < rendered.size(); ++i)
                worst = std::max(worst, std::abs(static_cast<double>(rendered[i] - input[i])));
            check(worst < 1e-6,
                  "zero Wavefold amount is sample-transparent (explicit, not an alias ratio)");
            check(foldBypassed.ratio < 1e-9,
                  "zero Wavefold amount generates no measurable folded energy");
        }
        const auto driveBypassed = measure(Type::drive, 0.0f, 1.0f, rate, 1707, .4);
        check(driveBypassed.ok && driveBypassed.rms > .05,
              "zero Drive amount still renders measurable level");
        {
            const auto settle = static_cast<std::size_t>(rate * .2);
            auto input = stimulus(rate, 1707, .4, settle);
            Chain chain;
            check(chain.prepare(rate) && chain.set(0, {Type::drive, 1.0f, 0.0f}),
                  "zero-amount Drive slot accepts settings");
            auto rendered = input;
            chain.process(rendered.data(), rendered.data(), rendered.size());
            double worst = 0;
            for (std::size_t i = settle; i < rendered.size(); ++i)
                worst = std::max(worst, std::abs(static_cast<double>(rendered[i] - input[i])));
            check(worst < 1e-6,
                  "zero Drive amount is sample-transparent (explicit, not an alias ratio)");
            check(driveBypassed.ratio < 1e-9,
                  "zero Drive amount generates no measurable folded energy");
        }
    }

    // ---------------------------------------------------------------------
    // 2. Shipping Wavefold equals the actual pre-change reference, sample by
    //    sample, with measured zero lag — including over-range input where
    //    the +/-8 clamp binds.
    // ---------------------------------------------------------------------
    for (const double amplitude : {1.0, 2.0}) {
        for (const int bin : {683, 1707}) {
            for (const float amount : {.5f, 1.0f}) {
                const double rate = 48000.0;
                const auto settle = static_cast<std::size_t>(rate * .2);
                auto input = stimulus(rate, bin, amplitude, settle);
                Chain chain;
                check(chain.prepare(rate) && chain.set(0, {Type::wavefold, 1.0f, amount}),
                      "Wavefold reference fixture accepts settings");
                auto rendered = input;
                chain.process(rendered.data(), rendered.data(), rendered.size());
                const auto reference = directWavefold(input, amount);
                const auto referenceNoClamp = directWavefold(input, amount, false);
                double errorAtZero = 0;
                const int lag = bestLag(rendered, reference, settle, errorAtZero);
                const double errorShifted = std::min(lagError(rendered, reference, settle, 1),
                                                     lagError(rendered, reference, settle, -1));
                const double worst = maxAbsDifference(rendered, reference, settle);
                const double worstNoClamp = maxAbsDifference(rendered, referenceNoClamp, settle);

                std::printf("wavefold reference amp %.1f f0 %7.1f amt %.1f: max diff %.3e, "
                            "best lag %d, error lag0 %.3e, unclamped ref diff %.3e\n",
                            amplitude, bin * rate / window, amount, worst, lag, errorAtZero,
                            worstNoClamp);

                check(lag == 0 && worst < smoothedTolerance,
                      "shipping Wavefold matches the pre-change reference with measured zero lag");
                check(errorShifted > 100 * errorAtZero + 1e-12,
                      "one-sample shift is far worse: zero lag is measured, not assumed");
                for (std::size_t i = settle; i < rendered.size(); ++i)
                    check(std::isfinite(rendered[i]) && std::abs(rendered[i]) <= 1.000001f,
                          "shipping Wavefold output finite and bounded");
                if (amplitude > 1.0) {
                    check(worstNoClamp > 1e-3,
                          "over-range fixture actually exercises the +/-8 clamp");
                    check(worst < worstNoClamp,
                          "shipping Wavefold follows the clamped reference, not the unclamped one");
                }
            }
        }
    }

    // ---------------------------------------------------------------------
    // 3. Passband: below the fold knee the transfer is a pure gain of
    //    (1+7*amount) — level and phase of the fundamental must survive
    //    with no distortion products.
    // ---------------------------------------------------------------------
    {
        const double rate = 48000.0;
        const int bin = 64; // 187.5 Hz: gain*amplitude stays below the fold knee
        const float amount = .5f;
        const double gain = 1.0 + 7.0 * amount; // 4.5; 4.5 * .2 = .9 < 1 knee
        const auto settle = static_cast<std::size_t>(rate * .2);
        auto input = stimulus(rate, bin, .2, settle);
        Chain chain;
        check(chain.prepare(rate) && chain.set(0, {Type::wavefold, 1.0f, amount}),
              "passband fixture accepts settings");
        auto rendered = input;
        chain.process(rendered.data(), rendered.data(), rendered.size());
        std::vector<float> expected(input.size());
        for (std::size_t i = 0; i < input.size(); ++i)
            expected[i] = static_cast<float>(static_cast<double>(input[i]) * gain);
        double worst = 0, rms = 0, harmonic = 0, total = 0;
        std::vector<float> settled(rendered.begin() + static_cast<std::ptrdiff_t>(settle), rendered.end());
        std::vector<float> expectedSettled(expected.begin() + static_cast<std::ptrdiff_t>(settle), expected.end());
        for (std::size_t i = 0; i < settled.size(); ++i) {
            worst = std::max(worst, std::abs(static_cast<double>(settled[i] - expectedSettled[i])));
            const double x = settled[i];
            total += x * x;
        }
        for (int b = bin; b <= window / 2; b += bin) {
            double coeff = 2 * std::cos(2 * pi * b / window), s1 = 0, s2 = 0;
            for (int i = 0; i < window; ++i) {
                const double s = settled[i] + coeff * s1 - s2;
                s2 = s1; s1 = s;
            }
            harmonic += s1 * s1 + s2 * s2 - coeff * s1 * s2;
        }
        rms = std::sqrt(total / window);
        check(rms > .1, "passband render has measurable level");
        check(worst < 1e-6, "passband level matches the linear gain sample by sample");
        check(harmonic / total > .99, "passband energy stays on the input fundamental");
        double errorAtZero = 0;
        check(bestLag(rendered, expected, settle, errorAtZero) == 0 && errorAtZero < 1e-8,
              "passband phase aligned with measured zero lag");
    }

    // ---------------------------------------------------------------------
    // 4. Transients: the shipping fold is memoryless — an impulse maps
    //    through the transfer once and produces no tail; a step produces a
    //    constant. Drive's ADAA spreads an impulse over two samples and then
    //    settles to exact silence.
    // ---------------------------------------------------------------------
    {
        const double rate = 48000.0;
        Chain fold;
        check(fold.prepare(rate) && fold.set(0, {Type::wavefold, 1.0f, 1.0f}),
              "transient Wavefold fixture accepts settings");
        std::vector<float> l(512), r(512);
        for (int block = 0; block < 4; ++block) fold.process(l.data(), r.data(), l.size()); // settle
        l[0] = .3f; r[0] = .3f;
        fold.process(l.data(), r.data(), l.size());
        check(std::isfinite(l[0]) && std::abs(l[0]) > 1e-3 && std::abs(l[0]) <= 1.0f,
              "wavefold impulse maps through the transfer");
        for (std::size_t i = 1; i < l.size(); ++i)
            check(l[i] == 0.0f,
                  "memoryless wavefold impulse produces no tail, state or ringing");
        l.assign(l.size(), 0.f); r.assign(r.size(), 0.f);
        for (int block = 0; block < 30; ++block) fold.process(l.data(), r.data(), l.size());
        l.assign(l.size(), .3f); r.assign(r.size(), .3f);
        double stepMin = 1e30, stepMax = -1e30;
        for (int block = 0; block < 8; ++block) {
            fold.process(l.data(), r.data(), l.size());
            for (float v : l) {
                stepMin = std::min(stepMin, static_cast<double>(v));
                stepMax = std::max(stepMax, static_cast<double>(v));
            }
            l.assign(l.size(), .3f); r.assign(r.size(), .3f);
        }
        const double expected =
            directWavefold(std::vector<float>{.3f}, 1.0f)[0];
        check(stepMax - stepMin < 1e-4 &&
              std::abs(stepMin - expected) < 1e-3 && std::abs(stepMax - expected) < 1e-3,
              "wavefold step response is a constant: pure transfer, no memory");

        Chain drive;
        check(drive.prepare(rate) && drive.set(0, {Type::drive, 1.0f, 1.0f}),
              "transient Drive fixture accepts settings");
        for (int block = 0; block < 4; ++block) drive.process(l.data(), r.data(), l.size());
        l.assign(l.size(), 0.f); r.assign(r.size(), 0.f);
        l[0] = .3f; r[0] = .3f;
        drive.process(l.data(), r.data(), l.size());
        double tail = 0;
        for (std::size_t i = 2; i < l.size(); ++i) tail += std::abs(l[i]);
        check(std::isfinite(l[0]) && std::abs(l[0]) < 1.0f,
              "Drive impulse response finite and bounded");
        check(tail == 0.0,
              "Drive ADAA impulse settles to exact silence within two samples");
    }

    // ---------------------------------------------------------------------
    // 5. Dry/wet: wet 0 is bit-exact dry, wet 1 is the full transfer, wet
    //    .5 is the arithmetic midpoint.
    // ---------------------------------------------------------------------
    {
        const double rate = 48000.0;
        const auto settle = static_cast<std::size_t>(rate * .2);
        auto input = stimulus(rate, 1707, .4, settle);
        const auto full = directWavefold(input, .7f);
        const float wets[] = {0.f, .5f, 1.f};
        std::vector<std::vector<float>> rendered;
        for (const float wet : wets) {
            Chain chain;
            check(chain.prepare(rate) && chain.set(0, {Type::wavefold, wet, .7f}),
                  "dry/wet fixture accepts settings");
            auto buffer = input;
            chain.process(buffer.data(), buffer.data(), buffer.size());
            rendered.push_back(std::move(buffer));
        }
        double dry = 0, wetFull = 0, mid = 0, midVsDry = 0, midVsWet = 0;
        for (std::size_t i = settle; i < input.size(); ++i) {
            dry = std::max(dry, std::abs(static_cast<double>(rendered[0][i] - input[i])));
            wetFull = std::max(wetFull, std::abs(static_cast<double>(rendered[2][i] - full[i])));
            mid = std::max(mid, std::abs(static_cast<double>(
                rendered[1][i] - (.5f * (input[i] + full[i])))));
            midVsDry = std::max(midVsDry, std::abs(static_cast<double>(rendered[1][i] - input[i])));
            midVsWet = std::max(midVsWet, std::abs(static_cast<double>(rendered[1][i] - full[i])));
        }
        check(dry == 0.0, "wet 0 is bit-exact dry output");
        check(wetFull < smoothedTolerance, "wet 1 is the full fold transfer");
        check(mid < smoothedTolerance, "wet .5 is the arithmetic midpoint of dry and wet");
        check(midVsDry > 1e-3 && midVsWet > 1e-3,
              "wet .5 differs from both endpoints (mix actually applied)");
    }

    // ---------------------------------------------------------------------
    // 6. Automation: the per-slot modulation array drives wet/amount like a
    //    host automation lane — bounded, clamped, converged and audible.
    // ---------------------------------------------------------------------
    {
        const double rate = 48000.0;
        const auto settle = static_cast<std::size_t>(rate * .2);
        auto input = stimulus(rate, 1707, .4, settle);
        const auto render = [&](float baseWet, float baseAmount, const std::vector<float>& route) {
            Chain chain;
            check(chain.prepare(rate) && chain.set(0, {Type::wavefold, baseWet, baseAmount}),
                  "automation fixture accepts settings");
            auto buffer = input;
            chain.process(buffer.data(), buffer.data(), buffer.size(), route.data());
            return buffer;
        };
        const std::vector<float> none(8, 0.f);
        const std::vector<float> amountUp{0.f, .5f, 0, 0, 0, 0, 0, 0};    // amount .5 -> 1
        const std::vector<float> wetDown{-1.f, 0.f, 0, 0, 0, 0, 0, 0};    // wet 1 -> 0
        const std::vector<float> clamped{0.f, 10.f, 0, 0, 0, 0, 0, 0};    // amount clamps at 1
        const auto base = render(1.f, .5f, none);
        const auto up = render(1.f, .5f, amountUp);
        const auto down = render(1.f, .5f, wetDown);
        const auto over = render(1.f, .5f, clamped);
        const auto amountOne = render(1.f, 1.f, none);
        double diffUp = 0, diffDown = 0, matchUp = 0, matchDown = 0, matchClamp = 0, bounded = 0;
        bool finite = true;
        for (std::size_t i = settle; i < input.size(); ++i) {
            diffUp = std::max(diffUp, std::abs(static_cast<double>(up[i] - base[i])));
            diffDown = std::max(diffDown, std::abs(static_cast<double>(down[i] - input[i])));
            matchUp = std::max(matchUp, std::abs(static_cast<double>(up[i] - amountOne[i])));
            matchDown = std::max(matchDown, std::abs(static_cast<double>(down[i] - input[i])));
            matchClamp = std::max(matchClamp, std::abs(static_cast<double>(over[i] - amountOne[i])));
            bounded = std::max(bounded, std::abs(static_cast<double>(up[i])));
            finite &= std::isfinite(up[i]) && std::isfinite(down[i]) && std::isfinite(over[i]);
        }
        check(finite && bounded <= 1.000001, "modulated output finite and bounded");
        check(diffUp > 1e-3, "amount modulation audibly changes the render");
        check(matchUp < smoothedTolerance, "amount +0.5 converges to the amount-1 transfer");
        check(matchDown < 1e-6, "wet -1 clamps to bit-exact dry");
        check(matchClamp < smoothedTolerance, "out-of-range amount modulation clamps at the host maximum");
        check(diffDown < 1e-9, "wet modulation to zero removes the effect entirely");
    }

    // ---------------------------------------------------------------------
    // 7. Boundaries: prepare/set validation, amount 0/1 and wet edges must
    //    fail or behave explicitly — never silently.
    // ---------------------------------------------------------------------
    {
        Chain chain;
        check(!chain.prepare(0) && !chain.prepare(7999) && !chain.prepare(384001) &&
              !chain.prepare(std::nan("")) && !chain.prepare(std::numeric_limits<double>::infinity()),
              "prepare rejects out-of-range and non-finite rates");
        check(chain.prepare(8000) && chain.prepare(384000), "prepare accepts supported rate edges");
        check(!chain.set(4, {}) &&
              !chain.set(0, {Type::wavefold, std::nan(""), 0.f}) &&
              !chain.set(0, {Type::wavefold, 1.5f, 0.f}) &&
              !chain.set(0, {Type::wavefold, 1.f, -0.1f}) &&
              !chain.set(0, {Type::wavefold, 1.f, std::nan("")}) &&
              !chain.set(0, {Type::wavefold, 1.f, 2.f}) &&
              !chain.set(0, {static_cast<Type>(200), 1.f, .5f}),
              "set rejects invalid slot, wet, amount and type");
        check(chain.set(0, {Type::wavefold, 1.f, 1.f}),
              "amount/wet upper boundary is accepted");
        // Amount 1 full fold, bounded and reference-matched.
        const double rate = 48000.0;
        const auto settle = static_cast<std::size_t>(rate * .2);
        auto input = stimulus(rate, 1707, 1.0, settle);
        check(chain.prepare(rate) && chain.set(0, {Type::wavefold, 1.0f, 1.0f}),
              "amount-1 fixture accepts settings");
        auto rendered = input;
        chain.process(rendered.data(), rendered.data(), rendered.size());
        const auto reference = directWavefold(input, 1.0f);
        const double worst = maxAbsDifference(rendered, reference, settle);
        check(worst < smoothedTolerance, "amount 1 matches the pre-change reference");
        double rms = 0;
        for (std::size_t i = settle; i < rendered.size(); ++i) {
            check(std::isfinite(rendered[i]) && std::abs(rendered[i]) <= 1.000001f,
                  "amount-1 output finite and bounded");
            rms += rendered[i] * rendered[i];
        }
        check(std::sqrt(rms / window) > .05, "amount-1 output is measurably non-silent");
    }

    // ---------------------------------------------------------------------
    // 8. Drive old-project compatibility: the shipping ADAA Drive must keep
    //    the legacy transfer's level and DC within tight bounds (automated
    //    compatibility evidence; listening remains an owner gate).
    // ---------------------------------------------------------------------
    {
        const double rate = 48000.0;
        const auto settle = static_cast<std::size_t>(rate * .2);
        for (const float amount : {.3f, .7f, 1.0f}) {
            auto input = stimulus(rate, 1707, .4, settle);
            Chain chain;
            check(chain.prepare(rate) && chain.set(0, {Type::drive, 1.0f, amount}),
                  "Drive compatibility fixture accepts settings");
            auto rendered = input;
            chain.process(rendered.data(), rendered.data(), rendered.size());
            const auto legacy = directDrive(input, amount);
            double rmsNew = 0, rmsOld = 0, meanNew = 0, meanOld = 0, peakNew = 0, peakOld = 0;
            bool finite = true;
            for (std::size_t i = settle; i < rendered.size(); ++i) {
                const double n = rendered[i], o = legacy[i];
                finite &= std::isfinite(n);
                rmsNew += n * n; rmsOld += o * o;
                meanNew += n; meanOld += o;
                peakNew = std::max(peakNew, std::abs(n));
                peakOld = std::max(peakOld, std::abs(o));
            }
            const double count = static_cast<double>(window);
            rmsNew = std::sqrt(rmsNew / count); rmsOld = std::sqrt(rmsOld / count);
            meanNew /= count; meanOld /= count;
            check(finite && rmsOld > .01, "legacy Drive reference has measurable level");
            check(std::abs(rmsNew - rmsOld) / rmsOld < .05,
                  "ADAA Drive keeps legacy RMS level within 5 percent");
            check(std::abs(meanNew - meanOld) < .01,
                  "ADAA Drive keeps legacy DC within 0.01");
            check(std::abs(peakNew - peakOld) / std::max(peakOld, 1e-9) < .05,
                  "ADAA Drive keeps legacy peak within 5 percent");
            std::printf("Drive compat amt %.1f: RMS legacy %.5f vs ADAA %.5f, DC %.5f vs %.5f\n",
                        amount, rmsOld, rmsNew, meanOld, meanNew);
        }
    }

    // Structural smoke: headroom through a real Drive slot.
    {
        Chain chain;
        check(chain.prepare(48000) && chain.set(0, {Type::drive, 1, .8f}),
              "drive slot accepts settings");
        std::vector<float> l(512), r(512);
        for (std::size_t i = 0; i < l.size(); ++i) l[i] = r[i] = static_cast<float>(.4 * std::sin(i * .05));
        chain.process(l.data(), r.data(), l.size());
        double peak = 0;
        for (float v : l) {
            check(std::isfinite(v), "finite drive output");
            peak = std::max(peak, std::abs(static_cast<double>(v)));
        }
        check(peak < 1.0, "drive keeps headroom");
    }

    std::printf("FX nonlinear alias: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
