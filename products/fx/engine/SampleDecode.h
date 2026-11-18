#pragma once
#include "SampleStore.h"
#include <juce_cryptography/juce_cryptography.h>
#include <bit>
#include <cstdint>
#include <span>
#include <vector>

namespace vox::fx::sample {
struct Decoded {
    std::vector<float> mono;
    std::uint32_t rate=0;
    juce::String sha256; // Hash of exact source file bytes, not decoded floats.
};
inline constexpr std::size_t maxFileBytes=64u*1024u*1024u;
// Worker/controller only. Strict bounded RIFF/WAVE subset: mono/stereo PCM
// 16/24/32-bit or IEEE float32, little-endian. Output changes only on success.
inline bool decodeWav(std::span<const std::uint8_t> bytes,Decoded& output) {
    if(bytes.size()<12||bytes.size()>maxFileBytes)return false;
    const auto tag=[&](std::size_t n,const char* s){return n+4<=bytes.size() &&
        bytes[n]==static_cast<std::uint8_t>(s[0])&&bytes[n+1]==static_cast<std::uint8_t>(s[1])&&
        bytes[n+2]==static_cast<std::uint8_t>(s[2])&&bytes[n+3]==static_cast<std::uint8_t>(s[3]);};
    const auto u16=[&](std::size_t n){return static_cast<std::uint32_t>(bytes[n])|
        (static_cast<std::uint32_t>(bytes[n+1])<<8);};
    const auto u32=[&](std::size_t n){return u16(n)|(u16(n+2)<<16);};
    if(!tag(0,"RIFF")||!tag(8,"WAVE")||static_cast<std::uint64_t>(u32(4))+8!=bytes.size())return false;
    std::size_t fmt=0,fmtSize=0,data=0,dataSize=0;
    bool foundFmt=false,foundData=false;
    for(std::size_t at=12;at<bytes.size();){
        if(bytes.size()-at<8)return false;
        const auto size=static_cast<std::size_t>(u32(at+4));const auto start=at+8;
        if(size>bytes.size()-start || (size&1u)>bytes.size()-start-size)return false;
        if(tag(at,"fmt ")){if(foundFmt)return false;foundFmt=true;fmt=start;fmtSize=size;}
        if(tag(at,"data")){if(foundData)return false;foundData=true;data=start;dataSize=size;}
        at=start+size+(size&1u);
    }
    if(!foundFmt||!foundData||fmtSize<16)return false;
    const auto format=u16(fmt),channels=u16(fmt+2),rate=u32(fmt+4),byteRate=u32(fmt+8),align=u16(fmt+12),bits=u16(fmt+14);
    if((channels!=1&&channels!=2)||rate<8000||rate>384000 ||
       !((format==1&&(bits==16||bits==24||bits==32))||(format==3&&bits==32)))return false;
    const auto bytesPerSample=bits/8;
    if(align!=channels*bytesPerSample || byteRate!=static_cast<std::uint64_t>(rate)*align ||
       dataSize==0 || dataSize%align!=0 || dataSize/align>Store::maxFrames)return false;
    const auto frames=dataSize/align;
    try {
        Decoded result;result.rate=rate;result.mono.resize(frames);
        for(std::size_t frame=0;frame<frames;++frame){
            double sum=0;
            for(std::size_t channel=0;channel<channels;++channel){
                const auto at=data+frame*align+channel*bytesPerSample;double value=0;
                if(format==3){value=std::bit_cast<float>(u32(at));}
                else if(bits==16){const auto raw=u16(at);value=(raw>=0x8000u?static_cast<std::int64_t>(raw)-65536:raw)/32768.0;}
                else if(bits==24){const auto raw=u16(at)|(static_cast<std::uint32_t>(bytes[at+2])<<16);
                    value=(raw>=0x800000u?static_cast<std::int64_t>(raw)-16777216:raw)/8388608.0;}
                else {value=std::bit_cast<std::int32_t>(u32(at))/2147483648.0;}
                if(!std::isfinite(value)||value < -1 || value > 1)return false;
                sum+=value;
            }
            result.mono[frame]=static_cast<float>(sum/channels);
        }
        result.sha256=juce::SHA256(bytes.data(),bytes.size()).toHexString();
        output=std::move(result);return true;
    } catch(...) {return false;}
}
// Worker/controller only; the exact bytes read are bounded before allocation
// and hashed. Missing, shortened, extended, or unreadable files leave output
// unchanged. Same-length concurrent edits are not detectable by this reader.
inline bool loadWav(const juce::File& file,Decoded& output) {
    juce::FileInputStream input(file);if(!input.openedOk())return false;
    const auto length=input.getTotalLength();
    if(length<12 || static_cast<std::uint64_t>(length)>maxFileBytes)return false;
    try {
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
        std::size_t offset=0;
        while(offset<bytes.size()){
            const auto count=static_cast<int>(std::min<std::size_t>(16384,bytes.size()-offset));
            const int read=input.read(bytes.data()+offset,count);if(read<=0)return false;
            offset+=static_cast<std::size_t>(read);
        }
        std::uint8_t extra=0;if(input.read(&extra,1)!=0)return false;
        return decodeWav(bytes,output);
    } catch(...) {return false;}
}
} // namespace vox::fx::sample
