#include "engine/NoiseRiser.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

int main() {
    int failures = 0;
    auto check = [&](bool ok, const char* message) { if (!ok) { std::printf("FAIL: %s\n", message); ++failures; } };
    for (double rate : {44100.0, 48000.0, 96000.0}) {
        vox::fx::NoiseRiser a, b, riser;
        a.prepare(rate); b.prepare(rate); riser.prepare(rate);
        vox::fx::NoiseProgram program; program.layers[0].family = vox::fx::NoiseFamily::shepard;
        check(a.setProgram(program) && b.setProgram(program), "valid Shepard program");
        check(a.noteOn(1,60,1,2) && b.noteOn(1,60,1,2) && riser.noteOn(1,60,1,2), "trigger");
        const int total = static_cast<int>(rate * 2);
        std::vector<float> l(total), r(total), l2(total), r2(total), noise(total), scratch(total);
        for (int i = 0; i < total; i += 64) {
            const int n = std::min(64, total-i);
            a.render(l.data()+i,r.data()+i,n);
        }
        for (int i = 0; i < total; i += 512) {
            const int n = std::min(512, total-i);
            b.render(l2.data()+i,r2.data()+i,n);
            riser.render(noise.data()+i,scratch.data()+i,n);
        }
        check(l == l2 && r == r2, "block-independent output");
        double energy = 0, difference = 0; float maxStep = 0;
        for (int i = 1; i < total; ++i) {
            check(std::isfinite(l[i]) && std::abs(l[i]) < 1, "finite bounded output");
            check(l[i] == r[i], "centered output");
            energy += l[i]*l[i]; difference += (l[i]-noise[i])*(l[i]-noise[i]);
            maxStep = std::max(maxStep, std::abs(l[i]-l[i-1]));
        }
        check(energy > 0.01 && difference > 0.01, "audible and distinct from noise");
        // Wide margin: detects discontinuous full-scale wrap, not a complete
        // click or spectral quality qualification.
        check(maxStep < 0.08f, "bounded adjacent-sample step");
        std::printf("%.0f Hz Shepard energy %.8g max step %.8g\n", rate, energy, maxStep);
    }
    std::printf("FX Shepard: %d failure(s)\n", failures); return failures ? 1 : 0;
}
