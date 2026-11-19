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
#include <utility>

// Preliminary DSP-only timing, NOT a plugin, DAW, or deadline qualification.
// Preparation is outside the timed region. 1536-event, four-bar schedule at
// 1000 BPM plus eight active 4-layer Shepard events, up to four short
// retirement fades and four serial drive effects.
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
        if(engine->droppedEventCount()!=static_cast<std::uint32_t>(dropped))++failures;
        if(clipped!=0 || peak>=0.9 || peak<=0.01)++failures; // no rail and no muted-output workaround
        std::sort(timings.begin(),timings.end());
        std::printf("%.0f Hz 8x4 Shepard + 4 drive + max pattern @1000 BPM: p50 %.2f p95 %.2f p99 %.2f max %.2f us; accepted %d dropped %d peak %.3f clipped %d checksum %.6f\n",
            rate,timings[iterations/2],timings[iterations*95/100],timings[iterations*99/100],timings.back(),accepted,dropped,peak,clipped,checksum);
    }
    {
        // Side-by-side scheduler-only full-scan reference; small callbacks at
        // realistic BPM with the densest schedule, no synth/effects included.
        constexpr int count=10000;constexpr double rate=48000,bpm2=137;
        std::vector<double> fullTimes,lookupTimes;fullTimes.reserve(count);lookupTimes.reserve(count);
        std::uint64_t fullChecksum=0,lookupChecksum=0;
        for(int n=0;n<count;++n){
            const double position=static_cast<double>(n)*block*bpm2/(60*rate);
            const double startTick=position*pattern::ppq,per=60*rate/(bpm2*pattern::ppq);
            const double endTick=startTick+block/per,period=static_cast<double>(p.bars)*16*pattern::ticksPerStep;
            const auto first=static_cast<std::int64_t>(std::floor((startTick-.5/per)/period));
            const auto last=static_cast<std::int64_t>(std::floor((endTick+.5/per)/period));
            auto begin=Clock::now();
            for(auto cycle=first;cycle<=last;++cycle)for(std::size_t i=0;i<schedule.count;++i){const auto& event=schedule.events[i];
                if(!event.on)continue;
                const double at=(static_cast<double>(cycle)*period+event.tick-startTick)*per;
                const auto rounded=std::floor(at+.5+1.0e-7);
                if(rounded>=0&&rounded<block)fullChecksum+=1+static_cast<std::uint64_t>(rounded)+event.note+event.velocity;
            }
            auto end=Clock::now();fullTimes.push_back(std::chrono::duration<double,std::micro>(end-begin).count());
            begin=Clock::now();
            const bool ok=pattern::forEachTrigger(schedule,p.bars,position,bpm2,rate,block,
                [&](int at,std::uint8_t note,std::uint8_t velocity) noexcept {lookupChecksum+=1+static_cast<std::uint64_t>(at)+note+velocity;});
            end=Clock::now();lookupTimes.push_back(std::chrono::duration<double,std::micro>(end-begin).count());
            if(!ok)++failures;
        }
        if(fullChecksum!=lookupChecksum)++failures;
        for(auto* times:{&fullTimes,&lookupTimes})std::sort(times->begin(),times->end());
        for(const auto& item:{std::pair{"full scan",&fullTimes},std::pair{"bounded lookup",&lookupTimes}})
            std::printf("scheduler only %s 48k 128 @137 BPM: p50 %.3f p95 %.3f p99 %.3f max %.3f us; checksum %llu\n",
                item.first,(*item.second)[count/2],(*item.second)[count*95/100],(*item.second)[count*99/100],item.second->back(),
                static_cast<unsigned long long>(fullChecksum));
    }
    return failures?1:0;
}
