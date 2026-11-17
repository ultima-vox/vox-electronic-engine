#include "engine/NoiseRiser.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

int main() {
    int failures = 0;
    auto check = [&](bool ok,const char* what) { if (!ok) { std::printf("FAIL: %s\n",what); ++failures; } };
    for (double rate : {44100.0,48000.0,96000.0}) for (int block : {64,128,512}) {
        auto engine = std::make_unique<vox::fx::NoiseRiser>(); engine->prepare(rate);
        vox::fx::NoiseProgram program; program.layers[0].family = vox::fx::NoiseFamily::whoosh;
        check(engine->setProgram(program) && engine->noteOn(1,60,1,2), "whoosh trigger");
        const int size = static_cast<int>(rate * 2); std::vector<float> left(size),right(size);
        for (int at = 0; at < size; at += block) engine->render(left.data()+at,right.data()+at,std::min(block,size-at));
        double earlyL=0, earlyR=0, lateL=0, lateR=0, energy=0;
        for (int i=0; i<size; ++i) {
            check(std::isfinite(left[i]) && std::isfinite(right[i]) && std::abs(left[i]) < 1 && std::abs(right[i]) < 1, "finite output");
            energy += left[i]*left[i] + right[i]*right[i];
            if (i > size*.2 && i < size*.35) { earlyL+=left[i]*left[i]; earlyR+=right[i]*right[i]; }
            if (i > size*.65 && i < size*.8) { lateL+=left[i]*left[i]; lateR+=right[i]*right[i]; }
        }
        check(energy > .001, "audible whoosh");
        check(earlyL > earlyR*2 && lateR > lateL*2, "left-to-right pass");
    }
    std::printf("FX whoosh: %d failure(s)\n",failures); return failures ? 1 : 0;
}
