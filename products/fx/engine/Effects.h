#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace vox::fx::effects {
// First non-delay effects. This is not yet connected to the product; delay,
// reverb, modulation effects, serialization and host controls remain.
enum class Type : std::uint8_t { bypass, lowpass, drive, wavefold, bitcrush, ring, gate, count };
struct Settings {
    Type type=Type::bypass;
    float wet=1, amount=.5f;
    bool valid() const noexcept {
        return static_cast<unsigned>(type)<static_cast<unsigned>(Type::count) &&
               std::isfinite(wet)&&wet>=0&&wet<=1&&std::isfinite(amount)&&amount>=0&&amount<=1;
    }
};
class Chain {
    struct State {
        Type type=Type::bypass;
        double lowL=0,lowR=0,phase=0,gate=0,filterAlpha=0;
        unsigned coefficientCounter=0;
    };
    struct Slot {
        State current{}, old{};
        Settings desired{};
        float wet=1, amount=.5f;
        unsigned transition=0;
    };
public:
    bool prepare(double rate) noexcept {
        if(!std::isfinite(rate)||rate<8000||rate>384000)return false;
        rate_=rate; fadeSamples_=static_cast<unsigned>(std::max(1.0,std::round(.005*rate)));
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
            for(auto& slot:slots_){
                if(slot.transition==0 && slot.current.type!=slot.desired.type){
                    slot.old=slot.current;slot.current={};slot.current.type=slot.desired.type;slot.transition=fadeSamples_;
                }
                slot.wet+=static_cast<float>((slot.desired.wet-slot.wet)*smoothing_);
                slot.amount+=static_cast<float>((slot.desired.amount-slot.amount)*smoothing_);
                double a=l,b=r;apply(slot.current,slot.amount,a,b);
                if(slot.current.type!=Type::bypass){a=l+(a-l)*slot.wet;b=r+(b-r)*slot.wet;}
                if(slot.transition){double oldL=l,oldR=r;apply(slot.old,slot.amount,oldL,oldR);
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
    void apply(State& state,float amount,double& l,double& r) noexcept {
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
        case Type::count: break;
        }
    }
    double rate_=44100,smoothing_=0;
    unsigned fadeSamples_=1;
    std::array<Slot,4> slots_{};
};
} // namespace vox::fx::effects
