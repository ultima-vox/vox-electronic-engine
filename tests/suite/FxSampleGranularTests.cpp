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
    check(aStore.publish(one,24000)&&bStore.publish(one,24000),"publish first samples");
    check(a->noteOn(1,60,1,1,true)&&b->noteOn(1,60,1,1,true),"sample event trigger");
    check(!a->attachSampleStore(nullptr),"cannot detach store while active");
    std::array<float,512> al{},ar{},bl{},br{};
    a->render(al.data(),ar.data(),512);b->render(bl.data(),br.data(),512);
    check(aStore.publish(two,48000),"replace sample during event");
    for(int i=0;i<6;++i){a->render(al.data(),ar.data(),512);b->render(bl.data(),br.data(),512);
        check(al==bl&&ar==br,"existing event retains original immutable sample");}
    a->allSoundOff(0);b->allSoundOff(0);a->render(al.data(),ar.data(),512);b->render(bl.data(),br.data(),512);
    check(a->activeEventCount()==0&&b->activeEventCount()==0,"old sample events retire");
    check(a->noteOn(1,60,1,1,true)&&b->noteOn(1,60,1,1,true),"new sample events");
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
    const bool stolen=a->noteOn(1,72,1,1,true);a->render(al.data(),ar.data(),512);
    a->allSoundOff(0);a->render(al.data(),ar.data(),512);
#ifdef VOX_TEST_INTERPOSE_MALLOC
    const auto allocations=voxProbeEnd();if(voxProbeAvailable())check(allocations==0,"sample event acquisition, steal and retirement allocate nothing");
#endif
    check(stolen&&a->activeEventCount()==0&&a->activeGrainCount()==0,"sample leases survive stealing and panic");
    check(aStore.publish(three,48000)&&aStore.publish(four,48000),"retired event leases permit sample replacement");
    aStore.clear();check(!a->noteOn(1,60,1,1,true),"missing new source rejected after clear");
    check(a->noteOn(1,60,1,1,false),"procedural source requires explicit selection");
    a->prepare(48000);b->prepare(48000);
    check(a->attachSampleStore(nullptr)&&b->attachSampleStore(nullptr),"detach after stopped reset");
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
    std::printf("FX sample granular ownership: %d failure(s)\n",fails);return fails?1:0;
}
