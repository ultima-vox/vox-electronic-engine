#include "Processor.h"
#include "content/FactoryCandidates.h"
#include "FxLockProbe.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <memory>
#include <string>
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
#ifdef VOX_TEST_INTERPOSE_MALLOC
#include "AllocationProbe.h"
#endif

namespace {
constexpr int blocksPerCase = 160;
using Samples = std::array<double, blocksPerCase>;

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

std::string cleanText(const juce::String& value) {
    auto result=value.toStdString();
    for(auto& character:result)
        if(character=='\t'||character=='\r'||character=='\n')character=' ';
    return result;
}

void writeMeta(std::ofstream& report,const char* key,const std::string& value) {
    report << "meta\t" << key << '\t' << value << '\n';
}

template <typename Value>
void writeMeta(std::ofstream& report,const char* key,Value value) {
    report << "meta\t" << key << '\t' << value << '\n';
}

double percentile(const Samples& source,std::size_t count,std::size_t index) {
    if(count<=index)return -1.0;
    auto sorted=source;
    std::sort(sorted.begin(),sorted.begin()+static_cast<std::ptrdiff_t>(count));
    return sorted[index];
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
    const char* requestedReport=std::getenv("VOX_FX_REPORT");
    const bool wantsReport=requestedReport!=nullptr&&*requestedReport!='\0';
    const char* profile=typical?"typical":(shapes?"authored":"routed");
#ifdef VOX_FX_LOCK_PROBE
    const bool lockProbeAvailable=voxFxLockAvailable()!=0;
#else
    const bool lockProbeAvailable=false;
#endif

    if(splitTiming&&threadMicros()<0){std::fputs("Requested thread CPU clock unavailable\n",stderr);return 2;}
    if(wantsReport&&!splitTiming){
        std::fputs("VOX_FX_REPORT requires VOX_FX_TIMING_THREAD_CPU=1\n",stderr);
        return 2;
    }

    bool allocationProbeAvailable=false;
#ifdef VOX_TEST_INTERPOSE_MALLOC
    allocationProbeAvailable=voxProbeAvailable()!=0;
#endif

    std::ofstream report;
    if(wantsReport){
        report.open(requestedReport,std::ios::out|std::ios::trunc);
        if(!report){
            std::fprintf(stderr,"Unable to open FX timing report: %s\n",requestedReport);
            return 2;
        }
        report << "# VOX_FX_T18_REPORT_V1\n";
        writeMeta(report,"format","VOX_FX_T18_REPORT_V1");
        writeMeta(report,"profile",profile);
        writeMeta(report,"source_sha",std::getenv("VOX_FX_SOURCE_SHA")?std::getenv("VOX_FX_SOURCE_SHA"):"not-supplied");
        writeMeta(report,"platform",cleanText(juce::SystemStats::getOperatingSystemName()));
        writeMeta(report,"cpu_model",cleanText(juce::SystemStats::getCpuModel()));
        writeMeta(report,"logical_cpus",juce::SystemStats::getNumCpus());
        writeMeta(report,"pointer_bits",sizeof(void*)==8?64:32);
#if JUCE_WINDOWS
        writeMeta(report,"timing_clock","GetThreadTimes");
#else
        writeMeta(report,"timing_clock","CLOCK_THREAD_CPUTIME_ID");
#endif
        writeMeta(report,"timing_requested",splitTiming?"1":"0");
        writeMeta(report,"allocation_probe",allocationProbeAvailable?"available":"unavailable");
        writeMeta(report,"lock_probe",lockProbeAvailable?"available":"unavailable");
        writeMeta(report,"wait_probe",lockProbeAvailable?"available":"unavailable");
        writeMeta(report,"blocks_per_case",blocksPerCase);
        writeMeta(report,"matrix","44100,48000,96000 Hz x 64,128,512 frames x 1,4,8 voices x Pattern off,on (typical is reduced)");
        report << "schema\tcase\tcase_id\tprofile\trate_hz\tframes\tinitial_voices\tpattern\tblocks\tbudget_us\talloc_calls\tlock_entries\twait_entries\tcpu_pairs\tzero_cpu_deltas\tmin_positive_cpu_us\twall_misses\tcpu_misses\twall_only_misses\tmax_grains\tpeak\tdc\twall_p50_us\twall_p95_us\twall_p99_us\twall_max_us\tcpu_p50_us\tcpu_p95_us\tcpu_p99_us\tcpu_max_us\tcpu_at_wall_max_us\n";
        report << "schema\tblock\tcase_id\tblock\twall_us\tcpu_us\tbudget_us\twall_miss\tcpu_miss\tevents\tgrains\n";
    }

    if(typical)std::puts("Typical profile: one triggered curated riser, no synchronized pattern or randomized MIDI");
    if(shapes&&!typical)std::puts("Non-default grain shape, step and held ADSR routes");
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
#ifdef VOX_TEST_INTERPOSE_MALLOC
    std::printf("Allocation probe available: %s\n",allocationProbeAvailable?"yes":"no");
#else
    std::puts("Allocation probe unavailable");
#endif
#if defined(VOX_FX_LOCK_PROBE)
    std::puts("Lock probe: platform synchronization and wait entry points");
#else
    std::puts("Lock probe unavailable");
#endif

    for(bool patternOn:{false,true}){
    if(typical&&patternOn)continue;
    std::puts(patternOn?"Max-ratchet synchronized Pattern + random MIDI":"Random MIDI without Pattern");
    for(double rate:{44100.,48000.,96000.})for(int frames:{64,128,512})for(int voices:{1,4,8}){
        if(typical&&voices!=1)continue;
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
            for(std::size_t i=0;i<4;++i)fx->setEffect(i,{effects::Type::drive,1,.7f});
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
        Samples durations{};Samples cpuDurations{};Samples cpuByBlock{};cpuByBlock.fill(-1.0);
        std::array<unsigned,blocksPerCase> eventCounts{};
        std::array<unsigned,blocksPerCase> grainCounts{};unsigned maximumGrains=0;double peak=0,sumL=0,sumR=0;
        std::uint32_t rng=0x154311u;bool finite=true,midiConsumed=true;
        unsigned long alloc=0,locks=0,waits=0;std::size_t cpuCount=0;
        const double budget=frames*1e6/rate;
        double maxWall=0,cpuAtMaxWall=-1,minPositiveCpu=0;
        int wallMisses=0,cpuMisses=0,wallOnly=0,cpuPairs=0,zeroCpuDeltas=0;
        for(int block=0;block<blocksPerCase;++block){
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
            if(patternOn)clock.advance(static_cast<std::int64_t>(block)*frames,rate);
#ifdef VOX_FX_LOCK_PROBE
            voxFxLockPrepare();
#endif
#ifdef VOX_TEST_INTERPOSE_MALLOC
            voxProbeBegin();
#endif
#ifdef VOX_FX_LOCK_PROBE
            voxFxLockBegin();
#endif
            const auto now=std::chrono::steady_clock::now();
            const double cpuStart=splitTiming?threadMicros():-1;
            fx->processBlock(io,midi);
            const double cpuEnd=splitTiming?threadMicros():-1;
            durations[block]=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-now).count();
            unsigned long lockCount=0,waitCount=0;
#ifdef VOX_FX_LOCK_PROBE
            lockCount=voxFxLockEnd();
            waitCount=voxFxWaitEnd();
#endif
            locks+=lockCount;waits+=waitCount;
#ifdef VOX_TEST_INTERPOSE_MALLOC
            alloc+=voxProbeEnd();
#endif
            const auto telemetry=fx->audioTelemetry();
            eventCounts[block]=telemetry.events;grainCounts[block]=telemetry.grains;
            if(splitTiming){
                const auto wall=durations[block];
                const bool hasCpu=cpuStart>=0&&cpuEnd>=cpuStart;
                const auto cpu=hasCpu?cpuEnd-cpuStart:-1.;
                wallMisses+=wall>budget;
                if(wall>maxWall){maxWall=wall;cpuAtMaxWall=cpu;}
                cpuByBlock[static_cast<std::size_t>(block)]=cpu;
                if(hasCpu){
                    ++cpuPairs;cpuDurations[cpuCount++]=cpu;
                    if(cpu==0)++zeroCpuDeltas;
                    else if(minPositiveCpu==0||cpu<minPositiveCpu)minPositiveCpu=cpu;
                    cpuMisses+=cpu>budget;wallOnly+=wall>budget&&cpu<=budget;
                }
            }
            midiConsumed &= midi.isEmpty();
            maximumGrains=std::max(maximumGrains,telemetry.grains);
            for(int i=0;i<frames;++i){const auto a=io.getSample(0,i),b=io.getSample(1,i);finite &= std::isfinite(a)&&std::isfinite(b);
                peak=std::max(peak,static_cast<double>(std::max(std::abs(a),std::abs(b))));sumL+=a;sumR+=b;}
        }
        const double dc=std::max(std::abs(sumL),std::abs(sumR))/(blocksPerCase*frames);
        const auto wallMissesFromSamples=[&]{int count=0;for(const auto value:durations)count+=value>budget;return count;};
        wallMisses=wallMissesFromSamples();
        if(!finite||!midiConsumed||!(peak>.00001&&peak<1&&dc<.01)||alloc||locks||waits||maximumGrains>64||(voices==8&&maximumGrains!=64))++errors;
        if(splitTiming&&cpuPairs!=blocksPerCase)++errors;
        if(splitTiming&&(!allocationProbeAvailable||!lockProbeAvailable))++errors;

        const auto wallP50=percentile(durations,blocksPerCase,79);
        const auto wallP95=percentile(durations,blocksPerCase,151);
        const auto wallP99=percentile(durations,blocksPerCase,158);
        const auto wallMax=percentile(durations,blocksPerCase,159);
        const auto cpuP50=percentile(cpuDurations,cpuCount,79);
        const auto cpuP95=percentile(cpuDurations,cpuCount,151);
        const auto cpuP99=percentile(cpuDurations,cpuCount,158);
        const auto cpuMax=percentile(cpuDurations,cpuCount,159);
        char caseId[96];std::snprintf(caseId,sizeof(caseId),"r%.0f_f%d_v%d_p%s",rate,frames,voices,patternOn?"on":"off");
        std::printf("%.0f Hz %d samples %d initial voices: peak %.6f DC %.7f grains %u alloc %lu lock %lu wait %lu p50 %.2f p95 %.2f p99 %.2f max %.2f us\n",
            rate,frames,voices,peak,dc,maximumGrains,alloc,locks,waits,wallP50,wallP95,wallP99,wallMax);
        if(splitTiming){
            std::printf("  thread CPU p50 %.2f p95 %.2f p99 %.2f max %.2f us; at wall max %.2f us thread %.2f us; misses wall/CPU/wall-only %d/%d/%d of %d\n",
                cpuP50,cpuP95,cpuP99,cpuMax,wallMax,cpuAtMaxWall,wallMisses,cpuMisses,wallOnly,blocksPerCase);
            std::printf("  thread CPU valid %d/%d; zero deltas %d; min positive delta %.2f us\n",
                cpuPairs,blocksPerCase,zeroCpuDeltas,minPositiveCpu);
            for(int block=0;block<blocksPerCase;++block){
                const auto cpu=cpuByBlock[static_cast<std::size_t>(block)];
                if(durations[block]>budget||cpu>budget)
                    std::printf("  deadline miss: rate=%.0f frames=%d initialVoices=%d pattern=%s block=%d events=%u grains=%u wall_us=%.3f cpu_us=%.3f budget_us=%.3f wall_miss=%d cpu_miss=%d\n",
                        rate,frames,voices,patternOn?"on":"off",block,eventCounts[block],grainCounts[block],durations[block],cpu,budget,durations[block]>budget,cpu>budget);
            }
        }
        if(report){
            report << "case\t" << caseId << '\t' << profile << '\t' << std::fixed << std::setprecision(9)
                   << static_cast<int>(rate) << '\t' << frames << '\t' << voices << '\t' << (patternOn?"on":"off") << '\t'
                   << blocksPerCase << '\t' << budget << '\t' << alloc << '\t' << locks << '\t' << waits << '\t'
                   << cpuPairs << '\t' << zeroCpuDeltas << '\t' << minPositiveCpu << '\t' << wallMisses << '\t'
                   << cpuMisses << '\t' << wallOnly << '\t' << maximumGrains << '\t' << peak << '\t' << dc << '\t'
                   << wallP50 << '\t' << wallP95 << '\t' << wallP99 << '\t' << wallMax << '\t'
                   << cpuP50 << '\t' << cpuP95 << '\t' << cpuP99 << '\t' << cpuMax << '\t' << cpuAtMaxWall << '\n';
            for(int block=0;block<blocksPerCase;++block){
                const auto cpu=cpuByBlock[static_cast<std::size_t>(block)];
                report << "block\t" << caseId << '\t' << block << '\t' << durations[block] << '\t' << cpu << '\t'
                       << budget << '\t' << (durations[block]>budget) << '\t' << (cpu>budget) << '\t'
                       << eventCounts[block] << '\t' << grainCounts[block] << '\n';
            }
        }
    }
    }
    if(report){
        report.flush();
        if(!report){
            std::fprintf(stderr,"FX timing report write failed: %s\n",requestedReport);
            return 2;
        }
    }
    std::printf("FX JUCE runtime stress: %d failing cases\n",errors);return errors?1:0;
}
