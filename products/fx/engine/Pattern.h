#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace vox::fx::pattern {
// Offline canonical trigger scheduler. Host-synchronized realtime integration
// is not implemented yet; the same tick events are used by SMF export.
inline constexpr std::uint32_t ppq=960, ticksPerStep=240;
enum class Grid : std::uint8_t { quarter, eighth, sixteenth, thirtysecond, quarterTriplet, eighthTriplet, sixteenthTriplet, thirtysecondTriplet, count };
inline constexpr std::uint32_t gridTicks(Grid grid) noexcept {
    constexpr std::array<std::uint32_t,8> ticks{960,480,240,120,640,320,160,80};
    const auto index=static_cast<unsigned>(grid);return index<ticks.size()?ticks[index]:0;
}
inline constexpr std::uint32_t stepsPerBar(Grid grid) noexcept {
    const auto ticks=gridTicks(grid);return ticks?4*ppq/ticks:0;
}
struct Step {
    bool enabled=false;
    std::uint8_t note=60, velocity=100, probability=100, ratchets=1;
    bool valid() const noexcept { return note<128 && velocity>0 && velocity<128 && probability<=100 && ratchets>=1 && ratchets<=4; }
};
struct Pattern {
    std::array<Step,192> steps{};
    std::uint8_t bars=1;
    Grid grid=Grid::sixteenth;
    float swing=0.0f; // 0..1: odd step starts are delayed up to half a step
    std::uint32_t seed=1;
    bool valid() const noexcept {
        if(bars<1||bars>4||gridTicks(grid)==0||!std::isfinite(swing)||swing<0||swing>1)return false;
        for(const auto& step:steps)if(!step.valid())return false;
        return true;
    }
};
struct Event { std::uint32_t tick=0;std::uint8_t note=0,velocity=0;bool on=false; };
// Fixed-capacity canonical schedule, also suitable for audio-thread handoff.
// Note-off precedes the following ratchet; construction is already ordered.
struct FixedSchedule { std::array<Event,1536> events{}; std::size_t count=0; };
inline bool scheduleFixed(const Pattern& p,FixedSchedule& output) noexcept {
    if(!p.valid())return false;
    FixedSchedule next;
    std::uint32_t rng=p.seed?p.seed:1;
    const auto stepTicks=gridTicks(p.grid);
    for(std::uint32_t i=0;i<static_cast<std::uint32_t>(p.bars)*stepsPerBar(p.grid);++i){
        const auto& s=p.steps[i];rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;
        if(!s.enabled || rng%100>=s.probability)continue;
        const auto shifted=i*stepTicks+((i&1u)?static_cast<std::uint32_t>(std::lround(p.swing*stepTicks*.5)):0u);
        const auto duration=(i+1)*stepTicks-shifted;
        for(std::uint32_t r=0;r<s.ratchets;++r){
            const auto start=shifted+(r*duration)/s.ratchets;
            const auto end=shifted+((r+1)*duration)/s.ratchets;
            const auto length=std::max(1u,(end-start)*3/4);
            next.events[next.count++]={start,s.note,s.velocity,true};
            next.events[next.count++]={start+length,s.note,0,false};
        }
    }
    output=next;return true;
}
inline bool schedule(const Pattern& p,std::vector<Event>& output) {
    FixedSchedule fixed;if(!scheduleFixed(p,fixed))return false;
    std::vector<Event> next(fixed.events.begin(),fixed.events.begin()+static_cast<std::ptrdiff_t>(fixed.count));
    output=std::move(next);return true;
}
// Host-synchronized trigger enumeration for a constant-tempo block. Events are
// rounded to the nearest sample, matching MIDI tick conversion; PPQ is in quarter notes. Bounds
// are proportional to the block duration, with at most 1536 events per 4 bars.
// Only note-on is emitted because the current instrument is one-shot.
template <typename Emit>
inline bool forEachTrigger(const FixedSchedule& schedule,unsigned bars,double ppqPosition,double bpm,
                           double rate,int samples,Emit emit) noexcept {
    if(bars<1||bars>4||schedule.count>schedule.events.size()||!std::isfinite(ppqPosition)||std::abs(ppqPosition)>1.0e9||
       !std::isfinite(bpm)||bpm<10||bpm>1000||!std::isfinite(rate)||rate<8000||rate>384000||samples<0)return false;
    if(samples==0||schedule.count==0)return true;
    const double start=ppqPosition*ppq, samplesPerTick=60.0*rate/(bpm*ppq);
    const double end=start+samples/samplesPerTick;
    const double period=static_cast<double>(bars)*16*ticksPerStep;
    const auto first=static_cast<std::int64_t>(std::floor((start-0.5/samplesPerTick)/period));
    const auto last=static_cast<std::int64_t>(std::floor((end+0.5/samplesPerTick)/period));
    for(auto cycle=first;cycle<=last;++cycle){
        for(std::size_t i=0;i<schedule.count;++i){const auto& event=schedule.events[i];
            if(!event.on)continue;
            const double at=(static_cast<double>(cycle)*period+event.tick-start)*samplesPerTick;
            // A half-sample look-behind allows a trigger rounded onto the
            // first sample of this block to cross the preceding boundary.
            const double rounded=std::floor(at+0.5+1.0e-7);
            if(rounded>=0 && rounded<samples)
                emit(static_cast<int>(rounded),event.note,event.velocity);
        }
    }
    return true;
}
inline bool tickToSample(std::uint32_t tick,double bpm,double rate,std::uint64_t& sample) noexcept {
    if(!std::isfinite(bpm)||bpm<10||bpm>1000||!std::isfinite(rate)||rate<8000||rate>384000)return false;
    sample=static_cast<std::uint64_t>(std::llround(static_cast<double>(tick)*60.0*rate/(ppq*bpm)));return true;
}
inline void be16(std::vector<std::uint8_t>& o,std::uint16_t v){o.push_back(static_cast<std::uint8_t>(v>>8));o.push_back(static_cast<std::uint8_t>(v));}
inline void be32(std::vector<std::uint8_t>& o,std::uint32_t v){o.push_back(static_cast<std::uint8_t>(v>>24));o.push_back(static_cast<std::uint8_t>(v>>16));o.push_back(static_cast<std::uint8_t>(v>>8));o.push_back(static_cast<std::uint8_t>(v));}
inline void vlq(std::vector<std::uint8_t>& out,std::uint32_t value){
    std::uint8_t bytes[5]{};int count=0;bytes[count++]=static_cast<std::uint8_t>(value&127);value>>=7;
    while(value){bytes[count++]=static_cast<std::uint8_t>((value&127)|128);value>>=7;}
    while(count>0)out.push_back(bytes[--count]);
}
// Type 0 Standard MIDI File, one track, tempo and 4/4 signature. Only the
// trigger notes are exported; generated audio is never represented as MIDI.
inline bool exportMidi(const Pattern& pattern,double bpm,std::vector<std::uint8_t>& output) {
    if(!std::isfinite(bpm)||bpm<10||bpm>1000)return false;
    std::vector<Event> events;if(!schedule(pattern,events))return false;
    const auto micros=static_cast<std::uint32_t>(std::llround(60000000.0/bpm));
    if(micros>0xffffffu)return false;
    std::vector<std::uint8_t> track;
    track.insert(track.end(),{0,0xff,0x51,3,static_cast<std::uint8_t>(micros>>16),static_cast<std::uint8_t>(micros>>8),static_cast<std::uint8_t>(micros)});
    track.insert(track.end(),{0,0xff,0x58,4,4,2,24,8});
    std::uint32_t last=0;
    for(const auto& event:events){vlq(track,event.tick-last);last=event.tick;
        track.push_back(event.on?0x90:0x80);track.push_back(event.note);track.push_back(event.velocity);}
    vlq(track,static_cast<std::uint32_t>(pattern.bars)*16*ticksPerStep-last);
    track.insert(track.end(),{0xff,0x2f,0});
    std::vector<std::uint8_t> bytes;bytes.insert(bytes.end(),{'M','T','h','d'});be32(bytes,6);be16(bytes,0);be16(bytes,1);be16(bytes,static_cast<std::uint16_t>(ppq));
    bytes.insert(bytes.end(),{'M','T','r','k'});be32(bytes,static_cast<std::uint32_t>(track.size()));bytes.insert(bytes.end(),track.begin(),track.end());
    output=std::move(bytes);return true;
}
inline bool factory(std::size_t index,Pattern& output) noexcept {
    if(index>=12)return false;
    Pattern next;next.bars=static_cast<std::uint8_t>(1+index/4);next.seed=0x125a5u+static_cast<std::uint32_t>(index)*0x51edu;
    const unsigned interval=2u+static_cast<unsigned>(index%4);
    for(unsigned i=0;i<static_cast<unsigned>(next.bars)*16;++i){auto& s=next.steps[i];
        s.enabled=(i%interval)==0;s.note=static_cast<std::uint8_t>(48+index);s.velocity=static_cast<std::uint8_t>(75+(i%4)*12);
        s.probability=static_cast<std::uint8_t>(index%3==0?80:100);s.ratchets=static_cast<std::uint8_t>((index%4==3 && i%8==0)?2:1);}
    output=next;return true;
}
} // namespace vox::fx::pattern
