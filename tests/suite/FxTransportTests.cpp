#include "engine/Transport.h"
#include <cstdio>
#include <limits>
#ifdef VOX_TEST_INTERPOSE_MALLOC
#include "AllocationProbe.h"
#endif
int main(){
    using namespace vox::fx::transport;
    int failures=0;auto check=[&](bool ok,const char* message){if(!ok){std::printf("FAIL: %s\n",message);++failures;}};
    Tracker tracker;Position position;position.playing=true;position.hasPpq=true;position.hasTempo=true;
    position.ppq=4;position.tempo=120;
    check(!tracker.update(position,48000,480),"first playing position is not seek");
    position.ppq=4.02;check(!tracker.update(position,48000,480),"PPQ continuity without sample position");
    position.ppq=4.04;position.tempo=90;check(!tracker.update(position,48000,480),"tempo change preserves expected prior block progression");
    position.ppq=4.055;check(!tracker.update(position,48000,480),"new tempo affects following block");
    position.ppq=12;check(tracker.update(position,48000,480),"PPQ-only seek detected");
    position.playing=false;check(tracker.update(position,48000,480),"stop detected");
    position.playing=true;position.ppq=0;check(!tracker.update(position,48000,480),"start does not fabricate seek");
    position.hasSample=true;position.sample=100;position.ppq=0;
    check(!tracker.update(position,48000,480),"switch to sample timeline without false seek");
    position.sample=580;position.ppq=8;check(!tracker.update(position,48000,480),"sample continuity has precedence over PPQ");
    position.sample=1061;check(tracker.update(position,48000,480),"sample discontinuity detected");
    tracker.unavailable();position.sample=10000;check(!tracker.update(position,48000,480),"missing host resets tracking");
    tracker.unavailable();position.hasSample=false;position.ppq=3;position.tempo=120;
    check(!tracker.update(position,48000,480),"PPQ small jitter setup");position.ppq=3.021;
    check(!tracker.update(position,48000,480),"PPQ jitter within threshold does not panic");
    position.ppq=std::numeric_limits<double>::quiet_NaN();check(!tracker.update(position,48000,480),"invalid PPQ ignored");
    position.ppq=40;check(!tracker.update(position,48000,480),"invalid PPQ resets continuity");
    position.tempo=0;check(!tracker.update(position,48000,480),"invalid tempo ignored");
    position.tempo=120;position.ppq=50;check(!tracker.update(position,48000,480),"invalid tempo resets PPQ prediction");
    tracker.unavailable();position.hasSample=true;position.sample=std::numeric_limits<std::int64_t>::max()-2;
    position.hasPpq=false;check(!tracker.update(position,48000,480),"sample overflow is not predicted");
    position.sample=0;check(!tracker.update(position,48000,480),"unpredictable overflow does not fabricate seek");
#ifdef VOX_TEST_INTERPOSE_MALLOC
    voxProbeBegin();
#endif
    for(int i=0;i<1000;++i){position.sample=static_cast<std::int64_t>(i)*480;tracker.update(position,48000,480);}
#ifdef VOX_TEST_INTERPOSE_MALLOC
    const auto allocations=voxProbeEnd();if(voxProbeAvailable())check(allocations==0,"transport updates allocate nothing");
#endif
    std::printf("FX transport tracking: %d failure(s)\n",failures);return failures?1:0;
}
