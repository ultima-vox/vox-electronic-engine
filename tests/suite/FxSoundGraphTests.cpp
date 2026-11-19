#include "engine/SoundGenerator.h"
#include "engine/PortablePreset.h"
#include "engine/PostMotion.h"
#include <cstdio>
#include <memory>
#ifdef VOX_TEST_INTERPOSE_MALLOC
#include "AllocationProbe.h"
#endif
using namespace vox::fx;
int main(){
    int failures=0;
    auto check=[&](bool pass,const char* message){if(!pass){++failures;std::printf("FAIL: %s\n",message);}};
    auto encoded=[](const auto& value){std::vector<std::byte> bytes;(void)portable::encode({value,{}},bytes);return bytes;};
    for(unsigned i=0;i<8;++i){
        ProgramStore::Snapshot base,a,b;bool builtinA=false,builtinB=false;
        base.generator.role=i;base.generator.seed=432;base.generator.style=static_cast<int>(i%6);
        check(generator::makeSound(base,false,a,builtinA)&&generator::makeSound(base,false,b,builtinB)&&encoded(a)==encoded(b),"same role/seed deterministically generates canonical sound");
        check(!builtinA&&a.program.mode==generator::rolePlans[i].mode&&a.macros.count==4&&a.program.layers[0].filters[0].type!=source::FilterType::off,
            "full role graph reaches real topology, typed controls, mode and motion");
        if(i==5){pattern::FixedSchedule schedule;check(pattern::scheduleFixed(a.pattern,schedule)&&schedule.count>10,"Rhythm generates a playable trigger grid");}
        auto frozen=a;frozen.generator.graphLocks=generator::graphAll;frozen.generator.locks.layers.fill(generator::all);
        check(generator::makeSound(frozen,true,b,builtinB)&&encoded(b)==encoded(frozen)&&!builtinB,"all locked fields and asset reference survive mutation exactly");
        check(generator::makeSound(frozen,false,b,builtinB)&&encoded(b)==encoded(frozen)&&!builtinB,"all locked fields and asset reference survive regeneration exactly");
        a.generator.graphLocks=generator::fxChain|generator::triggerPattern|generator::sampleRef;
        a.generator.locks.layers[0]=generator::filters|generator::transpose;
        check(generator::makeSound(a,true,b,builtinB)&&encoded(b)!=encoded(a)&&b.pattern==a.pattern&&b.effects==a.effects&&
              b.program.layers[0].filters[0].hz==a.program.layers[0].filters[0].hz&&b.program.layers[0].transpose==a.program.layers[0].transpose,
              "independent graph and layer locks survive a nontrivial mutation");
    }
    using S=mod::Source;using D=mod::Destination;
    macro::Config motion;std::vector<std::byte> bytes;
    check(motion.setRoute(S::macro1,D::gain,.2f,0)&&macro::codec::encode(motion,bytes),"historical route fixture");
    bytes.resize(4*(1+macro::codec::legacyWords));bytes[0]=std::byte{1};macro::Config read;
    check(macro::codec::decode(bytes,read)&&read==motion,"FMOD v1 keeps routes and default clocks");
    check(!motion.put(0,{S::phase,D::wet1,.4f,0})&&!motion.put(0,{S::follower,D::pitch,.4f,0}),"invalid scope and feedback path rejected");
    motion={};motion.lfos[0]={.5f,true};motion.randomSeed=17;
    for(unsigned i=0;i<17;++i){auto d=static_cast<D>(i);check(motion.put(i,{S::macro1,d,.05f,.005f}),"every typed destination addressable");}
    while(motion.count<64)check(motion.put(motion.count,{S::lfo1,D::layerPan,.001f,.015f}),"native row operation fills 64 routes");
    check(!motion.put(64,{S::macro1,D::gain,.01f,0}),"65th route rejected");
    check(motion.erase(12)&&motion.count==63&&motion.put(63,{S::follower,D::amount2,.09f,.05f}),"indexed deletion and insertion are complete");
    check(macro::codec::encode(motion,bytes)&&macro::codec::decode(bytes,read)&&read==motion,"64-route FMOD v2 roundtrip");
    ProgramStore store;const auto old=store.versionedMotion();
    check(store.replaceMotionIfCurrent(old,motion)&&!store.replaceMotionIfCurrent(old,{}),"stale matrix edit cannot clobber a concurrent edit");
    const auto beforeRestore=store.versionedMotion();check(store.publish(store.snapshot())&&!store.replaceMotionIfCurrent(beforeRestore,{}),"stale matrix edit cannot clobber restored Program");
    std::array<float,8> bases{1,.5f,.5f,.5f,.5f,.5f,.5f,.5f};
    {
        macro::Runtime clockA,clockB;macro::Config sync;sync.lfos[0]={.5f,true};sync.setRoute(S::lfo1,D::gain,1,0);
        clockA.prepare(48000);clockB.prepare(48000);clockA.setConfig(sync);clockB.setConfig(sync);clockA.setTempo(120);clockB.setTempo(60);
        for(int i=0;i<12000;++i){(void)clockA.evaluate(bases);(void)clockB.evaluate(bases);}
        check(std::abs(clockA.evaluate(bases)[0]-1)<.0001&&std::abs(clockB.evaluate(bases)[0]-.7071f)<.0002,"synced rates track BPM, with continuous oscillator phase");
    }
    {
        auto a=std::make_unique<NoiseRiser>(),b=std::make_unique<NoiseRiser>();
        NoiseProgram p;p.layers[0].family=NoiseFamily::texture;a->prepare(48000);b->prepare(48000);a->setProgram(p);
        macro::Config pitch;pitch.setRoute(S::velocity,D::pitch,1,0);a->setVoiceMotion(pitch);
        p.layers[0].transpose=12;b->setProgram(p);
        check(a->noteOn(1,60,1,.3)&&b->noteOn(1,60,1,.3),"voice pitch fixture admitted");
        a->setVoiceMotion({}); // must not change the route graph of an active event
        double error=0;
        for(int i=0;i<4096;++i){float l,r,x,y;a->render(&l,&r,1);b->render(&x,&y,1);error=std::max(error,std::abs(static_cast<double>(l)-x));}
        check(error<1e-6,"velocity pitch route on active voice equals authored +12 st despite graph replacement");
    }
    for(double rate:{44100.,48000.,96000.}){
        auto engine=std::make_unique<NoiseRiser>();engine->prepare(rate);engine->setVoiceMotion(motion);engine->setMacroBases(bases);
        NoiseProgram p;for(auto& l:p.layers){l.enabled=true;l.family=NoiseFamily::granular;l.filters[0]={source::FilterType::lowpass,5300,.73f,4};}
        check(engine->setProgram(p),"install max-route granular graph");
        effects::Chain chain;macro::Runtime post;check(chain.prepare(rate)&&post.prepare(rate)&&post.setConfig(motion),"prepare shared modulation and FX");
        for(unsigned i=0;i<4;++i)chain.set(i,{effects::Type::drive,.42f,.26f});
        std::array<float,128> left{},right{};double peak=0,mean=0;unsigned grains=0;bool finite=true;
#ifdef VOX_TEST_INTERPOSE_MALLOC
        voxProbeBegin();
#endif
        for(int i=0;i<8;++i)(void)engine->noteOn(1,48+i,1,2);
        for(int block=0;block<128;++block){
            engine->render(left.data(),right.data(),128);grains=std::max(grains,engine->activeGrainCount());
            macro::renderPost(chain,post,left.data(),right.data(),128,bases,[]{return .8f;});
            for(std::size_t i=0;i<128;++i){finite &= std::isfinite(left[i])&&std::isfinite(right[i]);mean+=left[i];peak=std::max(peak,static_cast<double>(std::max(std::abs(left[i]),std::abs(right[i]))));}
        }
#ifdef VOX_TEST_INTERPOSE_MALLOC
        const auto count=voxProbeEnd();if(voxProbeAvailable())check(count==0,"64-route note admission, voices and mix allocate nothing");
#endif
        check(finite&&peak>.0001&&peak<1&&std::abs(mean/16384)<.01&&grains==64&&bases[0]==1,"64-route mix/headroom/DC/grain bound and automation base integrity");
        std::printf("motion %.0f Hz 64 routes: peak %.6f DC %.7f / %u grains\n",rate,peak,std::abs(mean/16384),grains);
    }
    std::printf("FX sound graph/deterministic routes: %d failure(s)\n",failures);return failures?1:0;
}
