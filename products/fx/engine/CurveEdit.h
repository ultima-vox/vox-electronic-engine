#pragma once

#include "Timeline.h"

namespace vox::fx::curveedit {
using Selection = std::uint32_t;
constexpr Selection bit(unsigned i) noexcept { return i < 16 ? Selection{1} << i : 0; }

// The group retains spacing when dragging into an adjacent unselected node.
// Selected endpoints participate in value motion only; their times are fixed.
inline bool move(const timeline::Curve& input, Selection selection, float dt, float dv,
                 timeline::Curve& output) noexcept {
    if (!input.valid() || !std::isfinite(dt) || !std::isfinite(dv)) return false;
    float minTime=-1, maxTime=1, minValue=-1, maxValue=1;
    constexpr float gap=0.0001f;
    const auto movable=[&](unsigned i){return i>0 && i+1<input.count && (selection&bit(i));};
    for (unsigned i=0; i<input.count; ++i) {
        if (!(selection&bit(i))) continue;
        const auto& node=input.nodes[i];
        minValue=std::max(minValue,-node.value); maxValue=std::min(maxValue,1-node.value);
        if (!movable(i)) continue;
        if (!movable(i-1)) minTime=std::max(minTime,std::min(0.0f,input.nodes[i-1].time+gap-node.time));
        if (!movable(i+1)) maxTime=std::min(maxTime,std::max(0.0f,input.nodes[i+1].time-gap-node.time));
    }
    auto next=input;
    for (unsigned i=0; i<input.count; ++i) {
        if (!(selection&bit(i))) continue;
        next.nodes[i].value=std::clamp(input.nodes[i].value+std::clamp(dv,minValue,maxValue),0.0f,1.0f);
        if (movable(i)) next.nodes[i].time+=std::clamp(dt,minTime,maxTime);
    }
    if (!next.valid()) return false;
    output=next;return true;
}
inline bool erase(const timeline::Curve& input, Selection selection, timeline::Curve& output) noexcept {
    if (!input.valid()) return false;
    timeline::Curve next;next.count=0;
    for (unsigned i=0;i<input.count;++i)
        if (i==0 || i+1==input.count || !(selection&bit(i))) next.nodes[next.count++]=input.nodes[i];
    if (next.count==input.count) return false;
    output=next;return true;
}
} // namespace vox::fx::curveedit
