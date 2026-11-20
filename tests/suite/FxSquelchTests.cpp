#include "engine/NoiseRiser.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

int main() {
    int fails = 0;
    auto check = [&](bool ok,const char* text) { if (!ok) { std::printf("FAIL: %s\n",text); ++fails; } };
    for (double rate : {44100.0,48000.0,96000.0}) for (int block : {64,128,512}) {
        auto squelch=std::make_unique<vox::fx::NoiseRiser>(); auto laser=std::make_unique<vox::fx::NoiseRiser>();
        squelch->prepare(rate); laser->prepare(rate);
        vox::fx::NoiseProgram p; p.layers[0].family=vox::fx::NoiseFamily::squelch;
        check(squelch->setProgram(p) && squelch->noteOn(1,60,1,2), "squelch start");
        p.layers[0].family=vox::fx::NoiseFamily::laser;
        check(laser->setProgram(p) && laser->noteOn(1,60,1,2), "laser reference start");
        const int count=static_cast<int>(rate*2); std::vector<float> l(count),r(count),x(count),y(count);
        for(int i=0;i<count;i+=block) {
            const int n=std::min(block,count-i);
            squelch->render(l.data()+i,r.data()+i,n); laser->render(x.data()+i,y.data()+i,n);
        }
        double energy=0,difference=0,early=0,late=0;
        for(int i=0;i<count;++i) {
            check(std::isfinite(l[i]) && std::abs(l[i])<1, "finite bounded output");
            energy+=l[i]*l[i]; const auto d=l[i]-x[i]; difference+=d*d;
            if(i < rate*.5) early+=l[i]*l[i];
            if(i > rate*1.5) late+=l[i]*l[i];
        }
        check(energy>1e-5 && difference>1e-5, "audible and distinct from laser");
        check(early>late*10, "finite decaying envelope");
        check(l==r, "centered output");
    }
    {
        auto engine=std::make_unique<vox::fx::NoiseRiser>(); engine->prepare(96000);
        vox::fx::NoiseProgram p;
        for (auto& layer : p.layers) { layer.enabled=true; layer.family=vox::fx::NoiseFamily::squelch; }
        check(engine->setProgram(p), "four-layer program");
        for(int i=0;i<8;++i) check(engine->noteOn(1, 100+i, 1, 1), "overlapping squelches");
        float l[512]{}, r[512]{};
        for(int b=0;b<188;++b) {
            engine->render(l,r,512);
            for(float v:l) check(std::isfinite(v) && std::abs(v)<1, "polyphonic resonance remains bounded");
        }
    }
    std::printf("FX squelch: %d failure(s)\n",fails); return fails ? 1 : 0;
}
