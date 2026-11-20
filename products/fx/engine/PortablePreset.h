#pragma once

#include "ProgramStore.h"
#include "SampleReference.h"
#include "StateContainer.h"

namespace vox::fx::portable {
inline constexpr auto product=preset::makeTag("VXUP");
inline constexpr std::size_t maxBytes=64*1024;
inline constexpr unsigned version=3;
struct Sound { ProgramStore::Snapshot state; sample::Reference sample; };
struct Tags {
    std::string name,style,mood,use;
    unsigned role=8; // 0..7 curated roles, 8 = untagged
    bool favorite=false;
    bool valid() const noexcept {
        const auto text=[](const std::string& s){return s.size()<=128&&sample::validUtf8(s)&&
            std::none_of(s.begin(),s.end(),[](unsigned char c){return c<32||c==127;});};
        return !name.empty()&&role<=8&&text(name)&&text(style)&&text(mood)&&text(use);
    }
};
inline constexpr auto tagsTag=preset::makeTag("META");
inline bool putTags(const Tags& value,std::vector<std::byte>& bytes){
    if(!value.valid())return false;
    programcodec::putU32(bytes,value.role);programcodec::putU32(bytes,value.favorite?1:0);
    for(const auto* s:{&value.name,&value.style,&value.mood,&value.use}){
        programcodec::putU32(bytes,static_cast<unsigned>(s->size()));
        for(unsigned char c:*s)bytes.push_back(static_cast<std::byte>(c));
    }
    return true;
}
inline bool readTags(std::span<const std::byte> bytes,Tags& out){
    Tags next;std::size_t at=0;std::uint32_t favorite=0;
    if(!programcodec::getU32(bytes,at,next.role)||!programcodec::getU32(bytes,at,favorite)||favorite>1)return false;
    next.favorite=favorite==1;
    for(auto* s:{&next.name,&next.style,&next.mood,&next.use}){
        std::uint32_t length=0;if(!programcodec::getU32(bytes,at,length)||length>128||length>bytes.size()-at)return false;
        s->assign(reinterpret_cast<const char*>(bytes.data()+at),length);at+=length;
    }
    if(at!=bytes.size()||!next.valid())return false;
    out=std::move(next);return true;
}


// V1 is a sound, not a host project: no UI geometry, A/B bank or host
// automation base values. FX slot types are structural; wet/amount remain
// with the receiving project. An external sample is referenced, never embedded.
inline bool encode(const Sound& value,std::vector<std::byte>& out,const Tags* tags=nullptr){
    preset::StateDocument doc{product,effects::includesTempoDelay(value.state.effects)?version:tags?2u:1u,{}};
    auto section=[&](const char* tag)->std::vector<std::byte>&{
        doc.sections.push_back({preset::makeTag(tag),{}});return doc.sections.back().data;
    };
    if(!programcodec::encode(value.state.program,section("FPRG")) || !effects::validPacked(value.state.effects))return false;
    auto& fx=section("FTYP");
    for(unsigned i=0;i<4;++i)programcodec::putU32(fx,static_cast<unsigned>(effects::unpack(static_cast<std::uint16_t>(value.state.effects>>(16*i))).type));
    if(!pattern::codec::encode(value.state.pattern,section("FPAT")) || !macro::codec::encode(value.state.macros,section("FMOD")) ||
       !generator::statecodec::encode(value.state.generator,section("FGEN")) || !sample::referencecodec::encode(value.sample,section("FSMP")))return false;
    if(tags&&!putTags(*tags,section("META")))return false;
    auto bytes=preset::encode(doc);
    if(!bytes||bytes->size()>maxBytes)return false;
    out=std::move(*bytes);return true;
}
inline bool decode(std::span<const std::byte> bytes,Sound& out,Tags* tags=nullptr){
    if(bytes.size()>maxBytes)return false;
    const auto decoded=preset::decode(bytes,product,version);
    if(!decoded.ok())return false;
    const bool tagged=decoded.document.find(tagsTag)!=nullptr;
    if(decoded.document.sections.size()!=(tagged?7u:6u)||(decoded.document.schema==1&&tagged)||(decoded.document.schema==2&&!tagged))return false;
    const auto section=[&](const char* tag)->std::span<const std::byte>{
        const auto* found=decoded.document.find(preset::makeTag(tag));return found?std::span<const std::byte>(found->data):std::span<const std::byte>{};
    };
    Sound next;
    if(!programcodec::decode(section("FPRG"),next.state.program)||!pattern::codec::decode(section("FPAT"),next.state.pattern)||
       !macro::codec::decode(section("FMOD"),next.state.macros)||!generator::statecodec::decode(section("FGEN"),next.state.generator)||
       !sample::referencecodec::decode(section("FSMP"),next.sample))return false;
    const auto fx=section("FTYP");std::size_t at=0;next.state.effects=0;
    for(unsigned i=0;i<4;++i){
        std::uint32_t type=0;
        if(!programcodec::getU32(fx,at,type)||type>=static_cast<unsigned>(decoded.document.schema<3?effects::Type::tempoDelay:effects::Type::count))return false;
        next.state.effects|=static_cast<std::uint64_t>(effects::pack({static_cast<effects::Type>(type),1,.5f}))<<(16*i);
    }
    if(at!=fx.size())return false;
    Tags nextTags;
    if(tagged&&!readTags(section("META"),nextTags))return false;
    if(tags)*tags=std::move(nextTags);
    out=std::move(next);return true;
}
} // namespace vox::fx::portable
