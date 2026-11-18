#pragma once
#include "Modulation.h"
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace vox::fx::macro {
// Initial product routes: eight bipolar macro sources may target master gain.
// Other typed sources and destinations remain unavailable until implemented.
struct Config {
    std::array<mod::Route,64> routes{};
    std::uint32_t count=0;
    bool valid() const noexcept {
        if(count>routes.size())return false;
        for(std::size_t i=0;i<count;++i){const auto& r=routes[i];const auto source=static_cast<unsigned>(r.source);
            if(!r.valid()||r.destination!=mod::Destination::gain||source<static_cast<unsigned>(mod::Source::macro1)||
               source>static_cast<unsigned>(mod::Source::macro8))return false;}
        return true;
    }
    bool operator==(const Config& other) const noexcept {
        if(count!=other.count)return false;
        for(std::size_t i=0;i<count;++i){const auto& a=routes[i];const auto& b=other.routes[i];
            if(a.source!=b.source||a.destination!=b.destination||a.depth!=b.depth||a.smoothingSeconds!=b.smoothingSeconds)return false;}
        return true;
    }
    bool setGainRoute(std::size_t index,float depth,float smoothing) noexcept {
        if(index>=8||!std::isfinite(depth)||depth < -1||depth>1||!std::isfinite(smoothing)||smoothing<0||smoothing>1||!valid())return false;
        const auto source=static_cast<mod::Source>(static_cast<unsigned>(mod::Source::macro1)+index);
        std::size_t slot=0;while(slot<count && routes[slot].source!=source)++slot;
        if(depth==0){if(slot<count){for(std::size_t i=slot+1;i<count;++i)routes[i-1]=routes[i];--count;}return true;}
        if(slot==count){if(count==routes.size())return false;++count;}
        routes[slot]={source,mod::Destination::gain,depth,smoothing};return true;
    }
};
namespace codec {
inline constexpr std::size_t wordCount=1+64*4;
using Words=std::array<std::uint32_t,wordCount>;
inline bool toWords(const Config& config,Words& output) noexcept {
    if(!config.valid())return false;
    Words next{};next[0]=config.count;
    for(std::size_t i=0;i<config.count;++i){const auto& route=config.routes[i];const auto at=1+i*4;
        next[at]=static_cast<std::uint32_t>(route.source);next[at+1]=static_cast<std::uint32_t>(route.destination);
        next[at+2]=std::bit_cast<std::uint32_t>(route.depth);next[at+3]=std::bit_cast<std::uint32_t>(route.smoothingSeconds);}
    output=next;return true;
}
inline bool fromWords(const Words& words,Config& output) noexcept {
    if(words[0]>64)return false;
    Config next;next.count=words[0];
    for(std::size_t i=0;i<next.count;++i){const auto at=1+i*4;
        if(words[at]>255||words[at+1]>255)return false;
        next.routes[i]={static_cast<mod::Source>(words[at]),static_cast<mod::Destination>(words[at+1]),
            std::bit_cast<float>(words[at+2]),std::bit_cast<float>(words[at+3])};}
    for(std::size_t i=1+next.count*4;i<words.size();++i)if(words[i]!=0)return false;
    if(!next.valid())return false;
    output=next;return true;
}
inline bool encode(const Config& config,std::vector<std::byte>& output) {
    Words words{};if(!toWords(config,words))return false;std::vector<std::byte> next;next.reserve((wordCount+1)*4);
    const auto append=[&](std::uint32_t v){for(int shift=0;shift<32;shift+=8)next.push_back(static_cast<std::byte>((v>>shift)&255));};
    append(1);for(auto v:words)append(v);output=std::move(next);return true;
}
inline bool decode(std::span<const std::byte> bytes,Config& output) noexcept {
    if(bytes.size()!=(wordCount+1)*4)return false;
    auto read=[&](std::size_t i){std::uint32_t v=0;for(unsigned b=0;b<4;++b)v|=std::to_integer<std::uint32_t>(bytes[i*4+b])<<(8*b);return v;};
    if(read(0)!=1)return false;
    Words words{};for(std::size_t i=0;i<words.size();++i)words[i]=read(i+1);
    return fromWords(words,output);
}
}
class Runtime {
public:
    bool prepare(double rate) noexcept {
        if(!std::isfinite(rate)||rate<8000||rate>384000)return false;
        rate_=rate;return matrix_.prepare(rate_,config_.routes.data(),config_.count);
    }
    bool setConfig(const Config& config) noexcept {
        if(!config.valid())return false;
        if(config==config_)return true;
        if(!matrix_.prepare(rate_,config.routes.data(),config.count))return false;
        config_=config;return true;
    }
    float process(float base,const std::array<float,8>& macros) noexcept {
        if(config_.count==0)return std::isfinite(base)?std::clamp(base,0.0f,1.0f):0.0f;
        mod::Sources sources{};
        for(std::size_t i=0;i<macros.size();++i){const float v=macros[i];
            sources[static_cast<std::size_t>(mod::Source::macro1)+i]=std::isfinite(v)?2*std::clamp(v,0.0f,1.0f)-1:0;}
        mod::Destinations bases{};bases[static_cast<std::size_t>(mod::Destination::gain)]=base;
        return matrix_.step(bases,sources)[static_cast<std::size_t>(mod::Destination::gain)];
    }
private:
    double rate_=44100;Config config_;mod::Matrix matrix_;
};
} // namespace vox::fx::macro
