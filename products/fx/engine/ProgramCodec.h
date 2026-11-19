#pragma once
#include "NoiseRiser.h"
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace vox::fx::programcodec {

// Versions 1 and 2 contain gain/pan curves per layer; v2 appends bars.
// Version 3 appends per-layer pitch/cutoff and four Program trajectories;
// v4 appends playback mode and repeated-note policy. v5 appends Program
// key/scale and dual filter/transpose settings per layer; v6 appends signed
// family-specific Character on each layer. Older fields
// remain byte-identical; old Programs use chromatic, bypassed filters.
// Every decoder path is bounded, rejects unknown versions/truncation/trailing
// bytes, and assigns its output only after complete validation.
inline void putU32(std::vector<std::byte>& bytes, std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) bytes.push_back(static_cast<std::byte>((value >> shift) & 0xffu));
}
inline void putFloat(std::vector<std::byte>& bytes, float value) { putU32(bytes, std::bit_cast<std::uint32_t>(value)); }
inline bool getU32(std::span<const std::byte> bytes, std::size_t& offset, std::uint32_t& value) noexcept {
    if (offset > bytes.size() || bytes.size() - offset < 4) return false;
    value = 0;
    for (std::size_t i=0; i<4; ++i) value |= std::to_integer<std::uint32_t>(bytes[offset+i]) << (8*i);
    offset += 4; return true;
}
inline bool getFloat(std::span<const std::byte> bytes, std::size_t& offset, float& value) noexcept {
    std::uint32_t word = 0;
    if (!getU32(bytes, offset, word)) return false;
    value = std::bit_cast<float>(word); return true;
}
inline void putCurve(std::vector<std::byte>& bytes, const timeline::Curve& curve) {
    putU32(bytes, curve.count);
    for (std::size_t i=0; i<curve.count; ++i) {
        const auto& node = curve.nodes[i];
        putFloat(bytes,node.time); putFloat(bytes,node.value); putU32(bytes,static_cast<std::uint32_t>(node.shape));
    }
}
inline bool getCurve(std::span<const std::byte> bytes, std::size_t& offset, timeline::Curve& curve) noexcept {
    std::uint32_t count=0;
    if (!getU32(bytes,offset,count) || count < 2 || count > 16) return false;
    curve = {}; curve.count=static_cast<std::uint8_t>(count);
    for (std::size_t i=0; i<count; ++i) {
        auto& node=curve.nodes[i]; std::uint32_t shape=0;
        if (!getFloat(bytes,offset,node.time) || !getFloat(bytes,offset,node.value) ||
            !getU32(bytes,offset,shape) || shape > static_cast<std::uint32_t>(timeline::Shape::hold)) return false;
        node.shape=static_cast<timeline::Shape>(shape);
    }
    return curve.valid();
}
inline constexpr std::size_t soundBytes=8+4*(4+2*16);
inline constexpr std::size_t characterBytes=16;
inline constexpr std::size_t maxBytes=16 + 4 * (24 + 4 * (4 + 16 * 12)) + 4 * (4 + 16 * 12) + soundBytes + characterBytes;
inline bool encode(const NoiseProgram& program, std::vector<std::byte>& result) {
    if (!program.valid()) return false;
    std::vector<std::byte> next;
    next.reserve(maxBytes);
    putU32(next,6);
    for (const auto& layer:program.layers) {
        putU32(next,layer.enabled ? 1u : 0u); putU32(next,static_cast<std::uint32_t>(layer.family));
        putFloat(next,layer.timing.offset); putFloat(next,layer.timing.span); putFloat(next,layer.gain); putFloat(next,layer.pan);
        putCurve(next,layer.gainCurve); putCurve(next,layer.panCurve);
        putCurve(next,layer.pitchCurve); putCurve(next,layer.cutoffCurve);
    }
    putCurve(next,program.amplitudeCurve); putCurve(next,program.pitchCurve);
    putCurve(next,program.cutoffCurve); putCurve(next,program.panoramaCurve);
    putU32(next,program.bars); putU32(next,static_cast<std::uint32_t>(program.mode));
    putU32(next,static_cast<std::uint32_t>(program.triggerPolicy));
    putU32(next,program.tonality.root);putU32(next,static_cast<std::uint32_t>(program.tonality.scale));
    for(const auto& layer:program.layers){
        putFloat(next,layer.transpose);
        for(const auto& f:layer.filters){putU32(next,static_cast<std::uint32_t>(f.type));putFloat(next,f.hz);putFloat(next,f.q);putU32(next,f.poles);}
    }
    for(const auto& layer:program.layers)putFloat(next,layer.character);
    result=std::move(next); return true;
}
inline bool decode(std::span<const std::byte> bytes, NoiseProgram& result) noexcept {
    if (bytes.size() > maxBytes) return false;
    NoiseProgram next; std::size_t offset=0; std::uint32_t version=0;
    if (!getU32(bytes,offset,version) || (version < 1 || version > 6)) return false;
    for (auto& layer:next.layers) {
        std::uint32_t enabled=0, family=0;
        if (!getU32(bytes,offset,enabled) || enabled > 1 || !getU32(bytes,offset,family) ||
            family > static_cast<std::uint32_t>(NoiseFamily::granular) ||
            !getFloat(bytes,offset,layer.timing.offset) || !getFloat(bytes,offset,layer.timing.span) ||
            !getFloat(bytes,offset,layer.gain) || !getFloat(bytes,offset,layer.pan) ||
            !getCurve(bytes,offset,layer.gainCurve) || !getCurve(bytes,offset,layer.panCurve)) return false;
        layer.enabled=enabled != 0; layer.family=static_cast<NoiseFamily>(family);
        if (version >= 3 && (!getCurve(bytes,offset,layer.pitchCurve) || !getCurve(bytes,offset,layer.cutoffCurve))) return false;
    }
    if (version >= 3 && (!getCurve(bytes,offset,next.amplitudeCurve) || !getCurve(bytes,offset,next.pitchCurve) ||
                         !getCurve(bytes,offset,next.cutoffCurve) || !getCurve(bytes,offset,next.panoramaCurve))) return false;
    if (version >= 2) { std::uint32_t bars=0; if (!getU32(bytes,offset,bars) || bars > 16) return false; next.bars=static_cast<std::uint8_t>(bars); }
    if(version>=4){std::uint32_t mode=0,policy=0;
        if(!getU32(bytes,offset,mode)||mode>static_cast<std::uint32_t>(PlayMode::transition)||
           !getU32(bytes,offset,policy)||policy>static_cast<std::uint32_t>(TriggerPolicy::replace))return false;
        next.mode=static_cast<PlayMode>(mode);next.triggerPolicy=static_cast<TriggerPolicy>(policy);
    }
    if(version>=5){
        std::uint32_t root=0,scale=0;
        if(!getU32(bytes,offset,root)||root>=12||!getU32(bytes,offset,scale)||scale>static_cast<unsigned>(source::Scale::phrygian))return false;
        next.tonality={static_cast<source::Scale>(scale),static_cast<std::uint8_t>(root)};
        for(auto& layer:next.layers){
            if(!getFloat(bytes,offset,layer.transpose))return false;
            for(auto& f:layer.filters){
                std::uint32_t type=0,poles=0;
                if(!getU32(bytes,offset,type)||type>static_cast<unsigned>(source::FilterType::notch)||
                   !getFloat(bytes,offset,f.hz)||!getFloat(bytes,offset,f.q)||!getU32(bytes,offset,poles)||(poles!=2&&poles!=4))return false;
                f.type=static_cast<source::FilterType>(type);f.poles=static_cast<std::uint8_t>(poles);
            }
        }
    }
    if(version>=6)for(auto& layer:next.layers)if(!getFloat(bytes,offset,layer.character))return false;
    if (offset != bytes.size() || !next.valid()) return false;
    result=next; return true;
}
} // namespace vox::fx::programcodec
