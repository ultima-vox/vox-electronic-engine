#include "engine/NoiseRiser.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>
int main(){
    int fails=0;auto check=[&](bool b,const char* s){if(!b){std::printf("FAIL: %s\n",s);++fails;}};
    for(double rate:{44100.0,48000.0,96000.0})for(int block:{64,128,512}){
        auto e=std::make_unique<vox::fx::NoiseRiser>();e->prepare(rate);vox::fx::NoiseProgram p;
        p.layers[0].family=vox::fx::NoiseFamily::glitch;check(e->setProgram(p)&&e->noteOn(1,60,1,1),"glitch trigger");
        int n=static_cast<int>(rate);std::vector<float> l(n),r(n);
        for(int at=0;at<n;at+=block)e->render(l.data()+at,r.data()+at,std::min(block,n-at));
        double on=0,off=0;for(int i=0;i<n;++i){check(std::isfinite(l[i])&&std::abs(l[i])<1,"bounded output");
            if(i>rate*.14&&i<rate*.24)on+=l[i]*l[i];
            if(i>rate*.27&&i<rate*.36)off+=l[i]*l[i];
        }
        check(on>1e-5 && off==0,"deterministic gated slice");check(l==r,"centered output");
    }
    std::printf("FX glitch: %d failure(s)\n",fails);return fails?1:0;
}
