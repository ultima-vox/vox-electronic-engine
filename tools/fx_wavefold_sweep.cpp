// Wavefold ADAA2 frequency-response diagnostic; build against either Effects.h.
// A bin-centred, low-level sine keeps one slot in its linear region, so the
// measured output/input ratio is the method's own linear response. Run from
// tools/compare_wavefold_response.py; not part of the product.
#include "engine/Effects.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {
constexpr double pi = 3.14159265358979323846;
constexpr int window = 32768;

std::complex<double> fundamental(const std::vector<float>& samples,
                                 std::size_t offset, double rate, double f0) {
    double re = 0, im = 0;
    for (int i = 0; i < window; ++i) {
        const double w = 2 * pi * f0 * i / rate;
        const double x = samples[offset + static_cast<std::size_t>(i)];
        re += x * std::cos(w);
        im -= x * std::sin(w);
    }
    return {re, im};
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 6) return 2;
    const double rate = std::atof(argv[1]);
    const float amount = static_cast<float>(std::atof(argv[2]));
    const float wet = static_cast<float>(std::atof(argv[3]));
    const int slots = std::atoi(argv[4]);
    const double amplitude = std::atof(argv[5]);
    if (rate < 8000 || amount < 0 || amount > 1 || wet < 0 || wet > 1 ||
        slots < 1 || slots > 4 || amplitude <= 0 || amplitude > 1) return 2;
    const auto settle = static_cast<std::size_t>(rate * .25);
    for (double f = 100; f < rate * .47; f *= std::pow(1.5, 1.0 / 3)) {
        const int bin = static_cast<int>(std::lround(window * f / rate));
        if (bin < 2 || bin > window / 2) continue;
        const double f0 = bin * rate / window;
        std::vector<float> left(settle + window), right;
        for (std::size_t i = 0; i < left.size(); ++i)
            left[i] = static_cast<float>(amplitude * std::sin(2 * pi * f0 * i / rate));
        right = left;
        const std::vector<float> input(left.begin() + static_cast<std::ptrdiff_t>(settle), left.end());
        vox::fx::effects::Chain chain;
        if (!chain.prepare(rate)) return 3;
        for (int s = 0; s < slots; ++s)
            if (!chain.set(static_cast<std::size_t>(s), {vox::fx::effects::Type::wavefold, wet, amount})) return 3;
        // Small blocks exercise the same smoothing and crossfade paths.
        for (std::size_t position = 0; position < left.size(); position += 64)
            chain.process(left.data() + position, right.data() + position,
                          std::min<std::size_t>(64, left.size() - position));
        const auto h = fundamental(left, settle, rate, f0) / fundamental(input, 0, rate, f0);
        if (!std::isfinite(h.real()) || !std::isfinite(h.imag()) || std::abs(h) < 1e-12) return 4;
        std::printf("%.0f,%.6f,%.6f,%d,%.6f,%.9f,%.9f\n",
                    rate, amount, wet, slots, f0, 20 * std::log10(std::abs(h)),
                    std::arg(h) * 180 / pi);
    }
    return 0;
}
