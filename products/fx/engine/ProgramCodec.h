#pragma once
#include "NoiseRiser.h"
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace vox::fx::programcodec {

// Version 1: little-endian u32 version, followed by four layers. Each layer
// has u32 enabled, u32 family, f32 offset/span/gain/pan, and two curves.
// A curve has u32 node count followed by count records (f32 time/value,
// u32 shape). Values must pass the same validation as the DSP program.
// This codec is for controller/host threads, never the audio callback.
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
inline bool encode(const NoiseProgram& program, std::vector<std::byte>& result) {
    if (!program.valid()) return false;
    std::vector<std::byte> next;
    next.reserve(4 + 4 * (24 + 2 * (4 + 16 * 12)));
    putU32(next,1);
    for (const auto& layer:program.layers) {
        putU32(next,layer.enabled ? 1u : 0u); putU32(next,static_cast<std::uint32_t>(layer.family));
        putFloat(next,layer.timing.offset); putFloat(next,layer.timing.span); putFloat(next,layer.gain); putFloat(next,layer.pan);
        putCurve(next,layer.gainCurve); putCurve(next,layer.panCurve);
    }
    result=std::move(next); return true;
}
inline bool decode(std::span<const std::byte> bytes, NoiseProgram& result) noexcept {
    constexpr std::size_t maxBytes=4 + 4 * (24 + 2 * (4 + 16 * 12));
    if (bytes.size() > maxBytes) return false;
    NoiseProgram next; std::size_t offset=0; std::uint32_t version=0;
    if (!getU32(bytes,offset,version) || version != 1) return false;
    for (auto& layer:next.layers) {
        std::uint32_t enabled=0, family=0;
        if (!getU32(bytes,offset,enabled) || enabled > 1 || !getU32(bytes,offset,family) ||
            family > static_cast<std::uint32_t>(NoiseFamily::formant) ||
            !getFloat(bytes,offset,layer.timing.offset) || !getFloat(bytes,offset,layer.timing.span) ||
            !getFloat(bytes,offset,layer.gain) || !getFloat(bytes,offset,layer.pan) ||
            !getCurve(bytes,offset,layer.gainCurve) || !getCurve(bytes,offset,layer.panCurve)) return false;
        layer.enabled=enabled != 0; layer.family=static_cast<NoiseFamily>(family);
    }
    if (offset != bytes.size() || !next.valid()) return false;
    result=next; return true;
}
} // namespace vox::fx::programcodec
