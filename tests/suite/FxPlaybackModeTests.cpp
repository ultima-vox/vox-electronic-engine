#include "engine/NoiseRiser.h"
#include <algorithm>
#include <cstdio>
#include <memory>
#include <vector>

namespace {
void render(vox::fx::NoiseRiser& engine,int count){std::vector<float> l(static_cast<std::size_t>(count)),r(l.size());engine.render(l.data(),r.data(),count);}
}
int main(){
    int failures=0;auto check=[&](bool ok,const char* why){if(!ok){std::printf("FAIL: %s\n",why);++failures;}};
    vox::fx::NoiseProgram p;p.layers[0].enabled=true;p.layers[0].family=vox::fx::NoiseFamily::laser;
    p.mode=vox::fx::PlayMode::oneShot;
    auto oneShotOwner=std::make_unique<vox::fx::NoiseRiser>();auto& oneShot=*oneShotOwner;oneShot.prepare(48000);oneShot.setProgram(p);check(oneShot.noteOn(1,60,.8f,.1),"one-shot starts");
    oneShot.noteOff(1,60);render(oneShot,1200);check(oneShot.activeEventCount()==1,"one-shot ignores normal NoteOff");
    render(oneShot,4800);check(oneShot.activeEventCount()==0,"one-shot ends at captured duration");

    p.mode=vox::fx::PlayMode::gate;
    auto gateOwner=std::make_unique<vox::fx::NoiseRiser>();auto& gate=*gateOwner;gate.prepare(48000);gate.setProgram(p);
    check(gate.noteOn(1,60,.8f,.05)&&gate.noteOn(2,60,.8f,.05),"same pitch on different channels owns separate gate voices");
    render(gate,6000);check(gate.activeEventCount()==2,"gate remains alive beyond its attack timeline while held");
    gate.noteOff(1,60);render(gate,400);check(gate.activeEventCount()==1,"NoteOff releases only matching channel/note owner");
    gate.noteOff(2,60);render(gate,400);check(gate.activeEventCount()==0,"second gate releases independently");

    p.triggerPolicy=vox::fx::TriggerPolicy::overlap;
    auto overlapOwner=std::make_unique<vox::fx::NoiseRiser>();auto& overlap=*overlapOwner;overlap.prepare(48000);overlap.setProgram(p);
    check(overlap.noteOn(1,61,.3f,.05)&&overlap.noteOn(1,61,.9f,.05),"overlapping same-note owners are accepted");
    overlap.noteOff(1,61);render(overlap,400);
    check(overlap.activeEventCount()==1,"one same-note NoteOff releases only FIFO oldest held owner");
    overlap.noteOff(1,61);render(overlap,400);
    check(overlap.activeEventCount()==0,"second same-note NoteOff releases remaining FIFO owner");

    p.mode=vox::fx::PlayMode::continuous;
    auto continuousOwner=std::make_unique<vox::fx::NoiseRiser>();auto& continuous=*continuousOwner;continuous.prepare(48000);continuous.setProgram(p);
    check(continuous.noteOn(1,64,.7f,.05),"continuous mode starts");render(continuous,12000);
    check(continuous.activeEventCount()==1,"continuous event is not capped at the seconds timeline");
    continuous.noteOff(1,64);render(continuous,400);check(continuous.activeEventCount()==0,"continuous note release is bounded and honored");

    // Compare each cycle/transition edge against ordinary adjacent-sample
    // changes at three rates. The mode boundary must be click-managed, not a
    // larger discontinuity than the source's normal waveform slope.
    for(double rate:{44100.0,48000.0,96000.0})for(auto mode:{vox::fx::PlayMode::gate,vox::fx::PlayMode::continuous,vox::fx::PlayMode::loop}){
        vox::fx::NoiseProgram seamProgram;seamProgram.mode=mode;seamProgram.layers[0].family=vox::fx::NoiseFamily::laser;
        auto seamOwner=std::make_unique<vox::fx::NoiseRiser>();auto& seam=*seamOwner;seam.prepare(rate);seam.setProgram(seamProgram);seam.noteOn(1,60,.8f,.05);
        const int frames=static_cast<int>(rate*.3);std::vector<float> left(static_cast<std::size_t>(frames)),right(left.size());
        seam.render(left.data(),right.data(),frames);const int cycle=static_cast<int>(rate*.05);double edge=0,interior=0;
        for(int i=1;i<frames;++i){const double delta=std::abs(left[static_cast<std::size_t>(i)]-left[static_cast<std::size_t>(i-1)]);
            if(i%cycle<4||i%cycle>cycle-4)edge=std::max(edge,delta);else interior=std::max(interior,delta);}
        check(interior>0&&edge<interior*.35,"mode cycle boundary is click-managed across sample rates");
    }
    vox::fx::NoiseProgram longProgram;longProgram.mode=vox::fx::PlayMode::continuous;
    {
        using namespace vox::fx;
        auto held=std::make_unique<NoiseRiser>();held->prepare(8000);
        NoiseProgram custom;custom.mode=PlayMode::continuous;
        macro::Config shape;shape.event.customAdsr=true;shape.event.release=.25f;
        check(shape.put(0,{mod::Source::adsr,mod::Destination::pitch,.5f,0})&&
              held->setVoiceMotion(shape)&&held->setProgram(custom),"configure held ADSR release");
        check(held->noteOn(1,60,1,1),"held ADSR note");render(*held,400);
        held->noteOff(1,60);render(*held,1999);
        check(held->activeEventCount()==1,"captured ADSR release lasts configured duration");
        render(*held,1);check(held->activeEventCount()==0,"ADSR release ends at sample boundary");
        check(held->noteOn(1,60,1,1),"second held note");render(*held,200);
        held->noteOff(1,60);render(*held,100);held->allSoundOff(0);render(*held,40);
        check(held->activeEventCount()==0,"panic shortens in-flight musical release to 5 ms");
    }
    auto longContinuousOwner=std::make_unique<vox::fx::NoiseRiser>();auto& longContinuous=*longContinuousOwner;longContinuous.prepare(8000);longContinuous.setProgram(longProgram);
    check(longContinuous.noteOn(1,60,.8f,32.0),"32-second continuous event starts");render(longContinuous,33*8000);
    check(longContinuous.activeEventCount()==1,"continuous mode remains alive beyond the legacy 32-second timeline");
    longContinuous.noteOff(1,60);render(longContinuous,400);check(longContinuous.activeEventCount()==0,"long continuous event still responds to NoteOff");

    p.mode=vox::fx::PlayMode::loop;p.triggerPolicy=vox::fx::TriggerPolicy::ignore;
    auto loopOwner=std::make_unique<vox::fx::NoiseRiser>();auto& loop=*loopOwner;loop.prepare(48000);loop.setProgram(p);
    check(loop.noteOn(1,67,.7f,.05),"loop starts");check(!loop.noteOn(1,67,.7f,.05),"Ignore policy suppresses duplicate channel/note");
    loop.noteOff(1,67);render(loop,400);check(loop.activeEventCount()==0,"loop release stops the held owner");

    p.triggerPolicy=vox::fx::TriggerPolicy::replace;
    auto replaceOwner=std::make_unique<vox::fx::NoiseRiser>();auto& replace=*replaceOwner;replace.prepare(48000);replace.setProgram(p);
    check(replace.noteOn(1,69,.7f,.5)&&replace.noteOn(1,69,.5f,.5),"Replace policy accepts duplicate with retirement fade");
    render(replace,400);check(replace.activeEventCount()==1,"replaced duplicate retires without killing its successor");
    replace.noteOff(1,69);render(replace,400);
    check(replace.activeEventCount()==1,"replaced press consumes its own NoteOff");
    replace.noteOff(1,69);render(replace,400);
    check(replace.activeEventCount()==0,"second NoteOff releases replacement");

    {
        auto stolen=std::make_unique<vox::fx::NoiseRiser>();stolen->prepare(8000);
        p.mode=vox::fx::PlayMode::gate;p.triggerPolicy=vox::fx::TriggerPolicy::overlap;
        stolen->setProgram(p);
        stolen->noteOn(1,60,1,.1);stolen->noteOn(1,60,1,.1);
        for (int note=61;note<=67;++note) stolen->noteOn(1,note,1,.1);
        render(*stolen,41);check(stolen->activeEventCount()==8,"oldest voice retired after steal");
        stolen->noteOff(1,60);render(*stolen,41);
        check(stolen->activeEventCount()==8,"stolen press consumes its own NoteOff");
        stolen->noteOff(1,60);render(*stolen,41);
        check(stolen->activeEventCount()==7,"surviving press honors its matching NoteOff");
        stolen->allSoundOff(1);
        check(stolen->noteOn(1,61,1,.1),"channel panic clears outstanding presses");
        stolen->noteOff(1,61);render(*stolen,41);
        check(stolen->activeEventCount()==0,"prior panic fade cannot consume new key ownership");
    }
    {
        using vox::fx::TriggerSource;
        auto playback=std::make_unique<vox::fx::NoiseRiser>();playback->prepare(8000);
        p.mode=vox::fx::PlayMode::loop;p.triggerPolicy=vox::fx::TriggerPolicy::ignore;
        playback->setProgram(p);
        check(playback->noteOn(1,60,1,.05),"host MIDI loop starts");
        check(playback->noteOn(1,60,1,.05,false,0,TriggerSource::pattern),"pattern does not share MIDI Ignore ownership");
        check(playback->noteOn(1,60,1,.05,false,0,TriggerSource::preview),"preview owns a separate trigger");
        render(*playback,401);
        check(playback->activeEventCount()==1,"pattern and preview do not leave orphaned held voices");
        playback->noteOff(1,60);render(*playback,41);
        check(playback->activeEventCount()==0,"MIDI still owns and releases the loop");
    }
    {
        using namespace vox::fx;
        auto engine=std::make_unique<NoiseRiser>();engine->prepare(8000);
        NoiseProgram held;held.mode=PlayMode::gate;held.layers[0].family=NoiseFamily::laser;engine->setProgram(held);
        for(int note=48;note<55;++note)check(engine->noteOn(1,note,1,.1),"fill musical bank before audition");
        check(engine->noteOn(1,60,1,.1,false,0,TriggerSource::preview),"audition uses last free slot");
        check(!engine->noteOn(1,61,1,.1,false,0,TriggerSource::preview)&&engine->droppedEventCount()==0,
              "audition at capacity is refused without a musical drop");
        check(engine->noteOn(1,62,1,.1,false,0,TriggerSource::pattern)&&engine->activeEventCount()==9,
              "newer audition is retired before oldest music when a Pattern event arrives");
        // When preview is discarded, each of the original seven MIDI owners
        // must still respond to its own NoteOff.
        engine->discardPreview();
        for(int note=48;note<55;++note)engine->noteOff(1,note);
        render(*engine,41);check(engine->activeEventCount()==1,"all held MIDI owners were retained, and only Pattern remains");
        engine->prepare(8000);engine->setProgram(held);
        for(int note=48;note<55;++note)engine->noteOn(1,note,1,.1);
        engine->noteOn(1,60,1,.1,false,0,TriggerSource::preview);
        for(int note=62;note<=66;++note)check(engine->noteOn(1,note,1,.1),"musical admission can reclaim a retiring preview under pressure");
        check(engine->activeEventCount()==12&&engine->droppedEventCount()==0&&!engine->noteOn(1,67,1,.1)&&engine->droppedEventCount()==1,
              "only full musical occupancy increments the dropped-event counter");
    }
    {
        using namespace vox::fx;
        auto engine=std::make_unique<NoiseRiser>();engine->prepare(8000);
        NoiseProgram held;held.mode=PlayMode::gate;held.layers[0].family=NoiseFamily::laser;engine->setProgram(held);
        for(int note=48;note<55;++note)engine->noteOn(1,note,1,.1);
        auto finite=held;finite.mode=PlayMode::oneShot;engine->setProgram(finite);engine->noteOn(1,55,1,.05);
        render(*engine,390);engine->setProgram(held);
        for(int note=56;note<60;++note)engine->noteOn(1,note,1,.1);
        render(*engine,12);
        check(engine->activeEventCount()==11&&engine->noteOn(1,60,1,.1,false,0,TriggerSource::preview),
              "audition fills the slot naturally freed while all retirement slots are occupied");
        check(engine->noteOn(1,61,1,.1)&&engine->activeEventCount()==12&&engine->droppedEventCount()==0,
              "reclaim active audition when no retirement slot is available for it");
        engine->discardPreview();check(engine->activeEventCount()==12,"all active and retired owners are musical after reclaim");
    }
    std::printf("FX playback modes and note ownership: %d failure(s)\n",failures);return failures?1:0;
}
