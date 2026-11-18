#include "engine/Pattern.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>
#ifdef VOX_TEST_INTERPOSE_MALLOC
#include "AllocationProbe.h"
#endif
int main(){
    using namespace vox::fx::pattern;
    int fails=0;auto check=[&](bool ok,const char* msg){if(!ok){std::printf("FAIL: %s\n",msg);++fails;}};
    Pattern p;p.seed=765;p.bars=1;for(auto& step:p.steps){step.enabled=true;step.probability=100;step.ratchets=3;}
    FixedSchedule schedule;check(scheduleFixed(p,schedule),"prepare fixed schedule");
    for(double rate:{44100.0,48000.0,96000.0})for(double bpm:{75.0,120.0,173.0}){
        const int total=static_cast<int>(rate*4);std::vector<int> baseline;
        auto render=[&](int block,std::vector<int>& out){for(int start=0;start<total;start+=block){const int size=std::min(block,total-start);
            const double ppq=static_cast<double>(start)*bpm/(60*rate);
            check(forEachTrigger(schedule,p.bars,ppq,bpm,rate,size,[&](int at,std::uint8_t,std::uint8_t){out.push_back(start+at);}),"valid host timing");}};
        render(total,baseline);for(int block:{64,128,512}){std::vector<int> split;render(block,split);check(split==baseline,"block-invariant PPQ triggers");}
        check(!baseline.empty()&&baseline.front()==0,"initial downbeat");
        std::size_t index=0;
        for(std::size_t i=0;i<schedule.count;++i)if(schedule.events[i].on){
            std::uint64_t expected=0;check(tickToSample(schedule.events[i].tick,bpm,rate,expected),"tick conversion");
            if(expected<static_cast<std::uint64_t>(total)){
                check(index<baseline.size()&&baseline[index]==static_cast<int>(expected),"host offsets match MIDI tick conversion");++index;
            }
        }
        for(std::size_t i=1;i<baseline.size();++i)check(baseline[i]>baseline[i-1],"ordered triggers");
    }
    {
        // Two blocks at different tempos: elapsed PPQ is preserved at the
        // boundary and later triggers use the new rate, not a restarted clock.
        std::vector<int> triggers;const double rate=48000;
        check(forEachTrigger(schedule,1,0,120,rate,12000,[&](int at,std::uint8_t,std::uint8_t){triggers.push_back(at);}),"first tempo segment");
        check(forEachTrigger(schedule,1,.5,60,rate,24000,[&](int at,std::uint8_t,std::uint8_t){triggers.push_back(12000+at);}),"second tempo segment");
        check(std::is_sorted(triggers.begin(),triggers.end())&&!triggers.empty(),"tempo change preserves event order");
        check(std::count(triggers.begin(),triggers.end(),12000)==1,"tempo boundary trigger occurs exactly once");
    }
    std::array<int,512> offsets{};std::size_t count=0;
#ifdef VOX_TEST_INTERPOSE_MALLOC
    voxProbeBegin();
#endif
    const bool ok=forEachTrigger(schedule,p.bars,0.0,120.0,48000.0,48000,[&](int at,std::uint8_t,std::uint8_t){offsets[count++]=at;});
#ifdef VOX_TEST_INTERPOSE_MALLOC
    const auto allocations=voxProbeEnd();if(voxProbeAvailable())check(allocations==0,"host trigger scheduler allocates nothing");
#endif
    check(ok&&count>0,"bounded callback");
    check(!forEachTrigger(schedule,p.bars,0,0,48000,512,[](int,std::uint8_t,std::uint8_t){}),"invalid tempo rejected");
    std::printf("FX pattern playback scheduling: %d failure(s)\n",fails);return fails?1:0;
}
