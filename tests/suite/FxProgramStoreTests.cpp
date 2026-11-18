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
    std::printf("FX Program store: %d failure(s)\n",fails);return fails?1:0;
}
