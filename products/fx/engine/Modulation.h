#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace vox::fx::mod {
// These destinations are normalized offsets into the corresponding base
// values. Matrix results never mutate the host automation base.
enum class Source : std::uint8_t { lfo1, lfo2, step, random, adsr, phase, velocity, follower,
    macro1, macro2, macro3, macro4, macro5, macro6, macro7, macro8, count };
enum class Destination : std::uint8_t { gain, pan, pitch, cutoff, count };
struct Route {
    Source source = Source::lfo1;
    Destination destination = Destination::gain;
    float depth = 0.0f; // bipolar signed offset
    float smoothingSeconds = 0.0f;
    bool valid() const noexcept {
        return static_cast<unsigned>(source) < static_cast<unsigned>(Source::count) &&
               static_cast<unsigned>(destination) < static_cast<unsigned>(Destination::count) &&
               std::isfinite(depth) && depth >= -1 && depth <= 1 &&
               std::isfinite(smoothingSeconds) && smoothingSeconds >= 0 && smoothingSeconds <= 1;
    }
};
using Sources = std::array<float, static_cast<std::size_t>(Source::count)>;
using Destinations = std::array<float, static_cast<std::size_t>(Destination::count)>;

// Prepared outside callback, bounded per-sample route evaluation. Sources are
// supplied explicitly, so there are no internal feedback/cycle edges.
class Matrix {
public:
    bool prepare(double rate, const Route* routes, std::size_t count) noexcept {
        if (!std::isfinite(rate) || rate < 8000 || rate > 384000 || count > 64 || (count && !routes)) return false;
        for(std::size_t i=0;i<count;++i) if(!routes[i].valid()) return false;
        count_=count;
        for(std::size_t i=0;i<count;++i){
            routes_[i]=routes[i]; smoothed_[i]=0;
            const double time=routes[i].smoothingSeconds;
            alpha_[i]=time <= 0 ? 1.0 : 1.0-std::exp(-1.0/(time*rate));
        }
        return true;
    }
    Destinations step(const Destinations& base,const Sources& sources) noexcept {
        Destinations result{};
        for(std::size_t i=0;i<result.size();++i)
            result[i]=std::isfinite(base[i]) ? std::clamp(base[i],0.0f,1.0f) : 0.0f;
        for(std::size_t i=0;i<count_;++i){
            const auto& route=routes_[i];const auto source=sources[static_cast<std::size_t>(route.source)];
            const double target=(std::isfinite(source) ? std::clamp(source,-1.0f,1.0f) : 0.0f)*route.depth;
            smoothed_[i]+=alpha_[i]*(target-smoothed_[i]);
            result[static_cast<std::size_t>(route.destination)]+=static_cast<float>(smoothed_[i]);
        }
        for(auto& value:result)value=std::clamp(value,0.0f,1.0f);
        return result;
    }
private:
    std::size_t count_=0;
    std::array<Route,64> routes_{};
    std::array<double,64> alpha_{},smoothed_{};
};

// Deterministic, bounded oscillator/random helpers; phase/reset ownership is
// with the caller/event. These are not wired to the plugin Program yet.
class Lfo {
public:
    void reset() noexcept { phase_=0; }
    float next(double hz,double rate) noexcept {
        if(!std::isfinite(hz)||!std::isfinite(rate)||hz<0||rate<8000||rate>384000)return 0;
        hz=std::min(hz,rate*.45);
        const float value=static_cast<float>(std::sin(6.283185307179586*phase_));
        phase_+=hz/rate;if(phase_>=1)phase_-=1;
        return value;
    }
private: double phase_=0;
};
class SampleHold {
public:
    void reset(std::uint32_t seed) noexcept { rng_=seed ? seed : 1; phase_=0; value_=0; }
    float next(double hz,double rate) noexcept {
        if(!std::isfinite(hz)||!std::isfinite(rate)||hz<0||rate<8000||rate>384000)return value_;
        phase_+=std::min(hz,rate*.45)/rate;
        if(phase_>=1){phase_-=1;rng_^=rng_<<13;rng_^=rng_>>17;rng_^=rng_<<5;
            value_=static_cast<float>(static_cast<double>(rng_)/2147483648.0-1.0);}
        return value_;
    }
private: std::uint32_t rng_=1;double phase_=0;float value_=0;
};
} // namespace vox::fx::mod
