#include "engine/NoiseRiser.h"
#include "engine/Pattern.h"
#include "engine/Effects.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

// Preliminary DSP-only timing, NOT a plugin, DAW, or deadline qualification.
// Preparation is outside the timed region. 1536-event, four-bar schedule at
// 1000 BPM plus eight 4-layer Shepard events and four serial drive effects.
int main(){
    using namespace vox::fx;using Clock=std::chrono::steady_clock;
    constexpr int block=128,iterations=400;constexpr double bpm=1000;
    pattern::Pattern p;p.bars=4;p.grid=pattern::Grid::thirtysecondTriplet;
    for(auto& step:p.steps){step.enabled=true;step.ratchets=4;}
    pattern::FixedSchedule schedule;if(!pattern::scheduleFixed(p,schedule)||schedule.count!=1536)return 1;
    int failures=0;
    for(double rate:{44100.0,48000.0,96000.0}) {
        auto engine=std::make_unique<NoiseRiser>();engine->prepare(rate);engine->setTempo(bpm);
        NoiseProgram program;for(auto& layer:program.layers){layer.enabled=true;layer.family=NoiseFamily::shepard;}
        if(!engine->setProgram(program))return 1;
        for(int i=0;i<8;++i)if(!engine->noteOn(1,60+i,1,4))return 1;
        effects::Chain effects;if(!effects.prepare(rate))return 1;
        for(std::size_t i=0;i<4;++i)effects.set(i,{effects::Type::drive,1,.7f});
        std::array<float,block> left{},right{};std::array<double,iterations> timings{};
        int accepted=0,dropped=0,clipped=0;double checksum=0,peak=0;
        for(int n=0;n<iterations;++n){
            const double ppq=static_cast<double>(n)*block*bpm/(60*rate);int cursor=0;
            const auto start=Clock::now();
            const bool scheduled=pattern::forEachTrigger(schedule,p.bars,ppq,bpm,rate,block,
                [&](int at,std::uint8_t note,std::uint8_t velocity) noexcept {
                    if(at>cursor)engine->render(left.data()+cursor,right.data()+cursor,at-cursor);
                    cursor=at;
                    if(engine->noteOn(1,note,static_cast<float>(velocity)/127.0f,4))++accepted;else ++dropped;
                });
            if(cursor<block)engine->render(left.data()+cursor,right.data()+cursor,block-cursor);
            effects.process(left.data(),right.data(),block);const auto end=Clock::now();
            timings[static_cast<std::size_t>(n)]=std::chrono::duration<double,std::micro>(end-start).count();
            if(!scheduled)++failures;
            for(float value:left){if(!std::isfinite(value))++failures;
                if(std::abs(value)>=1)++clipped;
                peak=std::max(peak,static_cast<double>(std::abs(value)));checksum+=value;}
        }
        std::sort(timings.begin(),timings.end());
        std::printf("%.0f Hz 8x4 Shepard + 4 drive + max pattern @1000 BPM: p50 %.2f p95 %.2f p99 %.2f max %.2f us; accepted %d dropped %d peak %.3f clipped %d checksum %.6f\n",
            rate,timings[iterations/2],timings[iterations*95/100],timings[iterations*99/100],timings.back(),accepted,dropped,peak,clipped,checksum);
    }
    return failures?1:0;
}
