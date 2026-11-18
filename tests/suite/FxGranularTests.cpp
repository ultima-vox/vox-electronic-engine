#include "engine/NoiseRiser.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>
#ifdef VOX_TEST_INTERPOSE_MALLOC
#include "AllocationProbe.h"
#endif
int main(){
    int fails=0;auto check=[&](bool b,const char* s){if(!b){std::printf("FAIL: %s\n",s);++fails;}};
    for(double rate:{44100.0,48000.0,96000.0}){
        auto a=std::make_unique<vox::fx::NoiseRiser>(),b=std::make_unique<vox::fx::NoiseRiser>();a->prepare(rate);b->prepare(rate);
        vox::fx::NoiseProgram p;for(auto& layer:p.layers){layer.enabled=true;layer.family=vox::fx::NoiseFamily::granular;}
        check(a->setProgram(p)&&b->setProgram(p),"granular program");
        check(a->activeEventCount()==0 && a->activeGrainCount()==0,"empty telemetry");
        for(int i=0;i<8;++i)check(a->noteOn(1,60+i,1,1)&&b->noteOn(1,60+i,1,1),"8 granular events");
        check(a->activeEventCount()==8,"active event count includes eight voices");
        const int n=static_cast<int>(rate);std::vector<float> l(n),r(n),l2(n),r2(n);unsigned peak=0;
        for(int at=0;at<n;at+=64){int count=std::min(64,n-at);a->render(l.data()+at,r.data()+at,count);peak=std::max(peak,a->activeGrainCount());}
        for(int at=0;at<n;at+=512){int count=std::min(512,n-at);b->render(l2.data()+at,r2.data()+at,count);}
        double energy=0;for(float value:l){check(std::isfinite(value)&&std::abs(value)<1,"finite bounded output");energy+=value*value;}
        check(energy>1e-6,"audible granular cloud");check(peak==64,"global pool reaches but never exceeds 64");
        check(l==l2 && r==r2,"granular block invariance");
        float tailL[512]{},tailR[512]{};a->render(tailL,tailR,512);
        check(a->activeGrainCount()==0&&a->activeEventCount()==0,"grains and events retired after events end");
    }
    {
        auto e=std::make_unique<vox::fx::NoiseRiser>();e->prepare(48000);vox::fx::NoiseProgram p;
        p.layers[0].family=vox::fx::NoiseFamily::granular;check(e->setProgram(p),"steal program");
        for(int i=0;i<8;++i)check(e->noteOn(1,60+i,1,1),"steal initial events");
        float l[512]{},r[512]{};for(int i=0;i<6;++i)e->render(l,r,512);
#ifdef VOX_TEST_INTERPOSE_MALLOC
        voxProbeBegin();
#endif
        const bool stolen=e->noteOn(1,72,1,1);
        check(e->activeEventCount()==9,"event count includes bounded retirement fade");
        e->render(l,r,512);
#ifdef VOX_TEST_INTERPOSE_MALLOC
        const auto allocations=voxProbeEnd();if(voxProbeAvailable())check(allocations==0,"granular audio and stealing allocate nothing");
#endif
        check(stolen && e->activeGrainCount()<=64,"grain ownership survives event steal");
        e->allSoundOff(0);for(int i=0;i<2;++i)e->render(l,r,512);
        check(e->activeGrainCount()==0&&e->activeEventCount()==0,"panic retires grains and events");
    }
    {
        vox::fx::sample::Store store;auto engine=std::make_unique<vox::fx::NoiseRiser>();engine->prepare(48000);
        check(engine->attachSampleStore(&store),"attach missing sample store");
        vox::fx::NoiseProgram program;program.layers[0].family=vox::fx::NoiseFamily::riser;
        check(engine->setProgram(program)&&engine->noteOn(1,60,1,1,true,1),"missing sample does not silence non-granular event");
        float l[512]{},r[512]{};engine->render(l,r,512);
        check(std::any_of(l,l+512,[](float x){return x!=0;}),"non-granular event remains audible");
        engine->allSoundOff(0);engine->render(l,r,512);
        program.layers[0].family=vox::fx::NoiseFamily::granular;
        check(engine->setProgram(program)&&!engine->noteOn(1,60,1,1,true,1),"missing sample never triggers granular fallback");
        program.layers[0].family=vox::fx::NoiseFamily::riser;program.layers[1].enabled=true;
        program.layers[1].family=vox::fx::NoiseFamily::granular;
        check(engine->setProgram(program)&&!engine->noteOn(1,60,1,1,true,1),"mixed event with granular layer fails atomically");
        program.layers[1].enabled=false;
        check(engine->setProgram(program)&&engine->noteOn(1,60,1,1,true,1),"disabled granular layer does not require sample");
    }
    std::printf("FX granular: %d failure(s)\n",fails);return fails?1:0;
}
