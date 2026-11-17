#include "engine/NoiseRiser.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

int main() {
    int failures=0;
    auto check=[&](bool ok,const char* message){ if(!ok) { std::printf("FAIL: %s\n",message); ++failures; } };
    for (double rate : {44100.0,48000.0,96000.0}) for (int block : {64,128,512}) {
        auto engine=std::make_unique<vox::fx::NoiseRiser>(); engine->prepare(rate);
        vox::fx::NoiseProgram p; p.layers[0].family=vox::fx::NoiseFamily::laser;
        check(engine->setProgram(p) && engine->noteOn(1,60,1,1), "laser trigger");
        const int size=static_cast<int>(rate); std::vector<float> l(size),r(size);
        for(int i=0;i<size;i+=block) engine->render(l.data()+i,r.data()+i,std::min(block,size-i));
        double early=0,late=0,derivativeEarly=0,derivativeLate=0;
        for(int i=1;i<size;++i) {
            check(std::isfinite(l[i]) && std::abs(l[i])<1, "finite bounded audio");
            if(i>rate*.015 && i<rate*.045) { early+=l[i]*l[i]; const auto d=l[i]-l[i-1]; derivativeEarly+=d*d; }
            if(i>rate*.12 && i<rate*.15) { late+=l[i]*l[i]; const auto d=l[i]-l[i-1]; derivativeLate+=d*d; }
        }
        check(early>1e-5 && early>late*2, "audible short decay");
        check(derivativeEarly/early > derivativeLate/late*1.5, "descending chirp trajectory");
        check(l==r, "center stereo");
    }
    std::printf("FX laser: %d failure(s)\n",failures); return failures ? 1 : 0;
}
