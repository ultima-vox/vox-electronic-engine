#include "engine/NoiseRiser.h"
#include "engine/OfflineRender.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#ifdef VOX_TEST_INTERPOSE_MALLOC
#include "AllocationProbe.h"
#endif

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
    // Render through a nonzero MIDI offset. A shifted host bar grid (and
    // negative preroll) must use host bar lines, not trigger-relative bars.
    using vox::fx::timeline::BarGrid;
    constexpr double rate=8000;
    check(BarGrid::from(-7.5,120,rate,-8.5).target(-7.5,1)==-4.5,"negative PPQ and host grid origin");
    check(BarGrid::from(2.5,120,rate,0.5).target(4.5,2)==12.5,"exact bar-line starts retain the full timeline");
    check(!BarGrid::from(NAN,120,rate).valid(),"non-finite PPQ rejected");
    for (const int bars : {1,2,4,8,16}) for (const double bpm : {60.,100.,128.,140.,160.,200.}) {
        engine.prepare(rate);
        p.mode=vox::fx::PlayMode::transition;p.bars=static_cast<std::uint8_t>(bars);
        for (std::size_t i=1;i<p.layers.size();++i) p.layers[i].enabled=false;
        engine.setProgram(p);engine.setMusicalClockAvailable(true,bpm);
        check(!engine.noteOn(1,60,1),"transition requires a positioned bar grid");
        constexpr double start=6.5, origin=4.5;
        const auto grid=BarGrid::from(start,bpm,rate,origin);
        engine.setBarGrid(grid);render(173);
        check(engine.noteOn(1,60,1),"start aligned transition at MIDI offset");
        engine.noteOff(1,60);
        const double target=origin+4*bars;
        const int midpoint=static_cast<int>((target-start)*.5/grid.step);
        energy=0;render(midpoint-173);
        check(engine.activeEventCount()==1 && energy>0.01,"aligned tonal event is audible before tempo change");
        const double tempo2=bpm*.75;
        const auto grid2=BarGrid::from(grid.at(midpoint),tempo2,rate,origin);
        engine.setMusicalClockAvailable(true,tempo2);engine.setBarGrid(grid2);
        const auto end=static_cast<int>(std::ceil((target-grid2.ppq-BarGrid::tolerance)/grid2.step));
        render(end);
        check(engine.activeEventCount()==1,"transition remains live up to the target-bar sample");
        engine.setBarGrid(BarGrid::from(grid2.at(end),tempo2,rate,target));render(1);
        check(engine.activeEventCount()==0 && l[0]==0 && r[0]==0,"transition is dry-silent at first sample on or after target bar");
    }
    engine.setBarGrid(BarGrid::from(0,120,rate));engine.setMusicalClockAvailable(true,120);
    check(engine.noteOn(1,60,1),"aligned note starts before clock interruption");render(320);
    engine.setBarGrid({});render(41);
    check(engine.activeEventCount()==0 && !engine.noteOn(1,60,1),"invalid PPQ fades aligned events and rejects new ones");

#ifdef VOX_TEST_INTERPOSE_MALLOC
    voxProbeBegin();
#endif
    engine.setBarGrid(BarGrid::from(0,120,rate));
    const bool rtStarted = engine.noteOn(1,60,1);
    engine.noteOff(1,60);engine.render(l.data(),r.data(),256);
    engine.allSoundOff(0);engine.render(l.data(),r.data(),256);
#ifdef VOX_TEST_INTERPOSE_MALLOC
    const auto allocations=voxProbeEnd();
    if (voxProbeAvailable()) check(allocations==0,"aligned render, ownership and panic allocate nothing");
#endif
    check(rtStarted && engine.activeEventCount()==0,"panic retires aligned transition");

    vox::fx::offline::Settings settings;settings.sampleRate=8000;settings.tempo=200;
    std::vector<std::uint8_t> output;
    p.bars=4;
    check(vox::fx::offline::renderWav(p,settings,output) && output.size()==44+38400*6,"bar transition exports fixed-tempo WAV at PPQ zero");
    const auto previous=output;
    p.bars=16;settings.tempo=1;settings.sampleRate=48000;
    check(!vox::fx::offline::renderWav(p,settings,output) && output==previous,"oversized musical export rejects transactionally");
    std::printf("FX bar event: %d failure(s)\n",failures);return failures?1:0;
}
