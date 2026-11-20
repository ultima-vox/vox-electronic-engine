#include "engine/Pattern.h"
#include <cmath>
#include <cstdio>
#include <set>
#ifdef VOX_TEST_INTERPOSE_MALLOC
#include "AllocationProbe.h"
#endif
int main(){
    using namespace vox::fx::pattern;
    int fails=0;auto check=[&](bool b,const char* s){if(!b){std::printf("FAIL: %s\n",s);++fails;}};
    Pattern p;p.seed=123;p.steps[0].enabled=true;p.steps[0].ratchets=3;p.steps[2].enabled=true;
    std::vector<Event> a,b;check(schedule(p,a)&&schedule(p,b)&&a.size()==8,"schedule ratchets");
    FixedSchedule fixed;check(scheduleFixed(p,fixed)&&fixed.count==a.size(),"fixed realtime schedule");
    for(std::size_t i=0;i<fixed.count;++i)check(fixed.events[i].tick==a[i].tick && fixed.events[i].note==a[i].note &&
        fixed.events[i].on==a[i].on && fixed.events[i].velocity==a[i].velocity,"fixed and MIDI schedule match");
    Pattern maximum;maximum.bars=4;for(auto& step:maximum.steps){step.enabled=true;step.ratchets=4;}
#ifdef VOX_TEST_INTERPOSE_MALLOC
    voxProbeBegin();
#endif
    const bool full=scheduleFixed(maximum,fixed);
#ifdef VOX_TEST_INTERPOSE_MALLOC
    const auto allocations=voxProbeEnd();if(voxProbeAvailable())check(allocations==0,"fixed scheduler allocates nothing");
#endif
    check(full&&fixed.count==512,"fixed 512-event capacity");
    for(std::size_t i=1;i<fixed.count;++i)check(fixed.events[i-1].tick<=fixed.events[i].tick,"fixed events sorted");
    bool same=a.size()==b.size();for(std::size_t i=0;i<a.size()&&same;++i)same=a[i].tick==b[i].tick&&a[i].note==b[i].note&&a[i].on==b[i].on;
    check(same,"seeded deterministic scheduling");
    check(a[0].tick==0&&a[2].tick==80&&a[4].tick==160&&a[6].tick==480,"exact grid and ratchet offsets");
    std::uint64_t samples=0;check(tickToSample(960,120,48000,samples)&&samples==24000,"ticks to sample timing");
    std::vector<std::uint8_t> midi,midi2;check(exportMidi(p,120,midi)&&exportMidi(p,120,midi2)&&midi==midi2,"deterministic MIDI");
    check(midi.size()>40&&midi[0]=='M'&&midi[1]=='T'&&midi[2]=='h'&&midi[3]=='d'&&midi[8]==0&&midi[9]==0&&midi[10]==0&&midi[11]==1,"valid SMF header");
    std::vector<std::uint8_t> atHostTempo;
    check(exportMidi(p,73.125,atHostTempo)&&atHostTempo.size()>29&&atHostTempo[22]==0&&
          atHostTempo[23]==0xff&&atHostTempo[24]==0x51&&atHostTempo[25]==3,"host tempo MIDI metadata");
    if(atHostTempo.size()>28){const auto micros=(static_cast<std::uint32_t>(atHostTempo[26])<<16)|
        (static_cast<std::uint32_t>(atHostTempo[27])<<8)|atHostTempo[28];
        check(micros==static_cast<std::uint32_t>(std::llround(60000000.0/73.125)),"captured BPM encoded as microseconds per quarter");}
    const std::uint32_t len=(static_cast<std::uint32_t>(midi[18])<<24)|(static_cast<std::uint32_t>(midi[19])<<16)|(static_cast<std::uint32_t>(midi[20])<<8)|midi[21];
    check(len==midi.size()-22&&midi[midi.size()-3]==0xff&&midi[midi.size()-2]==0x2f&&midi.back()==0,"track length and end marker");
    // Parse the SMF event stream and compare every note and absolute tick to
    // the canonical scheduler (including note-off and ratchet ordering).
    std::size_t pos=22, eventIndex=0;std::uint32_t tick=0;
    while(pos<midi.size()) {
        std::uint32_t delta=0;std::uint8_t byte=0;
        do { if(pos>=midi.size())break;byte=midi[pos++];delta=(delta<<7)|(byte&127); } while(byte&128);
        tick+=delta;if(pos>=midi.size())break;const auto status=midi[pos++];
        if(status==0xff){if(pos+2>midi.size())break;const auto type=midi[pos++];const auto size=midi[pos++];
            if(type==0x2f)break;
            pos+=size;continue;
        }
        if((status==0x90 || status==0x80) && pos+2<=midi.size()) {
            const auto note=midi[pos++], velocity=midi[pos++];
            check(eventIndex<a.size() && a[eventIndex].tick==tick && a[eventIndex].note==note &&
                  a[eventIndex].velocity==velocity && a[eventIndex].on==(status==0x90),"SMF events match canonical schedule");
            ++eventIndex;
        } else {check(false,"unexpected MIDI event");break;}
    }
    check(eventIndex==a.size(),"all scheduled events exported");
    auto bad=p;bad.steps[0].ratchets=0;check(!schedule(bad,a)&&!exportMidi(bad,120,midi2),"invalid pattern rejected");
    check(!tickToSample(1,std::nan(""),48000,samples)&&!exportMidi(p,0,midi2),"invalid timing rejected");
    std::set<std::vector<std::uint8_t>> variants;for(std::size_t i=0;i<12;++i){Pattern pattern;std::vector<std::uint8_t> bytes;
        check(factory(i,pattern)&&schedule(pattern,a)&&!a.empty()&&exportMidi(pattern,120,bytes),"factory pattern generates trigger MIDI");variants.insert(bytes);}
    check(variants.size()==12,"12 distinct patterns");
    std::printf("FX trigger patterns: %d failure(s)\n",fails);return fails?1:0;
}
