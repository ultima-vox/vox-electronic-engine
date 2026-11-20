#include "engine/NoiseRiser.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>
int main() {
    int fails=0; auto check=[&](bool b,const char* s){if(!b){std::printf("FAIL: %s\n",s);++fails;}};
    for(double rate:{44100.0,48000.0,96000.0}) for(int block:{64,128,512}) {
        auto e=std::make_unique<vox::fx::NoiseRiser>();e->prepare(rate);vox::fx::NoiseProgram p;
        p.layers[0].family=vox::fx::NoiseFamily::texture;check(e->setProgram(p)&&e->noteOn(1,60,1,4),"texture trigger");
        int n=static_cast<int>(rate*4);std::vector<float> l(n),r(n);
        for(int at=0;at<n;at+=block)e->render(l.data()+at,r.data()+at,std::min(block,n-at));
        double early=0,late=0,stereoDifference=0;
        for(int i=0;i<n;++i){
            check(std::isfinite(l[i]) && std::isfinite(r[i]) && std::abs(l[i])<1 && std::abs(r[i])<1,"bounded output");
            if(i>rate*.5 && i<rate)early+=l[i]*l[i];
            if(i>rate*2.5 && i<rate*3)late+=l[i]*l[i];
            stereoDifference+=std::abs(l[i]-r[i]);
        }
        check(early>.001 && late>.001,"continuous audible texture");
        check(stereoDifference>.01,"stereo evolution");
    }
    std::printf("FX texture: %d failure(s)\n",fails);return fails?1:0;
}
