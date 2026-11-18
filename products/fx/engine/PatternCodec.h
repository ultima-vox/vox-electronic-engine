#pragma once
#include "Pattern.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace vox::fx::pattern::codec {
// FPAT v1: LE u32 version, bar count, seed, then 64 packed steps. Packing is
// explicit, validated and independent of C++ layout; reserved bits must be 0.
inline constexpr std::size_t wordCount=66;
using Words=std::array<std::uint32_t,wordCount>;
inline bool toWords(const Pattern& p,Words& result) noexcept {
    if(!p.valid())return false;
    Words next{};next[0]=p.bars;next[1]=p.seed;
    for(std::size_t i=0;i<p.steps.size();++i){const auto& s=p.steps[i];
        next[i+2]=(s.enabled?1u:0u)|(static_cast<std::uint32_t>(s.note)<<1)|
            (static_cast<std::uint32_t>(s.velocity)<<8)|(static_cast<std::uint32_t>(s.probability)<<15)|
            (static_cast<std::uint32_t>(s.ratchets)<<22);
    }
    result=next;return true;
}
inline bool fromWords(const Words& words,Pattern& result) noexcept {
    if(words[0]<1||words[0]>4)return false;
    Pattern next;next.bars=static_cast<std::uint8_t>(words[0]);next.seed=words[1];
    for(std::size_t i=0;i<next.steps.size();++i){const auto v=words[i+2];
        if((v&0xfe000000u)!=0)return false;
        auto& s=next.steps[i];s.enabled=(v&1u)!=0;s.note=static_cast<std::uint8_t>((v>>1)&127);
        s.velocity=static_cast<std::uint8_t>((v>>8)&127);s.probability=static_cast<std::uint8_t>((v>>15)&127);
        s.ratchets=static_cast<std::uint8_t>((v>>22)&7);
    }
    if(!next.valid())return false;
    result=next;return true;
}
inline bool encode(const Pattern& pattern,std::vector<std::byte>& result) {
    Words words{};if(!toWords(pattern,words))return false;
    std::vector<std::byte> bytes;bytes.reserve((wordCount+1)*4);
    const auto append=[&](std::uint32_t value){for(int shift=0;shift<32;shift+=8)bytes.push_back(static_cast<std::byte>((value>>shift)&255));};
    append(1);for(auto word:words)append(word);result=std::move(bytes);return true;
}
inline bool decode(std::span<const std::byte> bytes,Pattern& result) noexcept {
    if(bytes.size()!=(wordCount+1)*4)return false;
    auto read=[&](std::size_t i){std::uint32_t value=0;for(unsigned b=0;b<4;++b)value|=std::to_integer<std::uint32_t>(bytes[i*4+b])<<(8*b);return value;};
    if(read(0)!=1)return false;
    Words words{};for(std::size_t i=0;i<words.size();++i)words[i]=read(i+1);
    return fromWords(words,result);
}
} // namespace vox::fx::pattern::codec
