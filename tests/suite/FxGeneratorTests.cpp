#include "engine/Generator.h"
#include "engine/ProgramCodec.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <set>
#include <vector>
int main(){
    int fails=0;auto check=[&](bool b,const char* s){if(!b){std::printf("FAIL: %s\n",s);++fails;}};
    using namespace vox::fx;using namespace vox::fx::generator;
    Settings s;s.family=4;s.complexity=4;s.seed=123;Result a,b;Locks locks;
    check(generate(s,Result{},locks,a)&&generate(s,Result{},locks,b),"deterministic generation");
    std::vector<std::byte> x,y;programcodec::encode(a.program,x);programcodec::encode(b.program,y);check(x==y&&a.seconds==b.seconds,"identical seeds");
    locks.layers[0]=all;locks.layers[1]=gain|pan;locks.duration=true;
    a.program.layers[0].familyControls={.4f,-.3f,.6f};
    Result changed;s.seed=987;check(generate(s,a,locks,changed),"locked generation");
    check(changed.program.layers[0].family==a.program.layers[0].family && changed.program.layers[0].gain==a.program.layers[0].gain &&
          changed.program.layers[0].familyControls==a.program.layers[0].familyControls &&
          changed.program.layers[1].gain==a.program.layers[1].gain && changed.program.layers[1].pan==a.program.layers[1].pan && changed.seconds==a.seconds,"locked values preserved");
    Result m1,m2;check(mutate(a,locks,22,m1)&&mutate(a,locks,22,m2),"mutation");
    programcodec::encode(m1.program,x);programcodec::encode(m2.program,y);check(x==y,"mutation deterministic");
    check(m1.program.layers[0].gain==a.program.layers[0].gain && m1.program.layers[1].pan==a.program.layers[1].pan,"mutation respects locks");
    // familyControls are user-authored, not generator-owned: mutation must
    // keep them even when the Character lock is open on an unlocked layer.
    Locks openLocks;
    Result m3;check(mutate(a,openLocks,22,m3)&&
          m3.program.layers[0].familyControls==a.program.layers[0].familyControls,
          "mutation preserves authored family controls without a lock");
    Result va,vb;check(variation(s,a,locks,false,va)&&variation(s,a,locks,true,vb),"A/B generation");
    programcodec::encode(va.program,x);programcodec::encode(vb.program,y);check(x!=y,"A/B distinct");
    std::set<std::vector<std::byte>> programs;
    auto engine=std::make_unique<NoiseRiser>();engine->prepare(44100);
    for(std::size_t i=0;i<factoryCount;++i){Result preset;check(factory(i,preset)&&preset.program.valid(),"factory valid");
        check(static_cast<std::size_t>(preset.program.layers[0].family)==i/4,"four variants per family");
        programcodec::encode(preset.program,x);programs.insert(x);
        engine->prepare(44100);check(engine->setProgram(preset.program)&&engine->noteOn(1,60,1,preset.seconds),"factory trigger");
        std::array<float,512> l{},r{};double energy=0;
        for(int block=0;block<44;++block){engine->render(l.data(),r.data(),512);for(auto value:l){check(std::isfinite(value)&&std::abs(value)<1,"finite factory render");energy+=value*value;}}
        check(energy>1e-8,"factory program audible");
    }
    check(programs.size()==48,"factory structures unique");
    auto invalid=s;invalid.seconds=99;Result out=a;check(!generate(invalid,a,locks,out)&&out.seconds==a.seconds,"invalid request rejected");
    {
        Result designed;
        designed.program.mode=PlayMode::transition;designed.program.bars=8;
        designed.program.panoramaCurve=timeline::Curve::constant(.8f);
        designed.program.layers[0].pitchCurve=timeline::Curve::constant(.9f);
        designed.program.layers[0].cutoffCurve=timeline::Curve::constant(.2f);
        Locks motionLocks;motionLocks.layers[0]=pitchCurve|cutoffCurve;
        Result regenerated,mutated;
        check(generate(s,designed,motionLocks,regenerated)&&mutate(regenerated,motionLocks,23,mutated),"generation and mutation with new trajectory locks");
        check(mutated.program.mode==PlayMode::transition&&mutated.program.bars==8&&mutated.program.panoramaCurve.evaluate(.5f)==.8f,
              "generation does not reset the authored Program timeline");
        check(mutated.program.layers[0].pitchCurve.evaluate(.5f)==.9f&&mutated.program.layers[0].cutoffCurve.evaluate(.5f)==.2f,
              "pitch and cutoff locks survive both operations");
    }
    std::printf("FX generator: %d failure(s)\n",fails);return fails?1:0;
}
