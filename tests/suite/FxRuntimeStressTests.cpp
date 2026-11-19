#include "Processor.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#ifdef VOX_TEST_INTERPOSE_MALLOC
#include "AllocationProbe.h"
#endif
#ifdef VOX_FX_LOCK_PROBE
extern "C" void voxFxLockBegin();
extern "C" unsigned long voxFxLockEnd();
#endif

int main(){
    using namespace vox::fx;
    juce::ScopedJuceInitialiser_GUI init;
    int errors=0;
    for(double rate:{44100.,48000.,96000.})for(int frames:{64,128,512})for(int voices:{1,4,8}){
        auto fx=std::make_unique<FxProcessor>();fx->prepareToPlay(rate,frames);
        NoiseProgram p;p.mode=PlayMode::gate;
        for(auto& l:p.layers){l.enabled=true;l.family=NoiseFamily::granular;l.filters[0]={source::FilterType::lowpass,4800,.71f,2};}
        fx->setProgram(p);
        for(std::size_t i=0;i<4;++i)fx->setEffect(i,{effects::Type::drive,1,.7f});
        auto motion=fx->versionedMotion();auto cfg=motion.config;
        for(unsigned i=0;i<64;++i)(void)cfg.put(i,{i%2?mod::Source::macro1:mod::Source::lfo1,
            i%3==0?mod::Destination::grainRate:i%3==1?mod::Destination::width:mod::Destination::amount1,.005f,.002f});
        if(!fx->replaceMotionIfCurrent(motion,cfg))return 1;
        juce::AudioBuffer<float> io(2,frames);juce::MidiBuffer midi;midi.ensureSize(4096);
        constexpr int blocks=160;
        std::array<double,blocks> durations{};unsigned maximumGrains=0;double peak=0,sumL=0,sumR=0;
        std::uint32_t rng=0x154311u;bool finite=true,midiConsumed=true;
        unsigned long alloc=0,locks=0;
        for(int block=0;block<blocks;++block){
            if(block==0)for(int i=0;i<voices;++i)midi.addEvent(juce::MidiMessage::noteOn(1,47+i,juce::uint8(100)),0);
            if(block>=48&&block%3==0)for(int j=0;j<voices*2;++j){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;
                const int when=static_cast<int>(rng%frames),note=43+static_cast<int>((rng>>9)%17),channel=1+static_cast<int>((rng>>16)%3);
                const unsigned op=(rng>>24)%8;
                if(op==0)midi.addEvent(juce::MidiMessage::controllerEvent(channel,120,0),when);
                else if(op==1)midi.addEvent(juce::MidiMessage::controllerEvent(channel,123,0),when);
                else if(op==2)midi.addEvent(juce::MidiMessage::noteOn(channel,note,juce::uint8(0)),when);
                else if(op==3)midi.addEvent(juce::MidiMessage::noteOff(channel,note),when);
                else midi.addEvent(juce::MidiMessage::noteOn(channel,note,juce::uint8(36+(rng%90))),when);
            }
            if(block==129)fx->panic();
#ifdef VOX_TEST_INTERPOSE_MALLOC
            voxProbeBegin();
#endif
#ifdef VOX_FX_LOCK_PROBE
            voxFxLockBegin();
#endif
            const auto now=std::chrono::steady_clock::now();fx->processBlock(io,midi);
            durations[block]=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-now).count();
#ifdef VOX_FX_LOCK_PROBE
            locks+=voxFxLockEnd();
#endif
#ifdef VOX_TEST_INTERPOSE_MALLOC
            alloc+=voxProbeEnd();
#endif
            midiConsumed &= midi.isEmpty();
            maximumGrains=std::max(maximumGrains,fx->audioTelemetry().grains);
            for(int i=0;i<frames;++i){const auto a=io.getSample(0,i),b=io.getSample(1,i);finite &= std::isfinite(a)&&std::isfinite(b);
                peak=std::max(peak,static_cast<double>(std::max(std::abs(a),std::abs(b))));sumL+=a;sumR+=b;}
        }
        const double dc=std::max(std::abs(sumL),std::abs(sumR))/(blocks*frames);
        if(!finite||!midiConsumed||!(peak>.00001&&peak<1&&dc<.01)||alloc||locks||maximumGrains>64||(voices==8&&maximumGrains!=64))++errors;
        std::sort(durations.begin(),durations.end());
        std::printf("%.0f Hz %d samples %d initial voices: peak %.6f DC %.7f grains %u alloc %lu lock %lu p50 %.2f p95 %.2f p99 %.2f max %.2f us\n",
            rate,frames,voices,peak,dc,maximumGrains,alloc,locks,durations[79],durations[151],durations[158],durations.back());
    }
    std::printf("FX JUCE runtime stress: %d failing cases\n",errors);return errors?1:0;
}
