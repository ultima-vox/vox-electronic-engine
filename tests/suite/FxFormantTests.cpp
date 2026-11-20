#include "engine/NoiseRiser.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>
int main() {
    int fails=0;auto check=[&](bool b,const char* s){if(!b){std::printf("FAIL: %s\n",s);++fails;}};
    for(double rate:{44100.0,48000.0,96000.0})for(int block:{64,128,512}){
        auto form=std::make_unique<vox::fx::NoiseRiser>(),texture=std::make_unique<vox::fx::NoiseRiser>();
        form->prepare(rate);texture->prepare(rate);vox::fx::NoiseProgram p;p.layers[0].family=vox::fx::NoiseFamily::formant;
        check(form->setProgram(p)&&form->noteOn(1,60,1,2),"formant start");p.layers[0].family=vox::fx::NoiseFamily::texture;
        check(texture->setProgram(p)&&texture->noteOn(1,60,1,2),"reference start");
        int n=static_cast<int>(rate*2);std::vector<float> l(n),r(n),x(n),y(n);
        for(int at=0;at<n;at+=block){int count=std::min(block,n-at);form->render(l.data()+at,r.data()+at,count);texture->render(x.data()+at,y.data()+at,count);}
        double energy=0,diff=0;for(int i=0;i<n;++i){check(std::isfinite(l[i])&&std::abs(l[i])<1,"finite output");energy+=l[i]*l[i];double d=l[i]-x[i];diff+=d*d;}
        check(energy>.0001 && diff>.0001,"audible and distinct from texture");check(l==r,"centered");
    }
    auto poly=std::make_unique<vox::fx::NoiseRiser>();poly->prepare(96000);vox::fx::NoiseProgram p;
    for(auto& l:p.layers){l.enabled=true;l.family=vox::fx::NoiseFamily::formant;}
    check(poly->setProgram(p),"poly program");for(int i=0;i<8;++i)check(poly->noteOn(1,60+i,1,1),"poly trigger");
    float l[512]{},r[512]{};for(int b=0;b<188;++b){poly->render(l,r,512);for(float v:l)check(std::isfinite(v)&&std::abs(v)<1,"poly bound");}
    std::printf("FX formant: %d failure(s)\n",fails);return fails?1:0;
}
