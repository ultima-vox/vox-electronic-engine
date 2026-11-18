#pragma once
#include "Pattern.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace vox::fx::pattern::codec {
// FPAT v2: LE u32 version, bar count, seed, grid and 192 packed steps.
// v1 (64 steps, sixteenth grid) remains readable. Reserved bits must be 0.
inline constexpr std::size_t wordCount=195;
using Words=std::array<std::uint32_t,wordCount>;
inline bool toWords(const Pattern& p,Words& result) noexcept {
    if(!p.valid())return false;
    Words next{};next[0]=p.bars;next[1]=p.seed;next[2]=static_cast<std::uint32_t>(p.grid);
    for(std::size_t i=0;i<p.steps.size();++i){const auto& s=p.steps[i];
        next[i+3]=(s.enabled?1u:0u)|(static_cast<std::uint32_t>(s.note)<<1)|
            (static_cast<std::uint32_t>(s.velocity)<<8)|(static_cast<std::uint32_t>(s.probability)<<15)|
            (static_cast<std::uint32_t>(s.ratchets)<<22);
    }
    result=next;return true;
}
inline bool fromWords(const Words& words,Pattern& result) noexcept {
    if(words[0]<1||words[0]>4 || words[2]>=static_cast<std::uint32_t>(Grid::count))return false;
    Pattern next;next.bars=static_cast<std::uint8_t>(words[0]);next.seed=words[1];next.grid=static_cast<Grid>(words[2]);
    for(std::size_t i=0;i<next.steps.size();++i){const auto v=words[i+3];
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
    append(2);for(auto word:words)append(word);result=std::move(bytes);return true;
}
inline bool decode(std::span<const std::byte> bytes,Pattern& result) noexcept {
    if(bytes.size()!=67*4 && bytes.size()!=(wordCount+1)*4)return false;
    auto read=[&](std::size_t i){std::uint32_t value=0;for(unsigned b=0;b<4;++b)value|=std::to_integer<std::uint32_t>(bytes[i*4+b])<<(8*b);return value;};
    const auto version=read(0);
    if(version==2 && bytes.size()==(wordCount+1)*4){
        Words words{};for(std::size_t i=0;i<words.size();++i)words[i]=read(i+1);
        return fromWords(words,result);
    }
    if(version==1 && bytes.size()==67*4){
        Pattern defaults;Words words{};(void)toWords(defaults,words);
        words[0]=read(1);words[1]=read(2);
        for(std::size_t i=0;i<64;++i)words[i+3]=read(i+3);
        return fromWords(words,result);
    }
    return false;
}
} // namespace vox::fx::pattern::codec
