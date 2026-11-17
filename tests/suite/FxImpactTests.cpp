#include "engine/NoiseRiser.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

int main() {
    int failures = 0;
    auto check = [&](bool ok, const char* what) { if (!ok) { std::printf("FAIL: %s\n",what); ++failures; } };
    for (double rate : {44100.0, 48000.0, 96000.0}) for (int block : {64,128,512}) {
        auto engineOwner=std::make_unique<vox::fx::NoiseRiser>(); auto& engine=*engineOwner;
        vox::fx::NoiseProgram program; program.layers[0].family = vox::fx::NoiseFamily::impact;
        engine.prepare(rate); check(engine.setProgram(program) && engine.noteOn(1,60,1,4), "impact trigger");
        const int total = static_cast<int>(rate * 4);
        std::vector<float> l(total), r(total);
        for (int at = 0; at < total; at += block) engine.render(l.data()+at, r.data()+at, std::min(block,total-at));
        double early = 0, late = 0; float peak = 0;
        for (int i = 0; i < total; ++i) {
            check(std::isfinite(l[i]) && std::abs(l[i]) < 1, "finite output");
            peak = std::max(peak, std::abs(l[i]));
            if (i < rate * .5) early += l[i]*l[i];
            if (i >= rate * 3 && i < rate * 3.5) late += l[i]*l[i];
        }
        check(peak > 0.005 && early > late * 100, "audible impact with decaying tail");
        check(l == r, "centered output");
    }
    std::printf("FX impact: %d failure(s)\n", failures); return failures ? 1 : 0;
}
