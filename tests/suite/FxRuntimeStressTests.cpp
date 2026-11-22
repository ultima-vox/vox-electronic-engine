#include "Processor.h"
#include "content/FactoryCandidates.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
#elif defined(__linux__)
 #include <time.h>
#endif
#include <memory>
#ifdef VOX_TEST_INTERPOSE_MALLOC
#include "AllocationProbe.h"
#endif
#ifdef VOX_FX_LOCK_PROBE
extern "C" void voxFxLockBegin();
extern "C" unsigned long voxFxLockEnd();
#endif

namespace {
double threadMicros() noexcept {
#if JUCE_WINDOWS
    FILETIME created{},exited{},kernel{},user{};
    const auto ticks=[](const FILETIME& t){return (std::uint64_t{t.dwHighDateTime}<<32)|t.dwLowDateTime;};
    if(::GetThreadTimes(::GetCurrentThread(),&created,&exited,&kernel,&user))return (ticks(kernel)+ticks(user))*.1;
#elif defined(__linux__)
    timespec t{};
    if(clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t)==0)return t.tv_sec*1e6+t.tv_nsec*.001;
#endif
    return -1;
}
struct Clock final : juce::AudioPlayHead {
    juce::Optional<PositionInfo> getPosition() const override {return position;}
    void advance(std::int64_t at,double rate){
        PositionInfo p;p.setIsPlaying(true);p.setTimeInSamples(at);p.setPpqPosition(at*200./(60.*rate));
        p.setBpm(200);p.setTimeSignature(TimeSignature{4,4});position=p;
    }
    PositionInfo position;
};
}
int main(){
    using namespace vox::fx;
    juce::ScopedJuceInitialiser_GUI init;
    int errors=0;
    const bool typical=std::getenv("VOX_FX_STRESS_TYPICAL")!=nullptr;
    const bool shapes=std::getenv("VOX_FX_STRESS_AUTHORED")!=nullptr;
    const bool splitTiming=std::getenv("VOX_FX_TIMING_THREAD_CPU")!=nullptr;
    const bool wavefold=std::getenv("VOX_FX_STRESS_WAVEFOLD")!=nullptr;
    if(splitTiming&&threadMicros()<0){std::fputs("Requested thread CPU clock unavailable\n",stderr);return 2;}
    if(typical)std::puts("Typical profile: one triggered curated riser, no synchronized pattern or randomized MIDI");
    if(shapes&&!typical)std::puts("Non-default grain shape, step and held ADSR routes");
    if(wavefold&&!typical)std::puts("Wavefold stress: four serial slots, wet and amount 1");
    if(splitTiming){
        std::printf("Timing environment: %s; CPU %s; logical CPUs %d; %s\n",
            juce::SystemStats::getOperatingSystemName().toRawUTF8(),juce::SystemStats::getCpuModel().toRawUTF8(),
            juce::SystemStats::getNumCpus(),sizeof(void*)==8?"64-bit":"32-bit");
#if JUCE_WINDOWS
        std::puts("Windows GetThreadTimes enabled (outside callback); accounting may be coarser than a block, see zero deltas and min positive delta");
#else
        std::puts("Linux thread CPU clock enabled (outside callback); diagnostic, not a deadline relaxation");
#endif
    }
#if defined(VOX_TEST_INTERPOSE_MALLOC)
    std::printf("Allocation probe available: %s\n",voxProbeAvailable()?"yes":"no");
#else
    std::puts("Allocation probe unavailable");
#endif
#if defined(VOX_FX_LOCK_PROBE)
    std::puts("Lock probe: linked pthread mutex/rwlock entry points");
#else
    std::puts("Lock probe unavailable");
#endif
    for(bool patternOn:{false,true}){
    if(typical&&patternOn)continue;
    std::puts(patternOn?"Max-ratchet synchronized Pattern + random MIDI":"Random MIDI without Pattern");
    for(double rate:{44100.,48000.,96000.})for(int frames:{32,64,128,512})for(int voices:{1,4,8}){
        if((frames==32&&!wavefold)||(typical&&voices!=1))continue;
        auto fx=std::make_unique<FxProcessor>();fx->prepareToPlay(rate,frames);
        NoiseProgram p;p.mode=PlayMode::gate;
        for(auto& l:p.layers){l.enabled=true;l.family=NoiseFamily::granular;l.filters[0]={source::FilterType::lowpass,4800,.71f,2};}
        if(shapes){
            p.layers[0].grains={90,.43f,true};p.layers[1].grains={118,-1,true};
            p.layers[2].grains={106,.8f,false};p.layers[3].grains={131,-1,false};
        }
        fx->setProgram(p);
        Clock clock;
        if(patternOn){
            pattern::Pattern grid;grid.bars=4;grid.grid=pattern::Grid::thirtysecondTriplet;grid.swing=.23f;
            for(unsigned i=0;i<grid.bars*pattern::stepsPerBar(grid.grid);++i)grid.steps[i]={true,static_cast<std::uint8_t>(43+i%17),98,100,4};
            if(!fx->setPattern(grid))return 1;
            fx->patternEnabledParameter().setValueNotifyingHost(1.f);fx->setPlayHead(&clock);
        }
        int firstNote=47;
        if(typical){
            // A seconds-based riser without host tempo or transport. Peak and
            // latency are measured on its configured graph, not silent idle.
            const auto& entries=content::reviewEntries;
            const auto selected=std::find_if(std::begin(entries),std::end(entries),[](const auto& e){
                return e.id=="voxfx.riser.firefly-ladder";});
            if(selected==std::end(entries))return 1;
            const auto capture=content::makePreview(*selected,static_cast<unsigned>(rate));
            if(!fx->setProgram(capture.program))return 1;
            firstNote=capture.settings.note;
            auto& duration=fx->durationParameter();duration.setValueNotifyingHost(duration.convertTo0to1(capture.settings.duration));
            for(std::size_t i=0;i<4;++i){
                fx->setEffect(i,capture.settings.effects[i]);
                fx->effectWetParameter(i).setValueNotifyingHost(capture.settings.effects[i].wet);
                fx->effectAmountParameter(i).setValueNotifyingHost(capture.settings.effects[i].amount);
            }
            const auto m=fx->versionedMotion();if(!fx->replaceMotionIfCurrent(m,capture.settings.macroRoutes))return 1;
        }else{
            for(std::size_t i=0;i<4;++i){
                if(!fx->setEffect(i,{wavefold?effects::Type::wavefold:effects::Type::drive,1,wavefold?1.f:.7f}))return 1;
                if(wavefold){
                    fx->effectWetParameter(i).setValueNotifyingHost(1.f);
                    fx->effectAmountParameter(i).setValueNotifyingHost(1.f);
                }
            }
            auto motion=fx->versionedMotion();auto cfg=motion.config;
            for(unsigned i=0;i<64;++i)(void)cfg.put(i,{i%2?mod::Source::macro1:mod::Source::lfo1,
                i%3==0?mod::Destination::grainRate:i%3==1?mod::Destination::width:mod::Destination::amount1,.005f,.002f});
            if(shapes){
                cfg.event={{.21f,.78f,.46f,.11f,.64f,.3f,.73f,.42f},true,.04f,.23f,.53f,.25f};
                if(!cfg.put(0,{mod::Source::step,mod::Destination::grainRate,.17f,.01f})||
                   !cfg.put(1,{mod::Source::adsr,mod::Destination::layerGain,.2f,.002f}))return 1;
            }
            if(!fx->replaceMotionIfCurrent(motion,cfg))return 1;
        }
        juce::AudioBuffer<float> io(2,frames);juce::MidiBuffer midi;midi.ensureSize(4096);
        constexpr int blocks=160;
        std::array<double,blocks> durations{},cpuDurations{};cpuDurations.fill(-1.0);
        unsigned maximumGrains=0;double peak=0,sumL=0,sumR=0;
        std::uint32_t rng=0x154311u;bool finite=true,midiConsumed=true;
        unsigned long alloc=0,locks=0;
        const double budget=frames*1e6/rate;
        double maxCpu=0,maxWall=0,cpuAtMaxWall=-1,minPositiveCpu=0;
        int wallMisses=0,cpuMisses=0,wallOnly=0,cpuPairs=0,zeroCpuDeltas=0;
        struct Miss { int block; double wall,cpu; unsigned events,grains; };
        std::array<Miss,blocks> misses{};
        std::size_t missesRecorded=0;
        for(int block=0;block<blocks;++block){
            if(block==0)for(int i=0;i<voices;++i)midi.addEvent(juce::MidiMessage::noteOn(1,firstNote+i,juce::uint8(100)),0);
            if(!typical&&block>=48&&block%3==0)for(int j=0;j<voices*2;++j){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;
                const int when=static_cast<int>(rng%frames),note=43+static_cast<int>((rng>>9)%17),channel=1+static_cast<int>((rng>>16)%3);
                const unsigned op=(rng>>24)%8;
                if(op==0)midi.addEvent(juce::MidiMessage::controllerEvent(channel,120,0),when);
                else if(op==1)midi.addEvent(juce::MidiMessage::controllerEvent(channel,123,0),when);
                else if(op==2)midi.addEvent(juce::MidiMessage::noteOn(channel,note,juce::uint8(0)),when);
                else if(op==3)midi.addEvent(juce::MidiMessage::noteOff(channel,note),when);
                else midi.addEvent(juce::MidiMessage::noteOn(channel,note,juce::uint8(36+(rng%90))),when);
            }
            if(!typical&&block==129)fx->panic();
            if(patternOn)clock.advance(wavefold&&block==80?0:static_cast<std::int64_t>(block)*frames,rate);
#ifdef VOX_TEST_INTERPOSE_MALLOC
            voxProbeBegin();
#endif
#ifdef VOX_FX_LOCK_PROBE
            voxFxLockBegin();
#endif
            // Exclude probe installation (which walks loaded modules on
            // Windows) from CPU timing. Counter calls remain conservatively
            // inside allocation/lock probes, outside the wall interval.
            const double cpuStart=splitTiming?threadMicros():-1;
            const auto now=std::chrono::steady_clock::now();fx->processBlock(io,midi);
            durations[block]=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-now).count();
            const double cpuEnd=splitTiming?threadMicros():-1;
#ifdef VOX_FX_LOCK_PROBE
            locks+=voxFxLockEnd();
#endif
#ifdef VOX_TEST_INTERPOSE_MALLOC
            alloc+=voxProbeEnd();
#endif
            const auto telemetry=fx->audioTelemetry();
            if(splitTiming){
                const auto wall=durations[block];
                const bool hasCpu=cpuStart>=0&&cpuEnd>=cpuStart;
                const auto cpu=hasCpu?cpuEnd-cpuStart:-1.;
                cpuDurations[block]=cpu;
                wallMisses+=wall>=budget;
                if(wall>maxWall){maxWall=wall;cpuAtMaxWall=cpu;}
                if(hasCpu){
                    ++cpuPairs;
                    maxCpu=std::max(maxCpu,cpu);
                    if(cpu==0)++zeroCpuDeltas;
                    else if(minPositiveCpu==0||cpu<minPositiveCpu)minPositiveCpu=cpu;
                    cpuMisses+=cpu>=budget;wallOnly+=wall>=budget&&cpu<budget;
                }
                if(wall>=budget||cpu>=budget)
                    misses[missesRecorded++]={block,wall,cpu,telemetry.events,telemetry.grains};
            }
            midiConsumed &= midi.isEmpty();
            maximumGrains=std::max(maximumGrains,telemetry.grains);
            for(int i=0;i<frames;++i){const auto a=io.getSample(0,i),b=io.getSample(1,i);finite &= std::isfinite(a)&&std::isfinite(b);
                peak=std::max(peak,static_cast<double>(std::max(std::abs(a),std::abs(b))));sumL+=a;sumR+=b;}
        }
        // Optional raw evidence is written after all callbacks in this cell.
        if(const char* path=std::getenv("VOX_FX_WAVEFOLD_TIMING_CSV");path&&*path){
            static bool started=false;
            std::ofstream out(path,started?std::ios::app:std::ios::trunc);
            if(!out)++errors;
            else {
                if(!started){out<<"rate_hz,frames,initial_voices,pattern,block,wall_us,thread_cpu_us,budget_us,wall_miss,cpu_miss,allocations,locks\n";started=true;}
                for(int i=0;i<blocks;++i)
                    out<<rate<<','<<frames<<','<<voices<<','<<patternOn<<','<<i<<','<<durations[i]<<','<<cpuDurations[i]<<','<<budget<<','
                       <<(durations[i]>=budget)<<','<<(cpuDurations[i]>=0&&cpuDurations[i]>=budget)<<','<<alloc<<','<<locks<<'\n';
                if(!out)++errors;
            }
        }
        const double dc=std::max(std::abs(sumL),std::abs(sumR))/(blocks*frames);
        if(!finite||!midiConsumed||!(peak>.00001&&peak<1&&dc<.01)||alloc||locks||maximumGrains>64||(voices==8&&maximumGrains!=64))++errors;
        std::sort(durations.begin(),durations.end());
        std::printf("%.0f Hz %d samples %d initial voices: peak %.6f DC %.7f grains %u alloc %lu lock %lu p50 %.2f p95 %.2f p99 %.2f max %.2f us\n",
            rate,frames,voices,peak,dc,maximumGrains,alloc,locks,durations[79],durations[151],durations[158],durations.back());
        if(splitTiming){
            if(cpuPairs==blocks){
                std::sort(cpuDurations.begin(),cpuDurations.end());
                std::printf("  thread p50 %.2f p95 %.2f p99 %.2f max %.2f us\n",
                    cpuDurations[79],cpuDurations[151],cpuDurations[158],cpuDurations.back());
            }
            std::printf("  thread max %.2f us; at wall max %.2f us thread %.2f us; misses wall/CPU/wall-only %d/%d/%d of %d\n",
                maxCpu,maxWall,cpuAtMaxWall,wallMisses,cpuMisses,wallOnly,blocks);
            std::printf("  thread CPU valid %d/%d; zero deltas %d; min positive delta %.2f us\n",
                cpuPairs,blocks,zeroCpuDeltas,minPositiveCpu);
            for(std::size_t i=0;i<missesRecorded;++i){
                const auto& m=misses[i];
                std::printf("  deadline miss: rate=%.0f frames=%d initialVoices=%d pattern=%s block=%d events=%u grains=%u wall_us=%.3f cpu_us=%.3f budget_us=%.3f wall_miss=%d cpu_miss=%d\n",
                    rate,frames,voices,patternOn?"on":"off",m.block,m.events,m.grains,m.wall,m.cpu,budget,m.wall>=budget,m.cpu>=budget);
            }
            if(cpuPairs!=blocks)++errors;
        }
    }
    }
    std::printf("FX JUCE runtime stress: %d failing cases\n",errors);return errors?1:0;
}
