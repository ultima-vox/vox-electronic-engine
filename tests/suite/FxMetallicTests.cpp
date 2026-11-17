#include "engine/NoiseRiser.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

int main() {
    int fails=0; auto check=[&](bool b,const char* s){if(!b){std::printf("FAIL: %s\n",s);++fails;}};
    for(double rate:{44100.0,48000.0,96000.0}) for(int block:{64,128,512}) {
        auto engine=std::make_unique<vox::fx::NoiseRiser>(); engine->prepare(rate);
        vox::fx::NoiseProgram p; p.layers[0].family=vox::fx::NoiseFamily::metallic;
        check(engine->setProgram(p) && engine->noteOn(1,60,1,4),"trigger");
        const int n=static_cast<int>(rate*4); std::vector<float> l(n),r(n);
        for(int at=0;at<n;at+=block) engine->render(l.data()+at,r.data()+at,std::min(block,n-at));
        double early=0,late=0;
        for(int i=0;i<n;++i) {
            check(std::isfinite(l[i]) && std::abs(l[i]) < 1,"finite stable modes");
            if(i>rate*.1 && i<rate*.5) early+=l[i]*l[i];
            if(i>rate*3 && i<rate*3.4) late+=l[i]*l[i];
        }
        check(early>1e-4 && early>late*5,"audible damped resonances");
        check(l==r,"center stereo");
    }
    auto e=std::make_unique<vox::fx::NoiseRiser>(); e->prepare(96000); vox::fx::NoiseProgram p;
    for(auto& l:p.layers){l.enabled=true;l.family=vox::fx::NoiseFamily::metallic;}
    check(e->setProgram(p),"poly program");
    for(int i=0;i<8;++i) check(e->noteOn(1,20+i,1,1),"poly trigger");
    float l[512]{},r[512]{};
    for(int b=0;b<188;++b){e->render(l,r,512);for(float v:l)check(std::isfinite(v)&&std::abs(v)<1,"poly bound");}
    std::printf("FX metallic: %d failure(s)\n",fails); return fails?1:0;
}
