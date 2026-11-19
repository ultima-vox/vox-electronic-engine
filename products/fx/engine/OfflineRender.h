#pragma once
#include "NoiseRiser.h"
#include "Effects.h"
#include "MacroRouting.h"
#include "PostMotion.h"
#include "Pattern.h"
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
    pattern::Pattern pattern{};
};
inline void u16(std::vector<std::uint8_t>& out,std::uint16_t x){out.push_back(static_cast<std::uint8_t>(x));out.push_back(static_cast<std::uint8_t>(x>>8));}
inline void u32(std::vector<std::uint8_t>& out,std::uint32_t x){for(int shift=0;shift<32;shift+=8)out.push_back(static_cast<std::uint8_t>(x>>shift));}
inline void tag(std::vector<std::uint8_t>& out,const char* s){for(int i=0;i<4;++i)out.push_back(static_cast<std::uint8_t>(s[i]));}
// Export one finite pass or one Sequence cycle. Fixed tempo/PPQ zero, never
// a recorded host tempo map. Sequence lets its last admitted voice finish.
inline std::uint32_t frameCount(const NoiseProgram& program, const Settings& settings,
                                const pattern::Pattern* sequence = nullptr) noexcept {
    if (!program.valid() || !settings.valid()) return 0;
    if (program.mode == PlayMode::sequence && (!sequence || !sequence->valid() || settings.tempo < 10)) return 0;
    const double event = program.bars ? program.bars * 240.0 / settings.tempo : settings.duration;
    const double cycle = program.mode == PlayMode::sequence ? sequence->bars * 240.0 / settings.tempo : 0;
    const double duration = event + cycle;
    bool shortTail = false;
    double serialTail = 0;
    for (std::size_t slot=0;slot<settings.effects.size();++slot) {
        const auto& effect=settings.effects[slot];
        const auto amountDest=static_cast<mod::Destination>(macro::Runtime::at(mod::Destination::amount1)+2*slot);
        const bool modulatedAmount=std::any_of(settings.macroRoutes.routes.begin(),settings.macroRoutes.routes.begin()+settings.macroRoutes.count,
            [amountDest](const auto& route){return route.destination==amountDest&&route.depth!=0;});
        switch (effect.type) {
            case effects::Type::lowpass: case effects::Type::phaser:
            case effects::Type::flanger: case effects::Type::chorus: shortTail = true; break;
            case effects::Type::delay: serialTail += effect.amount > 0 || modulatedAmount ? effects::feedbackDelayTailSeconds : 0.5; break;
            case effects::Type::reverb: serialTail += 2.0; break;
            default: break;
        }
    }
    const double total = std::ceil(duration * settings.sampleRate) +
        std::ceil((serialTail + (shortTail ? 1.0 : 0.0)) * settings.sampleRate);
    const unsigned align = settings.format == WavFormat::float32 ? 8 : 6;
    constexpr unsigned maxPayload = 128u * 1024u * 1024u;
    return total <= maxPayload / align ? static_cast<std::uint32_t>(total) : 0;
}

// Worker thread only. Output is transactional: invalid input, cancellation or
// non-finite DSP leave the old bytes unchanged.
inline bool renderWavImpl(const NoiseProgram& program,const Settings& settings,std::vector<std::uint8_t>& output,
                          const std::atomic<bool>* cancel,std::atomic<float>* progress,const Capture* capture,std::uint32_t limit=0) {
    const auto* sequence = program.mode == PlayMode::sequence && capture ? &capture->pattern : nullptr;
    const auto fullFrames = frameCount(program, settings, sequence);
    const auto frames = limit==0?fullFrames:std::min(limit,fullFrames);
    if (frames == 0 || (cancel && cancel->load(std::memory_order_relaxed))) return false;
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
    pattern::FixedSchedule schedule;
    std::uint64_t cycleFrames = 0;
    if (sequence && (!pattern::scheduleFixed(*sequence,schedule) || schedule.count == 0 ||
        !pattern::tickToSample(sequence->bars*4u*pattern::ppq,settings.tempo,settings.sampleRate,cycleFrames))) return false;
    const auto grid = timeline::BarGrid::from(0, settings.tempo, settings.sampleRate);
    if (program.bars) {
        engine->setMusicalClockAvailable(true, settings.tempo);
        engine->setBarGrid(grid);
    }
    if(!engine->setVoiceMotion(settings.macroRoutes))return false;
    engine->setMacroBases(settings.macros);
    const bool useUser=capture&&capture->useUserSample;
    if(useUser && (!sampleStore.publish(capture->sample,capture->sampleRate)||!engine->attachSampleStore(&sampleStore)))return false;
    if(!engine->setProgram(program))return false;
    if(!sequence&&!engine->noteOn(1,settings.note,settings.velocity,settings.duration,useUser,0,TriggerSource::preview))return false;
    effects::Chain chain;if(!chain.prepare(settings.sampleRate))return false;
    macro::Runtime macroRuntime;if(!macroRuntime.prepare(settings.sampleRate)||!macroRuntime.setConfig(settings.macroRoutes))return false;
    macroRuntime.setTempo(settings.tempo);
    for(std::size_t i=0;i<settings.effects.size();++i)if(!chain.set(i,settings.effects[i]))return false;
    std::array<float,512> left{},right{};
    if(progress)progress->store(0,std::memory_order_relaxed);
    for(std::uint32_t at=0;at<frames;){
        if(cancel&&cancel->load(std::memory_order_relaxed))return false;
        const auto count=std::min<std::uint32_t>(512,frames-at);
        int cursor = 0;
        const auto renderTo = [&](int end) noexcept {
            if (end > cursor) engine->render(left.data()+cursor,right.data()+cursor,end-cursor);
            cursor = end;
        };
        if (sequence && at < cycleFrames) {
            const auto limit = static_cast<int>(std::min<std::uint64_t>(count,cycleFrames-at));
            if (!pattern::forEachTrigger(schedule,sequence->bars,grid.at(at),settings.tempo,settings.sampleRate,limit,
                [&](int offset,std::uint8_t note,std::uint8_t velocity) noexcept {
                    renderTo(offset);
                    (void)engine->noteOn(1,note,static_cast<float>(velocity)/127.0f,settings.duration,useUser,0,TriggerSource::pattern);
                })) return false;
        }
        renderTo(static_cast<int>(count));
        macro::renderPost(chain,macroRuntime,left.data(),right.data(),count,settings.macros,[&]{return settings.gain;});
        for(std::uint32_t i=0;i<count;++i){
          for(float value:{left[i],right[i]}){if(!std::isfinite(value)||std::abs(value)>1)return false;
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
