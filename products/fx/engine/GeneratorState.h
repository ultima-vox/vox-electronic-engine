#pragma once
#include "Generator.h"
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>
namespace vox::fx::generator {
// Settings actually exposed by the interim UI; family remains in the Program.
struct ControllerState {
    std::uint32_t seed=1;
    Locks locks{};
    bool valid() const noexcept {
        if(seed<1||seed>2147483647u||locks.duration)return false;
        for(auto mask:locks.layers)if(mask & ~static_cast<std::uint32_t>(all))return false;
        return true;
    }
};
namespace statecodec {
inline bool encode(const ControllerState& value,std::vector<std::byte>& output) {
    if(!value.valid())return false;
    std::vector<std::byte> next;next.reserve(24);
    auto put=[&](std::uint32_t word){for(int shift=0;shift<32;shift+=8)next.push_back(static_cast<std::byte>((word>>shift)&255));};
    put(1);put(value.seed);for(auto mask:value.locks.layers)put(mask);
    output=std::move(next);return true;
}
inline bool decode(std::span<const std::byte> bytes,ControllerState& output) noexcept {
    if(bytes.size()!=24)return false;
    auto read=[&](std::size_t n){std::uint32_t v=0;for(unsigned i=0;i<4;++i)v|=std::to_integer<std::uint32_t>(bytes[n*4+i])<<(8*i);return v;};
    if(read(0)!=1)return false;
    ControllerState next;next.seed=read(1);for(std::size_t i=0;i<4;++i)next.locks.layers[i]=read(i+2);
    if(!next.valid())return false;
    output=next;return true;
}
}
} // namespace vox::fx::generator
