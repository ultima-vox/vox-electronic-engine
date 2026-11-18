#include "engine/PatternCodec.h"
#include <array>
#include <cstdio>
#include <vector>
#ifdef VOX_TEST_INTERPOSE_MALLOC
#include "AllocationProbe.h"
#endif
int main(){
    using namespace vox::fx::pattern;
    int fails=0;auto check=[&](bool ok,const char* message){if(!ok){std::printf("FAIL: %s\n",message);++fails;}};
    constexpr std::array<Grid,8> grids{Grid::quarter,Grid::eighth,Grid::sixteenth,Grid::thirtysecond,
        Grid::quarterTriplet,Grid::eighthTriplet,Grid::sixteenthTriplet,Grid::thirtysecondTriplet};
    constexpr std::array<unsigned,8> stepCounts{4,8,16,32,6,12,24,48};
    for(std::size_t g=0;g<grids.size();++g){Pattern p;p.bars=4;p.grid=grids[g];
        const unsigned n=stepCounts[g]*p.bars;for(unsigned i=0;i<n;++i){p.steps[i].enabled=true;p.steps[i].ratchets=4;}
        FixedSchedule fixed;check(p.valid()&&stepsPerBar(p.grid)==stepCounts[g]&&scheduleFixed(p,fixed),"grid schedule valid");
        check(fixed.count==n*8,"all four ratchets emitted");
        for(std::size_t i=1;i<fixed.count;++i)check(fixed.events[i-1].tick<=fixed.events[i].tick,"grid sorted");
        std::vector<std::byte> bytes;Pattern decoded;check(codec::encode(p,bytes)&&codec::decode(bytes,decoded)&&decoded.grid==p.grid,
            "grid and steps survive state");
        check(decoded.steps[n-1].enabled&&decoded.steps[n-1].ratchets==4,"last step survives state");
        std::vector<Event> midi;check(schedule(p,midi)&&midi.size()==fixed.count,"offline and realtime schedule share events");
        for(double rate:{44100.0,48000.0,96000.0}) {
            const double bpm=137;const int total=static_cast<int>(rate*2);
            std::vector<int> baseline;auto render=[&](int block,std::vector<int>& out){
                for(int start=0;start<total;start+=block){const int size=std::min(block,total-start);
                    const double ppqPosition=static_cast<double>(start)*bpm/(60*rate);
                    check(forEachTrigger(fixed,p.bars,ppqPosition,bpm,rate,size,[&](int offset,std::uint8_t,std::uint8_t){out.push_back(start+offset);}),"grid playback");
                }};
            render(total,baseline);
            for(int block:{64,128,512}){std::vector<int> split;render(block,split);check(split==baseline,"grid block invariance");}
            std::size_t eventIndex=0;for(const auto& event:midi)if(event.on){std::uint64_t sample=0;
                check(tickToSample(event.tick,bpm,rate,sample),"grid tick to sample");
                if(sample<static_cast<std::uint64_t>(total))check(eventIndex<baseline.size() && baseline[eventIndex++]==static_cast<int>(sample),"grid MIDI/playback offset parity");
            }
        }
    }
    Pattern maximum;maximum.bars=4;maximum.grid=Grid::thirtysecondTriplet;
    for(auto& step:maximum.steps){step.enabled=true;step.ratchets=4;}
    FixedSchedule maxSchedule;
#ifdef VOX_TEST_INTERPOSE_MALLOC
    voxProbeBegin();
#endif
    const bool maxOk=scheduleFixed(maximum,maxSchedule);
#ifdef VOX_TEST_INTERPOSE_MALLOC
    const auto allocations=voxProbeEnd();if(voxProbeAvailable())check(allocations==0,"maximum schedule is allocation-free");
#endif
    check(maxOk&&maxSchedule.count==1536,"maximum grid and ratchet schedule capacity");
    Pattern bad;bad.grid=static_cast<Grid>(255);FixedSchedule f;
    check(!bad.valid()&&!scheduleFixed(bad,f),"unknown grid rejected");
    std::printf("FX pattern grid: %d failure(s)\n",fails);return fails?1:0;
}
