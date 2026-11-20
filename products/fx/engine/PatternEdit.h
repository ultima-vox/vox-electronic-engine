#pragma once
#include "Pattern.h"

namespace vox::fx::pattern::edit {
// A transient authoring buffer, not an FPAT schema or audio-thread object.
struct Bar {
    Grid grid=Grid::count;
    std::array<Step,48> steps{};
};

inline bool copyBar(const Pattern& source,unsigned bar,Bar& output) noexcept {
    if(!source.valid()||bar>=source.bars)return false;
    Bar next;next.grid=source.grid;
    const auto count=stepsPerBar(source.grid);
    std::copy_n(source.steps.begin()+bar*count,count,next.steps.begin());
    output=next;
    return true;
}

// Refuse cross-grid paste rather than silently reinterpret timing. The
// destination seed, swing, other bars and dormant steps are untouched.
inline bool pasteBar(Pattern& destination,unsigned bar,const Bar& copied) noexcept {
    if(!destination.valid()||bar>=destination.bars||destination.grid!=copied.grid)return false;
    const auto count=stepsPerBar(copied.grid);
    if(!std::all_of(copied.steps.begin(),copied.steps.begin()+count,[](const Step& step){return step.valid();}))return false;
    std::copy_n(copied.steps.begin(),count,destination.steps.begin()+bar*count);
    return true;
}
enum class Variation { earlier, later, reverse };
// Reorder complete step records within one bar on any supported grid. This
// does not reverse audio or move the global swing/probability seed; scheduler
// draws still apply at the new positions.
inline bool varyBar(Pattern& pattern,unsigned bar,Variation variation) noexcept {
    if(!pattern.valid()||bar>=pattern.bars)return false;
    const auto count=stepsPerBar(pattern.grid);
    const auto begin=pattern.steps.begin()+bar*count,end=begin+count;
    switch(variation){
        case Variation::earlier:std::rotate(begin,begin+1,end);return true;
        case Variation::later:std::rotate(begin,end-1,end);return true;
        case Variation::reverse:std::reverse(begin,end);return true;
    }
    return false;
}
} // namespace vox::fx::pattern::edit
