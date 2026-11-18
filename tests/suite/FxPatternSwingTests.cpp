#include "engine/PatternCodec.h"
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>
int main(){
    using namespace vox::fx::pattern;int failures=0;
    auto check=[&](bool ok,const char* message){if(!ok){std::printf("FAIL: %s\n",message);++failures;}};
    Pattern p;p.swing=1;p.steps[0].enabled=true;p.steps[1].enabled=true;p.steps[1].ratchets=2;p.steps[2].enabled=true;
    FixedSchedule schedule;check(scheduleFixed(p,schedule),"swing schedule");
    check(schedule.count==8 && schedule.events[0].tick==0 && schedule.events[2].tick==360 &&
          schedule.events[4].tick==420 && schedule.events[6].tick==480,"odd step delayed with bounded ratchets");
    for(std::size_t i=1;i<schedule.count;++i)check(schedule.events[i-1].tick<schedule.events[i].tick,"swing events ordered");
    std::vector<std::byte> bytes;Pattern restored;
    check(codec::encode(p,bytes)&&codec::decode(bytes,restored)&&restored.swing==1,"swing persisted");
    p.swing=std::numeric_limits<float>::quiet_NaN();check(!p.valid()&&!codec::encode(p,bytes),"nonfinite swing rejected");
    p.swing=-.01f;check(!p.valid(),"negative swing rejected");p.swing=1.01f;check(!p.valid(),"excess swing rejected");
    std::printf("FX pattern swing: %d failure(s)\n",failures);return failures?1:0;
}
