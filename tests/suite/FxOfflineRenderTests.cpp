#include "engine/OfflineRender.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <memory>
int main(){
    int fails=0;auto check=[&](bool b,const char* s){if(!b){std::printf("FAIL: %s\n",s);++fails;}};
    vox::fx::NoiseProgram program;vox::fx::offline::Settings s;s.duration=.05f;s.sampleRate=48000;
    std::vector<std::uint8_t> wav;std::atomic<float> progress{0};
    check(vox::fx::offline::renderWav(program,s,wav,nullptr,&progress),"PCM render");
    const auto frames=static_cast<std::uint32_t>(std::ceil(static_cast<double>(s.duration)*s.sampleRate));
    auto word=[&](std::size_t at){return static_cast<std::uint32_t>(wav[at])|(static_cast<std::uint32_t>(wav[at+1])<<8)|(static_cast<std::uint32_t>(wav[at+2])<<16)|(static_cast<std::uint32_t>(wav[at+3])<<24);};
    check(wav.size()==44+frames*6 && word(4)==wav.size()-8 && word(40)==frames*6 && wav[20]==1 && wav[34]==24,"PCM WAV header and frame count");
    check(progress.load()==1,"progress reaches completion");
    {
        auto reference=std::make_unique<vox::fx::NoiseRiser>();reference->prepare(s.sampleRate);reference->setProgram(program);reference->noteOn(1,s.note,s.velocity,s.duration);
        float l[512]{},r[512]{};reference->render(l,r,512);double error=0,energy=0;
        for(std::size_t i=0;i<512;++i){const std::size_t at=44+i*6;
            std::int32_t pcm=static_cast<std::int32_t>(wav[at]|(static_cast<std::uint32_t>(wav[at+1])<<8)|(static_cast<std::uint32_t>(wav[at+2])<<16));
            if(pcm&0x800000)pcm-=0x1000000;
            const double decoded=pcm/8388607.0;const double difference=decoded-l[i];error+=difference*difference;energy+=l[i]*l[i];}
        check(energy>0&&std::sqrt(error/512)<1e-7,"24-bit decoded waveform matches engine");
    }
    const auto old=wav;std::atomic<bool> cancel{true};check(!vox::fx::offline::renderWav(program,s,wav,&cancel)&&wav==old,"cancel is transactional");
    s.format=vox::fx::offline::WavFormat::float32;check(vox::fx::offline::renderWav(program,s,wav),"float render");
    check(wav.size()==56+frames*8 && word(4)==wav.size()-8 && wav[20]==3 && wav[34]==32 && word(52)==frames*8,"float WAV header and frame count");
    auto engine=std::make_unique<vox::fx::NoiseRiser>();engine->prepare(s.sampleRate);engine->setProgram(program);engine->noteOn(1,s.note,s.velocity,s.duration);
    float l[512]{},r[512]{};engine->render(l,r,512);
    for(std::size_t i=0;i<512;++i){const auto v=std::bit_cast<float>(word(56+i*8));check(v==l[i],"float WAV uses exact realtime DSP");}
    s.duration=33;const auto previous=wav;check(!vox::fx::offline::renderWav(program,s,wav)&&wav==previous,"invalid settings transactional");
    std::printf("FX offline WAV: %d failure(s)\n",fails);return fails?1:0;
}
