#include "engine/NoiseRiser.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

int main() {
    int failures = 0;
    auto check = [&](bool ok, const char* msg) { if (!ok) { std::printf("FAIL: %s\n", msg); ++failures; } };
    for (double rate : {44100.0, 48000.0, 96000.0}) for (int block : {64, 128, 512}) {
        vox::fx::NoiseRiser engine;
        vox::fx::NoiseProgram program; program.layers[0].family = vox::fx::NoiseFamily::downlifter;
        engine.prepare(rate); check(engine.setProgram(program) && engine.noteOn(1, 60, 1, 2), "downlifter starts");
        std::vector<float> left(static_cast<std::size_t>(rate * 2)), right(left.size());
        for (std::size_t at = 0; at < left.size(); at += static_cast<std::size_t>(block)) {
            const auto n = std::min(static_cast<std::size_t>(block), left.size() - at);
            engine.render(left.data() + at, right.data() + at, static_cast<int>(n));
        }
        double energy = 0, earlyVariation = 0, lateVariation = 0, earlyEnergy = 0, lateEnergy = 0;
        for (std::size_t i = 1; i < left.size(); ++i) {
            check(std::isfinite(left[i]) && std::abs(left[i]) < 1, "finite bounded downlifter");
            energy += left[i] * left[i];
            const double d = left[i] - left[i - 1];
            if (i > rate * .2 && i < rate * .5) { earlyVariation += d*d; earlyEnergy += left[i]*left[i]; }
            if (i > rate * 1.5 && i < rate * 1.8) { lateVariation += d*d; lateEnergy += left[i]*left[i]; }
        }
        const auto tonePower = [&](double centerPhase, double frequency) {
            const auto center = static_cast<std::size_t>(centerPhase * 2.0 * rate);
            const auto half = static_cast<std::size_t>(0.01 * rate);
            double re = 0, im = 0;
            for (std::size_t i = center - half; i < center + half; ++i) {
                const double angle = 6.283185307179586 * frequency * static_cast<double>(i) / rate;
                re += left[i] * std::cos(angle); im += left[i] * std::sin(angle);
            }
            return re*re + im*im;
        };
        const double noteHz = 440.0 * std::exp2((60.0 - 69.0) / 12.0);
        const double earlyHz = noteHz * std::exp2(3.0 * 0.8);
        const double lateHz = noteHz * std::exp2(3.0 * 0.2);
        check(tonePower(0.2, earlyHz) > tonePower(0.2, lateHz) * 3, "early high fundamental");
        check(tonePower(0.8, lateHz) > tonePower(0.8, earlyHz) * 3, "late low fundamental");
        check(energy > 1e-4, "downlifter audible");
        check(earlyVariation / earlyEnergy > lateVariation / lateEnergy * 2, "descending spectral movement");
        check(left == right, "centered stereo");
    }
    std::printf("FX downlifter: %d failure(s)\n", failures); return failures ? 1 : 0;
}
