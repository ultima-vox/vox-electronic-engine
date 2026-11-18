#include "engine/NoiseRiser.h"
#include <cstdio>
#include <vector>

namespace {
void render(vox::fx::NoiseRiser& engine,int count){std::vector<float> l(static_cast<std::size_t>(count)),r(l.size());engine.render(l.data(),r.data(),count);}
}
int main(){
    int failures=0;auto check=[&](bool ok,const char* why){if(!ok){std::printf("FAIL: %s\n",why);++failures;}};
    vox::fx::NoiseProgram p;p.layers[0].enabled=true;p.layers[0].family=vox::fx::NoiseFamily::laser;
    p.mode=vox::fx::PlayMode::oneShot;
    vox::fx::NoiseRiser oneShot;oneShot.prepare(48000);oneShot.setProgram(p);check(oneShot.noteOn(1,60,.8f,.1),"one-shot starts");
    oneShot.noteOff(1,60);render(oneShot,1200);check(oneShot.activeEventCount()==1,"one-shot ignores normal NoteOff");
    render(oneShot,4800);check(oneShot.activeEventCount()==0,"one-shot ends at captured duration");

    p.mode=vox::fx::PlayMode::gate;
    vox::fx::NoiseRiser gate;gate.prepare(48000);gate.setProgram(p);
    check(gate.noteOn(1,60,.8f,.05)&&gate.noteOn(2,60,.8f,.05),"same pitch on different channels owns separate gate voices");
    render(gate,6000);check(gate.activeEventCount()==2,"gate continues beyond its bounded timeline by cycling while held");
    gate.noteOff(1,60);render(gate,400);check(gate.activeEventCount()==1,"NoteOff releases only matching channel/note owner");
    gate.noteOff(2,60);render(gate,400);check(gate.activeEventCount()==0,"second gate releases independently");

    p.mode=vox::fx::PlayMode::continuous;
    vox::fx::NoiseRiser continuous;continuous.prepare(48000);continuous.setProgram(p);
    check(continuous.noteOn(1,64,.7f,.05),"continuous mode starts");render(continuous,12000);
    check(continuous.activeEventCount()==1,"continuous event is not capped at the seconds timeline");
    continuous.noteOff(1,64);render(continuous,400);check(continuous.activeEventCount()==0,"continuous note release is bounded and honored");

    p.mode=vox::fx::PlayMode::loop;p.triggerPolicy=vox::fx::TriggerPolicy::ignore;
    vox::fx::NoiseRiser loop;loop.prepare(48000);loop.setProgram(p);
    check(loop.noteOn(1,67,.7f,.05),"loop starts");check(!loop.noteOn(1,67,.7f,.05),"Ignore policy suppresses duplicate channel/note");
    loop.noteOff(1,67);render(loop,400);check(loop.activeEventCount()==0,"loop release stops the held owner");

    p.triggerPolicy=vox::fx::TriggerPolicy::replace;
    vox::fx::NoiseRiser replace;replace.prepare(48000);replace.setProgram(p);
    check(replace.noteOn(1,69,.7f,.5)&&replace.noteOn(1,69,.5f,.5),"Replace policy accepts duplicate with retirement fade");
    render(replace,400);check(replace.activeEventCount()==1,"replaced duplicate retires without killing its successor");
    std::printf("FX playback modes and note ownership: %d failure(s)\n",failures);return failures?1:0;
}
