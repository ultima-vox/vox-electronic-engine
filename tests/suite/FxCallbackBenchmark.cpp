#include "engine/NoiseRiser.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#ifdef VOX_TEST_INTERPOSE_MALLOC
#include "AllocationProbe.h"
#endif

// Standalone DSP timing, not DAW callback qualification. Preparation and
// triggering are outside the timed region. Run with optimization enabled.
int main() {
    using Clock=std::chrono::steady_clock;
    constexpr int measurements=100, block=128;
    const char* names[]{"riser","downlifter","shepard","impact","whoosh","laser","squelch","metallic","texture","formant","glitch","granular"};
    double checksum=0; int failures=0;
    for(double rate:{44100.0,48000.0,96000.0}) for(int family=0;family<12;++family) {
        auto engine=std::make_unique<vox::fx::NoiseRiser>();engine->prepare(rate);
        vox::fx::NoiseProgram p;for(auto& l:p.layers){l.enabled=true;l.family=static_cast<vox::fx::NoiseFamily>(family);}
        if(!engine->setProgram(p))return 1;
        for(int i=0;i<8;++i)if(!engine->noteOn(1,60+i,1,4))return 1;
        std::array<float,block> l{},r{};std::array<double,measurements> timings{};
        for(int i=0;i<measurements;++i){
            const auto start=Clock::now();engine->render(l.data(),r.data(),block);const auto end=Clock::now();
            timings[static_cast<std::size_t>(i)]=std::chrono::duration<double,std::micro>(end-start).count();
            for(float sample:l){if(!std::isfinite(sample)||std::abs(sample)>=1)++failures;checksum+=std::abs(sample);}
        }
        std::sort(timings.begin(),timings.end());
        std::printf("%.0f %s 8 events x 4 layers: p50 %.2f p95 %.2f p99 %.2f max %.2f us\n",
                    rate,names[family],timings[49],timings[94],timings[98],timings[99]);
    }
    for(double rate:{44100.0,48000.0,96000.0})for(int size:{64,128,512})for(int voices:{1,4,8}){
        using namespace vox::fx;
        auto engine=std::make_unique<NoiseRiser>();engine->prepare(rate);
        NoiseProgram p;
        for(auto& l:p.layers){l.enabled=true;l.family=NoiseFamily::granular;}
        if(!engine->setProgram(p))return 1;
        for(int i=0;i<voices;++i)if(!engine->noteOn(1,48+i,1,4))return 1;
        std::array<float,512> left{},right{};std::array<double,measurements> timings{};
        double sumL=0,sumR=0,peak=0;unsigned grains=0;
#ifdef VOX_TEST_INTERPOSE_MALLOC
        voxProbeBegin();
#endif
        for(int n=0;n<measurements;++n){
            const auto start=Clock::now();engine->render(left.data(),right.data(),size);const auto end=Clock::now();
            timings[n]=std::chrono::duration<double,std::micro>(end-start).count();
            grains=std::max(grains,engine->activeGrainCount());
            for(int i=0;i<size;++i){
                if(!std::isfinite(left[i])||!std::isfinite(right[i]))++failures;
                sumL+=left[i];sumR+=right[i];peak=std::max(peak,static_cast<double>(std::max(std::abs(left[i]),std::abs(right[i]))));
            }
        }
#ifdef VOX_TEST_INTERPOSE_MALLOC
        const auto allocations=voxProbeEnd();if(voxProbeAvailable()&&allocations!=0)++failures;
#endif
        const auto dc=std::max(std::abs(sumL),std::abs(sumR))/(measurements*size);
        if(!(peak>0&&peak<1&&dc<.01)||grains>64||(voices==8&&grains!=64))++failures;
        std::sort(timings.begin(),timings.end());
        std::printf("granular %.0f Hz %d frames %d events: grains %u peak %.6f DC %.7f p50 %.2f p95 %.2f p99 %.2f max %.2f us\n",
                    rate,size,voices,grains,peak,dc,timings[49],timings[94],timings[98],timings[99]);
    }
    std::printf("checksum %.6f, bound failures %d\n",checksum,failures);
    return failures ? 1 : 0;
}
