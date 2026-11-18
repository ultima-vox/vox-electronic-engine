#include "engine/NoiseRiser.h"
#include "engine/Effects.h"
#include "engine/MacroRouting.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <memory>
#include <vector>
int main(){
    using Clock=std::chrono::steady_clock;
    constexpr double rate=96000;constexpr int block=128,iterations=400;
    for(auto effect:{vox::fx::effects::Type::bypass,vox::fx::effects::Type::lowpass,
                     vox::fx::effects::Type::drive,vox::fx::effects::Type::wavefold,
                     vox::fx::effects::Type::bitcrush,vox::fx::effects::Type::ring,vox::fx::effects::Type::gate,vox::fx::effects::Type::phaser}){
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
}
