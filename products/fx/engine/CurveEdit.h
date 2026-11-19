#pragma once

#include "Timeline.h"

namespace vox::fx::curveedit {
// Message-thread view only; the Program always stores normalized times.
class Viewport {
public:
    double start() const noexcept { return start_; }
    double span() const noexcept { return span_; }
    double timeAt(double fraction) const noexcept { return start_+std::clamp(fraction,0.,1.)*span_; }
    double fractionAt(double time) const noexcept { return (time-start_)/span_; }
    bool contains(double time) const noexcept { return time>=start_&&time<=start_+span_; }
    void fit() noexcept { start_=0;span_=1; }
    void zoom(double anchor,double factor) noexcept {
        if(!std::isfinite(anchor)||!std::isfinite(factor)||factor<=0)return;
        anchor=std::clamp(anchor,0.,1.);
        const auto time=timeAt(anchor);
        span_=std::clamp(span_/factor,1./16,1.);
        start_=std::clamp(time-anchor*span_,0.,1.-span_);
    }
    void pan(double fraction) noexcept {
        if(std::isfinite(fraction))start_=std::clamp(start_+fraction*span_,0.,1.-span_);
    }
    void reveal(double time) noexcept {
        if(!std::isfinite(time)||contains(time))return;
        start_=std::clamp(time-.5*span_,0.,1.-span_);
    }
private:
    double start_=0,span_=1;
};
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
