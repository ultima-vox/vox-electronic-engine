#include "engine/NoiseRiser.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <limits>
#include <vector>
#ifdef VOX_TEST_INTERPOSE_MALLOC
#include "AllocationProbe.h"
#endif
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
    {
        auto e=std::make_unique<vox::fx::NoiseRiser>();e->prepare(48000);
        vox::fx::NoiseProgram program;program.layers[0].family=vox::fx::NoiseFamily::glitch;
        check(e->setProgram(program)&&e->setTempo(120)&&e->noteOn(1,60,1,1),"tempo glitch setup");
        std::vector<float> l(24000),r(24000);
        e->render(l.data(),r.data(),9000);
#ifdef VOX_TEST_INTERPOSE_MALLOC
        voxProbeBegin();
#endif
        const bool tempoChanged=e->setTempo(60);e->render(l.data()+9000,r.data()+9000,15000);
#ifdef VOX_TEST_INTERPOSE_MALLOC
        const auto allocations=voxProbeEnd();if(voxProbeAvailable())check(allocations==0,"tempo change and rendering allocate nothing");
#endif
        check(tempoChanged,"mid-event tempo change");
        double before=0,muted=0;
        for(int i=12960;i<14400;++i)before+=l[i]*l[i];
        for(int i=15840;i<17280;++i)muted+=l[i]*l[i];
        check(before>1e-8 && muted==0,"tempo change preserves grid phase and moves gate boundary");
        check(!e->setTempo(0)&&!e->setTempo(std::numeric_limits<double>::quiet_NaN()),"invalid tempo rejected");
        auto split=std::make_unique<vox::fx::NoiseRiser>();split->prepare(48000);split->setProgram(program);
        split->setTempo(120);split->noteOn(1,60,1,1);
        std::vector<float> sl(24000),sr(24000);
        for(int at=0;at<24000;){if(at==9000)split->setTempo(60);
            const int stop=at<9000?9000:24000;const int count=std::min(127,stop-at);
            split->render(sl.data()+at,sr.data()+at,count);at+=count;}
        check(l==sl&&r==sr,"tempo change is invariant to block partitioning");
    }
    {
        using namespace vox::fx;
        auto subject=std::make_unique<NoiseRiser>(),reference=std::make_unique<NoiseRiser>();
        NoiseProgram p;p.layers[0].family=NoiseFamily::glitch;p.layers[0].character=-.75f;
        p.layers[0].glitch={source::SliceGrid::thirtySecond,source::SliceRead::reverse,2};
        const auto setup=[&](NoiseRiser& engine,const NoiseProgram& program){
            engine.prepare(48000);return engine.setProgram(program)&&engine.noteOn(1,60,1,.4);
        };
        check(setup(*subject,p)&&setup(*reference,p),"reversed 1/32 slice fixture");
        std::vector<float> a(12000),b(12000),aq(12000),bq(12000);
        subject->render(a.data(),aq.data(),4500);reference->render(b.data(),bq.data(),4500);
        auto future=p;future.layers[0].glitch.gates=0;
        check(subject->setProgram(future)&&subject->setTempo(60)&&reference->setTempo(60),"edit source during active slice and change tempo");
#ifdef VOX_TEST_INTERPOSE_MALLOC
        voxProbeBegin();
#endif
        subject->render(a.data()+4500,aq.data()+4500,7500);
        for(int at=4500;at<12000;at+=127)reference->render(b.data()+at,bq.data()+at,std::min(127,12000-at));
#ifdef VOX_TEST_INTERPOSE_MALLOC
        const auto allocations=voxProbeEnd();if(voxProbeAvailable())check(allocations==0,"custom slice render allocates nothing");
#endif
        const auto energy=[](const auto& samples,int start,int end){double sum=0;
            for(int i=start;i<end;++i)sum+=static_cast<double>(samples[i])*samples[i];return sum;};
        check(a==b&&a==aq&&energy(a,0,2990)==0&&energy(a,3400,4300)>1e-5&&
              energy(a,6200,7000)>1e-5&&energy(a,7510,12000)==0,
              "note captures custom settings, source rests are dry-silent, tempo moves future edges only");
        auto forward=p;forward.layers[0].glitch.read=source::SliceRead::forward;
        check(setup(*subject,forward),"forward replay fixture");subject->render(b.data(),bq.data(),4500);
        double difference=0;for(int i=3100;i<4400;++i)difference+=std::abs(a[i]-b[i]);
        check(difference>.01,"direction independently changes the sampled audio");
        check(setup(*subject,future),"zero-mask replay fixture");subject->render(b.data(),bq.data(),12000);
        check(energy(b,0,12000)==0,"custom zero mask emits neither startup capture nor dry Character audio");
        auto slower=p;slower.layers[0].glitch.grid=source::SliceGrid::eighth;
        check(setup(*subject,slower),"eighth replay fixture");subject->render(b.data(),bq.data(),12000);
        check(energy(b,0,11990)==0,"grid division independently delays the first enabled eighth slice");
        std::printf("custom Glitch: open-slice energy %.7f, reverse-forward difference %.7f\n",energy(a,3400,4300),difference);
    }
    std::printf("FX glitch: %d failure(s)\n",fails);return fails?1:0;
}
