#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace vox::fx::effects {
// Compact, bounded effects; advanced controls and quality modes remain.
inline constexpr double feedbackDelayTailSeconds = 10.0;
// An eighth note on 30..1000 BPM. Slower host tempi clamp at one second.
// No PPQ is inferred (only BPM determines the interval).
inline double tempoDelaySeconds(double tempo) noexcept {
    return 30.0/std::clamp(std::isfinite(tempo)?tempo:120.,30.,1000.);
}
enum class Type : std::uint8_t { bypass, lowpass, drive, wavefold, bitcrush, ring, gate, phaser, flanger, delay, chorus, reverb, tempoDelay, count };
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
// FXSL v1 stored 3-bit type and 6/7-bit values; decode them before host range snapping.
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
inline bool includesTempoDelay(std::uint64_t packed) noexcept {
    for(unsigned i=0;i<4;++i)if(unpack(static_cast<std::uint16_t>(packed>>(16*i))).type==Type::tempoDelay)return true;
    return false;
}
namespace detail {
// Morph only the sub-sample correction. At entry it is anchored to the
// previous read position; at exit both endpoints are fixed. Interpolating
// toward a live rounded position would jump at every half-sample crossing.
class FreeDelayTap {
public:
    double update(double ideal,float target,unsigned fadeSamples) noexcept {
        if(target_<0 || (!moving_&&remaining_==0&&target_==target)) {
            target_=target;
            lastIdeal_=ideal;
            return lastPosition_=std::round(ideal); // legacy fixed/cold path
        }
        if(target_!=target) {
            target_=target;quiet_=0;
            if(!moving_) {
                moving_=true;
                start(lastPosition_-lastIdeal_,0.,fadeSamples);
            }
        } else {
            if(quiet_<fadeSamples)++quiet_;
            // Wait for the float smoother's actual plateau, not distance
            // from the raw target: its last representable step can be farther
            // than .01 sample at high rates, or straddle a rounding boundary.
            if(moving_&&remaining_==0&&quiet_==fadeSamples&&ideal==lastIdeal_) {
                moving_=false;
                start(0.,std::round(ideal)-ideal,fadeSamples);
            }
        }
        lastIdeal_=ideal;
        if(remaining_==0)return lastPosition_=ideal;
        --remaining_;
        const double bias=to_+(from_-to_)*(static_cast<double>(remaining_)/fadeSamples);
        return lastPosition_=ideal+bias;
    }
private:
    void start(double from,double to,unsigned length) noexcept {
        from_=from;to_=to;remaining_=length;
    }
    float target_=-1;
    bool moving_=false;
    unsigned remaining_=0,quiet_=0;
    double lastIdeal_=0,lastPosition_=0,from_=0,to_=0;
};

// The Wavefold static shape is an unmodified triangle fold:
//   u = clamp(gain*x, -8, 8),  y = tri(u),
// where tri is the 4-periodic triangle with tri(u)=u on [-1,1], peaks +-1 at
// odd integers and zeros at even integers. Evaluating it with asin/sin is
// exact but point-sampled, so a folding sinusoid at audio rate generates
// harmonics far above Nyquist that fold back as inharmonic tones.
//
// First- and second-order antiderivatives have exact piecewise-polynomial
// closed forms, which lets the slot use second-order antiderivative
// antialiasing (ADAA) without oversampling, buffers or added integer latency.
// For u in [2m, 2m+2), s = |u| - 2m:
//   tri(u)   = (-1)^m * A'(s),          F(u)   = (-1)^m * A(s) + S(m)
//   F2(u)    = (-1)^m * B(s) + S(m)*s + m
// with A(s)=s^2/2 (s<=1), A(s)=2s-s^2/2-1 (s>1), B(s)=int A, and
// S(m)=1 for odd m, 0 otherwise. F is even, F2 is odd, both are C1 and match
// the clamped zero output beyond |u|=8.
inline double wavefoldA(double s) noexcept {
    return s <= 1.0 ? s*s*.5 : 2.0*s - s*s*.5 - 1.0;
}
inline double wavefoldB(double s) noexcept {
    return s <= 1.0 ? s*s*s/6.0 : s*s - s*s*s/6.0 - s + 1.0/3.0;
}
inline int wavefoldParity(double m) noexcept {
    return static_cast<int>(m) & 1;
}
// Point-sampled transfer function, identical to the previous slot shape.
inline double wavefoldValue(double x,double gain) noexcept {
    const double u=gain*x;
    if(u<=-8.0||u>=8.0)return 0.0; // clamp region ends at tri(+-8)=0
    const double au=std::abs(u),m=std::floor(au*.5),s=au-2.0*m;
    const double tri=s<=1.0?s:2.0-s;
    const double folded=wavefoldParity(m)?-tri:tri;
    return u<0?-folded:folded; // the triangle fold is odd
}
inline double wavefoldFirst(double x,double gain) noexcept {
    const double u=gain*x;
    if(std::abs(u)>=8.0)return 0.0;
    const double au=std::abs(u),m=std::floor(au*.5),s=au-2.0*m;
    const double a=wavefoldA(s);
    const double g=wavefoldParity(m)?-a:a;
    return ((wavefoldParity(m)?1.0:0.0)+g)/gain;
}
inline double wavefoldSecond(double x,double gain) noexcept {
    const double u=gain*x;
    const double au=std::abs(u);
    double h;
    if(au>=8.0)h=4.0;
    else {
        const double m=std::floor(au*.5),s=au-2.0*m;
        const double b=wavefoldB(s);
        h=wavefoldParity(m)?m+s-b:m+b;
    }
    return (u<0?-h:h)/(gain*gain);
}
// Per-channel ADAA history: the previous two inputs, the previous divided
// difference and the cached F2(x[n-1]) for the gain it was generated with.
struct WavefoldMemory {
    double x1=0,x2=0,f2x1=0,lastDifference=0,gain=-1;
};
inline double wavefoldTick(double x,double gain,WavefoldMemory& memory) noexcept {
    // Absolute 1e-9 thresholds allow catastrophic cancellation in the
    // second divided difference (including on bounded, nearly constant input).
    // Below this scale use the continuous mean-value limit; the Wavefold slope
    // is bounded by gain <= 8 in the supported parameter range.
    constexpr double relativeTolerance=1e-5;
    // The floor/parity mapping is only defined for finite input. Clearing the
    // history keeps a non-finite sample from poisoning later output, matching
    // the bounded-output behaviour of the previous expression.
    if(!std::isfinite(x)){memory=WavefoldMemory{};return 0.0;}
    const double f2x0=wavefoldSecond(x,gain);
    const double scale=std::max({1.0,std::abs(x),std::abs(memory.x1),std::abs(memory.x2)});
    const double tolerance=relativeTolerance*scale;
    // Gain follows the smoothed amount. Recompute the cached antiderivative
    // and the previous divided difference only when the shape actually moved;
    // a steady slot pays a single F2 evaluation per sample.
    if(memory.gain!=gain) {
        memory.f2x1=wavefoldSecond(memory.x1,gain);
        const double previous=memory.x1-memory.x2;
        memory.lastDifference=std::abs(previous)<tolerance
            ?wavefoldFirst((memory.x1+memory.x2)*.5,gain)
            :(memory.f2x1-wavefoldSecond(memory.x2,gain))/previous;
        memory.gain=gain;
    }
    const double current=x-memory.x1;
    const double divided=std::abs(current)<tolerance
        ?wavefoldFirst((x+memory.x1)*.5,gain)
        :(f2x0-memory.f2x1)/current;
    const double span=x-memory.x2;
    double y;
    const double low=std::min({x,memory.x1,memory.x2});
    const double high=std::max({x,memory.x1,memory.x2});
    // Within one linear segment the ADAA2 integral is exactly the transfer
    // at the mean. Use that identity for nearby inputs before subtracting
    // nearly equal antiderivatives. Adjacent triangle slope changes are at
    // odd integer values of gain*x; +/-8 are the clamp boundaries.
    const bool nearby=high-low<10.0*tolerance;
    const double lowU=low*gain,highU=high*gain;
    const bool oneSegment=nearby&&((lowU>=8.0||highU<=-8.0)||
        (lowU>-8.0&&highU<8.0&&std::floor((lowU+1.0)*.5)==std::floor((highU+1.0)*.5)));
    if(oneSegment||high-low<tolerance) {
        // Across a corner the mean-value approximation is bounded by
        // gain*(high-low); within one segment the expression is exact.
        y=wavefoldValue((x+memory.x1+memory.x2)/3.0,gain);
    } else if(std::abs(span)<tolerance) {
        // x[n] and x[n-2] coincide: use the mean-value form around x[n-1].
        const double middle=(x+memory.x2)*.5;
        const double delta=middle-memory.x1;
        y=std::abs(delta)<tolerance
            ?wavefoldValue((middle+memory.x1)*.5,gain)
            :(2.0/delta)*(wavefoldFirst(middle,gain)
                +(memory.f2x1-wavefoldSecond(middle,gain))/delta);
    } else {
        y=2.0*(divided-memory.lastDifference)/span;
    }
    memory.x2=memory.x1;memory.x1=x;memory.f2x1=f2x0;
    memory.lastDifference=divided;
    return y;
}
} // namespace detail
class Chain {
    struct State {
        Type type=Type::bypass;
        double lowL=0,lowR=0,phase=0,gate=0,filterAlpha=0;
        unsigned coefficientCounter=0;
        std::array<double,4> allpassL{},allpassR{};
        double phaseCoefficientL=0,phaseCoefficientR=0;
        unsigned bank=0;
        std::size_t write=0,age=0,silentSamples=0;
        double syncTap=0,oldSyncTap=0;
        unsigned syncFade=0;
        detail::FreeDelayTap freeTap;
        double drivePreviousL=0,drivePreviousR=0;
        detail::WavefoldMemory wavefoldL{},wavefoldR{};
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
        const auto size=static_cast<std::size_t>(std::ceil(rate))+4;
        std::array<std::array<Bank,2>,4> next;
        try { for(auto& slot:next)for(auto& bank:slot){bank.left.assign(size,0);bank.right.assign(size,0);} }
        catch(...) { return false; }
        banks_.swap(next); rate_=rate; fadeSamples_=static_cast<unsigned>(std::max(1.0,std::round(.005*rate)));
        smoothing_=1.0-std::exp(-1.0/(.005*rate)); slots_={};
        syncTap_=0;setTempo(120);return true;
    }
    void setTempo(double bpm) noexcept {
        syncTap_=std::round(rate_*tempoDelaySeconds(bpm));
    }
    // Bounded and allocation-free; type changes crossfade for 5ms. A later
    // request during a crossfade is coalesced until that transition completes.
    bool set(std::size_t slot,Settings settings) noexcept {
        if(slot>=slots_.size()||!settings.valid())return false;
        // Preserve the outgoing Wavefold history for its crossfade. The
        // incoming slot is value-initialized when the transition actually
        // starts, so it always receives fresh Wavefold state. Drive's existing
        // type-change behavior remains unchanged.
        if(slots_[slot].desired.type!=settings.type){
            auto clearDrive=[](State& state){state.drivePreviousL=state.drivePreviousR=0;};
            clearDrive(slots_[slot].current);clearDrive(slots_[slot].old);
        }
        slots_[slot].desired=settings;return true;
    }
    void process(float* left,float* right,std::size_t count,const float* modulation=nullptr) noexcept {
        if(left==nullptr||right==nullptr)return;
        for(std::size_t i=0;i<count;++i){double l=left[i],r=right[i];
            for(std::size_t slotIndex=0;slotIndex<slots_.size();++slotIndex){auto& slot=slots_[slotIndex];
                if(slot.transition==0 && slot.current.type!=slot.desired.type){
                    slot.old=slot.current;slot.current={};slot.current.bank=1-slot.old.bank;
                    slot.current.type=slot.desired.type;slot.transition=fadeSamples_;
                }
                const float wet=modulation?std::clamp(slot.desired.wet+modulation[2*slotIndex],0.f,1.f):slot.desired.wet;
                const float amount=modulation?std::clamp(slot.desired.amount+modulation[2*slotIndex+1],0.f,1.f):slot.desired.amount;
                slot.wet+=static_cast<float>((wet-slot.wet)*smoothing_);
                slot.amount+=static_cast<float>((amount-slot.amount)*smoothing_);
                double a=l,b=r;apply(slot.current,banks_[slotIndex][slot.current.bank],slot.amount,amount,a,b);
                if(slot.current.type!=Type::bypass){a=l+(a-l)*slot.wet;b=r+(b-r)*slot.wet;}
                if(slot.transition){double oldL=l,oldR=r;apply(slot.old,banks_[slotIndex][slot.old.bank],slot.amount,amount,oldL,oldR);
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
    void apply(State& state,Bank& bank,float amount,float targetAmount,double& l,double& r) noexcept {
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
        case Type::drive: {
            // Gain-compensated parallel saturation. Unity at zero amount;
            // increasing drive lowers the wet ceiling instead of driving
            // every serial slot towards the same full-scale rail.
            //
            // The linear part stays exact; only the saturating part goes
            // through first-order antiderivative antialiasing. Direct tanh
            // generation folded 4-13% of a 5-8 kHz test tone back below
            // Nyquist; the antiderivative form removes 5-7 dB of that without
            // oversampling, latency or new buffers. At amount 0 the nonlinear
            // term is identically zero, so a bypassed slot stays bit-exact.
            const double gain=1.0+7.0*amount;
            const double level=1.0/std::sqrt(gain);
            const auto nonlinear=[=](double x){return amount*level*std::tanh(x*gain);};
            const auto antiderivative=[=](double x){
                // integral of level*tanh(gain*t) dt = level*log(cosh(gain*t))/gain
                const double scaled=std::abs(x)*gain;
                const double logCosh=scaled+std::log1p(std::exp(-2.0*scaled))-0.6931471805599453;
                return amount*level*logCosh/gain;
            };
            const auto tick=[&](double x,double& previous){
                if(amount==0.0){previous=x;return x;}
                const double delta=x-previous;
                const double y=std::abs(delta)>1e-9
                    ?(antiderivative(x)-antiderivative(previous))/delta
                    :nonlinear(x);
                previous=x;
                // Parallel saturation: the dry term stays untouched, and the
                // wet term is the antialiased saturator rather than its
                // point-sampled version.
                return x-amount*x+y;
            };
            l=tick(l,state.drivePreviousL);r=tick(r,state.drivePreviousR);break;
        }
        case Type::wavefold: {
            // Same triangle fold as before, now through second-order
            // antiderivative antialiasing (ADAA). The static shape is
            // unchanged; the two-stage divided difference removes the
            // fold-generated harmonics that used to fold below Nyquist.
            // No oversampling, buffer or integer latency is added; ADAA2
            // carries the method's inherent ~1-sample (p/2) phase delay, as
            // the existing first-order Drive ADAA carries ~0.5 sample.
            const double gain=1+7*amount;
            l=detail::wavefoldTick(l,gain,state.wavefoldL);
            r=detail::wavefoldTick(r,gain,state.wavefoldR);
            break;
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
        case Type::delay: case Type::tempoDelay: {
            const auto size=bank.left.size();if(size<4)break;
            // Stereo feedback tap: 50–500 ms and 0–75% feedback. A hard
            // ten-second silence cap bounds even the maximum-feedback tail;
            // logical retirement avoids clearing the delay buffer on RT.
            if(std::abs(l)>1.0e-12||std::abs(r)>1.0e-12)state.silentSamples=0;
            else if(state.silentSamples<static_cast<std::size_t>(rate_*feedbackDelayTailSeconds))++state.silentSamples;
            if(state.silentSamples>=static_cast<std::size_t>(rate_*feedbackDelayTailSeconds)){
                state.write=state.age=0;l=r=0;break;
            }
            double length;
            if(state.type==Type::tempoDelay){
                if(state.syncTap==0)state.syncTap=syncTap_;
                if(state.syncFade==0&&state.syncTap!=syncTap_){
                    state.oldSyncTap=state.syncTap;state.syncTap=syncTap_;state.syncFade=fadeSamples_;
                }
                length=state.syncTap;
            }else length=state.freeTap.update(rate_*(.05+.45*amount),targetAmount,fadeSamples_);
            auto delayedL=readDelay(state,bank.left,length),delayedR=readDelay(state,bank.right,length);
            if(state.syncFade){
                const double blend=1.-static_cast<double>(state.syncFade)/fadeSamples_;
                const auto oldL=readDelay(state,bank.left,state.oldSyncTap),oldR=readDelay(state,bank.right,state.oldSyncTap);
                delayedL=oldL+(delayedL-oldL)*blend;delayedR=oldR+(delayedR-oldR)*blend;
                --state.syncFade;
            }
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
    double rate_=44100,smoothing_=0,syncTap_=11025;
    unsigned fadeSamples_=1;
    std::array<Slot,4> slots_{};
    std::array<std::array<Bank,2>,4> banks_{};
};
} // namespace vox::fx::effects
