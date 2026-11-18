#pragma once
#include "NoiseRiser.h"
#include "Effects.h"
#include "MacroRouting.h"
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace vox::fx::offline {
enum class WavFormat { pcm24, float32 };
struct Settings {
    std::uint32_t sampleRate=48000;
    float duration=4, velocity=1, gain=1;
    double tempo=120;
    int note=60;
    WavFormat format=WavFormat::pcm24;
    std::array<effects::Settings,4> effects{};
    macro::Config macroRoutes{};
    std::array<float,8> macros{.5f,.5f,.5f,.5f,.5f,.5f,.5f,.5f};
    bool valid() const noexcept {
        for(const auto& effect:effects)if(!effect.valid())return false;
        if(!macroRoutes.valid())return false;
        for(float value:macros)if(!std::isfinite(value)||value<0||value>1)return false;
        return sampleRate>=8000&&sampleRate<=384000&&std::isfinite(duration)&&duration>=.05f&&duration<=32&&
               std::isfinite(tempo)&&tempo>=1&&tempo<=1000&&std::isfinite(velocity)&&velocity>0&&velocity<=1&&std::isfinite(gain)&&gain>=0&&gain<=1&&
               note>=0&&note<128&&(format==WavFormat::pcm24||format==WavFormat::float32);
    }
};
struct Capture {
    NoiseProgram program{};
    Settings settings{};
    bool useUserSample=false;
    std::uint32_t sampleRate=0;
    std::vector<float> sample; // immutable worker-owned copy, not a lease
};
inline void u16(std::vector<std::uint8_t>& out,std::uint16_t x){out.push_back(static_cast<std::uint8_t>(x));out.push_back(static_cast<std::uint8_t>(x>>8));}
inline void u32(std::vector<std::uint8_t>& out,std::uint32_t x){for(int shift=0;shift<32;shift+=8)out.push_back(static_cast<std::uint8_t>(x>>shift));}
inline void tag(std::vector<std::uint8_t>& out,const char* s){for(int i=0;i<4;++i)out.push_back(static_cast<std::uint8_t>(s[i]));}
// Worker thread only. Renders the same event engine and shared effects;
// allow one second of short-effect decay, 0.5 s per serial delay and
// two seconds per serial reverb.
// Output is transactional:
// invalid input, cancellation or non-finite DSP leave the old bytes unchanged.
inline bool renderWavImpl(const NoiseProgram& program,const Settings& settings,std::vector<std::uint8_t>& output,
                          const std::atomic<bool>* cancel,std::atomic<float>* progress,const Capture* capture) {
    if(!program.valid()||!settings.valid())return false;
    bool hasShortTail=false;unsigned delays=0,reverbs=0;
    for(const auto& effect:settings.effects){
        if(effect.type==effects::Type::lowpass||effect.type==effects::Type::phaser||effect.type==effects::Type::flanger||effect.type==effects::Type::chorus)hasShortTail=true;
        if(effect.type==effects::Type::delay)++delays;
        if(effect.type==effects::Type::reverb)++reverbs;
    }
    const auto tail=std::ceil(settings.sampleRate*((hasShortTail?1.0:0.0)+.5*delays+2.0*reverbs));
    const auto frames=static_cast<std::uint32_t>(std::ceil(static_cast<double>(settings.duration)*settings.sampleRate)+tail);
    const bool floating=settings.format==WavFormat::float32;
    const std::uint16_t bits=floating?32:24, bytesPerSample=bits/8, blockAlign=static_cast<std::uint16_t>(2*bytesPerSample);
    const auto dataBytes=frames*blockAlign;
    const auto riffSize=4u+24u+(floating?12u:0u)+8u+dataBytes;
    std::vector<std::uint8_t> bytes;bytes.reserve(static_cast<std::size_t>(riffSize)+8);
    tag(bytes,"RIFF");u32(bytes,riffSize);tag(bytes,"WAVE");tag(bytes,"fmt ");u32(bytes,16);
    u16(bytes,floating?3:1);u16(bytes,2);u32(bytes,settings.sampleRate);u32(bytes,settings.sampleRate*blockAlign);
    u16(bytes,blockAlign);u16(bytes,bits);
    if(floating){tag(bytes,"fact");u32(bytes,4);u32(bytes,frames);}
    tag(bytes,"data");u32(bytes,dataBytes);
    sample::Store sampleStore; // outlives engine and its event leases
    auto engine=std::make_unique<NoiseRiser>();engine->prepare(settings.sampleRate);
    if(!engine->setTempo(settings.tempo))return false;
    const bool useUser=capture&&capture->useUserSample;
    if(useUser && (!sampleStore.publish(capture->sample,capture->sampleRate)||!engine->attachSampleStore(&sampleStore)))return false;
    if(!engine->setProgram(program)||!engine->noteOn(1,settings.note,settings.velocity,settings.duration,useUser))return false;
    effects::Chain chain;if(!chain.prepare(settings.sampleRate))return false;
    macro::Runtime macroRuntime;if(!macroRuntime.prepare(settings.sampleRate)||!macroRuntime.setConfig(settings.macroRoutes))return false;
    for(std::size_t i=0;i<settings.effects.size();++i)if(!chain.set(i,settings.effects[i]))return false;
    std::array<float,512> left{},right{};
    if(progress)progress->store(0,std::memory_order_relaxed);
    for(std::uint32_t at=0;at<frames;){
        if(cancel&&cancel->load(std::memory_order_relaxed))return false;
        const auto count=std::min<std::uint32_t>(512,frames-at);
        engine->render(left.data(),right.data(),static_cast<int>(count));
        chain.process(left.data(),right.data(),count);
        for(std::uint32_t i=0;i<count;++i){
          const auto gain=macroRuntime.processStereo(settings.gain,settings.macros);
          for(float value:{left[i]*gain.left,right[i]*gain.right}){if(!std::isfinite(value)||std::abs(value)>1)return false;
            if(floating)u32(bytes,std::bit_cast<std::uint32_t>(value));
            else {const auto pcm=static_cast<std::int32_t>(std::lround(value*8388607.0f));
                const auto word=static_cast<std::uint32_t>(pcm);bytes.push_back(static_cast<std::uint8_t>(word));
                bytes.push_back(static_cast<std::uint8_t>(word>>8));bytes.push_back(static_cast<std::uint8_t>(word>>16));}
          }
        }
        at+=count;if(progress)progress->store(static_cast<float>(at)/frames,std::memory_order_relaxed);
    }
    output=std::move(bytes);return true;
}
inline bool renderWav(const NoiseProgram& program,const Settings& settings,std::vector<std::uint8_t>& output,
                      const std::atomic<bool>* cancel=nullptr,std::atomic<float>* progress=nullptr) {
    return renderWavImpl(program,settings,output,cancel,progress,nullptr);
}
inline bool renderWav(const Capture& capture,std::vector<std::uint8_t>& output,
                      const std::atomic<bool>* cancel=nullptr,std::atomic<float>* progress=nullptr) {
    return renderWavImpl(capture.program,capture.settings,output,cancel,progress,&capture);
}
} // namespace vox::fx::offline
