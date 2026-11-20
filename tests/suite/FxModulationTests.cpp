#include "engine/Modulation.h"
#include <cmath>
#include <cstdio>
#include <limits>
#ifdef VOX_TEST_INTERPOSE_MALLOC
#include "AllocationProbe.h"
#endif
int main(){
    int fails=0;auto check=[&](bool b,const char* s){if(!b){std::printf("FAIL: %s\n",s);++fails;}};
    using namespace vox::fx::mod;
    Route routes[2]{{Source::lfo1,Destination::gain,.4f,0},{Source::velocity,Destination::pan,-.25f,0}};
    Matrix matrix;check(matrix.prepare(48000,routes,2),"valid routes");
    Destinations base{.5f,.5f,.5f,.5f};Sources src{};src[0]=.5f;src[6]=1;
    const auto result=matrix.step(base,src);
    check(std::abs(result[0]-.7f)<1e-6f && std::abs(result[1]-.25f)<1e-6f,"typed route values");
    check(base[0]==.5f && base[1]==.5f,"host bases unchanged");
    routes[0].depth=2;check(!matrix.prepare(48000,routes,2),"invalid depth rejected");
    check(!matrix.prepare(48000,routes,65),"route bound enforced");
    routes[0].depth=1;routes[0].smoothingSeconds=.01f;check(matrix.prepare(48000,routes,1),"smoothed route");
    src[0]=1;const auto first=matrix.step(base,src);check(first[0]>.5f && first[0]<.51f,"smoothing begins gradually");
    for(int i=0;i<24000;++i)(void)matrix.step(base,src);
    check(matrix.step(base,src)[0]==1,"clamped destination");
#ifdef VOX_TEST_INTERPOSE_MALLOC
    voxProbeBegin();
    for(int i=0;i<1024;++i)(void)matrix.step(base,src);
    const auto allocations=voxProbeEnd();
    if(voxProbeAvailable())check(allocations==0,"matrix audio evaluation allocates nothing");
#endif
    src[0]=std::numeric_limits<float>::quiet_NaN();check(std::isfinite(matrix.step(base,src)[0]),"non-finite source sanitized");
    Lfo lfo;check(lfo.next(1,48000)==0,"LFO reset");for(int i=0;i<12000;++i)(void)lfo.next(1,48000);
    check(lfo.next(1,48000)>.99f,"LFO phase");
    SampleHold a,b;a.reset(123);b.reset(123);
    for(int i=0;i<100000;++i)check(a.next(20,48000)==b.next(20,48000),"sample-hold deterministic");
    std::printf("FX modulation primitives: %d failure(s)\n",fails);return fails?1:0;
}
