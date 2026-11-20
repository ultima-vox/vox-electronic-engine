#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace vox::fx::source {
enum class Scale : std::uint8_t { chromatic, major, minor, dorian, phrygian };
struct Tonality {
    Scale scale = Scale::chromatic;
    std::uint8_t root = 0; // pitch class, 0 = C
    bool valid() const noexcept { return root < 12 && static_cast<unsigned>(scale) <= static_cast<unsigned>(Scale::phrygian); }
    int map(int midi) const noexcept {
        if(!valid()||scale==Scale::chromatic||midi<0||midi>127)return midi;
        constexpr unsigned masks[]{0xfff, 0xab5, 0x5ad, 0x6ad, 0x5ab};
        const auto mask=masks[static_cast<unsigned>(scale)];
        const auto allowed=[&](int note){return note>=0 && note<=127 && (mask & (1u<<((note+12-root)%12)));};
        for (int d=0; d<=12; ++d) {
            if (allowed(midi-d)) return midi-d; // equal-distance ties go down
            if (allowed(midi+d)) return midi+d;
        }
        return midi;
    }
};
struct GrainShape {
    float milliseconds=80;
    float position=-1; // -1 = legacy seeded scatter, [0, 1] = fixed start
    bool reverse=false;
    float travel=0; // signed fraction of source traversed over one layer timeline
    double startAt(double phase) const noexcept {
        return travel==0?static_cast<double>(position):
            std::clamp(static_cast<double>(position)+travel*phase,0.,1.);
    }
    bool valid() const noexcept {
        return std::isfinite(milliseconds)&&milliseconds>=10&&milliseconds<=180&&
            (position==-1||(std::isfinite(position)&&position>=0&&position<=1))&&
            std::isfinite(travel)&&travel>=-1&&travel<=1;
    }
    bool operator==(const GrainShape&) const = default;
};
// A four-step, event-local gate over a fixed procedural 256-sample capture.
// Values describe source playback, not host Pattern, PPQ or pitch automation.
enum class SliceGrid : std::uint8_t { eighth, sixteenth, thirtySecond };
enum class SliceRead : std::uint8_t { alternatingSpeed, forward, reverse };
struct GlitchShape {
    SliceGrid grid=SliceGrid::sixteenth;
    SliceRead read=SliceRead::alternatingSpeed;
    std::uint8_t gates=0b1011;
    constexpr bool valid() const noexcept {
        return grid<=SliceGrid::thirtySecond&&read<=SliceRead::reverse&&gates<=15;
    }
    constexpr double gridScale() const noexcept {
        return grid==SliceGrid::eighth?.5:grid==SliceGrid::thirtySecond?2.:1.;
    }
    constexpr bool gate(std::uint64_t step) const noexcept {return (gates&(1u<<(step&3u)))!=0;}
    bool operator==(const GlitchShape&) const = default;
};
enum class FilterType : std::uint8_t { off, lowpass, highpass, bandpass, notch };
struct Filter {
    FilterType type = FilterType::off;
    float hz = 1000, q = .707107f;
    std::uint8_t poles = 2; // 2 or 4; 12/24 dB/oct for low/high pass
    bool valid() const noexcept {
        return static_cast<unsigned>(type)<=static_cast<unsigned>(FilterType::notch) &&
            std::isfinite(hz)&&hz>=20&&hz<=20000&&std::isfinite(q)&&q>=.5f&&q<=4 && (poles==2||poles==4);
    }
};
// TPT state-variable filter. A second identical stage is optional. The
// voice owns memory/state: authoring a new filter never mutates active notes.
class FilterState {
public:
    void prepare(Filter f,double rate) noexcept {
        type_=f.type;count_=f.poles==4?2:1;
        if(type_==FilterType::off)return;
        const double g=std::tan(3.14159265358979323846*std::min<double>(f.hz,rate*.45)/rate);
        k_=1.0/f.q;a1_=1.0/(1+g*(g+k_));a2_=g*a1_;a3_=g*a2_;
    }
    double tick(double input) noexcept {
        if(type_==FilterType::off)return input;
        for(int i=0;i<count_;++i) {
            const double delta=input-i2_[i],band=a1_*i1_[i]+a2_*delta,low=i2_[i]+a2_*i1_[i]+a3_*delta;
            const double next1=2*band-i1_[i],next2=2*low-i2_[i];
            i1_[i]=std::abs(next1)<1e-20?0:next1;i2_[i]=std::abs(next2)<1e-20?0:next2;
            const double high=input-k_*band-low;
            switch(type_){
                case FilterType::lowpass: input=low; break;
                case FilterType::highpass: input=high; break;
                case FilterType::bandpass: input=k_*band; break;
                case FilterType::notch: input=high+low; break;
                default: break;
            }
        }
        return input;
    }
private:
    FilterType type_=FilterType::off;
    int count_=1;
    double k_=0,a1_=0,a2_=0,a3_=0,i1_[2]{},i2_[2]{};
};
} // namespace vox::fx::source
