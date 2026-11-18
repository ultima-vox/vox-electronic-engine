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
    s.effects[0]={vox::fx::effects::Type::lowpass,1,.5f};
    check(vox::fx::offline::renderWav(program,s,wav),"effected WAV render");
    check(wav.size()==56+(frames+s.sampleRate)*8,"filter tail included");
    auto effected=std::make_unique<vox::fx::NoiseRiser>();effected->prepare(s.sampleRate);effected->setProgram(program);effected->noteOn(1,s.note,s.velocity,s.duration);
    vox::fx::effects::Chain chain;chain.prepare(s.sampleRate);chain.set(0,s.effects[0]);
    effected->render(l,r,512);chain.process(l,r,512);
    for(std::size_t i=0;i<512;++i)check(std::bit_cast<float>(word(56+i*8))==l[i],"offline effects match realtime chain");
    s.effects[0]={vox::fx::effects::Type::phaser,1,.7f};
    check(vox::fx::offline::renderWav(program,s,wav),"phaser WAV render");
    check(wav.size()==56+(frames+s.sampleRate)*8,"phaser tail included");
    auto phased=std::make_unique<vox::fx::NoiseRiser>();phased->prepare(s.sampleRate);phased->setProgram(program);phased->noteOn(1,s.note,s.velocity,s.duration);
    vox::fx::effects::Chain phaser;phaser.prepare(s.sampleRate);phaser.set(0,s.effects[0]);
    phased->render(l,r,512);phaser.process(l,r,512);
    for(std::size_t i=0;i<512;++i)check(std::bit_cast<float>(word(56+i*8))==l[i],"offline phaser matches realtime chain");
    s.effects[0]={vox::fx::effects::Type::flanger,1,.7f};
    check(vox::fx::offline::renderWav(program,s,wav),"flanger WAV render");
    check(wav.size()==56+(frames+s.sampleRate)*8,"flanger tail included");
    auto flanged=std::make_unique<vox::fx::NoiseRiser>();flanged->prepare(s.sampleRate);flanged->setProgram(program);flanged->noteOn(1,s.note,s.velocity,s.duration);
    vox::fx::effects::Chain flanger;flanger.prepare(s.sampleRate);flanger.set(0,s.effects[0]);
    flanged->render(l,r,512);flanger.process(l,r,512);
    for(std::size_t i=0;i<512;++i)check(std::bit_cast<float>(word(56+i*8))==l[i],"offline flanger matches realtime chain");
    s.effects[0]={vox::fx::effects::Type::delay,1,0};
    check(vox::fx::offline::renderWav(program,s,wav),"delay WAV render");
    check(wav.size()==56+(frames+s.sampleRate/2)*8,"single delay tail included");
    double delayEnergy=0;for(std::size_t i=frames;i<frames+s.sampleRate/2;++i)
        delayEnergy+=std::abs(std::bit_cast<float>(word(56+i*8)));
    check(delayEnergy>0,"delay tail contains event audio");
    s.effects[1]={vox::fx::effects::Type::delay,1,0};
    check(vox::fx::offline::renderWav(program,s,wav)&&wav.size()==56+(frames+s.sampleRate)*8,"serial delay tails accumulate");
    s.effects[0]={vox::fx::effects::Type::chorus,1,.7f};s.effects[1]={};
    check(vox::fx::offline::renderWav(program,s,wav)&&wav.size()==56+(frames+s.sampleRate)*8,"chorus WAV with tail");
    auto chorused=std::make_unique<vox::fx::NoiseRiser>();chorused->prepare(s.sampleRate);chorused->setProgram(program);chorused->noteOn(1,s.note,s.velocity,s.duration);
    vox::fx::effects::Chain chorus;chorus.prepare(s.sampleRate);chorus.set(0,s.effects[0]);
    chorused->render(l,r,512);chorus.process(l,r,512);
    for(std::size_t i=0;i<512;++i)check(std::bit_cast<float>(word(56+i*8))==l[i],"offline chorus matches realtime chain");
    s.effects[0]={vox::fx::effects::Type::reverb,1,.8f};
    check(vox::fx::offline::renderWav(program,s,wav),"reverb WAV render");
    check(wav.size()==56+(frames+2*s.sampleRate)*8,"reverb tail included");
    double reverbEnergy=0;for(std::size_t i=frames;i<frames+2*s.sampleRate;++i)
        reverbEnergy+=std::abs(std::bit_cast<float>(word(56+i*8)));
    check(reverbEnergy>0,"reverb tail contains event audio");
    s.effects[1]={vox::fx::effects::Type::delay,1,0};
    check(vox::fx::offline::renderWav(program,s,wav)&&wav.size()==56+(frames+5*s.sampleRate/2)*8,"reverb and delay tails accumulate");
    s.effects[1]={};
    auto reverbed=std::make_unique<vox::fx::NoiseRiser>();reverbed->prepare(s.sampleRate);reverbed->setProgram(program);reverbed->noteOn(1,s.note,s.velocity,s.duration);
    vox::fx::effects::Chain reverb;reverb.prepare(s.sampleRate);reverb.set(0,s.effects[0]);
    // Re-render without slot 2 for direct block parity.
    check(vox::fx::offline::renderWav(program,s,wav),"reverb parity WAV");
    reverbed->render(l,r,512);reverb.process(l,r,512);
    for(std::size_t i=0;i<512;++i)check(std::bit_cast<float>(word(56+i*8))==l[i],"offline reverb matches realtime chain");
    s.effects[0]={};s.macroRoutes.setGainRoute(0,-.5f,0);s.macros[0]=1;
    check(vox::fx::offline::renderWav(program,s,wav),"macro routed WAV render");
    auto macroEngine=std::make_unique<vox::fx::NoiseRiser>();macroEngine->prepare(s.sampleRate);macroEngine->setProgram(program);macroEngine->noteOn(1,s.note,s.velocity,s.duration);
    macroEngine->render(l,r,512);
    for(std::size_t i=0;i<512;++i)check(std::bit_cast<float>(word(56+i*8))==l[i]*.5f,"offline macro routing matches gain DSP");
    s.macroRoutes={};s.macroRoutes.setSourceGainRoute(vox::fx::mod::Source::lfo1,-.5f,0);s.gain=.5f;
    check(vox::fx::offline::renderWav(program,s,wav),"LFO routed WAV render");
    auto lfoEngine=std::make_unique<vox::fx::NoiseRiser>();lfoEngine->prepare(s.sampleRate);lfoEngine->setProgram(program);lfoEngine->noteOn(1,s.note,s.velocity,s.duration);
    vox::fx::macro::Runtime motion;motion.prepare(s.sampleRate);motion.setConfig(s.macroRoutes);lfoEngine->render(l,r,512);
    for(std::size_t i=0;i<512;++i)check(std::bit_cast<float>(word(56+i*8))==l[i]*motion.process(.5f,s.macros),"offline motion matches gain DSP");
    s.macroRoutes={};s.macroRoutes.setRoute(vox::fx::mod::Source::macro1,vox::fx::mod::Destination::pan,.5f,0);
    s.macros[0]=1;s.gain=.5f;
    check(vox::fx::offline::renderWav(program,s,wav),"pan routed WAV render");
    auto panEngine=std::make_unique<vox::fx::NoiseRiser>();panEngine->prepare(s.sampleRate);panEngine->setProgram(program);panEngine->noteOn(1,s.note,s.velocity,s.duration);
    panEngine->render(l,r,512);
    for(std::size_t i=0;i<512;++i){check(std::bit_cast<float>(word(56+i*8))==0,"hard-right pan mutes left");
        check(std::bit_cast<float>(word(60+i*8))==r[i]*.5f,"hard-right pan preserves right");}
    {
        vox::fx::offline::Capture capture;capture.program=program;capture.program.layers[0].family=vox::fx::NoiseFamily::granular;
        capture.settings=s;capture.settings.duration=.05f;capture.settings.gain=1;capture.settings.macroRoutes={};
        capture.useUserSample=true;capture.sampleRate=24000;capture.sample.assign(512,.7f);
        check(vox::fx::offline::renderWav(capture,wav),"captured user sample WAV render");
        vox::fx::sample::Store store;store.publish(capture.sample,capture.sampleRate);
        auto sampled=std::make_unique<vox::fx::NoiseRiser>();sampled->prepare(s.sampleRate);sampled->attachSampleStore(&store);
        sampled->setProgram(capture.program);sampled->noteOn(1,s.note,s.velocity,capture.settings.duration,true);sampled->render(l,r,512);
        for(std::size_t i=0;i<512;++i)check(std::bit_cast<float>(word(56+i*8))==l[i],"user sample WAV matches granular realtime DSP");
        capture.sample.clear();const auto saved=wav;
        check(!vox::fx::offline::renderWav(capture,wav)&&wav==saved,"missing user sample fails transactionally, no fallback");
    }
    {
        vox::fx::offline::Settings tempoSettings;tempoSettings.duration=.4f;tempoSettings.format=vox::fx::offline::WavFormat::float32;
        tempoSettings.tempo=73.125;vox::fx::NoiseProgram glitch;glitch.layers[0].family=vox::fx::NoiseFamily::glitch;
        check(vox::fx::offline::renderWav(glitch,tempoSettings,wav),"tempo captured glitch WAV");
        auto reference=std::make_unique<vox::fx::NoiseRiser>();reference->prepare(48000);reference->setTempo(tempoSettings.tempo);
        reference->setProgram(glitch);reference->noteOn(1,60,1,tempoSettings.duration);
        for(int at=0;at<19200;at+=512){const int count=std::min(512,19200-at);reference->render(l,r,count);
            for(int i=0;i<count;++i)check(std::bit_cast<float>(word(56+static_cast<std::size_t>(at+i)*8))==l[i],
                "offline captured tempo matches realtime glitch");}
        const auto saved=wav;tempoSettings.tempo=0;
        check(!vox::fx::offline::renderWav(glitch,tempoSettings,wav)&&wav==saved,"invalid captured tempo rejected transactionally");
    }
    s.duration=33;const auto previous=wav;check(!vox::fx::offline::renderWav(program,s,wav)&&wav==previous,"invalid settings transactional");
    std::printf("FX offline WAV: %d failure(s)\n",fails);return fails?1:0;
}
