#include "engine/NoiseRiser.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

int main() {
    int failures = 0;
    auto check = [&](bool ok, const char* message) { if (!ok) { std::printf("FAIL: %s\n", message); ++failures; } };
    for (double rate : {44100.0, 48000.0, 96000.0}) {
        for (int block : {64, 128, 512}) {
            vox::fx::NoiseRiser a, b;
            a.prepare(rate); b.prepare(rate);
            check(a.noteOn(1, 60, 1.0f) && b.noteOn(1, 60, 1.0f), "trigger");
            std::array<float, 512> l{}, r{}, l2{}, r2{};
            double energy = 0;
            for (int n = 0; n < 20; ++n) {
                a.render(l.data(), r.data(), block);
                b.render(l2.data(), r2.data(), block);
                for (int i = 0; i < block; ++i) {
                    check(std::isfinite(l[i]) && std::abs(l[i]) < 1.0f, "finite bounded audio");
                    check(l[i] == r[i] && l[i] == l2[i] && l2[i] == r2[i], "deterministic stereo");
                    energy += l[i] * l[i];
                }
            }
            check(energy > 1e-8, "audible output");
            a.allSoundOff(1);
            for (int n = 0; n < 20; ++n) a.render(l.data(), r.data(), block);
            check(std::all_of(l.begin(), l.begin() + block, [](float x) { return x == 0; }), "panic release reaches silence");
        }
    }
    // The sample clock, not block boundaries, defines the event timeline.
    vox::fx::NoiseRiser x, y; x.prepare(48000); y.prepare(48000);
    x.noteOn(1, 60, 0.8f); y.noteOn(1, 60, 0.8f);
    std::array<float, 512> xl{}, xr{}, yl{}, yr{};
    x.render(xl.data(), xr.data(), 512);
    for (int offset = 0; offset < 512; offset += 64)
        y.render(yl.data() + offset, yr.data() + offset, 64);
    check(xl == yl && xr == yr, "block-boundary invariant");
    vox::fx::NoiseRiser p; p.prepare(48000);
    for (int i = 0; i < 8; ++i) check(p.noteOn(1, 60 + i, 1), "eight events");
    check(p.noteOn(1, 72, 1), "bounded stealing");
    check(!p.noteOn(1, 73, 1), "reject when retirement slot occupied");
    std::array<float, 512> l{}, r{};
    p.render(l.data(), r.data(), 512);
    check(p.noteOn(1, 73, 1), "retirement slot reused");
    p.prepare(48000); p.render(l.data(), r.data(), 512);
    check(std::all_of(l.begin(), l.end(), [](float x) { return x == 0; }), "prepare resets all voices");
    std::printf("FX noise riser: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
