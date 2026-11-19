#pragma once
#include "Modulation.h"
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace vox::fx::macro {
// Scope is implicit in a typed destination. Voice destinations use the note's
// immutable route snapshot; mix/FX routes use the current controller graph.
constexpr bool voiceDestination(mod::Destination dest) noexcept {
    return dest>=mod::Destination::pitch&&dest<=mod::Destination::layerPan;
}
constexpr bool eventSource(mod::Source src) noexcept {
    return src==mod::Source::step||src==mod::Source::adsr||src==mod::Source::phase||src==mod::Source::velocity;
}
struct LfoSettings {
    float rate=.5f; // Hz or cycles/quarter note; tempo sync never fabricates PPQ
    bool sync=false;
    bool valid() const noexcept {return std::isfinite(rate)&&rate>=.03125f&&rate<=16;}
    bool operator==(const LfoSettings&) const = default;
};
struct Config {
    std::array<mod::Route,64> routes{};
    std::uint32_t count=0;
    std::array<LfoSettings,2> lfos{{{.5f,false},{1,false}}};
    float randomRate=4;
    std::uint32_t randomSeed=0x57a3u;
    static bool supports(mod::Source src,mod::Destination dest) noexcept {
        return src<mod::Source::count&&dest<mod::Destination::count&&
            (!eventSource(src)||voiceDestination(dest))&&(src!=mod::Source::follower||!voiceDestination(dest));
    }
    bool valid() const noexcept {
        if(count>routes.size()||!lfos[0].valid()||!lfos[1].valid()||!std::isfinite(randomRate)||randomRate<.03125f||randomRate>16)return false;
        for(std::size_t i=0;i<count;++i){const auto& r=routes[i];if(!r.valid()||!supports(r.source,r.destination))return false;}
        return true;
    }
    bool operator==(const Config& other) const noexcept {
        if(count!=other.count||lfos!=other.lfos||randomRate!=other.randomRate||randomSeed!=other.randomSeed)return false;
        for(std::size_t i=0;i<count;++i){const auto& a=routes[i];const auto& b=other.routes[i];
            if(a.source!=b.source||a.destination!=b.destination||a.depth!=b.depth||a.smoothingSeconds!=b.smoothingSeconds)return false;}
        return true;
    }
    bool put(std::size_t index,mod::Route route) noexcept {
        if(index>count||index>=routes.size()||!route.valid()||!supports(route.source,route.destination))return false;
        routes[index]=route;if(index==count)++count;return true;
    }
    bool erase(std::size_t index) noexcept {
        if(index>=count)return false;
        for(auto i=index+1;i<count;++i)routes[i-1]=routes[i];
        routes[--count]={};return true;
    }
    bool setGainRoute(std::size_t index,float depth,float smoothing) noexcept {
        if(index>=8)return false;
        return setSourceGainRoute(static_cast<mod::Source>(static_cast<unsigned>(mod::Source::macro1)+index),depth,smoothing);
    }
    bool setSourceGainRoute(mod::Source source,float depth,float smoothing) noexcept {
        return setRoute(source,mod::Destination::gain,depth,smoothing);
    }
    bool setRoute(mod::Source source,mod::Destination destination,float depth,float smoothing) noexcept {
        if(!supports(source,destination)||
           !std::isfinite(depth)||depth < -1||depth>1||!std::isfinite(smoothing)||smoothing<0||smoothing>1||!valid())return false;
        std::size_t slot=0;while(slot<count && (routes[slot].source!=source||routes[slot].destination!=destination))++slot;
        if(depth==0){if(slot<count){for(std::size_t i=slot+1;i<count;++i)routes[i-1]=routes[i];--count;}return true;}
        if(slot==count){if(count==routes.size())return false;++count;}
        routes[slot]={source,destination,depth,smoothing};return true;
    }
};
namespace codec {
inline constexpr std::size_t legacyWords=1+64*4;
inline constexpr std::size_t wordCount=legacyWords+6;
using Words=std::array<std::uint32_t,wordCount>;
inline bool toWords(const Config& config,Words& output) noexcept {
    if(!config.valid())return false;
    Words next{};next[0]=config.count;
    for(std::size_t i=0;i<config.count;++i){const auto& route=config.routes[i];const auto at=1+i*4;
        next[at]=static_cast<std::uint32_t>(route.source);next[at+1]=static_cast<std::uint32_t>(route.destination);
        next[at+2]=std::bit_cast<std::uint32_t>(route.depth);next[at+3]=std::bit_cast<std::uint32_t>(route.smoothingSeconds);}
    for(unsigned i=0;i<2;++i){next[legacyWords+2*i]=std::bit_cast<std::uint32_t>(config.lfos[i].rate);next[legacyWords+2*i+1]=config.lfos[i].sync?1:0;}
    next[legacyWords+4]=std::bit_cast<std::uint32_t>(config.randomRate);next[legacyWords+5]=config.randomSeed;
    output=next;return true;
}
inline bool fromWords(const Words& words,Config& output) noexcept {
    if(words[0]>64)return false;
    Config next;next.count=words[0];
    for(std::size_t i=0;i<next.count;++i){const auto at=1+i*4;
        if(words[at]>255||words[at+1]>255)return false;
        next.routes[i]={static_cast<mod::Source>(words[at]),static_cast<mod::Destination>(words[at+1]),
            std::bit_cast<float>(words[at+2]),std::bit_cast<float>(words[at+3])};}
    for(std::size_t i=1+next.count*4;i<legacyWords;++i)if(words[i]!=0)return false;
    for(unsigned i=0;i<2;++i){const auto mode=words[legacyWords+2*i+1];if(mode>1)return false;
        next.lfos[i]={std::bit_cast<float>(words[legacyWords+2*i]),mode==1};}
    next.randomRate=std::bit_cast<float>(words[legacyWords+4]);next.randomSeed=words[legacyWords+5];
    if(!next.valid())return false;
    output=next;return true;
}
inline bool encode(const Config& config,std::vector<std::byte>& output) {
    Words words{};if(!toWords(config,words))return false;std::vector<std::byte> next;next.reserve((wordCount+1)*4);
    const auto append=[&](std::uint32_t v){for(int shift=0;shift<32;shift+=8)next.push_back(static_cast<std::byte>((v>>shift)&255));};
    append(2);for(auto v:words)append(v);output=std::move(next);return true;
}
inline bool decode(std::span<const std::byte> bytes,Config& output) noexcept {
    if(bytes.size()<4)return false;
    auto read=[&](std::size_t i){std::uint32_t v=0;for(unsigned b=0;b<4;++b)v|=std::to_integer<std::uint32_t>(bytes[i*4+b])<<(8*b);return v;};
    const auto version=read(0);
    if((version!=1&&version!=2)||bytes.size()!=4*(1+(version==1?legacyWords:wordCount)))return false;
    Words words{};if(!toWords({},words))return false;
    for(std::size_t i=0;i<(version==1?legacyWords:wordCount);++i)words[i]=read(i+1);
    Config next;
    if(!fromWords(words,next))return false;
    if(version==1)for(std::size_t i=0;i<next.count;++i){const auto r=next.routes[i];
        if(r.destination>mod::Destination::pan||eventSource(r.source)||r.source==mod::Source::follower)return false;}
    output=next;return true;
}
}
struct StereoGain { float left=0,right=0,gain=0; };
struct EventInput { float phase=0,velocity=0,ageSeconds=0,release=1; };
class Runtime {
public:
    bool prepare(double rate) noexcept {
        if(!std::isfinite(rate)||rate<8000||rate>384000)return false;
        rate_=rate;restart();return compile();
    }
    bool setConfig(const Config& config,bool voice=false) noexcept {
        if(!config.valid())return false;
        if(config==config_&&voice==voice_)return true;
        const bool seedChanged=config.randomSeed!=config_.randomSeed;
        config_=config;voice_=voice;if(seedChanged)random_.reset(config_.randomSeed);
        return compile();
    }
    void restart(std::uint32_t salt=0) noexcept {
        for(auto& lfo:lfos_)lfo.reset();random_.reset(config_.randomSeed^salt);follower_=0;
    }
    bool active() const noexcept { return used_!=0; }
    bool routesFromFollower() const noexcept { return has(mod::Source::follower); }
    bool routesToEffects() const noexcept { return effects_; }
    bool routesTo(mod::Destination dest) const noexcept { return dest<mod::Destination::count&&(targets_ & (1u<<at(dest)))!=0; }
    void setTempo(double bpm) noexcept {if(std::isfinite(bpm)&&bpm>=1&&bpm<=1000)tempo_=bpm;}
    mod::Destinations evaluate(const std::array<float,8>& macros,float baseGain=0,EventInput event={},float dry=0) noexcept {
        mod::Destinations result{};
        if(!voice_){result[at(mod::Destination::gain)]=std::isfinite(baseGain)?std::clamp(baseGain,0.f,1.f):0.f;result[at(mod::Destination::pan)]=.5f;}
        if(!active())return result;
        mod::Sources sources{};
        for(std::size_t i=0;i<macros.size();++i)if(has(static_cast<mod::Source>(at(mod::Source::macro1)+i))){const auto v=macros[i];
            sources[at(mod::Source::macro1)+i]=std::isfinite(v)?2*std::clamp(v,0.f,1.f)-1:0;}
        for(unsigned i=0;i<2;++i)if(has(static_cast<mod::Source>(i))){const auto lfo=config_.lfos[i];
            sources[i]=lfos_[i].next(lfo.rate*(lfo.sync?tempo_/60.0:1),rate_);}
        if(has(mod::Source::random))sources[at(mod::Source::random)]=random_.next(config_.randomRate,rate_);
        if(has(mod::Source::follower)){
            // Only the pre-FX dry mix drives this detector. Never read a
            // previous effective FX parameter or the post-FX output here.
            const float x=std::isfinite(dry)?std::clamp(std::abs(dry),0.f,1.f):0.f;
            follower_+=(x-follower_)*(x>follower_?followerAttack_:followerRelease_);
            sources[at(mod::Source::follower)]=follower_;
        }
        if(voice_){
            event.phase=std::isfinite(event.phase)?std::clamp(event.phase,0.f,1.f):0.f;
            sources[at(mod::Source::phase)]=event.phase;sources[at(mod::Source::velocity)]=event.velocity;
            constexpr float step[]{0,.8f,.35f,1,.22f,.72f,.41f,0};
            sources[at(mod::Source::step)]=step[std::clamp(static_cast<int>(event.phase*8),0,7)];
            const float envelope=event.ageSeconds<.01f?event.ageSeconds*100:std::max(.65f,1-(event.ageSeconds-.01f)*3.5f);
            sources[at(mod::Source::adsr)]=envelope*event.release;
        }
        matrix_.sum(result,sources);return result;
    }
    static StereoGain stereo(const mod::Destinations& values) noexcept {
        const auto gain=std::clamp(values[at(mod::Destination::gain)],0.f,1.f);
        const auto pan=std::clamp(values[at(mod::Destination::pan)],0.f,1.f);
        return {gain*std::min(1.0f,2.0f*(1.0f-pan)),gain*std::min(1.0f,2.0f*pan),gain};
    }
    float process(float base,const std::array<float,8>& macros) noexcept { return processStereo(base,macros).gain; }
    StereoGain processStereo(float base,const std::array<float,8>& macros) noexcept {return stereo(evaluate(macros,base));}
    template<class E> static constexpr unsigned at(E x) noexcept {return static_cast<unsigned>(x);}
private:
    bool has(mod::Source src) const noexcept {return (used_&(1u<<at(src)))!=0;}
    bool compile() noexcept {
        used_=targets_=0;effects_=false;std::array<mod::Route,64> selected{};unsigned n=0;
        for(unsigned i=0;i<config_.count;++i){const auto r=config_.routes[i];if(voice_!=voiceDestination(r.destination))continue;
            selected[n++]=r;used_|=1u<<at(r.source);targets_|=1u<<at(r.destination);
            effects_|=r.destination>=mod::Destination::wet1;
        }
        followerAttack_=static_cast<float>(1-std::exp(-1/(.005*rate_)));
        followerRelease_=static_cast<float>(1-std::exp(-1/(.09*rate_)));
        return matrix_.prepare(rate_,selected.data(),n);
    }
    double rate_=44100,tempo_=120;Config config_;mod::Matrix matrix_;
    std::array<mod::Lfo,2> lfos_;mod::SampleHold random_;
    std::uint32_t used_=0,targets_=0;
    float follower_=0,followerAttack_=0,followerRelease_=0;
    bool voice_=false,effects_=false;
};
} // namespace vox::fx::macro
