#pragma once
#include <cmath>
#include <cstdint>
#include <limits>

namespace vox::fx::transport {
struct Position {
    bool playing=false,hasSample=false,hasPpq=false,hasTempo=false;
    std::int64_t sample=0;
    double ppq=0,tempo=120;
};
// Single audio thread; bounded, allocation-free. Sample positions take
// precedence. If unavailable, detect PPQ jumps >= 1/32 note (0.125 quarter
// notes) to tolerate host rounding and unreported intra-block tempo changes.
// Smaller PPQ-only seeks can intentionally go undetected. Unknown position
// resets history rather than inferring a stop or a seek.
class Tracker {
public:
    void unavailable() noexcept { known_=playing_=sampleExpected_=ppqExpected_=ppqJumped_=false; }
    bool musicalDiscontinuity() const noexcept { return ppqJumped_; }
    bool update(const Position& p,double rate,int blockSize) noexcept {
        const bool stopped=known_&&playing_&&!p.playing;
        bool jumped=false;
        ppqJumped_=false;
        if(known_&&playing_&&p.playing){
            ppqJumped_=ppqExpected_&&p.hasPpq&&std::isfinite(p.ppq)&&std::abs(p.ppq-nextPpq_)>=.125;
            if(sampleExpected_&&p.hasSample)jumped=p.sample!=nextSample_;
            else jumped=ppqJumped_;
        }
        const bool sizeValid=blockSize>=0;
        sampleExpected_=p.playing&&p.hasSample&&sizeValid&&
            p.sample<=std::numeric_limits<std::int64_t>::max()-static_cast<std::int64_t>(blockSize);
        if(sampleExpected_)nextSample_=p.sample+blockSize;
        ppqExpected_=p.playing&&p.hasPpq&&p.hasTempo&&std::isfinite(p.ppq)&&std::isfinite(p.tempo)&&
            p.tempo>=1&&p.tempo<=1000&&std::isfinite(rate)&&rate>=8000&&rate<=384000&&sizeValid;
        if(ppqExpected_){
            nextPpq_=p.ppq+static_cast<double>(blockSize)*p.tempo/(60.0*rate);
            if(!std::isfinite(nextPpq_))ppqExpected_=false;
        }
        known_=true;playing_=p.playing;
        return stopped||jumped;
    }
private:
    bool known_=false,playing_=false,sampleExpected_=false,ppqExpected_=false,ppqJumped_=false;
    std::int64_t nextSample_=0;
    double nextPpq_=0;
};
} // namespace vox::fx::transport
