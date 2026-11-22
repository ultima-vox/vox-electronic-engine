// Reproducible Chain-level Wavefold diagnostic; build against either Effects.h.
// stdout contains little-endian IEEE float32 mono samples, after warm-up.
#include "engine/Effects.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

int main(int argc, char** argv)
{
    if (argc != 5) return 2;
    const int rate = std::atoi(argv[1]), mode = std::atoi(argv[2]);
    const float wet = static_cast<float>(std::atof(argv[3]));
    const int slots = std::atoi(argv[4]);
    if (rate < 8000 || (mode != 0 && mode != 1) || wet < 0 || wet > 1 || slots < 1 || slots > 4) return 2;
    vox::fx::effects::Chain chain;
    if (!chain.prepare(rate)) return 3;
    for (int s = 0; s < slots; ++s)
        if (!chain.set(static_cast<std::size_t>(s), {vox::fx::effects::Type::wavefold, wet, 1.f})) return 3;
    // An integral number of periods follows at least half a second of warm-up.
    const int period = 256, settle = ((rate / 2 + period - 1) / period) * period;
    const int capture = mode == 0 ? 65536 : rate * 2;
    std::vector<float> left(static_cast<std::size_t>(settle + capture)), right(left.size());
    constexpr double pi = 3.14159265358979323846;
    for (int i = 0; i < settle + capture; ++i) {
        const int phase = i % period;
        // Construct opposite half cycles by negation, not a second sin call.
        const double symmetric = (phase < period / 2 ? 1. : -1.) * std::sin(2 * pi * (phase % (period / 2)) / period);
        const double x = mode == 0 ? .7 * symmetric : .35 * symmetric + .2 * std::sin(2 * pi * 3301. * i / rate);
        left[static_cast<std::size_t>(i)] = static_cast<float>(x);
    }
    right = left;
    // Small blocks exercise the same slot transitions and parameter smoothing.
    for (int position = 0; position < settle + capture; position += 64) {
        const int n = std::min(64, settle + capture - position);
        if (mode == 1 && position >= settle) {
            const double t = static_cast<double>(position - settle) / rate;
            const float automatedWet = static_cast<float>(std::clamp(t / .5, 0., 1.)) * wet;
            for (int s = 0; s < slots; ++s) {
                const auto type = (s == 0 && t >= 1.0 && t < 1.15)
                                    ? vox::fx::effects::Type::bypass : vox::fx::effects::Type::wavefold;
                if (!chain.set(static_cast<std::size_t>(s), {type, automatedWet, 1.f})) return 3;
            }
        }
        chain.process(left.data() + position, right.data() + position, static_cast<std::size_t>(n));
    }
    return std::fwrite(left.data() + settle, sizeof(float), capture, stdout) == static_cast<std::size_t>(capture) ? 0 : 4;
}
