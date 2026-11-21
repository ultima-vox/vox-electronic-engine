// Linux diagnostic, real NoiseRiser + fixed Pattern schedule + four existing
// production Wavefold slots. Not the plugin callback or FX-T18 qualification.
#include "engine/NoiseRiser.h"
#include "engine/Pattern.h"
#include "engine/Effects.h"
#include <array>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <memory>

static double cpuMicros() {
    timespec t{};
    if(clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t)!=0)return -1;
    return t.tv_sec*1e6+t.tv_nsec*.001;
}
int main(){
    using namespace vox::fx;
    using Clock=std::chrono::steady_clock;
    constexpr int block=64,iterations=160;constexpr double rate=96000,bpm=200;
    pattern::Pattern p;p.bars=4;p.grid=pattern::Grid::thirtysecondTriplet;
    for(auto& step:p.steps){step.enabled=true;step.ratchets=4;}
    pattern::FixedSchedule schedule;if(!pattern::scheduleFixed(p,schedule))return 1;
    std::puts("trial,initial_voices,block_index,wall_us,thread_cpu_us,budget_us,wall_miss,cpu_miss,accepted,dropped,peak");
    for(int voices:{1,4,8})for(int trial=1;trial<=3;++trial){
        auto engine=std::make_unique<NoiseRiser>();engine->prepare(rate);engine->setTempo(bpm);
        NoiseProgram program;for(auto& layer:program.layers){layer.enabled=true;layer.family=NoiseFamily::shepard;}
        if(!engine->setProgram(program))return 3;
        for(int i=0;i<voices;++i)if(!engine->noteOn(1,60+i,1,4))return 4;
        effects::Chain chain;if(!chain.prepare(rate))return 5;
        for(std::size_t i=0;i<4;++i)if(!chain.set(i,{effects::Type::wavefold,1,1}))return 6;
        std::array<float,block> left{},right{};
        for(int n=0;n<iterations;++n){
            const double ppq=static_cast<double>(n)*block*bpm/(60*rate);int cursor=0,accepted=0,dropped=0;
            const double cpuStart=cpuMicros();const auto wallStart=Clock::now();
            const bool scheduled=pattern::forEachTrigger(schedule,p.bars,ppq,bpm,rate,block,
                [&](int at,std::uint8_t note,std::uint8_t velocity) noexcept {
                    if(at>cursor)engine->render(left.data()+cursor,right.data()+cursor,at-cursor);
                    cursor=at;if(engine->noteOn(1,note,static_cast<float>(velocity)/127.f,4))++accepted;else ++dropped;
                });
            if(cursor<block)engine->render(left.data()+cursor,right.data()+cursor,block-cursor);
            chain.process(left.data(),right.data(),block);
            const auto wallEnd=Clock::now();const double cpuEnd=cpuMicros();
            if(!scheduled||cpuStart<0||cpuEnd<cpuStart)return 7;
            const double wall=std::chrono::duration<double,std::micro>(wallEnd-wallStart).count();
            const double cpu=cpuEnd-cpuStart,budget=block*1e6/rate;float peak=0;
            for(auto x:left){if(!std::isfinite(x))return 8;peak=std::max(peak,std::abs(x));}
            std::printf("%d,%d,%d,%.3f,%.3f,%.3f,%d,%d,%d,%d,%.8f\n",
                trial,voices,n,wall,cpu,budget,wall>budget,cpu>budget,accepted,dropped,peak);
        }
    }
}
