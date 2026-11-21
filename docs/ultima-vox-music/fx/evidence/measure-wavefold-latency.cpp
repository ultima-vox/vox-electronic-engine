// FX-04 Wavefold anti-aliasing — group-delay probe (evidence tool).
//
// Prototype-only diagnostic; not a test and not linked into a product.
// A bin-centred low-level tone keeps Wavefold inside its |u|<=1 linear region
// (gain 1+7*0.28 at amplitude 0.1), so the measured phase lag is the method's
// group delay. Compare with the 0.5-sample ADAA1 delay of Drive's wet term,
// reduced by its parallel dry path. Build and run from the repository root:
//
//   g++ -std=c++20 -O2 -Iproducts/fx
//       docs/ultima-vox-music/fx/evidence/measure-wavefold-latency.cpp -o /tmp/wflat
//   /tmp/wflat
#include "engine/Effects.h"
#include <cmath>
#include <cstdio>
#include <vector>

namespace {
constexpr double pi = 3.14159265358979323846;

double groupDelay(vox::fx::effects::Type type, float amount, double rate, int bin) {
    constexpr int window = 32768;
    const auto settle = static_cast<std::size_t>(rate * .25);
    const double f0 = bin * rate / window;
    std::vector<float> input(settle + window), left, right;
    for (std::size_t i = 0; i < input.size(); ++i)
        input[i] = static_cast<float>(.1 * std::sin(2 * pi * f0 * i / rate));
    left = input;
    right = input;
    vox::fx::effects::Chain chain;
    if (!chain.prepare(rate) || !chain.set(0, {type, 1, amount})) return -1;
    chain.process(left.data(), right.data(), left.size());
    const auto amplitude = [&](const std::vector<float>& v) {
        double re = 0, im = 0;
        for (int i = 0; i < window; ++i) {
            const double w = 2 * pi * f0 * i / rate;
            re += v[settle + static_cast<std::size_t>(i)] * std::cos(w);
            im -= v[settle + static_cast<std::size_t>(i)] * std::sin(w);
        }
        return std::pair(re, im);
    };
    const auto in = amplitude(input), out = amplitude(left);
    double phase = std::atan2(out.second, out.first) - std::atan2(in.second, in.first);
    while (phase > pi) phase -= 2 * pi;
    while (phase < -pi) phase += 2 * pi;
    return -phase / (2 * pi * f0 / rate);
}
} // namespace

int main() {
    for (const double rate : {44100.0, 48000.0, 96000.0})
        for (const int bin : {341, 683})
            std::printf("%5.0f Hz f %7.1f: Wavefold %.3f samples, Drive %.3f samples\n",
                        rate, bin * rate / 32768,
                        groupDelay(vox::fx::effects::Type::wavefold, .28f, rate, bin),
                        groupDelay(vox::fx::effects::Type::drive, .28f, rate, bin));
    return 0;
}
