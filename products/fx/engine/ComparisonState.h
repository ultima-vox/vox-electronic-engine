#pragma once

#include "ProgramCodec.h"
#include "StateContainer.h"

namespace vox::fx::compare {
inline constexpr auto sectionTag=preset::makeTag("FXAB");
inline constexpr auto productTag=preset::makeTag("VXFX");
inline constexpr auto uiTag=preset::makeTag("UIST");
inline constexpr std::size_t maxSlotBytes=96*1024;
using Bank=std::array<std::vector<std::byte>,2>;

inline bool empty(const Bank& b) noexcept {return b[0].empty()&&b[1].empty();}
inline preset::Section encode(const Bank& b) {
    preset::Section section;section.tag=sectionTag;
    programcodec::putU32(section.data,1);
    for(const auto& slot:b){
        programcodec::putU32(section.data,static_cast<std::uint32_t>(slot.size()));
        section.data.insert(section.data.end(),slot.begin(),slot.end());
    }
    return section;
}
// Inner snapshots cannot themselves hold an A/B bank. Follow only the two
// fixed-length entries and validate both before returning either one.
inline bool slotDocument(std::span<const std::byte> slot,preset::StateDocument& out) {
    if(slot.empty()||slot.size()>maxSlotBytes)return false;
    auto decoded=preset::decode(slot,productTag,1);
    if(!decoded.ok()||decoded.document.find(sectionTag)||!decoded.document.find(uiTag))return false;
    out=std::move(decoded.document);return true;
}
inline bool decode(std::span<const std::byte> bytes,Bank& result) {
    if(bytes.size()>2*maxSlotBytes+12)return false;
    std::size_t offset=0;std::uint32_t version=0;
    if(!programcodec::getU32(bytes,offset,version)||version!=1)return false;
    Bank next;
    for(auto& slot:next){
        std::uint32_t size=0;
        if(!programcodec::getU32(bytes,offset,size)||size>maxSlotBytes||size>bytes.size()-offset)return false;
        if(size){
            const auto contents=bytes.subspan(offset,size);
            preset::StateDocument verified;
            if(!slotDocument(contents,verified))return false;
            slot.assign(contents.begin(),contents.end());
        }
        offset+=size;
    }
    if(offset!=bytes.size())return false;
    result=std::move(next);return true;
}
} // namespace vox::fx::compare
