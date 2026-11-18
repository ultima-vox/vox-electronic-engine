#include "engine/NoiseRiser.h"
#include "engine/OfflineRender.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>

int main() {
    int failures=0; auto check=[&](bool ok,const char* what){if(!ok){std::printf("FAIL: %s\n",what);++failures;}};
    auto owner=std::make_unique<vox::fx::NoiseRiser>(); auto& engine=*owner;
    engine.prepare(8000); vox::fx::NoiseProgram p; p.bars=1;
    p.layers[0].family=vox::fx::NoiseFamily::shepard;
    check(engine.setProgram(p),"install bar program");
    check(!engine.noteOn(1,60,1),"missing host clock rejects note");
    engine.setMusicalClockAvailable(true,120);
    check(engine.noteOn(1,60,1),"trigger bar note");
    std::array<float,256> l{},r{}; double energy=0;
    auto render=[&](int count){while(count>0){int n=std::min(count,256);engine.render(l.data(),r.data(),n);
        for(int i=0;i<n;++i){check(std::isfinite(l[i])&&std::isfinite(r[i]),"finite audio");energy+=std::abs(l[i]);}count-=n;}};
    render(4000); // 1 quarter at 120 BPM
    check(engine.activeEventCount()==1 && energy>0.01,"tonal bar event actually renders");
    engine.setMusicalClockAvailable(true,60); // three remaining quarters = 3 seconds
    render(23999); check(engine.activeEventCount()==1,"tempo change retains beat progress");
    render(2); check(engine.activeEventCount()==0,"event ends at bar boundary within one sample");
    check(engine.noteOn(1,60,1),"retrigger");render(500);
    engine.setMusicalClockAvailable(false);render(41);
    check(engine.activeEventCount()==0 && !engine.noteOn(1,60,1),"lost clock fades and rejects notes");
    p.bars=0;check(engine.setProgram(p)&&engine.noteOn(1,60,1),"seconds voice needs no host clock");
    engine.setMusicalClockAvailable(false);render(100);
    check(engine.activeEventCount()==1,"clock loss does not stop seconds voice");
    engine.allSoundOff(0);render(41);
    p.bars=16;for(auto& layer:p.layers)layer.enabled=true;
    check(engine.setProgram(p),"four-layer 16-bar program");engine.setMusicalClockAvailable(true,60);
    check(engine.noteOn(1,60,1),"long note begins");energy=0;
    render(511999);check(engine.activeEventCount()==1 && energy>0.01,"four layers render beyond 32 seconds");
    render(2);check(engine.activeEventCount()==0,"16 bars ends at 60 BPM");
    vox::fx::offline::Settings settings; std::vector<std::uint8_t> output{42};
    check(!vox::fx::offline::renderWav(p,settings,output)&&output==std::vector<std::uint8_t>{42},"bar WAV rejects transactionally");
    std::printf("FX bar event: %d failure(s)\n",failures);return failures?1:0;
}
