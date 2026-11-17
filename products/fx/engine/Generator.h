#pragma once
#include "NoiseRiser.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace vox::fx::generator {
// Work-in-progress program generator. Pitch/key, pattern generation, quality
// and preset browser integration are not yet implemented.
struct Settings {
    int family=0, style=0, complexity=1; // family 0..11, style 0..5, active layers 1..4
    float intensity=0.7f, energy=0.7f, stereo=0.5f, seconds=4.0f;
    std::uint32_t seed=1;
    bool valid() const noexcept {
        return family>=0&&family<12&&style>=0&&style<6&&complexity>=1&&complexity<=4&&
            std::isfinite(intensity)&&intensity>=0&&intensity<=1&&std::isfinite(energy)&&energy>=0&&energy<=1&&
            std::isfinite(stereo)&&stereo>=0&&stereo<=1&&std::isfinite(seconds)&&seconds>=0.05f&&seconds<=32;
    }
};
enum Field : std::uint32_t { family=1u, enabled=2u, timing=4u, gain=8u, pan=16u, gainCurve=32u, panCurve=64u, all=127u };
struct Locks { std::array<std::uint32_t,4> layers{}; bool duration=false; };
struct Result { NoiseProgram program; float seconds=4; };
class Random {
public:
    explicit Random(std::uint32_t seed) noexcept : state_(seed?seed:0x9e3779b9u) {}
    std::uint32_t next() noexcept { state_^=state_<<13;state_^=state_>>17;state_^=state_<<5;return state_; }
    float unit() noexcept { return static_cast<float>(next() >> 8) * (1.0f / 16777216.0f); }
private: std::uint32_t state_;
};
inline void applyLocks(NoiseLayer& next,const NoiseLayer& old,std::uint32_t mask) noexcept {
    if(mask&family)next.family=old.family;
    if(mask&enabled)next.enabled=old.enabled;
    if(mask&timing)next.timing=old.timing;
    if(mask&gain)next.gain=old.gain;
    if(mask&pan)next.pan=old.pan;
    if(mask&gainCurve)next.gainCurve=old.gainCurve;
    if(mask&panCurve)next.panCurve=old.panCurve;
}
inline bool generate(const Settings& settings,const Result& previous,const Locks& locks,Result& output) noexcept {
    if(!settings.valid() || !previous.program.valid() || !std::isfinite(previous.seconds) || previous.seconds<.05f || previous.seconds>32) return false;
    for(auto mask:locks.layers)if(mask & ~static_cast<std::uint32_t>(all))return false;
    Random rng(settings.seed ^ (0x85ebca6bu * static_cast<std::uint32_t>(settings.style+1)));
    Result next;next.seconds=locks.duration?previous.seconds:settings.seconds;
    for(std::size_t i=0;i<next.program.layers.size();++i){
        auto& l=next.program.layers[i];
        l.enabled=static_cast<int>(i)<settings.complexity;
        l.family=static_cast<NoiseFamily>(i==0?settings.family:static_cast<int>(rng.next()%12));
        l.timing.offset=i==0?0.0f:rng.unit()*.35f;
        l.timing.span=std::min(1.0f-l.timing.offset, .45f+.55f*rng.unit());
        l.gain=std::clamp((.25f+.65f*settings.intensity)*(.7f+.3f*rng.unit()),0.0f,1.0f);
        l.pan=(2.0f*rng.unit()-1.0f)*settings.stereo;
        l.gainCurve.count=3;
        l.gainCurve.nodes[0]={0.0f,.25f+.4f*settings.energy,timeline::Shape::smooth};
        l.gainCurve.nodes[1]={.3f+.4f*rng.unit(),.55f+.45f*rng.unit(),timeline::Shape::smooth};
        l.gainCurve.nodes[2]={1.0f,.3f+.6f*settings.energy,timeline::Shape::linear};
        l.panCurve=timeline::Curve::constant(.5f);
        applyLocks(l,previous.program.layers[i],locks.layers[i]);
    }
    if(!next.program.valid())return false;
    output=next;return true;
}
// Small bounded mutation, deliberately leaves family and timing unchanged.
inline bool mutate(const Result& input,const Locks& locks,std::uint32_t seed,Result& output) noexcept {
    if(!input.program.valid() || !std::isfinite(input.seconds) || input.seconds<.05f || input.seconds>32)return false;
    for(auto mask:locks.layers)if(mask & ~static_cast<std::uint32_t>(all))return false;
    Result next=input;Random rng(seed);
    for(std::size_t i=0;i<next.program.layers.size();++i){auto& l=next.program.layers[i];
        if(!(locks.layers[i]&gain))l.gain=std::clamp(l.gain+(rng.unit()-.5f)*.2f,0.0f,1.0f);
        if(!(locks.layers[i]&pan))l.pan=std::clamp(l.pan+(rng.unit()-.5f)*.25f,-1.0f,1.0f);
        if(!(locks.layers[i]&gainCurve))for(std::size_t n=0;n<l.gainCurve.count;++n)
            l.gainCurve.nodes[n].value=std::clamp(l.gainCurve.nodes[n].value+(rng.unit()-.5f)*.12f,0.0f,1.0f);
    }
    if(!next.program.valid())return false;
    output=next;
    return true;
}
inline bool variation(const Settings& settings,const Result& previous,const Locks& locks,bool alternate,Result& output) noexcept {
    auto copy=settings;copy.seed^=alternate?0xb5297a4du:0x68e31da4u;return generate(copy,previous,locks,output);
}
inline constexpr std::size_t factoryCount=48;
inline bool factory(std::size_t index,Result& output) noexcept {
    if(index>=factoryCount)return false;
    Settings s;s.family=static_cast<int>(index/4);const int variant=static_cast<int>(index%4);
    s.style=variant;s.complexity=1+variant;s.seconds=1.0f+variant;
    s.intensity=.55f+.1f*variant;s.energy=.4f+.15f*variant;s.stereo=.2f+.25f*variant;
    s.seed=0x13579bdu+static_cast<std::uint32_t>(index)*0x9e3779b9u;
    return generate(s,Result{},Locks{},output);
}
} // namespace vox::fx::generator
