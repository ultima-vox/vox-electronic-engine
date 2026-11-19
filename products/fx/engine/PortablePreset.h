#pragma once

#include "ProgramStore.h"
#include "SampleReference.h"
#include "StateContainer.h"

namespace vox::fx::portable {
inline constexpr auto product=preset::makeTag("VXUP");
inline constexpr std::size_t maxBytes=64*1024;
inline constexpr unsigned version=1;
struct Sound { ProgramStore::Snapshot state; sample::Reference sample; };

// V1 is a sound, not a host project: no UI geometry, A/B bank or host
// automation base values. FX slot types are structural; wet/amount remain
// with the receiving project. An external sample is referenced, never embedded.
inline bool encode(const Sound& value,std::vector<std::byte>& out){
    preset::StateDocument doc{product,version,{}};
    auto section=[&](const char* tag)->std::vector<std::byte>&{
        doc.sections.push_back({preset::makeTag(tag),{}});return doc.sections.back().data;
    };
    if(!programcodec::encode(value.state.program,section("FPRG")) || !effects::validPacked(value.state.effects))return false;
    auto& fx=section("FTYP");
    for(unsigned i=0;i<4;++i)programcodec::putU32(fx,static_cast<unsigned>(effects::unpack(static_cast<std::uint16_t>(value.state.effects>>(16*i))).type));
    if(!pattern::codec::encode(value.state.pattern,section("FPAT")) || !macro::codec::encode(value.state.macros,section("FMOD")) ||
       !generator::statecodec::encode(value.state.generator,section("FGEN")) || !sample::referencecodec::encode(value.sample,section("FSMP")))return false;
    auto bytes=preset::encode(doc);
    if(!bytes||bytes->size()>maxBytes)return false;
    out=std::move(*bytes);return true;
}
inline bool decode(std::span<const std::byte> bytes,Sound& out){
    if(bytes.size()>maxBytes)return false;
    const auto decoded=preset::decode(bytes,product,version);
    if(!decoded.ok()||decoded.document.sections.size()!=6)return false;
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
        if(!programcodec::getU32(fx,at,type)||type>=static_cast<unsigned>(effects::Type::count))return false;
        next.state.effects|=static_cast<std::uint64_t>(effects::pack({static_cast<effects::Type>(type),1,.5f}))<<(16*i);
    }
    if(at!=fx.size())return false;
    out=std::move(next);return true;
}
} // namespace vox::fx::portable
