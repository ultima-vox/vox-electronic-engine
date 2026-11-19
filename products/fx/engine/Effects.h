#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace vox::fx::effects {
// Compact, bounded effects; advanced controls and quality modes remain.
enum class Type : std::uint8_t { bypass, lowpass, drive, wavefold, bitcrush, ring, gate, phaser, flanger, delay, chorus, reverb, count };
struct Settings {
    Type type=Type::bypass;
    float wet=1, amount=.5f;
    bool valid() const noexcept {
        return static_cast<unsigned>(type)<static_cast<unsigned>(Type::count) &&
               std::isfinite(wet)&&wet>=0&&wet<=1&&std::isfinite(amount)&&amount>=0&&amount<=1;
    }
};
// Compact, bounded structural state; non-automated snapshot resolution is 6/6 bits. One atomic
// 64-bit word can publish all four slots as a coherent snapshot.
inline std::uint16_t pack(Settings s) noexcept {
    return static_cast<std::uint16_t>(static_cast<unsigned>(s.type) |
        (static_cast<unsigned>(std::lround(s.wet*63))<<4) |
        (static_cast<unsigned>(std::lround(s.amount*63))<<10));
}
inline Settings unpack(std::uint16_t word) noexcept {
    return {static_cast<Type>(word&15u), static_cast<float>((word>>4)&63u)/63.0f,
            static_cast<float>((word>>10)&63u)/63.0f};
}
// FXSL v1 stored 3-bit type and 6/7-bit values; keep its exact host values.
inline Settings unpackLegacy(std::uint16_t word) noexcept {
    return {static_cast<Type>(word&7u),static_cast<float>((word>>3)&63u)/63.0f,
            static_cast<float>((word>>9)&127u)/127.0f};
}
inline std::uint64_t defaults() noexcept {
    std::uint64_t result=0;for(unsigned i=0;i<4;++i)result|=static_cast<std::uint64_t>(pack({}))<<(16*i);
    return result;
}
inline bool validPacked(std::uint64_t value) noexcept {
    for(unsigned i=0;i<4;++i)if(!unpack(static_cast<std::uint16_t>(value>>(16*i))).valid())return false;
    return true;
}
class Chain {
    struct State {
        Type type=Type::bypass;
        double lowL=0,lowR=0,phase=0,gate=0,filterAlpha=0;
        unsigned coefficientCounter=0;
        std::array<double,4> allpassL{},allpassR{};
        double phaseCoefficientL=0,phaseCoefficientR=0;
        unsigned bank=0;
        std::size_t write=0,age=0,silentSamples=0;
        std::array<std::size_t,4> reverbL{},reverbR{};
    };
    struct Bank { std::vector<float> left,right; };
    struct Slot {
        State current{}, old{};
        Settings desired{};
        float wet=1, amount=.5f;
        unsigned transition=0;
    };
public:
    bool prepare(double rate) noexcept {
        if(!std::isfinite(rate)||rate<8000||rate>384000)return false;
        // Two banks per slot permit bounded crossfades without copying or
        // clearing delay memory on the callback. Allocation is prepare-only.
        const auto size=static_cast<std::size_t>(std::ceil(rate*.5))+4;
        std::array<std::array<Bank,2>,4> next;
        try { for(auto& slot:next)for(auto& bank:slot){bank.left.assign(size,0);bank.right.assign(size,0);} }
        catch(...) { return false; }
        banks_.swap(next); rate_=rate; fadeSamples_=static_cast<unsigned>(std::max(1.0,std::round(.005*rate)));
        smoothing_=1.0-std::exp(-1.0/(.005*rate)); slots_={}; return true;
    }
    // Bounded and allocation-free; type changes crossfade for 5ms. A later
    // request during a crossfade is coalesced until that transition completes.
    bool set(std::size_t slot,Settings settings) noexcept {
        if(slot>=slots_.size()||!settings.valid())return false;
        slots_[slot].desired=settings;return true;
    }
    void process(float* left,float* right,std::size_t count) noexcept {
        if(left==nullptr||right==nullptr)return;
        for(std::size_t i=0;i<count;++i){double l=left[i],r=right[i];
            for(std::size_t slotIndex=0;slotIndex<slots_.size();++slotIndex){auto& slot=slots_[slotIndex];
                if(slot.transition==0 && slot.current.type!=slot.desired.type){
                    slot.old=slot.current;slot.current={};slot.current.bank=1-slot.old.bank;
                    slot.current.type=slot.desired.type;slot.transition=fadeSamples_;
                }
                slot.wet+=static_cast<float>((slot.desired.wet-slot.wet)*smoothing_);
                slot.amount+=static_cast<float>((slot.desired.amount-slot.amount)*smoothing_);
                double a=l,b=r;apply(slot.current,banks_[slotIndex][slot.current.bank],slot.amount,a,b);
                if(slot.current.type!=Type::bypass){a=l+(a-l)*slot.wet;b=r+(b-r)*slot.wet;}
                if(slot.transition){double oldL=l,oldR=r;apply(slot.old,banks_[slotIndex][slot.old.bank],slot.amount,oldL,oldR);
                    if(slot.old.type!=Type::bypass){oldL=l+(oldL-l)*slot.wet;oldR=r+(oldR-r)*slot.wet;}
                    const double mix=1.0-static_cast<double>(slot.transition)/fadeSamples_;
                    a=oldL+(a-oldL)*mix;b=oldR+(b-oldR)*mix;--slot.transition;
                }
                l=a;r=b;
            }
            left[i]=static_cast<float>(l);right[i]=static_cast<float>(r);
        }
    }
private:
    static double readDelay(const State& state,const std::vector<float>& buffer,double delay) noexcept {
        const auto size=buffer.size();const auto whole=static_cast<std::size_t>(delay);
        if(size<4||whole>=size||state.age<whole)return 0;
        const double fraction=delay-whole;
        const auto index=state.write>=whole?state.write-whole:state.write+size-whole;
        const auto previous=index==0?size-1:index-1;
        const double older=state.age>whole?buffer[previous]:0.0;
        return buffer[index]*(1-fraction)+older*fraction;
    }
    void apply(State& state,Bank& bank,float amount,double& l,double& r) noexcept {
        constexpr double tau=6.2831853071795864769;
        switch(state.type){
        case Type::bypass: break;
        case Type::lowpass: {
            if(state.coefficientCounter==0) {
                const double hz=60.0*std::pow(300.0,amount);
                state.filterAlpha=1.0-std::exp(-tau*std::min(hz,rate_*.45)/rate_);
            }
            state.coefficientCounter=(state.coefficientCounter+1)&31u;
            state.lowL+=state.filterAlpha*(l-state.lowL);state.lowR+=state.filterAlpha*(r-state.lowR);
            if(std::abs(state.lowL)<1.0e-15)state.lowL=0;
            if(std::abs(state.lowR)<1.0e-15)state.lowR=0;
            l=state.lowL;r=state.lowR;break;
        }
        case Type::drive: {const double gain=1+15*amount,normalizer=1.0/std::tanh(gain);
            l=std::tanh(l*gain)*normalizer;r=std::tanh(r*gain)*normalizer;break;}
        case Type::wavefold: {
            const double gain=1+7*amount;
            auto fold=[gain](double x){x=std::clamp(x*gain,-8.0,8.0);return 2.0/std::acos(-1.0)*std::asin(std::sin(std::acos(-1.0)*x*.5));};
            l=fold(l);r=fold(r);break;
        }
        case Type::bitcrush: {const double levels=std::round(32768.0*std::pow(1.0/8192.0,amount));
            l=std::round(std::clamp(l,-1.0,1.0)*levels)/levels;
            r=std::round(std::clamp(r,-1.0,1.0)*levels)/levels;break;}
        case Type::ring: {const double mod=std::sin(tau*state.phase);l*=mod;r*=mod;
            state.phase+=(20.0+1980.0*amount)/rate_;if(state.phase>=1)state.phase-=1;break;}
        case Type::gate: {const double target=state.phase<.5?1.0:0.0;
            state.gate+=smoothing_*(target-state.gate);l*=state.gate;r*=state.gate;
            state.phase+=(1.0+15.0*amount)/rate_;if(state.phase>=1)state.phase-=1;break;}
        case Type::phaser: {
            // Four first-order allpasses per channel; parallel dry creates
            // moving notches. Fixed-rate stereo quadrature LFO, bounded sweep.
            if(state.coefficientCounter==0){
                const auto coefficient=[&](double oscillator){
                    const double frequency=std::min(1000.0*std::exp2(2.0*amount*oscillator),rate_*.4);
                    const double g=std::tan(3.14159265358979323846*frequency/rate_);
                    return (g-1.0)/(g+1.0);
                };
                state.phaseCoefficientL=coefficient(std::sin(tau*state.phase));
                state.phaseCoefficientR=coefficient(std::cos(tau*state.phase));
            }
            state.coefficientCounter=(state.coefficientCounter+1)&15u;
            const auto process=[](double input,std::array<double,4>& memory,double coefficient){
                double signal=input;
                for(auto& z:memory){const double output=coefficient*signal+z;
                    z=signal-coefficient*output;if(std::abs(z)<1.0e-15)z=0;signal=output;}
                return .5*(input+signal);
            };
            l=process(l,state.allpassL,state.phaseCoefficientL);
            r=process(r,state.allpassR,state.phaseCoefficientR);
            state.phase+=.23/rate_;if(state.phase>=1)state.phase-=1;break;
        }
        case Type::flanger: {
            const auto size=bank.left.size();if(size<4)break; // failed/unprepared allocation: safe bypass
            const auto delay=[&](double phase){return rate_*(.0005+.002*(1.0+amount*std::sin(tau*phase)));};
            const auto delayedL=readDelay(state,bank.left,delay(state.phase));
            const auto delayedR=readDelay(state,bank.right,delay(state.phase+.25));
            const auto write=[](double input,double delayed){
                const auto value=std::clamp(input+.35*delayed,-8.0,8.0);
                return static_cast<float>(std::abs(value)<1e-15?0:value);
            };
            bank.left[state.write]=write(l,delayedL);bank.right[state.write]=write(r,delayedR);
            if(++state.write==size)state.write=0;
            if(state.age<size)++state.age;
            l=.5*(l+delayedL);r=.5*(r+delayedR);
            state.phase+=.25/rate_;if(state.phase>=1)state.phase-=1;break;
        }
        case Type::delay: {
            const auto size=bank.left.size();if(size<4)break;
            // Stereo feedback tap: 50–500 ms and 0–75% feedback. A hard
            // ten-second silence cap bounds even the maximum-feedback tail;
            // logical retirement avoids clearing the delay buffer on RT.
            if(std::abs(l)>1.0e-12||std::abs(r)>1.0e-12)state.silentSamples=0;
            else if(state.silentSamples<static_cast<std::size_t>(rate_*10))++state.silentSamples;
            if(state.silentSamples>=static_cast<std::size_t>(rate_*10)){
                state.write=state.age=0;l=r=0;break;
            }
            const double length=std::round(rate_*(.05+.45*amount));
            const auto delayedL=readDelay(state,bank.left,length),delayedR=readDelay(state,bank.right,length);
            const double feedback=.75*amount;
            bank.left[state.write]=static_cast<float>(std::clamp(l+feedback*delayedL,-8.0,8.0));
            bank.right[state.write]=static_cast<float>(std::clamp(r+feedback*delayedR,-8.0,8.0));
            if(++state.write==size)state.write=0;
            if(state.age<size)++state.age;
            l=delayedL;r=delayedR;break;
        }
        case Type::chorus: {
            const auto size=bank.left.size();if(size<4)break;
            // Quadrature modulated 9–23 ms taps, no feedback.
            const auto delay=[&](double phase){return rate_*(.016+.007*amount*std::sin(tau*phase));};
            const auto delayedL=readDelay(state,bank.left,delay(state.phase));
            const auto delayedR=readDelay(state,bank.right,delay(state.phase+.25));
            bank.left[state.write]=static_cast<float>(l);bank.right[state.write]=static_cast<float>(r);
            if(++state.write==size)state.write=0;
            if(state.age<size)++state.age;
            l=.5*(l+delayedL);r=.5*(r+delayedR);
            state.phase+=.22/rate_;if(state.phase>=1)state.phase-=1;break;
        }
        case Type::reverb: {
            const auto size=bank.left.size();if(size<4)break;
            if(state.coefficientCounter==0){
                constexpr double leftTimes[]{.0297,.0371,.0411,.0437};
                constexpr double rightTimes[]{.0311,.0389,.0443,.0479};
                for(std::size_t i=0;i<4;++i){state.reverbL[i]=static_cast<std::size_t>(std::round(rate_*leftTimes[i]));
                    state.reverbR[i]=static_cast<std::size_t>(std::round(rate_*rightTimes[i]));}
                state.coefficientCounter=1;
            }
            if(std::abs(l)>1e-15||std::abs(r)>1e-15)state.silentSamples=0;
            else if(state.silentSamples<static_cast<std::size_t>(rate_*2))++state.silentSamples;
            // Hard tail bound: after two seconds without input, retire this
            // bank logically (no buffer clear on the audio thread).
            if(state.silentSamples>=static_cast<std::size_t>(rate_*2)){
                state.write=state.age=0;l=r=0;break;
            }
            double sumL=0,sumR=0;
            for(std::size_t i=0;i<4;++i){sumL+=readDelay(state,bank.left,static_cast<double>(state.reverbL[i]));
                sumR+=readDelay(state,bank.right,static_cast<double>(state.reverbR[i]));}
            sumL*=.25;sumR*=.25;
            const double feedback=.1+.3*amount;
            const auto safe=[](double value){value=std::clamp(value,-8.0,8.0);return static_cast<float>(std::abs(value)<1e-15?0:value);};
            bank.left[state.write]=safe(l+feedback*(.8*sumL+.2*sumR));
            bank.right[state.write]=safe(r+feedback*(.8*sumR+.2*sumL));
            if(++state.write==size)state.write=0;
            if(state.age<size)++state.age;
            l=sumL;r=sumR;break;
        }
        case Type::count: break;
        }
    }
    double rate_=44100,smoothing_=0;
    unsigned fadeSamples_=1;
    std::array<Slot,4> slots_{};
    std::array<std::array<Bank,2>,4> banks_{};
};
} // namespace vox::fx::effects
