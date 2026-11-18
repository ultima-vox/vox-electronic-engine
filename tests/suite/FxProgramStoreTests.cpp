#include "engine/ProgramStore.h"
#include <atomic>
#include <cstdio>
#include <thread>
#ifdef VOX_TEST_INTERPOSE_MALLOC
#include "AllocationProbe.h"
#endif
int main(){
    int fails=0;auto check=[&](bool b,const char* s){if(!b){std::printf("FAIL: %s\n",s);++fails;}};
    vox::fx::ProgramStore store; vox::fx::NoiseProgram result; std::uint64_t seen=0;
    check(store.tryRead(result,seen)&&result.layers[0].enabled,"initial program");
    check(!store.tryRead(result,seen),"unchanged version skipped");
    auto invalid=store.snapshot();invalid.layers[0].gain=2;
    check(!store.publish(invalid)&&!store.tryRead(result,seen),"invalid publication rejected");
    auto testProgram=store.snapshot(); testProgram.layers[0].gain=.25f;
    check(store.publish(testProgram), "publish for audio probe");
#ifdef VOX_TEST_INTERPOSE_MALLOC
    voxProbeBegin();
#endif
    const bool read=store.tryRead(result,seen);
#ifdef VOX_TEST_INTERPOSE_MALLOC
    const auto allocations=voxProbeEnd();
    if(voxProbeAvailable()) check(allocations==0,"snapshot read allocates nothing");
#endif
    check(read && result.layers[0].gain==.25f,"audio snapshot applied");
    check(!store.editProgram([](vox::fx::NoiseProgram& p){p.layers[0].gain=.9f;return false;}) &&
          store.snapshot().layers[0].gain==.25f,"aborted whole-program edit leaves state unchanged");
    check(!store.editProgram([](vox::fx::NoiseProgram& p){p.layers[0].gain=2;return true;}) &&
          store.snapshot().layers[0].gain==.25f,"invalid whole-program edit leaves state unchanged");
    check(store.editProgram([](vox::fx::NoiseProgram& p){p.layers[2].gain=.75f;return true;}) &&
          store.snapshot().layers[2].gain==.75f,"whole-program edit published");
    (void) store.tryRead(result,seen); // consume the pre-stress edit
    constexpr int iterations=30000;std::atomic<bool> done{false};
    std::thread writer([&]{for(int i=0;i<iterations;++i){vox::fx::NoiseProgram p;
        p.layers[0].gain=static_cast<float>(i)/iterations;p.layers[1].gain=p.layers[0].gain;
        if(!store.publish(p))std::abort();}done.store(true,std::memory_order_release);});
    do {
        if(store.tryRead(result,seen))check(result.layers[0].gain==result.layers[1].gain,"coherent snapshot");
        else std::this_thread::yield();
    } while(!done.load(std::memory_order_acquire));
    writer.join();
    while(store.tryRead(result,seen)){}
    check(result.layers[0].gain==static_cast<float>(iterations-1)/iterations,"latest state delivered");
    {
        vox::fx::ProgramStore multi;
        std::thread a([&] { for(int i=0;i<1000;++i) if(!multi.setLayerFamily(0,vox::fx::NoiseFamily::downlifter)) std::abort(); });
        std::thread b([&] { for(int i=0;i<1000;++i) if(!multi.setLayerFamily(1,vox::fx::NoiseFamily::formant)) std::abort(); });
        a.join(); b.join();
        const auto final=multi.snapshot();
        check(final.layers[0].family==vox::fx::NoiseFamily::downlifter &&
              final.layers[1].family==vox::fx::NoiseFamily::formant, "concurrent layer edits preserved");
    }
    {
        vox::fx::ProgramStore unified;vox::fx::NoiseProgram audio;std::uint64_t fx=0,seq=0;
        (void)unified.tryRead(audio,fx,seq);
        std::atomic<bool> finished{false};
        std::thread producer([&]{for(int i=0;i<10000;++i){vox::fx::NoiseProgram program;
            const bool drive=(i&1)!=0;program.layers[0].gain=drive?.75f:.25f;
            auto packed=vox::fx::effects::defaults();
            const auto setting=vox::fx::effects::pack({drive?vox::fx::effects::Type::drive:vox::fx::effects::Type::bypass,1,.5f});
            packed=(packed&~std::uint64_t{0xffff})|setting;
            if(!unified.publishState(program,packed))std::abort();
        }finished.store(true,std::memory_order_release);});
        do {if(unified.tryRead(audio,fx,seq)){
            const auto type=vox::fx::effects::unpack(static_cast<std::uint16_t>(fx)).type;
            check((type==vox::fx::effects::Type::drive&&audio.layers[0].gain==.75f)||
                  (type==vox::fx::effects::Type::bypass&&audio.layers[0].gain==.25f),"coherent Program and FX snapshot");
        }}while(!finished.load(std::memory_order_acquire));
        producer.join();
        const auto state=unified.snapshotState();check(state.program.layers[0].gain==.75f&&
            vox::fx::effects::unpack(static_cast<std::uint16_t>(state.effects)).type==vox::fx::effects::Type::drive,"latest unified state");
        check(!unified.publishState(state.program,(state.effects&~std::uint64_t{7})|7),"invalid effects state rejected");
        std::thread layerWriter([&]{for(int i=0;i<1000;++i)if(!unified.setLayerFamily(1,vox::fx::NoiseFamily::formant))std::abort();});
        std::thread fxWriter([&]{for(int i=0;i<1000;++i)if(!unified.setEffect(2,{vox::fx::effects::Type::gate,.5f,.5f}))std::abort();});
        layerWriter.join();fxWriter.join();const auto both=unified.snapshotState();
        check(both.program.layers[1].family==vox::fx::NoiseFamily::formant &&
            vox::fx::effects::unpack(static_cast<std::uint16_t>(both.effects>>32)).type==vox::fx::effects::Type::gate,
            "concurrent layer and FX edits preserved");
    }
    {
        vox::fx::ProgramStore shared;
        vox::fx::NoiseProgram program;auto effects=vox::fx::effects::defaults();vox::fx::pattern::Pattern pattern;
        pattern.bars=4;pattern.seed=300;pattern.steps[1].enabled=true;program.layers[0].gain=.3f;
        check(shared.publishState(program,effects,pattern),"publish Program and pattern");
        vox::fx::NoiseProgram audio;vox::fx::pattern::Pattern audioPattern;std::uint64_t fx=0,seq=0;
        check(shared.tryRead(audio,fx,audioPattern,seq)&&audioPattern.seed==300&&audioPattern.steps[1].enabled&&
              audio.layers[0].gain==.3f,"audio snapshot includes pattern");
        check(shared.editPattern([](vox::fx::pattern::Pattern& p){p.steps[2].enabled=true;}) &&
              shared.snapshotState().pattern.steps[2].enabled,"transactional pattern step edit");
        check(!shared.editPattern([](vox::fx::pattern::Pattern& p){p.bars=0;}) &&
              shared.snapshotState().pattern.bars==4,"invalid pattern step edit rejected");
        (void)shared.tryRead(audio,fx,audioPattern,seq);
        auto invalid=pattern;invalid.steps[0].ratchets=0;
        check(!shared.setPattern(invalid)&&shared.snapshotState().pattern.seed==300,"invalid pattern rejected transactionally");
        std::atomic<bool> done{false};
        std::thread writer([&]{for(int i=0;i<5000;++i){auto next=program;auto pat=pattern;
            next.layers[0].gain=(i&1)? .75f : .25f;pat.seed=(i&1)?75:25;
            if(!shared.publishState(next,effects,pat))std::abort();}done.store(true,std::memory_order_release);});
        do {if(shared.tryRead(audio,fx,audioPattern,seq))check((audio.layers[0].gain==.75f&&audioPattern.seed==75)||
            (audio.layers[0].gain==.25f&&audioPattern.seed==25),"coherent Program and pattern");
        }while(!done.load(std::memory_order_acquire));
        writer.join();
    }
    {
        vox::fx::ProgramStore shared;auto state=shared.snapshotState();vox::fx::macro::Config routes;
        check(routes.setGainRoute(3,.4f,.01f)&&shared.publishState(state.program,state.effects,state.pattern,routes),"publish typed macro routes");
        vox::fx::NoiseProgram audio;vox::fx::pattern::Pattern pattern;vox::fx::macro::Config audioRoutes;std::uint64_t fx=0,sequence=0;
        check(shared.tryRead(audio,fx,pattern,audioRoutes,sequence)&&audioRoutes==routes,"audio snapshot includes routes");
        auto invalid=routes;invalid.count=65;
        check(!shared.setMacroConfig(invalid)&&shared.snapshotState().macros==routes,"invalid route publication rejected");
        check(shared.editMacroConfig([](vox::fx::macro::Config& c){return c.setGainRoute(4,.5f,0);})&&
              shared.snapshotState().macros.count==2,"serialized route edit");
    }
    std::printf("FX Program store: %d failure(s)\n",fails);return fails?1:0;
}
