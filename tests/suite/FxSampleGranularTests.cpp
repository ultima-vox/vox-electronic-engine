#include "engine/NoiseRiser.h"
#include "engine/SampleStore.h"
#include <array>
#include <atomic>
#include <thread>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>
#ifdef VOX_TEST_INTERPOSE_MALLOC
#include "AllocationProbe.h"
#endif
int main(){
    using namespace vox::fx;int fails=0;auto check=[&](bool ok,const char* m){if(!ok){std::printf("FAIL: %s\n",m);++fails;}};
    sample::Store aStore,bStore;auto a=std::make_unique<NoiseRiser>(),b=std::make_unique<NoiseRiser>();
    a->prepare(48000);b->prepare(48000);
    NoiseProgram program;program.layers[0].family=NoiseFamily::granular;
    check(a->setProgram(program)&&b->setProgram(program),"granular Program");
    check(a->attachSampleStore(&aStore)&&b->attachSampleStore(&bStore),"attach sample stores while stopped");
    check(!a->noteOn(1,60,1,1,true),"missing requested sample does not silently fall back");
    std::vector<float> one(512,.2f),two(512,.7f),three(512,-.4f),four(512,.1f);
    std::uint64_t firstToken=0,secondToken=0;
    check(aStore.publish(one,24000,&firstToken)&&bStore.publish(one,24000),"publish first samples");
    check(a->noteOn(1,60,1,1,true,firstToken)&&b->noteOn(1,60,1,1,true),"sample event trigger");
    check(!a->attachSampleStore(nullptr),"cannot detach store while active");
    std::array<float,512> al{},ar{},bl{},br{};
    a->render(al.data(),ar.data(),512);b->render(bl.data(),br.data(),512);
    check(aStore.publish(two,48000,&secondToken),"replace sample during event");
    check(!a->noteOn(1,60,1,1,true,firstToken)&&a->activeEventCount()==1,"stale token cannot trigger newer content");
    for(int i=0;i<6;++i){a->render(al.data(),ar.data(),512);b->render(bl.data(),br.data(),512);
        check(al==bl&&ar==br,"existing event retains original immutable sample");}
    a->allSoundOff(0);b->allSoundOff(0);a->render(al.data(),ar.data(),512);b->render(bl.data(),br.data(),512);
    check(a->activeEventCount()==0&&b->activeEventCount()==0,"old sample events retire");
    check(a->noteOn(1,60,1,1,true,secondToken)&&b->noteOn(1,60,1,1,true),"new sample events");
    double difference=0;for(int i=0;i<6;++i){a->render(al.data(),ar.data(),512);b->render(bl.data(),br.data(),512);
        for(std::size_t j=0;j<al.size();++j)difference+=std::abs(al[j]-bl[j]);}
    check(difference>.1,"new event uses replacement sample");
    check(NoiseRiser::samplePlaybackStep(24000,48000,1)==.5 && NoiseRiser::samplePlaybackStep(96000,48000,2)==4,
          "sample pitch step accounts for source and output rates");
    a->allSoundOff(0);a->render(al.data(),ar.data(),512);
    for(int i=0;i<8;++i)check(a->noteOn(1,60+i,1,1,true),"sample polyphony trigger");
#ifdef VOX_TEST_INTERPOSE_MALLOC
    voxProbeBegin();
#endif
    const bool stolen=a->noteOn(1,72,1,1,true)&&a->noteOn(1,73,1,1,true)&&
        a->noteOn(1,74,1,1,true)&&a->noteOn(1,75,1,1,true);
    const bool full=!a->noteOn(1,76,1,1,true)&&a->activeEventCount()==12;
    a->render(al.data(),ar.data(),512);
    a->allSoundOff(0);a->render(al.data(),ar.data(),512);
#ifdef VOX_TEST_INTERPOSE_MALLOC
    const auto allocations=voxProbeEnd();if(voxProbeAvailable())check(allocations==0,"sample event acquisition, steal and retirement allocate nothing");
#endif
    check(stolen&&full&&a->activeEventCount()==0&&a->activeGrainCount()==0,"multiple sample leases survive stealing and panic");
    check(aStore.publish(three,48000)&&aStore.publish(four,48000),"retired event leases permit sample replacement");
    aStore.clear();check(!a->noteOn(1,60,1,1,true),"missing new source rejected after clear");
    check(a->noteOn(1,60,1,1,false),"procedural source requires explicit selection");
    a->prepare(48000);b->prepare(48000);
    check(a->attachSampleStore(nullptr)&&b->attachSampleStore(nullptr),"detach after stopped reset");
    {
        sample::Store leaseStore;auto engine=std::make_unique<NoiseRiser>();engine->prepare(48000);
        engine->attachSampleStore(&leaseStore);engine->setProgram(program);
        std::uint64_t first=0,second=0;
        check(leaseStore.publish(one,48000,&first),"first multi-retirement sample");
        for(int i=0;i<8;++i)check(engine->noteOn(1,60+i,1,1,true,first),"initial sample voices");
        engine->render(al.data(),ar.data(),512);
        check(leaseStore.publish(two,48000,&second),"second multi-retirement sample");
        for(int i=0;i<4;++i)check(engine->noteOn(1,72+i,1,1,true,second),"steal into separate sample retirement slot");
        check(engine->activeEventCount()==12&&engine->activeGrainCount()<=64,"multiple retirements respect event and grain bounds");
        check(leaseStore.publish(three,48000),"third version published");
        check(!leaseStore.publish(four,48000),"retiring and active voices retain both old sample versions");
        engine->allSoundOff(0);engine->render(al.data(),ar.data(),512);
        check(engine->activeEventCount()==0&&engine->activeGrainCount()==0&&leaseStore.publish(four,48000),
              "panic releases all retirement leases and grains");
    }
    {
        sample::Store pitchedStore;auto e=std::make_unique<NoiseRiser>();e->prepare(48000);e->setProgram(program);e->attachSampleStore(&pitchedStore);
        std::vector<float> sine(240);for(std::size_t i=0;i<sine.size();++i)sine[i]=static_cast<float>(.7*std::sin(6.283185307179586*i/sine.size()));
        check(pitchedStore.publish(sine,24000)&&e->noteOn(1,60,1,1,true),"source-rate pitch test setup");
        std::vector<float> l(48000),r(48000);e->render(l.data(),r.data(),static_cast<int>(l.size()));
        const auto energy=[&](double hz){double total=0;for(std::size_t at=0;at+4096<=l.size();at+=4096){double re=0,im=0;
            for(std::size_t i=0;i<4096;++i){const double phase=6.283185307179586*hz*i/48000;
                re+=l[at+i]*std::cos(phase);im+=l[at+i]*std::sin(phase);}total+=re*re+im*im;}return total;};
        check(energy(100)>energy(200)*3,"24 kHz sample plays at correct 100 Hz pitch at 48 kHz output");
    }
    {
        sample::Store tiny;auto e=std::make_unique<NoiseRiser>();e->prepare(8000);e->setProgram(program);e->attachSampleStore(&tiny);
        const std::vector<float> single{.3f};check(tiny.publish(single,384000)&&e->noteOn(1,127,1,.1,true),"single-frame extreme rate sample");
        std::array<float,512> l{},r{};e->render(l.data(),r.data(),512);
        for(float value:l)check(std::isfinite(value)&&std::abs(value)<1,"high-speed short sample bounded");
    }
    {
        sample::Store shared;auto e=std::make_unique<NoiseRiser>();e->prepare(48000);e->setProgram(program);e->attachSampleStore(&shared);
        const std::vector<float> x(512,.2f),y(512,-.2f);check(shared.publish(x,48000),"concurrent sample setup");
        std::atomic<bool> ready{false};std::thread writer([&]{while(!ready.load(std::memory_order_acquire))std::this_thread::yield();
            for(int i=0;i<10000;++i)(void)shared.publish((i&1)?x:y,48000);});
        ready.store(true,std::memory_order_release);std::array<float,512> l{},r{};
        for(int i=0;i<1000;++i){e->allSoundOff(0);e->render(l.data(),r.data(),512);
            (void)e->noteOn(1,60,1,.05,true);e->render(l.data(),r.data(),512);
            for(float value:l)check(std::isfinite(value)&&std::abs(value)<1,"concurrent replacement remains finite");}
        writer.join();
    }
    {
        const auto sound=[&](source::GrainShape grains){
            sample::Store store;auto engine=std::make_unique<NoiseRiser>();engine->prepare(48000);
            auto modified=program;modified.layers[0].grains=grains;
            std::vector<float> ramp(997);for(std::size_t i=0;i<ramp.size();++i)ramp[i]=.7f*static_cast<float>(i)/ramp.size()-.3f;
            check(engine->setProgram(modified)&&store.publish(ramp,44100)&&engine->attachSampleStore(&store)&&
                  engine->noteOn(1,60,1,.3,true),"custom grain render setup");
            std::vector<float> l(14400),r(l.size());engine->render(l.data(),r.data(),static_cast<int>(l.size()));
            check(engine->activeGrainCount()<=64&&std::all_of(l.begin(),l.end(),[](float x){return std::isfinite(x)&&std::abs(x)<1;}),"grain outputs finite and bounded");
            return l;
        };
        const auto base=sound({80,0,false});
        check(base==sound({80,0,false})&&base!=sound({30,0,false})&&base!=sound({80,.35f,false})&&
              base!=sound({80,0,true}),"grain length, normalized position and reverse each alter deterministic audio");
    }
    {
        // Admit one grain at pitch 1, then sweep the programmed pitch before
        // the next admission. Its exact reverse readout must still follow
        // the original step, even through repeated odd-length wraps.
        sample::Store store;auto engine=std::make_unique<NoiseRiser>();
        const std::vector<float> ring{.1f,-.2f,.3f,-.4f,.2f,-.1f,.4f};
        check(store.publish(ring,24000)&&engine->attachSampleStore(&store),"reverse pitch fixture source");
        for(double rate:{44100.,48000.,96000.}){
            auto shape=program;shape.mode=PlayMode::gate;shape.layers[0].grains={80,0,true};
            auto swept=shape;swept.layers[0].pitchCurve.nodes[1].value=1.f;
            const auto render=[&](const NoiseProgram& graph){
                engine->prepare(rate);engine->setProgram(graph);
                check(engine->noteOn(1,60,1,.05,true),"reverse grain at new host rate");
                std::vector<float> l(static_cast<std::size_t>(rate*.05)),r(l.size());
                // Exercise changing callback partitions, rather than only a single block.
                for(int at=0;at<static_cast<int>(l.size());){const auto n=std::min(127,static_cast<int>(l.size())-at);
                    engine->render(l.data()+at,r.data()+at,n);at+=n;}
                return l;
            };
            const auto flat=render(shape), varying=render(swept);
            const auto first=static_cast<int>(rate*.025),length=static_cast<int>(rate*.08);
            check(std::equal(flat.begin(),flat.begin()+first,varying.begin())&&flat!=varying,
                  "within-grain pitch is fixed; next admitted grain takes the changed trajectory");
            double maxError=0;
            for(int i=0;i<first;++i){
                // Independent modulo reference from an unwrapped signed position.
                const double travel=-i*24000./rate, floor=std::floor(travel);
                const auto wrap=[&](long long j){return static_cast<std::size_t>((j%7+7)%7);};
                const auto at=wrap(static_cast<long long>(floor)),next=(at+1)%ring.size();
                const double x=ring[at]+(travel-floor)*(ring[next]-ring[at]);
                const double w=static_cast<double>(i)*1024/(length-1), base=std::floor(w);
                const auto hann=[](double j){return .5-.5*std::cos(6.283185307179586*j/1024);};
                const double window=hann(base)+(w-base)*(hann(base+1)-hann(base));
                const double want=x*window*.35*std::min(1.,i/(rate*.01))*.055;
                maxError=std::max(maxError,std::abs(flat[i]-want));
            }
            check(maxError<2e-8,"reverse short-ring audio agrees with independent modulo/interpolation reference");
            shape.layers[0].grains.position=-1;
            const auto scattered=render(shape);
            check(scattered==render(shape)&&scattered!=flat&&std::all_of(scattered.begin(),scattered.end(),[](float x){return std::isfinite(x);}),
                  "seeded scatter remains repeatable, non-fixed and finite after host-rate reset");
        }
    }
    std::printf("FX sample granular ownership: %d failure(s)\n",fails);return fails?1:0;
}
