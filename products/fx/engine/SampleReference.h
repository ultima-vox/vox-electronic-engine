#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace vox::fx::sample {
inline bool validUtf8(const std::string& s) noexcept {
    for(std::size_t i=0;i<s.size();){
        const auto c=static_cast<unsigned char>(s[i]);
        if(c==0)return false;
        if(c<0x80){++i;continue;}
        const std::size_t length=c>=0xc2&&c<=0xdf?2:c>=0xe0&&c<=0xef?3:c>=0xf0&&c<=0xf4?4:0;
        if(length==0||length>s.size()-i)return false;
        for(std::size_t k=1;k<length;++k)if((static_cast<unsigned char>(s[i+k])&0xc0)!=0x80)return false;
        const auto second=static_cast<unsigned char>(s[i+1]);
        if((c==0xe0&&second<0xa0)||(c==0xed&&second>=0xa0)||(c==0xf0&&second<0x90)||(c==0xf4&&second>=0x90))return false;
        i+=length;
    }
    return true;
}
struct Reference {
    enum class Kind : std::uint32_t { builtin,external };
    Kind kind=Kind::builtin;
    std::string path,sha256;
    bool valid() const noexcept {
        if(kind==Kind::builtin)return path.empty()&&sha256.empty();
        if(kind!=Kind::external||path.empty()||path.size()>4096||!validUtf8(path)||sha256.size()!=64)return false;
        for(char c:sha256)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;
        return true;
    }
    bool operator==(const Reference&) const = default;
};
namespace referencecodec {
inline bool encode(const Reference& reference,std::vector<std::byte>& output) {
    if(!reference.valid())return false;
    try {
        std::vector<std::byte> next;next.reserve(12+reference.path.size()+reference.sha256.size());
        const auto append=[&](std::uint32_t n){for(unsigned i=0;i<4;++i)next.push_back(static_cast<std::byte>((n>>(8*i))&255));};
        append(1);append(static_cast<std::uint32_t>(reference.kind));append(static_cast<std::uint32_t>(reference.path.size()));
        for(char c:reference.path)next.push_back(static_cast<std::byte>(static_cast<unsigned char>(c)));
        for(char c:reference.sha256)next.push_back(static_cast<std::byte>(static_cast<unsigned char>(c)));
        output=std::move(next);return true;
    } catch(...) {return false;}
}
inline bool decode(std::span<const std::byte> bytes,Reference& output) noexcept {
    if(bytes.size()<12)return false;
    const auto read=[&](std::size_t at){std::uint32_t n=0;for(unsigned i=0;i<4;++i)n|=std::to_integer<std::uint32_t>(bytes[at+i])<<(8*i);return n;};
    const auto version=read(0),kind=read(4),length=read(8);
    if(version!=1||kind>1||length>4096)return false;
    const std::size_t hashLength=kind==1?64u:0u;
    if(bytes.size()!=12u+length+hashLength)return false;
    try {
        Reference next;next.kind=static_cast<Reference::Kind>(kind);
        next.path.assign(reinterpret_cast<const char*>(bytes.data()+12),length);
        if(hashLength)next.sha256.assign(reinterpret_cast<const char*>(bytes.data()+12+length),hashLength);
        if(!next.valid())return false;
        output=std::move(next);return true;
    } catch(...) {return false;}
}
} // namespace referencecodec
} // namespace vox::fx::sample
