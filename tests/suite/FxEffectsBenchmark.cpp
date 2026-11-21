#include "engine/NoiseRiser.h"
#include "engine/Effects.h"
#include "engine/MacroRouting.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>
int main(){
    using Clock=std::chrono::steady_clock;
    constexpr double rate=96000;constexpr int block=128,iterations=400;
    for(auto effect:{vox::fx::effects::Type::bypass,vox::fx::effects::Type::lowpass,
                     vox::fx::effects::Type::drive,vox::fx::effects::Type::wavefold,
                     vox::fx::effects::Type::bitcrush,vox::fx::effects::Type::ring,vox::fx::effects::Type::gate,vox::fx::effects::Type::phaser,vox::fx::effects::Type::flanger,vox::fx::effects::Type::delay,vox::fx::effects::Type::chorus,vox::fx::effects::Type::reverb}){
        auto engine=std::make_unique<vox::fx::NoiseRiser>();engine->prepare(rate);
        vox::fx::NoiseProgram program;for(auto& layer:program.layers){layer.enabled=true;layer.family=vox::fx::NoiseFamily::shepard;}
        if(!engine->setProgram(program))return 1;
        for(int i=0;i<8;++i)if(!engine->noteOn(1,60+i,1,4))return 1;
        vox::fx::effects::Chain chain;if(!chain.prepare(rate))return 1;
        for(std::size_t i=0;i<4;++i)if(!chain.set(i,{effect,1,.7f}))return 1;
        std::array<float,block> l{},r{};std::vector<double> times;times.reserve(iterations);double checksum=0;
        for(int n=0;n<iterations;++n){const auto start=Clock::now();engine->render(l.data(),r.data(),block);chain.process(l.data(),r.data(),block);
            const auto end=Clock::now();times.push_back(std::chrono::duration<double,std::micro>(end-start).count());checksum+=l[block-1];}
        std::sort(times.begin(),times.end());
        std::printf("96k Shepard 8x4 + 4x effect %u: p50 %.2f p95 %.2f p99 %.2f max %.2f us; checksum %.6f\n",
                    static_cast<unsigned>(effect),times[iterations/2],times[iterations*95/100],times[iterations*99/100],times.back(),checksum);
    }
    {
        auto engine=std::make_unique<vox::fx::NoiseRiser>();engine->prepare(rate);
        vox::fx::NoiseProgram p;for(auto& layer:p.layers){layer.enabled=true;layer.family=vox::fx::NoiseFamily::shepard;}
        if(!engine->setProgram(p))return 1;
        for(int i=0;i<8;++i)if(!engine->noteOn(1,60+i,1,4))return 1;
        vox::fx::effects::Chain chain;chain.prepare(rate);for(std::size_t i=0;i<4;++i)chain.set(i,{vox::fx::effects::Type::drive,1,.7f});
        vox::fx::macro::Config config;config.count=64;for(auto& route:config.routes)route={vox::fx::mod::Source::macro1,vox::fx::mod::Destination::gain,.01f,.01f};
        vox::fx::macro::Runtime runtime;runtime.prepare(rate);runtime.setConfig(config);std::array<float,8> macros{};macros.fill(.7f);
        std::array<float,block> l{},r{};std::vector<double> times;times.reserve(iterations);double checksum=0;
        for(int n=0;n<iterations;++n){const auto start=Clock::now();engine->render(l.data(),r.data(),block);chain.process(l.data(),r.data(),block);
            for(int i=0;i<block;++i){const float gain=runtime.process(.8f,macros);l[i]*=gain;r[i]*=gain;}
            const auto end=Clock::now();times.push_back(std::chrono::duration<double,std::micro>(end-start).count());checksum+=l[block-1];}
        std::sort(times.begin(),times.end());
        std::printf("96k Shepard 8x4 + 4x drive + 64 routes: p50 %.2f p95 %.2f p99 %.2f max %.2f us; checksum %.6f\n",
                    times[iterations/2],times[iterations*95/100],times[iterations*99/100],times.back(),checksum);
    }
    // Focused slot cost without the Shepard generator, which otherwise masks
    // a single effect's cost in the whole-engine rows above. Four serial
    // stereo slots, fixed amount, 128-sample blocks at 96 kHz.
    {
        constexpr int slotIterations=20000;
        for(auto type:{vox::fx::effects::Type::drive,vox::fx::effects::Type::wavefold})
            for(float amount:{.5f,1.0f}){
                vox::fx::effects::Chain chain;if(!chain.prepare(rate))return 1;
                for(std::size_t i=0;i<4;++i)if(!chain.set(i,{type,1,amount}))return 1;
                std::array<float,block> l{},r{};
                const auto fill=[&](int n){
                    for(int i=0;i<block;++i){const auto t=static_cast<double>(n*block+i);
                        l[i]=static_cast<float>(.31*std::sin(t*.017)+.12*std::sin(t*.101));
                        r[i]=static_cast<float>(.27*std::sin(t*.013+.7)+.09*std::sin(t*.071));}
                };
                for(int n=0;n<1000;++n){fill(n);chain.process(l.data(),r.data(),block);}
                std::vector<double> times;times.reserve(slotIterations);double checksum=0;
                for(int n=1000;n<1000+slotIterations;++n){fill(n);
                    const auto start=Clock::now();chain.process(l.data(),r.data(),block);
                    const auto end=Clock::now();times.push_back(std::chrono::duration<double,std::micro>(end-start).count());
                    checksum+=l[block-1];}
                std::sort(times.begin(),times.end());
                const auto median=times[slotIterations/2];
                std::printf("96k 4x slot-only %s amount %.1f: p50 %.2f p95 %.2f p99 %.2f max %.2f us; %.2f ns/sample/channel/slot; checksum %.6f\n",
                    type==vox::fx::effects::Type::drive?"drive":"wavefold",amount,
                    median,times[slotIterations*95/100],times[slotIterations*99/100],times.back(),
                    median*1000.0/(block*2.0*4.0),checksum);
            }
    }
}
