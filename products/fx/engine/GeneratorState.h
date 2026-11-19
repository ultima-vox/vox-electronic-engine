#pragma once
#include "Generator.h"
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>
namespace vox::fx::generator {
// Settings actually exposed by the interim UI; family remains in the Program.
struct ControllerState {
    std::uint32_t seed=1;
    Locks locks{};
    int style=0,complexity=4;
    float intensity=.7f,energy=.7f,stereo=.5f;
    Settings settings(int family) const noexcept {
        Settings result;result.family=family;result.style=style;result.complexity=complexity;
        result.intensity=intensity;result.energy=energy;result.stereo=stereo;result.seed=seed;return result;
    }
    bool valid() const noexcept {
        if(seed<1||seed>2147483647u||locks.duration||!settings(0).valid())return false;
        for(auto mask:locks.layers)if(mask & ~static_cast<std::uint32_t>(all))return false;
        return true;
    }
};
namespace statecodec {
inline bool encode(const ControllerState& value,std::vector<std::byte>& output) {
    if(!value.valid())return false;
    std::vector<std::byte> next;next.reserve(44);
    auto put=[&](std::uint32_t word){for(int shift=0;shift<32;shift+=8)next.push_back(static_cast<std::byte>((word>>shift)&255));};
    put(3);put(value.seed);for(auto mask:value.locks.layers)put(mask);
    put(static_cast<std::uint32_t>(value.style));put(static_cast<std::uint32_t>(value.complexity));
    put(std::bit_cast<std::uint32_t>(value.intensity));put(std::bit_cast<std::uint32_t>(value.energy));put(std::bit_cast<std::uint32_t>(value.stereo));
    output=std::move(next);return true;
}
inline bool decode(std::span<const std::byte> bytes,ControllerState& output) noexcept {
    if(bytes.size()!=24&&bytes.size()!=44)return false;
    auto read=[&](std::size_t n){std::uint32_t v=0;for(unsigned i=0;i<4;++i)v|=std::to_integer<std::uint32_t>(bytes[n*4+i])<<(8*i);return v;};
    const auto version=read(0);
    if ((version==1 && bytes.size()!=24) || ((version==2||version==3) && bytes.size()!=44) || version<1 || version>3) return false;
    ControllerState next;next.seed=read(1);
    constexpr std::uint32_t oldAll = 127u;
    for(std::size_t i=0;i<4;++i) {
        const auto mask=read(i+2);
        if(version<3 && (mask & ~oldAll))return false;
        next.locks.layers[i]=version<3 && mask==oldAll?all:mask;
    }
    if(version>=2){
        const auto style=read(6),complexity=read(7);
        if(style>=6||complexity<1||complexity>4)return false;
        next.style=static_cast<int>(style);next.complexity=static_cast<int>(complexity);
        next.intensity=std::bit_cast<float>(read(8));next.energy=std::bit_cast<float>(read(9));next.stereo=std::bit_cast<float>(read(10));
    }
    if(!next.valid())return false;
    output=next;return true;
}
}
} // namespace vox::fx::generator
