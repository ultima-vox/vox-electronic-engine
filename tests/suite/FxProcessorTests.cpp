#include "Processor.h"
#include "FxEditor.h"
#include "content/ReviewWaveBAssets.h"
#include <cmath>
#include <cstring>
#include <bit>
#include <cstdio>
#include <condition_variable>
#include <memory>
#include <limits>
#include <span>

#ifdef VOX_TEST_INTERPOSE_MALLOC
#include "AllocationProbe.h"
#endif

namespace {
struct TestPlayHead : juce::AudioPlayHead {
    juce::Optional<PositionInfo> position;
    juce::Optional<PositionInfo> getPosition() const override { return position; }
    void set(bool playing, std::int64_t sample) {
        PositionInfo value; value.setIsPlaying(playing); value.setTimeInSamples(sample); position = value;
    }
};
int failures = 0;
void check (bool condition, const char* message) {
    if (!condition) { std::printf("FAIL: %s\n", message); ++failures; }
}
juce::MemoryBlock save (vox::fx::FxProcessor& processor) {
    juce::MemoryBlock data; processor.getStateInformation(data); return data;
}
void block (vox::fx::FxProcessor& p, juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi) {
    p.processBlock(audio, midi); midi.clear();
}
void checkRestoreAdmission() {
    using namespace vox::fx;
    struct Listener final : juce::AudioProcessorParameter::Listener {
        void parameterValueChanged(int,float) override {
            std::unique_lock lock(mutex);entered=true;cv.notify_all();
            cv.wait(lock,[this]{return released;});
        }
        void parameterGestureChanged(int,bool) override {}
        std::mutex mutex;std::condition_variable cv;bool entered=false,released=false;
    } gate;
    auto fx=std::make_unique<FxProcessor>(),donor=std::make_unique<FxProcessor>();
    donor->setFamily(static_cast<int>(NoiseFamily::laser));donor->outputParameter().setValueNotifyingHost(.47f);
    const auto state=save(*donor);fx->prepareToPlay(48000,512);
    juce::AudioBuffer<float> audio(2,512);juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1,62,juce::uint8{100}),0);block(*fx,audio,midi);
    check(fx->audioTelemetry().events==1,"an old event sounds before restore starts");
    auto& gain=fx->outputParameter();gain.addListener(&gate);
    const auto stale=fx->previewBus().ticket();
    std::thread host([&]{fx->setStateInformation(state.getData(),static_cast<int>(state.getSize()));});
    bool interleaved;
    {
        std::unique_lock lock(gate.mutex);
        interleaved=gate.cv.wait_for(lock,std::chrono::seconds(5),[&]{return gate.entered;});
    }
    if(interleaved){
        midi.addEvent(juce::MidiMessage::noteOn(1,71,juce::uint8{100}),0);block(*fx,audio,midi);
        check(fx->audioTelemetry().events==1,"blocked restore cannot admit a note from partially published graph or host bases");
    }else check(false,"restore reaches the synchronously notified host parameter");
    {std::lock_guard lock(gate.mutex);gate.released=true;}gate.cv.notify_all();host.join();gain.removeListener(&gate);
    const std::array<float,4> stalePreview{.4f,.4f,.4f,.4f};
    check(!fx->previewBus().publish(stalePreview,48000,stale),"successful restore invalidates in-flight preview ticket");
    block(*fx,audio,midi);
    check(fx->audioTelemetry().events==0&&fx->family()==static_cast<int>(NoiseFamily::laser),"first complete restored block retires old events");
    midi.addEvent(juce::MidiMessage::noteOn(1,72,juce::uint8{100}),0);block(*fx,audio,midi);
    check(fx->audioTelemetry().events==1,"new notes are admitted after the restore commits");
    fx->setNonRealtime(true);
    const auto offlineTicket=fx->previewBus().ticket();block(*fx,audio,midi);
    check(!fx->previewBus().publish(stalePreview,48000,offlineTicket),"offline host processing cannot resurrect prior preview");
}

void checkSoloAndPlayhead() {
    using namespace vox::fx;
    auto fx=std::make_unique<FxProcessor>();
    NoiseProgram p;p.mode=PlayMode::gate;
    p.layers[0].family=NoiseFamily::laser;p.layers[0].pan=-1;
    p.layers[1]=p.layers[0];p.layers[1].family=NoiseFamily::granular;p.layers[1].pan=1;p.layers[1].timing={.25f,.75f};
    p.layers[1].filters[0]={source::FilterType::lowpass,2400,.71f,2};
    p.layers[2]=p.layers[1];p.layers[2].filters={};
    p.layers[3]=p.layers[1];p.layers[3].enabled=false;
    check(fx->setProgram(p),"monitor fixture with procedural, filtered and unfiltered grains");
    fx->durationParameter().setValueNotifyingHost(fx->durationParameter().convertTo0to1(.5f));
    fx->prepareToPlay(8000,512);
    const auto project=save(*fx);
    std::vector<std::uint8_t> before, after;
    offline::Settings wav;wav.sampleRate=8000;
    check(fx->renderProgramWav(wav,before),"render state without Solo");
    TestPlayHead host;fx->setPlayHead(&host);
    juce::AudioBuffer<float> audio(2,512);juce::MidiBuffer midi;
    const auto play=[&](int sample,int note=-1,int offset=0){
        host.set(true,sample);if(note>=0)midi.addEvent(juce::MidiMessage::noteOn(1,note,juce::uint8{100}),offset);block(*fx,audio,midi);
    };
    play(0,60);
    const auto publication=fx->versionedProgram().publication;
    const auto cursor=fx->curvePlayhead(publication);
    check(cursor&&std::abs(*cursor-511.f/4000)<.0002f&&!fx->curvePlayhead(publication,1),
          "initial cursor follows rendered samples and hides future layers");
    {
        auto panel=std::make_unique<LayerPanel>(*fx,0);
        panel->soloButton().onClick();panel->refresh(p.layers[0]);
        check(panel->soloButton().getToggleState()&&fx->layerSoloMask()==1,"native Solo toggles only its own monitor bit");
    }
    play(512);play(1024);
    check(audio.getMagnitude(0,0,512)>0&&audio.getMagnitude(1,0,512)==0&&
          fx->audioTelemetry().events==1&&fx->audioTelemetry().grains>0,
          "solo isolates procedural output while filtered and unfiltered grains keep running");
    const auto local=fx->curvePlayhead(publication,1);
    check(local&&std::abs(*local-(1535.f/4000-.25f)/.75f)<.0002f&&!fx->curvePlayhead(publication,3),
          "layer cursor maps the captured window and hides disabled layers");
    auto reopened=std::make_unique<LayerPanel>(*fx,0);
    check(reopened->soloButton().getToggleState()&&reopened->curveEditor().visiblePlayhead().has_value(),
          "editor reopen reads processor monitor state and live cursor");
    check(save(*fx)==project&&fx->renderProgramWav(wav,after)&&before==after,"Solo and cursor stay out of project state and exported WAV");
    fx->setNonRealtime(true);play(1536);
    check(audio.getMagnitude(1,0,512)>0&&!fx->curvePlayhead(publication),"offline host bounce bypasses Solo and cursor");
    fx->setNonRealtime(false);fx->toggleLayerSolo(1);play(2048);
    check(fx->layerSoloMask()==3&&audio.getMagnitude(0,100,412)>0&&audio.getMagnitude(1,100,412)>0,
          "multiple Solo layers monitor together without restarting the voice");
    auto& curve=reopened->curveEditor();curve.refresh();
    check(curve.visiblePlayhead().has_value(),"cursor recovers on return to live playback");
    check(curve.setCurve(timeline::Curve::constant(.75f))&&!curve.visiblePlayhead()&&curve.undo()&&!curve.visiblePlayhead(),
          "edit and undo invalidate cursor for voices carrying a prior graph");
    play(2560);
    check(!fx->curvePlayhead(fx->versionedProgram().publication)&&fx->audioTelemetry().events==1,
          "pre-edit voice never draws against a different graph");
    play(8192);
    check(!fx->curvePlayhead(fx->versionedProgram().publication)&&fx->audioTelemetry().events==0,"host seek retires old voice and cursor");
    play(8704,64,128);
    const auto retrigger=fx->curvePlayhead(fx->versionedProgram().publication);
    check(retrigger&&std::abs(*retrigger-383.f/4000)<.0002f,"post-seek cursor tracks MIDI offset");
    fx->setStateInformation(project.getData(),static_cast<int>(project.getSize()));
    curve.refresh();check(fx->layerSoloMask()==0&&!curve.visiblePlayhead(),"restore clears monitor and hides stale cursor immediately");
    host.set(false,9216);block(*fx,audio,midi);
    check(!fx->curvePlayhead(fx->versionedProgram().publication)&&fx->audioTelemetry().events==0,"stop and restore retire live cursor");
    fx->audition();fx->stopAudition();block(*fx,audio,midi);
    check(fx->audioTelemetry().events==0,"STOP cancels queued audition before note admission");
    const auto stale=fx->previewBus().ticket();fx->stopAudition();
    const std::array<float,4> preview{.1f,-.2f,.3f,-.4f};
    check(!fx->previewBus().publish(preview,8000,stale),"STOP invalidates queued external preview result");
    fx->setPlayHead(nullptr);fx->releaseResources();
    check(!fx->curvePlayhead(fx->versionedProgram().publication)&&fx->outputPeak()==0&&!fx->consumeClip(),"release resets cursor and peak/clip");
}

void checkBounceMonitorIsolation() {
    using namespace vox::fx;
    auto fx=std::make_unique<FxProcessor>(),reference=std::make_unique<FxProcessor>();
    NoiseProgram p;p.layers[0].family=NoiseFamily::granular;p.layers[0].pan=-1;
    p.layers[1]=p.layers[0];p.layers[1].family=NoiseFamily::laser;p.layers[1].pan=1;
    for(auto* instance:{fx.get(),reference.get()}){
        check(instance->setProgram(p),"bounce monitor fixture");
        instance->prepareToPlay(48000,512);
    }
    juce::AudioBuffer<float> audio(2,512),expected(2,512);juce::MidiBuffer midi,expectedMidi;
    fx->audition();block(*fx,audio,midi);
    check(audio.getMagnitude(0,0,512)>0&&fx->audioTelemetry().grains>0,"live audition has sounding grains");
    fx->setNonRealtime(true);block(*fx,audio,midi);
    check(audio.getMagnitude(0,512)==0&&fx->audioTelemetry().events==0&&fx->audioTelemetry().grains==0,
          "in-place bounce discards live audition voices and grains before dry render");
    fx->setNonRealtime(false);block(*fx,audio,midi);
    check(audio.getMagnitude(0,512)==0,"discarded audition does not resume on return to realtime");
    fx->prepareToPlay(48000,512);
    pattern::Pattern sequence;sequence.steps[0].enabled=true;
    TestPlayHead clock;juce::AudioPlayHead::PositionInfo pos;
    pos.setIsPlaying(true);pos.setTimeInSamples(0);pos.setPpqPosition(0.0);pos.setBpm(120.0);
    pos.setTimeSignature(juce::AudioPlayHead::TimeSignature{4,4});clock.position=pos;
    for(auto* instance:{fx.get(),reference.get()}){
        check(instance->setPattern(sequence),"install offline trigger pattern");
        instance->patternEnabledParameter().setValueNotifyingHost(1.f);instance->setPlayHead(&clock);
    }
    fx->toggleLayerSolo(0);fx->setNonRealtime(true);
    fx->audition();
    constexpr std::array<float,4> preview{.4f,.1f,-.2f,-.1f};
    check(fx->previewBus().publish(preview,48000,fx->previewBus().ticket()),"queue browser preview during bounce");
    midi.addEvent(juce::MidiMessage::noteOn(2,67,juce::uint8{95}),127);expectedMidi=midi;
    block(*fx,audio,midi);block(*reference,expected,expectedMidi);
    const auto same=[&](int size){return std::equal(audio.getReadPointer(0),audio.getReadPointer(0)+size,expected.getReadPointer(0))&&
        std::equal(audio.getReadPointer(1),audio.getReadPointer(1)+size,expected.getReadPointer(1));};
    check(same(512)&&audio.getMagnitude(0,0,512)>0&&audio.getMagnitude(1,0,512)>0&&fx->audioTelemetry().events==2,
          "offline Pattern and MIDI match unmonitored realtime reference in both channels");
    pos.setTimeInSamples(512);pos.setPpqPosition(512.0/24000);pos.setBpm(84.0);clock.position=pos;
    audio.setSize(2,128);expected.setSize(2,128);
    fx->outputParameter().setValueNotifyingHost(.35f);reference->outputParameter().setValueNotifyingHost(.35f);
    fx->toggleLayerSolo(1);fx->audition();block(*fx,audio,midi);block(*reference,expected,expectedMidi);
    check(same(128)&&fx->layerSoloMask()==3&&fx->outputParameter().get()==reference->outputParameter().get(),
          "different offline block size, changed tempo, automation and multi-solo do not change reference audio");
    fx->setNonRealtime(false);fx->setPlayHead(nullptr);reference->setPlayHead(nullptr);
    block(*fx,audio,midi);block(*reference,expected,expectedMidi);
    check(same(128)&&fx->audioTelemetry().events==2,"return to live processing does not replay a queued audition");
}

void checkInPlaceBounceHistory() {
    using namespace vox::fx;
    for(const auto type:{effects::Type::delay,effects::Type::reverb}){
        auto fx=std::make_unique<FxProcessor>(),continuous=std::make_unique<FxProcessor>();
        NoiseProgram p;p.mode=PlayMode::gate;p.layers[0].family=NoiseFamily::laser;p.layers[0].pan=-.44f;
        p.layers[1]=p.layers[0];p.layers[1].family=NoiseFamily::formant;p.layers[1].pan=.78f;
        double rate=type==effects::Type::delay?48000.:96000.;
        const float amount=type==effects::Type::delay?.8f:.7f;
        TestPlayHead clock;juce::AudioPlayHead::PositionInfo pos;
        pos.setIsPlaying(true);pos.setTimeSignature(juce::AudioPlayHead::TimeSignature{4,4});
        pattern::Pattern pattern;pattern.steps[0].enabled=true;
        for(auto* instance:{fx.get(),continuous.get()}){
            check(instance->setProgram(p)&&instance->setPattern(pattern)&&instance->setEffect(0,{type,1.f,amount}),"install in-place history fixture");
            instance->durationParameter().setValueNotifyingHost(instance->durationParameter().convertTo0to1(.05f));
            instance->effectWetParameter(0).setValueNotifyingHost(1.f);
            instance->effectAmountParameter(0).setValueNotifyingHost(amount);
            instance->prepareToPlay(rate,512);instance->setPlayHead(&clock);
            instance->audition();
        }
        const auto state=save(*fx);
        juce::AudioBuffer<float> a(2,512),b(2,512);juce::MidiBuffer ma,mb;
        std::int64_t sample=0;double ppq=.5;
        const auto render=[&](int size,double bpm,bool hasPpq=true){
            pos.setTimeInSamples(sample);pos.setPpqPosition(hasPpq?juce::Optional<double>(ppq):juce::Optional<double>{});
            pos.setBpm(bpm);clock.position=pos;
            a.setSize(2,size);b.setSize(2,size);block(*fx,a,ma);block(*continuous,b,mb);
            sample+=size;ppq+=size*bpm/(60*rate);
        };
        const auto same=[&]{
            const auto size=a.getNumSamples();
            for(int c=0;c<2;++c)
                if(!std::equal(a.getReadPointer(c),a.getReadPointer(c)+size,b.getReadPointer(c)))return false;
            return true;
        };
        // End the finite synth audition naturally, leaving a known nonempty
        // shared FX history on both instances. Only the subject plays browser PCM.
        const auto switchAt=static_cast<std::int64_t>(rate*(type==effects::Type::delay?.44:.072));
        while(sample+512<switchAt)render(512,120.);
        std::vector<float> preview(4096,.25f);
        auto& bus=fx->previewBus();
        check(bus.publish(preview,static_cast<std::uint32_t>(rate),bus.ticket()),"publish live browser preview");
        render(static_cast<int>(switchAt-sample),120.);
        check(!same()&&bus.playing()&&fx->audioTelemetry().events==0,"browser PCM is leased and synth audition left only effects history");
        const auto stale=bus.ticket();
        fx->setNonRealtime(true);fx->audition();render(128,120.);
        check(same()&&a.getMagnitude(0,128)>1e-7f&&fx->audioTelemetry().events==0,
              "in-place bounce keeps nonempty shared effect history but not preview PCM or audition voice");
        check(!bus.playing()&&!bus.publish(preview,static_cast<std::uint32_t>(rate),stale),"pending browser ticket cannot restore preview after bounce begins");
        fx->setNonRealtime(false);render(64,120.);
        check(same()&&a.getMagnitude(0,64)>1e-7f&&fx->audioTelemetry().events==0,
              "returning online mid-tail preserves the shared FX bank without restoring preview");
        fx->setNonRealtime(true);
        // Admit a held MIDI note; losing PPQ disables Pattern, not the MIDI
        // owner. Recover clock at a known downbeat without a sample-time seek.
        ma.addEvent(juce::MidiMessage::noteOn(3,67,juce::uint8{108}),31);mb=ma;
        render(64,120.);
        check(same()&&fx->audioTelemetry().events==1,"held MIDI is admitted over the existing tail");
        for(auto* instance:{fx.get(),continuous.get()})instance->patternEnabledParameter().setValueNotifyingHost(1.f);
        render(512,94.,false);
        check(same()&&fx->audioTelemetry().events==1&&fx->patternClock()==FxProcessor::PatternClock::invalidPpq,
              "tempo change with missing PPQ disables Pattern without removing a seconds-based held note");
        ppq=4.;render(128,94.);
        check(same()&&fx->audioTelemetry().events==2&&fx->patternClock()==FxProcessor::PatternClock::ready44,
              "PPQ recovery admits exactly one Pattern trigger without restarting held MIDI");
        fx->setNonRealtime(false);render(64,94.);
        check(same()&&fx->audioTelemetry().events==2&&!bus.playing(),"leaving bounce preserves wet history and does not resume preview");
        ma.addEvent(juce::MidiMessage::noteOff(3,67),0);mb=ma;
        for(int i=0;i<5;++i)render(128,94.);
        check(same()&&fx->audioTelemetry().events==1,"post-bounce NoteOff still releases the original held owner");
        int tailBlocks=0;
        while(tailBlocks++<160&&(fx->audioTelemetry().events!=0||a.getMagnitude(0,a.getNumSamples())<=1e-7f))
            render(256,94.,false);
        check(same()&&fx->audioTelemetry().events==0&&a.getMagnitude(0,a.getNumSamples())>1e-7f,
              "explicit rate-change fixture contains wet tail but no remaining voice");
        rate=44100.;
        for(auto* instance:{fx.get(),continuous.get()}){
            instance->patternEnabledParameter().setValueNotifyingHost(0.f);
            instance->prepareToPlay(rate,64);
        }
        render(64,120.,false);
        check(same()&&a.getMagnitude(0,64)==0&&fx->audioTelemetry().events==0&&save(*fx)==state,
              "explicit sample-rate prepare resets audio history but keeps project state");
    }
}

void checkAuditionAtCapacity() {
    using namespace vox::fx;
    auto fx=std::make_unique<FxProcessor>(),reference=std::make_unique<FxProcessor>();
    NoiseProgram p;p.mode=PlayMode::gate;p.layers[0].family=NoiseFamily::laser;
    for(auto* instance:{fx.get(),reference.get()}){
        check(instance->setProgram(p),"install held polyphony fixture");instance->prepareToPlay(48000,512);
    }
    juce::AudioBuffer<float> a(2,512),b(2,512);juce::MidiBuffer ma,mb;
    const auto compare=[&]{block(*fx,a,ma);block(*reference,b,mb);
        return std::equal(a.getReadPointer(0),a.getReadPointer(0)+512,b.getReadPointer(0))&&
               std::equal(a.getReadPointer(1),a.getReadPointer(1)+512,b.getReadPointer(1));};
    for(int i=0;i<8;++i)ma.addEvent(juce::MidiMessage::noteOn(2,48+i,juce::uint8{100}),i);
    mb=ma;
    check(compare()&&fx->audioTelemetry().events==8,"eight held notes match before audition request");
    fx->audition();
    check(compare()&&fx->audioTelemetry().events==8&&fx->audioTelemetry().dropped==0,
          "an audition request at capacity cannot steal a held note or pollute the musical drop counter");
    fx->setNonRealtime(true);ma.addEvent(juce::MidiMessage::noteOff(2,48),0);mb=ma;
    check(compare()&&fx->audioTelemetry().events==7,"offline transition and oldest held NoteOff preserve the other MIDI owners");
}

void checkGranularPreviewPressure(double rate) {
    using namespace vox::fx;
    auto fx=std::make_unique<FxProcessor>(),reference=std::make_unique<FxProcessor>();
    auto dir=juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("vox-fx-voice-pressure","",false);
    check(dir.createDirectory().wasOk(),"create granular pressure test directory");
    const auto before=dir.getChildFile("preview.wav"),after=dir.getChildFile("music.wav");
    using content::waveb::Asset;
    const auto write=[&](const juce::File& file,Asset asset){const auto wave=content::waveb::sourceWav(asset);
        return file.replaceWithData(wave.data(),wave.size());};
    check(write(before,Asset::rail)&&write(after,Asset::ceramic),"write original test sample generations");
    const auto load=[&](FxProcessor& instance,const juce::File& file){
        return instance.importSample(file)&&instance.waitForSample(std::chrono::seconds(5))&&
               instance.sampleStatus()==sample::Controller::Status::ready;
    };
    TestPlayHead head;juce::AudioPlayHead::PositionInfo pos;pos.setIsPlaying(true);pos.setBpm(120.);
    pos.setTimeSignature(juce::AudioPlayHead::TimeSignature{4,4});
    NoiseProgram program;program.mode=PlayMode::gate;program.layers[0].family=NoiseFamily::granular;
    program.layers[0].grains={95,.19f,true};pattern::Pattern grid;grid.steps[0]={true,63,97,100,1};
    for(auto* instance:{fx.get(),reference.get()}){
        instance->prepareToPlay(rate,512);instance->setPlayHead(&head);
        check(instance->setProgram(program)&&instance->setPattern(grid)&&load(*instance,before),"install external granular audition fixture");
    }
    juce::AudioBuffer<float> a(2,512),b(2,512);juce::MidiBuffer ma,mb;
    std::int64_t sample=0;double ppq=.5;
    const auto render=[&](int size,bool playing){
        pos.setIsPlaying(playing);pos.setTimeInSamples(sample);pos.setPpqPosition(ppq);head.position=pos;
        a.setSize(2,size);b.setSize(2,size);mb=ma;block(*fx,a,ma);block(*reference,b,mb);
        sample+=size;ppq+=size*2./rate;
        for(int c=0;c<2;++c)for(int i=0;i<size;++i)
            if(!std::isfinite(a.getSample(c,i))||a.getSample(c,i)!=b.getSample(c,i))return false;
        return true;
    };
    for(int cycle=0;cycle<2;++cycle){
        for(auto* instance:{fx.get(),reference.get()}){
            instance->patternEnabledParameter().setValueNotifyingHost(0.f);
            check(load(*instance,before),"select first external generation for preview");instance->audition();
        }
        check(render(512,true)&&a.getMagnitude(0,512)>0&&fx->audioTelemetry().events==1&&fx->audioTelemetry().grains>0,
              "external sample is leased by an audible preview");
        const auto oldHash=fx->sampleReference().sha256;
        for(auto* instance:{fx.get(),reference.get()})check(load(*instance,after),"replace selected sample while preview owns the old lease");
        check(fx->sampleReference().sha256!=oldHash,"music selects a genuinely different sample generation");
        for(int note=48;note<55;++note)ma.addEvent(juce::MidiMessage::noteOn(2,note,juce::uint8{100}),0);
        check(render(64,true)&&fx->audioTelemetry().events==8,"fill the active bank with preview plus seven held MIDI notes");
        for(auto* instance:{fx.get(),reference.get()})instance->patternEnabledParameter().setValueNotifyingHost(1.f);
        ppq=4.;
        ma.addEvent(juce::MidiMessage::noteOn(2,70,juce::uint8{100}),0);
        for(int i=1;i<=4;++i)ma.addEvent(juce::MidiMessage::noteOn(2,70+i,juce::uint8{100}),i*8);
        check(render(64,true)&&fx->audioTelemetry().events==12&&fx->audioTelemetry().dropped==static_cast<unsigned>(cycle+1),
              "same-offset MIDI and Pattern, then repeated steals reclaim retiring preview before truly musical overflow");
        for(auto* instance:{fx.get(),reference.get()})instance->stopAudition();
        check(render(512,true)&&fx->audioTelemetry().events==8&&a.getMagnitude(0,512)>0,
              "STOP audition never kills musical owners under external sample pressure");
        ma.addEvent(juce::MidiMessage::noteOff(2,48),0);
        check(render(512,true)&&fx->audioTelemetry().events==8,"a stolen MIDI press consumes only its own NoteOff");
        ma.addEvent(juce::MidiMessage::noteOff(2,70),0);
        check(render(512,true)&&fx->audioTelemetry().events==7,"a surviving MIDI press releases its own held voice");
        check(render(512,false)&&fx->audioTelemetry().events==0&&fx->audioTelemetry().grains==0,
              "host stop drains musical and sample retirement state before restart");
    }
    fx.reset();reference.reset();dir.deleteRecursively();
}

void checkPatternClipboardRestore() {
    using namespace vox::fx;
    auto fx=std::make_unique<FxProcessor>();
    pattern::Pattern initial;initial.bars=2;initial.grid=pattern::Grid::thirtysecondTriplet;
    initial.steps[47]={true,72,96,67,3};
    auto restored=initial;restored.steps[47]={true,64,88,100,2};
    check(fx->setPattern(restored),"prepare same-grid restore fixture");const auto saved=save(*fx);
    check(fx->setPattern(initial),"prepare pre-restore bar");
    auto panel=std::make_unique<FxPanel>(*fx);
    const auto command=[&](int id){panel->patternActionsSelector().setSelectedId(id,juce::sendNotificationSync);};
    command(5);panel->patternBarSelector().setSelectedId(2,juce::sendNotificationSync);
    fx->setStateInformation(saved.getData(),static_cast<int>(saved.getSize()));
    command(6);check(fx->patternSnapshot()==restored&&save(*fx)==saved,"stale pre-restore paste does not edit the newly restored pattern");
    command(6);auto expected=restored;expected.steps[95]=initial.steps[47];
    check(fx->patternSnapshot()==expected,"deliberate same-grid paste uses the editor-local buffer after refreshing restore");
    command(3);check(fx->patternSnapshot()==restored,"paste undo returns to restored pattern, not pre-restore data");
    panel.reset();panel=std::make_unique<FxPanel>(*fx);
    panel->patternBarSelector().setSelectedId(2,juce::sendNotificationSync);command(6);
    check(fx->patternSnapshot()==restored&&save(*fx)==saved,"recreating editor clears the bar buffer without affecting host state");
}

void checkExportAcrossEditorClose() {
    using namespace vox::fx;
    auto fx=std::make_unique<FxProcessor>();
    auto panel=std::make_unique<FxPanel>(*fx);
    auto capture=fx->captureProgramWav({});
    capture.settings.duration=.075f;capture.settings.sampleRate=44100;
    std::vector<std::uint8_t> expected;
    check(offline::renderWav(capture,expected),"capture reference before closing editor");
    const auto dir=juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("vox-fx-panel-export","",false);
    check(dir.createDirectory().wasOk(),"export test directory");
    const auto path=dir.getChildFile("capture.wav");
    check(fx->startWavExport(capture,path)&&fx->fileExportStatus(),"processor starts and owns an export while editor is open");
    panel.reset();
    check(fx->fileExportStatus().has_value()&&!fx->startWavExport(capture,path),"closing editor neither cancels export nor admits a duplicate");
    fx->setFamily(static_cast<int>(NoiseFamily::laser));
    for(int i=0;i<3;++i){
        auto recreated=std::make_unique<FxPanel>(*fx);
        check(fx->fileExportStatus().has_value(),"repeated panel recreation leaves export status owned by processor");
    }
    auto reopened=std::make_unique<FxPanel>(*fx);
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    std::optional<fileexport::Completion> result;
    while(!result&&std::chrono::steady_clock::now()<end){
        result=fx->takeFileExportResult();
        if(!result)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(result&&result->kind==fileexport::Kind::wav&&result->result==fileexport::Result::success&&!fx->fileExportStatus()&&!fx->takeFileExportResult(),"reopened editor can consume completion exactly once");
    juce::MemoryBlock bytes;
    check(path.loadFileAsData(bytes)&&bytes.getSize()==expected.size()&&std::memcmp(bytes.getData(),expected.data(),expected.size())==0,
          "export contains original capture despite editor close and subsequent Program edit");
    dir.deleteRecursively();
}

void checkPendingExportLifecycle() {
    using namespace vox::fx;
    auto fx=std::make_unique<FxProcessor>();
    auto capture=fx->captureProgramWav({});capture.settings.duration=32;capture.settings.sampleRate=192000;
    for(auto& layer:capture.program.layers){layer.enabled=true;layer.family=NoiseFamily::granular;}
    const auto project=save(*fx);
    const auto dir=juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("vox-fx-export-lifecycle","",false);
    check(dir.createDirectory().wasOk(),"lifecycle test directory");
    const auto path=dir.getChildFile("during-save.wav");
    check(path.replaceWithText("old destination"),"prepare overwrite sentinel");
    check(fx->startWavExport(capture,path)&&fx->fileExportStatus()->result==fileexport::Result::running,"export pending at session save");
    bool hostSave=false,refused=false;
    std::thread host([&]{
        hostSave=save(*fx)==project;
        refused=!fx->fileExportStatus()&&!fx->cancelFileExport(fileexport::Kind::wav)&&!fx->takeFileExportResult()&&
                !fx->startWavExport(capture,dir.getChildFile("off-thread.wav"));
    });host.join();
    check(hostSave&&refused,"host-thread save excludes export job; job controls refuse off-message access");
    const bool accepted=fx->cancelFileExport(fileexport::Kind::wav);
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    std::optional<fileexport::Completion> result;
    while(!result&&std::chrono::steady_clock::now()<end){
        result=fx->takeFileExportResult();
        if(!result)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(accepted&&result&&result->kind==fileexport::Kind::wav&&result->result==fileexport::Result::cancelled&&path.loadFileAsString()=="old destination",
          "controller cancellation preserves existing WAV target");
    const auto unloadPath=dir.getChildFile("during-unload.wav");
    check(fx->startWavExport(std::move(capture),unloadPath),"start long render before processor unload");
    fx.reset();
    check(!unloadPath.exists()&&dir.findChildFiles(juce::File::findFiles,false).size()==1,
          "processor destruction cancels, joins and leaves no partial export");
    dir.deleteRecursively();
}

void checkMidiExportLifecycle() {
    using namespace vox::fx;
    struct EncoderGate {
        std::mutex mutex;std::condition_variable cv;bool entered=false,released=false;
    } gate;
    const auto holdFirstEvent=[](void* context,std::size_t count) noexcept {
        if(count!=1)return;
        auto& g=*static_cast<EncoderGate*>(context);
        std::unique_lock lock(g.mutex);g.entered=true;g.cv.notify_all();
        g.cv.wait(lock,[&]{return g.released;});
    };
    auto fx=std::make_unique<FxProcessor>();
    auto panel=std::make_unique<FxPanel>(*fx);
    pattern::Pattern snapshot;snapshot.bars=2;snapshot.steps[1]={true,53,92,100,3};
    std::vector<std::uint8_t> expected;
    check(pattern::exportMidi(snapshot,133.75,expected),"reference MIDI file encodes captured tempo and Pattern");
    const auto dir=juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("vox-fx-midi-export","",false);
    check(dir.createDirectory().wasOk(),"create MIDI export test directory");
    const auto target=dir.getChildFile("pattern.mid");
    check(target.replaceWithText("old file")&&!fx->startMidiExport(snapshot,9.,target)&&
          !fx->startMidiExport(snapshot,120.,dir.getChildFile("wrong.wav")),"reject invalid tempo or extension before starting a file job");
    check(fx->startMidiExport(snapshot,133.75,target,holdFirstEvent,&gate),"processor owns MIDI job");
    {
        std::unique_lock lock(gate.mutex);
        check(gate.cv.wait_for(lock,std::chrono::seconds(5),[&]{return gate.entered;}),"worker reached active MIDI encoding");
    }
    panel.reset();
    check(!fx->startWavExport({},dir.getChildFile("second.wav"))&&!fx->cancelFileExport(fileexport::Kind::wav),
          "MIDI and WAV share one worker slot, and wrong-kind cancellation is refused");
    snapshot.steps[1].note=80;check(fx->setPattern(snapshot),"edit Pattern after confirming MIDI export");
    panel=std::make_unique<FxPanel>(*fx);
    const auto wait=[&]{std::optional<fileexport::Completion> result;
        const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(!(result=fx->takeFileExportResult())&&std::chrono::steady_clock::now()<until)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return result;
    };
    juce::MemoryBlock hostSaved;
    bool refused=false;
    std::atomic<bool> hostFinished{false};
    std::thread host([&]{
        hostSaved=save(*fx);refused=!fx->takeFileExportResult();
        hostFinished.store(true,std::memory_order_release);
    });
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!hostFinished.load(std::memory_order_acquire)&&std::chrono::steady_clock::now()<deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const auto duringSave=fx->fileExportStatus();
    check(hostFinished.load(std::memory_order_acquire)&&duringSave&&duringSave->kind==fileexport::Kind::midi&&
          duringSave->result==fileexport::Result::running&&target.loadFileAsString()=="old file",
          "host save returns while the MIDI encoder is blocked and the old destination is intact");
    {
        std::lock_guard lock(gate.mutex);gate.released=true;
    }
    gate.cv.notify_all();host.join();
    auto result=wait();
    check(hostSaved==save(*fx)&&refused,"active MIDI export is excluded from project state and the host thread cannot consume UI completion");
    juce::MemoryBlock actual;
    check(result&&result->kind==fileexport::Kind::midi&&result->result==fileexport::Result::success&&
          target.loadFileAsData(actual)&&actual.getSize()==expected.size()&&std::memcmp(actual.getData(),expected.data(),expected.size())==0,
          "editor recreation and Pattern edits leave the immutable MIDI capture byte-exact");
    check(!fx->takeFileExportResult(),"MIDI result is consumed once");
    check(fx->startMidiExport(snapshot,120.,dir.getChildFile("missing/target.mid")),"start deterministic MIDI filesystem failure");
    result=wait();
    check(result&&result->kind==fileexport::Kind::midi&&result->result==fileexport::Result::failure&&
          dir.findChildFiles(juce::File::findFiles,false).size()==1,"MIDI failure is reported and leaves no additional file");
    dir.deleteRecursively();
}

void checkExportChooserReentry() {
    using namespace vox::fx;
    using Kind=fileexport::Kind;
    auto fx=std::make_unique<FxProcessor>();
    FxPanel::SaveResult confirm;
    int pickerCalls=0;
    Kind kind=Kind::midi;
    const FxPanel::SavePicker picker=[&](Kind requested,FxPanel::SaveResult callback){
        ++pickerCalls;kind=requested;confirm=std::move(callback);
    };
    auto panel=std::make_unique<FxPanel>(*fx,picker);
    const auto midiClick=[&]{panel->midiExportButton().onClick();};
    const auto wavRequest=[&]{panel->wavOptionsPanel().onSave(offline::Settings{});};
    midiClick();
    check(bool(confirm)&&kind==Kind::midi&&pickerCalls==1&&!panel->midiExportButton().isEnabled()&&!panel->wavExportButton().isEnabled(),
          "MIDI chooser immediately disables both export entry points");
    const auto cancelled=confirm;
    midiClick();wavRequest();
    check(pickerCalls==1&&!fx->fileExportStatus(),"reentrant MIDI or WAV actions cannot create a second picker or worker");
    if(cancelled)cancelled({});
    check(panel->midiExportButton().isEnabled()&&panel->wavExportButton().isEnabled(),"picker cancellation re-enables controls synchronously");
    wavRequest();
    check(pickerCalls==2&&kind==Kind::wav&&!panel->midiExportButton().isEnabled(),"WAV picker can open after MIDI cancellation");
    if(cancelled)cancelled(juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("vox-stale-picker.mid"));
    midiClick();wavRequest();
    check(pickerCalls==2&&!panel->midiExportButton().isEnabled()&&!fx->fileExportStatus(),
          "stale MIDI completion cannot touch the new WAV picker or start an export");
    const auto afterClose=confirm;
    panel.reset();
    if(afterClose)afterClose(juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("vox-closed-picker.wav"));
    check(!fx->fileExportStatus(),"late confirmation after editor destruction cannot start a job");
    pattern::Pattern pattern;pattern.steps[0]={true,57,98,100,2};
    std::vector<std::uint8_t> expected;
    check(fx->setPattern(pattern)&&pattern::exportMidi(pattern,120.,expected),"prepare immutable picker snapshot");
    panel=std::make_unique<FxPanel>(*fx,picker);
    midiClick();const auto current=confirm;
    pattern.steps[0].note=72;check(fx->setPattern(pattern),"edit Pattern after opening picker");
    const auto dir=juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("vox-fx-picker","",false);
    check(dir.createDirectory().wasOk(),"create picker fixture directory");
    const auto target=dir.getChildFile("accepted.mid"),duplicate=dir.getChildFile("duplicate.mid");
    if(current){current(target);current(duplicate);}
    std::optional<fileexport::Completion> result;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!(result=fx->takeFileExportResult())&&std::chrono::steady_clock::now()<deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    juce::MemoryBlock actual;
    check(result&&result->kind==Kind::midi&&result->result==fileexport::Result::success&&!duplicate.exists()&&
          target.loadFileAsData(actual)&&actual.getSize()==expected.size()&&std::memcmp(actual.getData(),expected.data(),expected.size())==0,
          "confirmed picker exports the pre-picker Pattern exactly once despite later edits");
    dir.deleteRecursively();
}

void checkTimelineViewport() {
    using namespace vox::fx;
    auto fx=std::make_unique<FxProcessor>();NoiseProgram p;
    p.layers[0].timing={.25f,.5f};check(fx->setProgram(p),"axis layer fixture");
    auto editor=std::make_unique<CurveEditor>(*fx,0);
    editor->setBounds(0,0,320,140);
    const auto params=save(*fx);
    check(!editor->axisUsesBars()&&editor->axisLength()==2,"layer axis uses span times duration in seconds");
    check(editor->keyPressed(juce::KeyPress('+'))&&editor->viewport().start()==.25&&editor->viewport().span()==.5,
          "keyboard zoom selects center half");
    check(save(*fx)==params,"viewport does not affect host state or automation");
    const auto now=juce::Time::getCurrentTime();const juce::Point<float> pos{198.f,58.f};
    const juce::MouseEvent click(juce::Desktop::getInstance().getMainMouseSource(),pos,juce::ModifierKeys{},0,0,0,0,0,
        editor.get(),editor.get(),now,pos,now,2,false);
    editor->mouseDoubleClick(click);
    const auto inserted=fx->program().layers[0].gainCurve;
    check(inserted.count==3&&inserted.nodes[1].time==.5625f&&editor->undo(),"zoomed insertion uses visible-to-normalized mapping and undo");
    check(fx->program().layers[0].gainCurve.count==2&&editor->viewport().span()==.5,"undo restores curve without resetting view");
    check(editor->keyPressed(juce::KeyPress(juce::KeyPress::rightKey,juce::ModifierKeys::altModifier,0))&&
          editor->viewport().start()==.375&&editor->keyPressed(juce::KeyPress('0'))&&editor->viewport().span()==1,
          "keyboard pan and fit do not edit the curve");
    check(editor->keyPressed(juce::KeyPress('+'))&&editor->keyPressed(juce::KeyPress(']'))&&editor->viewport().start()==0&&
          editor->keyPressed(juce::KeyPress('['))&&editor->viewport().start()==.5,"bracket keys reveal hidden endpoint nodes without OS Alt+Tab");
    check(editor->keyPressed(juce::KeyPress('0')),"fit after focus navigation");
    const juce::MouseEvent select(juce::Desktop::getInstance().getMainMouseSource(),{8.f,8.f},juce::ModifierKeys{},0,0,0,0,0,
        editor.get(),editor.get(),now,{8.f,8.f},now,1,false);
    editor->mouseDown(select);editor->mouseUp(select);
    check(fx->program().layers[0].gainCurve.count==2&&editor->redo()&&fx->program().layers[0].gainCurve.count==3,
          "selecting without dragging preserves redo history");
    auto& seconds=fx->durationParameter();seconds.setValueNotifyingHost(seconds.convertTo0to1(.75f));editor->refresh();
    check(!editor->axisUsesBars()&&std::abs(editor->axisLength()-.375)<1e-4,"seconds axis follows host automation");
    p.bars=8;check(fx->setProgram(p),"switch authoring timeline to bars");editor->refresh();
    check(editor->axisUsesBars()&&editor->axisLength()==4,"layer axis switches to local bar span");
    fx->setStateInformation(params.getData(),static_cast<int>(params.getSize()));editor->refresh();
    check(fx->lastStateError().isEmpty()&&!editor->axisUsesBars()&&std::abs(editor->axisLength()-2)<1e-4,
          "axis follows project restore");
}

void checkHostTempoCursor() {
    using namespace vox::fx;
    TestPlayHead automated, reference;
    auto a=std::make_unique<FxProcessor>(), b=std::make_unique<FxProcessor>();
    a->setPlayHead(&automated);b->setPlayHead(&reference);
    NoiseProgram p;p.mode=PlayMode::gate;p.layers[0].family=NoiseFamily::laser;
    const auto init=[&](FxProcessor& fx){
        auto m=fx.versionedMotion();auto config=m.config;config.lfos[0]={1,true};
        check(config.put(0,{mod::Source::lfo1,mod::Destination::layerPan,.43f,0})&&fx.replaceMotionIfCurrent(m,config),"tempo voice route setup");
        check(fx.setProgram(p),"seconds voice setup");fx.prepareToPlay(8000,400);
    };
    init(*a);init(*b);
    juce::AudioBuffer<float> x(2,400),y(2,400);juce::MidiBuffer mx,my;
    const auto position=[&](TestPlayHead& host,int sample,double bpm,std::optional<double> ppq={}){
        juce::AudioPlayHead::PositionInfo info;info.setIsPlaying(true);info.setTimeInSamples(sample);info.setBpm(bpm);
        if(ppq){info.setPpqPosition(*ppq);info.setTimeSignature(juce::AudioPlayHead::TimeSignature{4,4});}
        host.position=info;
    };
    mx.addEvent(juce::MidiMessage::noteOn(1,60,juce::uint8{100}),0);my.addEvents(mx,0,400,0);
    position(automated,0,84);position(reference,0,84);block(*a,x,mx);block(*b,y,my);
    check(std::equal(x.getReadPointer(0),x.getReadPointer(0)+400,y.getReadPointer(0)),"identical initial BPM yields identical motion");
    double delta=0;
    for(int at=400;at<=1600;at+=400){
        position(automated,at,140);position(reference,at,84);block(*a,x,mx);block(*b,y,my);
        for(int i=0;i<400;++i)delta+=std::abs(x.getSample(0,i)-y.getSample(0,i));
    }
    check(delta>.001&&a->observedTempo()==140&&a->musicalClockUnavailable()&&a->audioTelemetry().events==1,
          "automated host BPM without PPQ changes an existing seconds LFO, not bar clock admission");
    a->prepareToPlay(8000,400);p.mode=PlayMode::transition;p.bars=1;check(a->setProgram(p),"bar-aligned cursor setup");
    const auto graph=a->versionedProgram().publication;
    position(automated,0,120);mx.addEvent(juce::MidiMessage::noteOn(1,60,juce::uint8{100}),0);block(*a,x,mx);
    check(a->audioTelemetry().events==0&&!a->curvePlayhead(graph),"valid BPM without PPQ does not admit bar events or create a playhead");
    position(automated,400,120,2.);mx.addEvent(juce::MidiMessage::noteOn(1,60,juce::uint8{100}),0);block(*a,x,mx);
    const auto first=a->curvePlayhead(graph);
    position(automated,800,60,2.1);block(*a,x,mx);
    const auto second=a->curvePlayhead(graph);
    check(first&&second&&std::abs(*first-.049875)<.0002&&std::abs(*second-.0749375)<.0002,
          "Transition cursor uses actual bar-line target and tempo-segment timing");
    position(automated,1200,60);block(*a,x,mx);
    check(!a->curvePlayhead(graph)&&a->audioTelemetry().events==0,"clock loss retires aligned Transition and its cursor");
    a->setPlayHead(nullptr);b->setPlayHead(nullptr);
}

// Compare actual processBlock to the rendered float32 file. Deliberately use
// different/irregular blocks and the same finite audition contract, not a
// loop/held performance with an arbitrary MIDI NoteOff or a DAW tempo map.
void checkCaptureParity() {
    using namespace vox::fx;
    for(unsigned mode=0;mode<7;++mode){
        auto fx=std::make_unique<FxProcessor>();TestPlayHead host;fx->setPlayHead(&host);
        constexpr int rate=48000,cycle=rate;
        const auto clock=[&](int at){
            juce::AudioPlayHead::PositionInfo pos;pos.setIsPlaying(true);pos.setBpm(240);
            pos.setTimeInSamples(at);pos.setPpqPosition(4.*at/rate);
            pos.setTimeSignature(juce::AudioPlayHead::TimeSignature{4,4});host.position=pos;
        };
        NoiseProgram program;program.mode=static_cast<PlayMode>(mode<6?mode:0);
        if(program.mode==PlayMode::transition)program.bars=1;
        program.layers[0].family=NoiseFamily::laser;program.layers[0].pan=-.18f;
        program.layers[1]=program.layers[0];program.layers[1].family=NoiseFamily::formant;program.layers[1].pan=.25f;
        program.layers[1].gain=.49f;program.layers[1].timing={.09f,.85f};
        check(fx->setProgram(program),"configure fixed-tempo multi-layer capture");
        fx->durationParameter().setValueNotifyingHost(fx->durationParameter().convertTo0to1(.11f));
        fx->outputParameter().setValueNotifyingHost(.63f);
        fx->setEffect(0,{effects::Type::reverb,.32f,.2f});
        fx->effectWetParameter(0).setValueNotifyingHost(.32f);fx->effectAmountParameter(0).setValueNotifyingHost(.2f);
        if(mode==5){
            fx->setEffect(1,{effects::Type::tempoDelay,.37f,0});
            fx->effectWetParameter(1).setValueNotifyingHost(.37f);fx->effectAmountParameter(1).setValueNotifyingHost(0.f);
        }
        auto motion=fx->versionedMotion();auto routes=motion.config;
        (void)routes.put(0,{mod::Source::lfo2,mod::Destination::pitch,.11f,.02f});
        (void)routes.put(1,{mod::Source::follower,mod::Destination::amount1,.2f,.018f});
        (void)routes.put(2,{mod::Source::lfo1,mod::Destination::width,.12f,.014f});
        check(fx->replaceMotionIfCurrent(motion,routes),"capture has pre- and post-DSP motion");
        const bool sequence=program.mode==PlayMode::sequence||mode==6;
        if(sequence){
            pattern::Pattern beat;beat.bars=1;beat.grid=pattern::Grid::sixteenthTriplet;beat.swing=.19f;
            beat.steps[0]={true,58,112,100,4};beat.steps[7]={true,65,92,100,2};beat.steps[23]={true,63,120,100,1};
            check(fx->setPattern(beat),"capture an explicitly edited Sequence");
        }
        if(mode==6)fx->patternEnabledParameter().setValueNotifyingHost(1.f);
        juce::MidiBuffer midi;fx->prepareToPlay(rate,512);clock(0);
        juce::AudioBuffer<float> empty(2,0);fx->processBlock(empty,midi);
        offline::Settings settings;settings.sampleRate=rate;settings.format=offline::WavFormat::float32;settings.velocity=.8f;
        const auto capture=fx->captureProgramWav(settings);std::vector<std::uint8_t> wav;
        check(capture.patternEnabled==(mode==6)&&capture.settings.tempo==240,"capture sees the Pattern switch and observed tempo");
        if(!offline::renderWav(capture,wav)){check(false,"processor snapshot renders to float32 WAV");continue;}
        if(!sequence)fx->audition();
        const int frames=static_cast<int>((wav.size()-56)/8);
        double peak=0,tail=0,error=0;
        juce::AudioBuffer<float> audio(2,512);
        constexpr int sizes[]{64,127,512,71,311};
        for(int at=0,index=0;at<frames;++index){
            if(sequence&&at==cycle){
                // Leave transport continuous so the last voices and post-FX
                // ring out, but stop scheduling after exactly one cycle.
                if(mode==6)fx->patternEnabledParameter().setValueNotifyingHost(0.f);
                else {program.mode=PlayMode::oneShot;fx->setProgram(program);}
            }
            const int bound=sequence&&at<cycle?cycle:frames;
            const int count=std::min(sizes[index%5],bound-at);
            audio.setSize(2,count,false,false,true);clock(at);fx->processBlock(audio,midi);
            for(int i=0;i<count;++i)for(int ch=0;ch<2;++ch){
                const auto offset=56+8*(at+i)+ch*4;std::uint32_t bits=0;
                for(unsigned k=0;k<4;++k)bits|=std::uint32_t{wav[offset+k]}<<(8*k);
                const double actual=audio.getSample(ch,i);
                peak=std::max(peak,std::abs(actual));
                if(at+i>=frames-rate*19/10)tail=std::max(tail,std::abs(actual));
                error=std::max(error,std::abs(std::bit_cast<float>(bits)-actual));
            }
            at+=count;
        }
        std::printf("capture parity case %u: %d frames peak %.7f tail %.7f max error %.9g\n",mode,frames,peak,tail,error);
        check(peak>.001&&error<=2e-6,"native callback/WAV match at fixed tempo across the complete render extent");
        if(mode==0)check(tail>1e-6,"One Shot capture contains a post-event reverb tail");
    }
}

}
int main() {
    const auto phase=[](const char* label){
        std::fprintf(stderr,"[FX processor] %s\n",label);
        std::fflush(stderr); // survive abrupt termination with redirected CTest output
    };
    phase("JUCE initialization");
    juce::ScopedJuceInitialiser_GUI gui;
    phase("layer solo and synchronized cursor");checkSoloAndPlayhead();checkHostTempoCursor();
    phase("offline host monitor isolation");checkBounceMonitorIsolation();
    phase("in-place bounce effect history and audition priority");checkInPlaceBounceHistory();checkAuditionAtCapacity();
    phase("External granular preview under repeated MIDI and Pattern pressure");
    for(double rate:{44100.,48000.,96000.})checkGranularPreviewPressure(rate);
    phase("Pattern clipboard during restore and editor recreation");checkPatternClipboardRestore();
    phase("timeline viewport keyboard and selection");checkTimelineViewport();
    phase("WAV export across editor close and recreation");checkExportAcrossEditorClose();
    phase("WAV export pending host save, cancellation and processor unload");checkPendingExportLifecycle();
    phase("MIDI export lifecycle and picker reentry");checkMidiExportLifecycle();checkExportChooserReentry();
    phase("capture parity");checkCaptureParity();
    phase("restore admission");checkRestoreAdmission();
    auto pOwner=std::make_unique<vox::fx::FxProcessor>(); auto& p=*pOwner;
    phase("motion, generator and preset browser");
    {
        using namespace vox::fx;
        auto owner=std::make_unique<FxProcessor>();auto panelStorage1=std::make_unique<vox::fx::FxPanel>(*owner); auto& panel=*panelStorage1;
        auto& motion=panel.motionPanel();
        motion.refresh(0);motion.sourceSelector().setSelectedId(static_cast<int>(mod::Source::velocity)+1,juce::sendNotificationSync);
        motion.destinationSelector().setSelectedId(static_cast<int>(mod::Destination::pitch)+1,juce::sendNotificationSync);
        motion.applyButton().onClick();
        check(owner->macroConfig().count==1&&owner->macroConfig().routes[0].destination==mod::Destination::pitch,"native Motion row publishes a typed voice route");
        const auto restored=save(*owner);owner->setStateInformation(restored.getData(),static_cast<int>(restored.getSize()));
        motion.destinationSelector().setSelectedId(static_cast<int>(mod::Destination::cutoff)+1,juce::sendNotificationSync);
        motion.applyButton().onClick();
        check(owner->macroConfig().routes[0].destination==mod::Destination::pitch,"stale Motion draft cannot overwrite a host restore");
        motion.stepLevel(3).setValue(37,juce::dontSendNotification);
        motion.customAdsrButton().setToggleState(true,juce::dontSendNotification);
        motion.adsrValue(3).setValue(280,juce::dontSendNotification);
        motion.applyShapeButton().onClick();
        const auto authoredMotion=owner->macroConfig();
        check(authoredMotion.event.customAdsr&&std::abs(authoredMotion.event.steps[3]-.37f)<1e-6f&&
              std::abs(authoredMotion.event.release-.28f)<1e-6f,"native shape action publishes FMOD3 settings");
        const auto shaped=save(*owner);owner->setStateInformation(shaped.getData(),static_cast<int>(shaped.getSize()));
        motion.stepLevel(3).setValue(99,juce::dontSendNotification);motion.applyShapeButton().onClick();
        check(owner->macroConfig()==authoredMotion,"stale step/ADSR draft rejected after host restore");
        owner->editGeneratorState([](auto& state){state.role=5;});owner->effectWetParameter(0).setValueNotifyingHost(.31f);
        check(owner->generateSound(false)&&owner->program().mode==PlayMode::sequence&&owner->patternSnapshot().steps[0].enabled&&
            std::abs(owner->effectWetParameter(0).get()-.31f)<1e-5f,"full graph generation keeps host automation bases");
        const auto root=juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("voxfx-ui-"+juce::Uuid().toString());
        {
            auto browserStorage11=std::make_unique<vox::fx::PresetBrowser>(*owner,root); auto& browser=*browserStorage11;browser.setSize(920,456);browser.open();
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
            while(browser.indexing()&&std::chrono::steady_clock::now()<deadline){browser.pollBackground();std::this_thread::yield();}
            check(!browser.indexing(),"managed user scan completes through the background indexer");
            const auto full=browser.matchCount();
            auto& tonality=browser.tonalityFacet();auto& duration=browser.durationFacet();auto& style=browser.styleFacet();
            tonality.setSelectedId(3,juce::sendNotificationSync);
            check(browser.matchCount()>0&&browser.matchCount()<full,"tonality facet excludes pitched review Programs");
            duration.setSelectedId(4,juce::sendNotificationSync);
            const auto expectedBars=[](bool pitched){
                return static_cast<int>(std::count_if(std::begin(content::reviewEntries),std::end(content::reviewEntries),[=](const auto& e){
                    return e.pitchedSource==pitched&&(e.bars>0||e.mode==PlayMode::sequence);
                }));
            };
            check(expectedBars(false)>0&&browser.matchCount()==expectedBars(false),"keyless bar rolls and transitions match both facets");
            tonality.setSelectedId(2,juce::sendNotificationSync);
            check(expectedBars(true)>0&&browser.matchCount()==expectedBars(true),"pitched bar duration includes transitions and pattern cycles");
            for(int i=0;i<style.getNumItems();++i)if(style.getItemText(i)=="Uplifting")style.setSelectedItemIndex(i,juce::sendNotificationSync);
            check(browser.matchCount()>0&&browser.matchCount()<full,"style facet combines with pitch and duration");
            tonality.setSelectedId(1,juce::sendNotificationSync);duration.setSelectedId(1,juce::sendNotificationSync);style.setSelectedId(1,juce::sendNotificationSync);
            browser.searchField().setText("cobalt choir",false);browser.searchField().onTextChange();
            check(browser.matchCount()==1,"native browser keyword filter finds a unique review row");
            browser.searchField().setText("",false);browser.searchField().onTextChange();browser.nameField().setText("Saved Native Example",true);browser.saveButton().onClick();
            browser.searchField().setText("saved native example",false);browser.searchField().onTextChange();
            check(browser.matchCount()==1&&browser.loadButton().isEnabled(),"user preset appears in native browser after save and search");
            owner->setFamily(0);const auto base=owner->effectWetParameter(0).get();browser.loadButton().onClick();
            check(owner->family()==static_cast<int>(NoiseFamily::glitch)&&owner->effectWetParameter(0).get()==base,"browser loads saved structure without resetting automation");
        }
        (void)root.deleteRecursively();
        for(float scale:{1.f,1.25f,1.5f,2.f}){
            panel.setSize(1040,760);
            const auto image=panel.createComponentSnapshot(panel.getLocalBounds(),true,scale);
            check(image.isValid()&&image.getWidth()==static_cast<int>(1040*scale)&&image.getHeight()==static_cast<int>(760*scale),
                  "offscreen JUCE painting supports explicit scaling factors");
        }
    }
    phase("editor/parameters and state");
    {
        auto panelStorage2=std::make_unique<vox::fx::FxPanel>(p); auto& panel=*panelStorage2;
        panel.outputSlider().setValue(0.5, juce::sendNotificationSync);
        check(std::abs(p.outputParameter().get() - 0.5f) < 1e-6f, "editor knob controls host parameter");
        panel.durationSlider().setValue(8.0, juce::sendNotificationSync);
        check(std::abs(p.durationParameter().get() - 8.0f) < 1e-4f, "duration editor controls parameter");
        p.durationParameter().setValueNotifyingHost(p.durationParameter().convertTo0to1(4.0f));
        panel.familySelector().setSelectedId(2, juce::sendNotificationSync);
        check(p.family() == 1, "family selector updates structural state");
        panel.eventBarsSelector().setSelectedId(6,juce::sendNotificationSync);
        check(p.program().bars==16,"bar duration editor changes Program");
        auto barState=save(p);auto barRecall=std::make_unique<vox::fx::FxProcessor>();
        barRecall->setStateInformation(barState.getData(),static_cast<int>(barState.getSize()));
        check(barRecall->program().bars==16,"bar duration survives state recall");
        panel.eventBarsSelector().setSelectedId(1,juce::sendNotificationSync);
        p.setFamily(0);
        panel.layerPanel(1).gainSlider().setValue(0.42, juce::sendNotificationSync);
        panel.layerPanel(1).panSlider().setValue(-0.5, juce::sendNotificationSync);
        const auto edited = p.program();
        check(std::abs(edited.layers[1].gain - 0.42f) < 1e-5f && edited.layers[1].pan == -0.5f,
              "layer UI edits structural program");
        auto& curve = panel.layerPanel(1).curveEditor();
        check(curve.setCurve(vox::fx::timeline::Curve::constant(0.25f)) &&
              p.program().layers[1].gainCurve.evaluate(0.5f) == 0.25f, "curve edit changes program");
        check(curve.undo() && p.program().layers[1].gainCurve.evaluate(0.5f) == 1.0f, "curve undo");
        check(curve.redo() && p.program().layers[1].gainCurve.evaluate(0.5f) == 0.25f, "curve redo");
        auto& selector=panel.layerPanel(1).curveSelector();
        selector.setSelectedId(4,juce::sendNotificationSync);
        check(curve.setCurve(vox::fx::timeline::Curve::constant(.8f))&&
              p.program().layers[1].panCurve.evaluate(.5f)==.8f&&p.program().layers[1].gainCurve.evaluate(.5f)==.25f,
              "pan curve editor updates only pan curve");
        check(curve.undo()&&p.program().layers[1].panCurve.evaluate(.5f)==.5f,"pan curve undo");
        check(curve.redo()&&p.program().layers[1].panCurve.evaluate(.5f)==.8f,"pan curve redo");
        selector.setSelectedId(1,juce::sendNotificationSync);
        check(!curve.undo(),"switching curves clears incompatible undo history");
        auto replacement=p.program();replacement.layers[1].gainCurve=vox::fx::timeline::Curve::constant(.8f);
        check(p.setProgram(replacement),"replace Program after curve edit");curve.refresh();
        check(!curve.undo(),"whole Program replacement invalidates stale curve history");
        curve.setBounds(0,0,100,100);curve.refresh();
        const auto source=juce::Desktop::getInstance().getMainMouseSource();const auto now=juce::Time::getCurrentTime();
        auto mouse=[&](juce::Point<float> position){return juce::MouseEvent(source,position,juce::ModifierKeys{},0,0,0,0,0,
            &curve,&curve,now,{8.0f,24.8f},now,1,true);};
        curve.mouseDown(mouse({8.0f,24.8f}));
        auto restored=p.program();restored.layers[1].gainCurve=vox::fx::timeline::Curve::constant(.4f);
        auto donor=std::make_unique<vox::fx::FxProcessor>();check(donor->setProgram(restored),"establish stored host state for curve drag");
        const auto incoming=save(*donor);p.setStateInformation(incoming.getData(),static_cast<int>(incoming.getSize()));
        check(p.lastStateError().isEmpty(),"replace Program from host while curve drag is active");
        curve.mouseDrag(mouse({8.0f,80.0f}));curve.mouseUp(mouse({8.0f,80.0f}));
        check(p.program().layers[1].gainCurve.evaluate(.5f)==.4f,"stale drag cannot overwrite replaced Program");
        p.outputParameter().setValueNotifyingHost(1.0f);
        check(std::abs(panel.outputSlider().getValue() - 1.0) < 1e-6, "host updates editor knob");
    }
    phase("layer editor");
    {
        using namespace vox::fx;
        auto processor = std::make_unique<FxProcessor>();
        auto panelStorage3=std::make_unique<vox::fx::FxPanel>(*processor); auto& panel=*panelStorage3;
        panel.setSize(1040,700);
        auto& toggle = panel.programCurvesButton();
        auto& curves = panel.programCurves();
        check(!curves.isVisible() && panel.layerPanel(0).isVisible(), "opens on layer cards");
        toggle.setToggleState(true,juce::sendNotificationSync);
        check(curves.isVisible() && !panel.layerPanel(0).isVisible() && curves.editor().getHeight()>100,
              "Program trajectory view replaces layer cards at the minimum size");
        processor->outputParameter().setValueNotifyingHost(.4f);
        for (int id=1; id<=4; ++id) {
            curves.selector().setSelectedId(id,juce::sendNotificationSync);
            const float value = static_cast<float>(id)*.15f;
            check(curves.editor().setCurve(timeline::Curve::constant(value)), "Program trajectory editor applies change");
        }
        const auto edited = processor->program();
        check(edited.amplitudeCurve.evaluate(.5f)==.15f && edited.pitchCurve.evaluate(.5f)==2*.15f &&
              edited.cutoffCurve.evaluate(.5f)==3*.15f && edited.panoramaCurve.evaluate(.5f)==4*.15f &&
              edited.layers[0].panCurve.evaluate(.5f)==.5f && std::abs(processor->outputParameter().get()-.4f)<1e-6f,
              "four Program bindings leave layer and automation values alone");
        check(curves.editor().resetCurve() && processor->program().panoramaCurve.evaluate(.5f)==.5f,
              "Reset returns the selected trajectory to neutral");
        check(curves.editor().undo() && processor->program().panoramaCurve.evaluate(.5f)==4*.15f,
              "Program reset is undoable");
        auto state = save(*processor);
        auto recalled = std::make_unique<FxProcessor>();
        recalled->setStateInformation(state.getData(),static_cast<int>(state.getSize()));
        check(recalled->program().panoramaCurve.evaluate(.5f)==4*.15f &&
              recalled->program().amplitudeCurve.evaluate(.5f)==.15f, "Program curves round trip through host state");
        processor->setStateInformation(state.getData(),static_cast<int>(state.getSize()));
        check(!curves.editor().redo(), "Program restore invalidates curve actions from the old revision");
        toggle.setToggleState(false,juce::sendNotificationSync);
        check(!curves.isVisible() && panel.layerPanel(0).isVisible(), "layer cards remain accessible");
    }
    phase("generation");
    {
        auto generated = std::make_unique<vox::fx::FxProcessor>();
        auto panelStorage4=std::make_unique<vox::fx::FxPanel>(*generated); auto& panel=*panelStorage4;
        generated->durationParameter().setValueNotifyingHost(generated->durationParameter().convertTo0to1(7.0f));
        panel.familySelector().setSelectedId(4, juce::sendNotificationSync);
        panel.factorySelector().setSelectedId(21,juce::sendNotificationSync);
        check(generated->family()==5 && std::abs(generated->durationParameter().get()-7.0f)<1e-4f,
              "factory seed selection installs real Program without overwriting duration automation");
        panel.familySelector().setSelectedId(4, juce::sendNotificationSync);
        panel.seedSlider().setValue(123, juce::sendNotificationSync);
        panel.generateButton().onClick();
        auto before=generated->program();
        check(before.layers[0].family==vox::fx::NoiseFamily::impact && before.layers[3].enabled,
              "editor generate publishes a four-layer program");
        check(std::abs(generated->durationParameter().get()-7.0f)<1e-4f,"generator preserves host duration automation");
        auto saved=save(*generated);auto recalled=std::make_unique<vox::fx::FxProcessor>();
        recalled->setStateInformation(saved.getData(),static_cast<int>(saved.getSize()));
        std::vector<std::byte> expected,actual;
        vox::fx::programcodec::encode(before,expected);vox::fx::programcodec::encode(recalled->program(),actual);
        check(expected==actual,"generated program survives host state recall");
        panel.layerLock(0).setToggleState(true,juce::sendNotificationSync);
        auto generatorSaved=save(*generated);auto generatorRecall=std::make_unique<vox::fx::FxProcessor>();
        generatorRecall->setStateInformation(generatorSaved.getData(),static_cast<int>(generatorSaved.getSize()));
        auto recalledPanelStorage5=std::make_unique<vox::fx::FxPanel>(*generatorRecall); auto& recalledPanel=*recalledPanelStorage5;
        check(recalledPanel.seedSlider().getValue()==123 && recalledPanel.layerLock(0).getToggleState(),"generator seed and locks recalled in editor");
        auto invalidGenerator=vox::preset::decode(std::span(static_cast<const std::byte*>(generatorSaved.getData()),generatorSaved.getSize()),vox::preset::makeTag("VXFX"),1);
        for(auto& section:invalidGenerator.document.sections)if(section.tag==vox::fx::fxGeneratorSectionTag)section.data[0]=std::byte{5};
        const auto invalidGeneratorBytes=vox::preset::encode(invalidGenerator.document);
        if(invalidGeneratorBytes){generatorRecall->setStateInformation(invalidGeneratorBytes->data(),static_cast<int>(invalidGeneratorBytes->size()));
            check(generatorRecall->lastStateError().isNotEmpty()&&save(*generatorRecall)==generatorSaved,"future generator state rejected transactionally");}
        else check(false,"encode invalid generator state");
        panel.mutateButton().onClick();
        check(generated->program().layers[0].gain==before.layers[0].gain,"editor lock preserves selected layer");
        vox::fx::programcodec::encode(generated->program(),actual);
        check(expected!=actual,"editor mutate changes the real program");
        vox::fx::generator::Locks locks;locks.layers[0]=vox::fx::generator::all;
        auto locked=generated->program();
        check(generated->mutateProgram(222,locks),"locked mutation accepted");
        check(generated->program().layers[0].gain==locked.layers[0].gain,"locked layer preserved");
        auto& settingsPanel=panel.generatorPanel();
        settingsPanel.styleSelector().setSelectedId(4,juce::sendNotificationSync);
        settingsPanel.complexitySelector().setSelectedId(2,juce::sendNotificationSync);
        settingsPanel.intensitySlider().setValue(.2,juce::sendNotificationSync);
        settingsPanel.energySlider().setValue(.4,juce::sendNotificationSync);
        settingsPanel.stereoSlider().setValue(.8,juce::sendNotificationSync);
        const auto controls=generated->generatorState();
        check(controls.style==3&&controls.complexity==2&&std::abs(controls.intensity-.2f)<1e-5f&&
              std::abs(controls.energy-.4f)<1e-5f&&std::abs(controls.stereo-.8f)<1e-5f,"generator settings UI binds controller");
        panel.generateButton().onClick();
        check(generated->program().layers[1].enabled&&!generated->program().layers[2].enabled&&
              !generated->program().layers[3].enabled,"generator uses selected complexity");
        settingsPanel.lockLayerSelector().setSelectedId(2,juce::sendNotificationSync);
        settingsPanel.fieldLock(3).setToggleState(true,juce::sendNotificationSync);
        settingsPanel.fieldLock(4).setToggleState(true,juce::sendNotificationSync);
        check(generated->generatorState().locks.layers[1]==(vox::fx::generator::gain|vox::fx::generator::pan),
              "field lock controls store only selected layer fields");
        const auto protectedLayer=generated->program().layers[1];panel.mutateButton().onClick();
        check(generated->program().layers[1].gain==protectedLayer.gain&&generated->program().layers[1].pan==protectedLayer.pan,
              "field locks protect gain and pan during mutation");
        const auto settingsSaved=save(*generated);auto settingsRecall=std::make_unique<vox::fx::FxProcessor>();
        settingsRecall->setStateInformation(settingsSaved.getData(),static_cast<int>(settingsSaved.getSize()));
        const auto restored=settingsRecall->generatorState();
        check(restored.style==controls.style&&restored.complexity==controls.complexity&&
              restored.intensity==controls.intensity&&restored.energy==controls.energy&&restored.stereo==controls.stereo&&
              restored.locks.layers[1]==(vox::fx::generator::gain|vox::fx::generator::pan),
              "generator controls and field locks survive project state roundtrip");
    }
    phase("FX slots, comparison and Source editor");
    {
        auto effected=std::make_unique<vox::fx::FxProcessor>();
        auto dry=std::make_unique<vox::fx::FxProcessor>();
        auto panelStorage6=std::make_unique<vox::fx::FxPanel>(*effected); auto& panel=*panelStorage6;
        check(panel.comparisonSelector().getNumItems()==4,"A/B comparison actions exposed");
        check(panel.midiExportButton().isEnabled() && panel.midiExportButton().getTooltip().contains("host tempo"),
              "MIDI export control communicates captured host tempo");
        check(panel.effectSelector(0).getNumItems()==static_cast<int>(vox::fx::effects::Type::count),
              "every implemented effect is selectable");
        check(panel.effectSelector(0).getItemText(11)=="Reverb", "last effect appears in selector");
        check(!effected->recallComparison(0),"empty comparison recall rejected");
        effected->outputParameter().setValueNotifyingHost(.4f);
        panel.comparisonSelector().setSelectedId(1,juce::sendNotificationSync);
        effected->setFamily(3);effected->outputParameter().setValueNotifyingHost(.8f);
        effected->setEffect(3,{vox::fx::effects::Type::reverb,1,.5f});
        check(effected->setMacroGainRoute(0,.25f,0),"comparison B route");
        auto comparePattern=effected->patternSnapshot();comparePattern.steps[0].enabled=true;
        check(effected->setPattern(comparePattern),"comparison B pattern");
        panel.comparisonSelector().setSelectedId(3,juce::sendNotificationSync);
        panel.comparisonSelector().setSelectedId(2,juce::sendNotificationSync);
        check(effected->family()==0&&std::abs(effected->outputParameter().get()-.4f)<1e-5f &&
              effected->effect(3).type==vox::fx::effects::Type::bypass && effected->macroConfig().count==0 &&
              !effected->patternSnapshot().steps[0].enabled,"comparison A recalls full base state");
        panel.comparisonSelector().setSelectedId(4,juce::sendNotificationSync);
        check(effected->family()==3&&std::abs(effected->outputParameter().get()-.8f)<1e-5f &&
              effected->effect(3).type==vox::fx::effects::Type::reverb && effected->macroConfig().count==1 &&
              effected->patternSnapshot().steps[0].enabled,"comparison B recalls independent state");
        {
            auto bankState=save(*effected);auto reopened=std::make_unique<vox::fx::FxProcessor>();
            reopened->setStateInformation(bankState.getData(),static_cast<int>(bankState.getSize()));
            check(reopened->recallComparison(0)&&reopened->family()==0&&std::abs(reopened->outputParameter().get()-.4f)<1e-5f,
                  "closed editor A/B bank survives reopen");
            check(reopened->recallComparison(1)&&reopened->family()==3&&reopened->recallComparison(0)&&reopened->family()==0,
                  "recall never consumes either comparison slot");
            std::vector<std::byte> preset;
            check(effected->encodePortablePreset(preset),"encode portable sound");
            reopened->outputParameter().setValueNotifyingHost(.21f);
            check(reopened->restorePortablePreset(preset)&&reopened->family()==3&&std::abs(reopened->outputParameter().get()-.21f)<1e-5f,
                  "user preset replaces structural state but leaves automation alone");
            check(reopened->recallComparison(0)&&reopened->family()==0,"portable preset never replaces comparison slots");
            auto decoded=vox::preset::decode({static_cast<const std::byte*>(bankState.getData()),bankState.getSize()},vox::fx::compare::productTag,1);
            vox::fx::compare::Bank slots;
            for(auto& section:decoded.document.sections)if(section.tag==vox::fx::compare::sectionTag){
                check(vox::fx::compare::decode(section.data,slots),"read saved comparison bank");
                vox::preset::StateDocument inner;
                if(vox::fx::compare::slotDocument(slots[0],inner)){
                    for(auto& payload:inner.sections)if(payload.tag==vox::preset::makeTag("FPRG"))payload.data.clear();
                    if(auto invalid=vox::preset::encode(inner))slots[0]=std::move(*invalid);
                    section=vox::fx::compare::encode(slots);
                }
            }
            if(auto malformed=vox::preset::encode(decoded.document)){
                const auto before=save(*reopened);
                reopened->setStateInformation(malformed->data(),static_cast<int>(malformed->size()));
                check(reopened->lastStateError().isNotEmpty()&&save(*reopened)==before,"bad comparison payload rolls back whole restore");
            }else check(false,"encode malformed comparison fixture");
        }
        {
            auto dragProcessor=std::make_unique<vox::fx::FxProcessor>();auto dragPanelStorage7=std::make_unique<vox::fx::FxPanel>(*dragProcessor); auto& dragPanel=*dragPanelStorage7;
            auto& source=dragPanel.sourcePanel();
            source.typeSelector(0).setSelectedId(2,juce::sendNotificationSync);
            source.cutoffSlider(0).setValue(800,juce::sendNotificationSync);
            const auto captured=save(*dragProcessor);
            auto& cutoff=source.cutoffSlider(0);
            {
                juce::Slider::ScopedDragNotification gesture(cutoff);
                cutoff.setValue(1000,juce::sendNotificationAsync);
                check(dragProcessor->program().layers[0].filters[0].hz==1000,"drag writes synchronously before restore");
                dragProcessor->setStateInformation(captured.getData(),static_cast<int>(captured.getSize()));
                source.refresh();cutoff.setValue(1250,juce::sendNotificationAsync);
                check(dragProcessor->program().layers[0].filters[0].hz==800,"stale drag write rejected through host restore");
            }
            check(cutoff.getValue()==800,"gesture release reads back restored value");
            cutoff.setValue(900,juce::sendNotificationSync);
            check(dragProcessor->program().layers[0].filters[0].hz==900,"new gesture accepted after restore");
            check(!source.grainLengthSlider().isVisible(),"non-granular source hides grain length");
            check(dragProcessor->editLayer(0,[](auto& l){l.family=vox::fx::NoiseFamily::granular;}),"switch source to granular");
            source.refresh();auto& length=source.grainLengthSlider();
            check(length.isVisible(),"granular source exposes length");
            length.setValue(47.5,juce::sendNotificationSync);
            check(dragProcessor->program().layers[0].grains.milliseconds==47.5f,"native grain length publishes FPRG7");
            const auto grains=save(*dragProcessor);
            {
                juce::Slider::ScopedDragNotification gesture(length);
                length.setValue(71,juce::sendNotificationAsync);
                dragProcessor->setStateInformation(grains.getData(),static_cast<int>(grains.getSize()));
                source.refresh();length.setValue(96,juce::sendNotificationAsync);
                check(dragProcessor->program().layers[0].grains.milliseconds==47.5f,"stale grain drag cannot overwrite host restore");
            }
            check(length.getValue()==47.5,"native grain length refreshes after stale drag");
        }
        check(effected->setMacroGainRoute(0,0,0),"clear comparison test route");
        effected->outputParameter().setValueNotifyingHost(1.0f);
        panel.effectSelector(0).setSelectedId(3,juce::sendNotificationSync);
        panel.effectWet(0).setValue(.7,juce::sendNotificationSync);
        check(effected->effect(0).type==vox::fx::effects::Type::drive &&
              std::abs(effected->effectWetParameter(0).get()-.7f)<1e-5f,"effect UI updates host-automatable slot wet");
        effected->effectAmountParameter(0).setValueNotifyingHost(.3f);
        vox::fx::generator::Settings settings;check(effected->generateProgram(settings,{}),"generate while slot automation active");
        check(std::abs(effected->effectWetParameter(0).get()-.7f)<1e-5f &&
              std::abs(effected->effectAmountParameter(0).get()-.3f)<1e-5f,"generation preserves FX automation");
        panel.effectSelector(1).setSelectedId(8,juce::sendNotificationSync);
        check(effected->effect(1).type==vox::fx::effects::Type::phaser,"editor selects phaser");
        panel.effectSelector(2).setSelectedId(9,juce::sendNotificationSync);
        check(effected->effect(2).type==vox::fx::effects::Type::flanger,"editor selects flanger");
        panel.effectSelector(3).setSelectedId(12,juce::sendNotificationSync);
        check(effected->effect(3).type==vox::fx::effects::Type::reverb,"editor selects reverb");
        {auto reverbState=save(*effected);auto reverbRecall=std::make_unique<vox::fx::FxProcessor>();
            reverbRecall->setStateInformation(reverbState.getData(),static_cast<int>(reverbState.getSize()));
            check(reverbRecall->effect(3).type==vox::fx::effects::Type::reverb,"reverb type recalled");}
        panel.effectSelector(3).setSelectedId(13,juce::sendNotificationSync);
        {
            const auto synced=save(*effected);auto recall=std::make_unique<vox::fx::FxProcessor>();
            recall->setStateInformation(synced.getData(),static_cast<int>(synced.getSize()));
            check(recall->lastStateError().isEmpty()&&recall->effect(3).type==vox::fx::effects::Type::tempoDelay,
                  "editor and FXSL v3 restore the tempo delay without reassigning host IDs");
            const auto decoded=vox::preset::decode(std::span(static_cast<const std::byte*>(synced.getData()),synced.getSize()),vox::preset::makeTag("VXFX"),1);
            auto misversioned=decoded.document;
            for(auto& section:misversioned.sections)if(section.tag==vox::fx::fxEffectsSectionTag)section.data[0]=std::byte{2};
            const auto payload=vox::preset::encode(misversioned);
            if(payload){recall->setStateInformation(payload->data(),static_cast<int>(payload->size()));
                check(recall->lastStateError().isNotEmpty()&&save(*recall)==synced,"v2 payload cannot introduce a v3 effect type");}
            else check(false,"encode invalid version test fixture");
        }
        panel.effectSelector(3).setSelectedId(10,juce::sendNotificationSync);
        check(effected->effect(3).type==vox::fx::effects::Type::delay,"editor selects delay");
        auto state=save(*effected);auto restoredFx=std::make_unique<vox::fx::FxProcessor>();
        restoredFx->setStateInformation(state.getData(),static_cast<int>(state.getSize()));
        check(restoredFx->effect(0).type==effected->effect(0).type &&
              std::abs(restoredFx->effectWetParameter(0).get()-.7f)<1e-5f &&
              restoredFx->effect(1).type==vox::fx::effects::Type::phaser &&
              restoredFx->effect(2).type==vox::fx::effects::Type::flanger &&
              restoredFx->effect(3).type==vox::fx::effects::Type::delay,"effects restored in state");
        auto legacy=vox::preset::decode(std::span(static_cast<const std::byte*>(state.getData()),state.getSize()),vox::preset::makeTag("VXFX"),1);
        for(auto& section:legacy.document.sections)if(section.tag==vox::fx::fxEffectsSectionTag){
            section.data.clear();std::uint64_t old=0;
            for(std::size_t i=0;i<4;++i){const auto settings=effected->effect(i);
                const auto legacyType=static_cast<unsigned>(settings.type)>7?0u:static_cast<unsigned>(settings.type);
                const auto packed=static_cast<std::uint16_t>(legacyType|
                    (static_cast<unsigned>(std::lround(settings.wet*63))<<3)|
                    (static_cast<unsigned>(std::lround(settings.amount*127))<<9));
                old|=static_cast<std::uint64_t>(packed)<<(i*16);
            }
            for(const auto word:{std::uint32_t{1},static_cast<std::uint32_t>(old),static_cast<std::uint32_t>(old>>32)})
                for(int shift=0;shift<32;shift+=8)section.data.push_back(static_cast<std::byte>((word>>shift)&255));
        }
        const auto old=vox::preset::encode(legacy.document);auto migratedFx=std::make_unique<vox::fx::FxProcessor>();
        if(old){migratedFx->setStateInformation(old->data(),static_cast<int>(old->size()));
            check(migratedFx->lastStateError().isEmpty()&&migratedFx->effect(0).type==vox::fx::effects::Type::drive &&
                  std::abs(migratedFx->effectWetParameter(0).get()-migratedFx->effectWetParameter(0).getNormalisableRange().snapToLegalValue(std::round(.7f*63)/63))<1e-5f &&
                  std::abs(migratedFx->effectAmountParameter(0).get()-migratedFx->effectAmountParameter(0).getNormalisableRange().snapToLegalValue(std::round(.3f*127)/127))<1e-5f,
                  "FXSL v1 migrates type and quantizes legacy values to the unchanged JUCE host steps");}
        else check(false,"encode old FXSL");
        auto document=vox::preset::decode(std::span(static_cast<const std::byte*>(state.getData()),state.getSize()),vox::preset::makeTag("VXFX"),1);
        for(auto& section:document.document.sections)if(section.tag==vox::fx::fxEffectsSectionTag)section.data[4]=std::byte{0xff};
        const auto bad=vox::preset::encode(document.document);
        if(bad){restoredFx->setStateInformation(bad->data(),static_cast<int>(bad->size()));
            check(restoredFx->lastStateError().isNotEmpty()&&save(*restoredFx)==state,"invalid effects state rejected transactionally");}
        check(dry->setProgram(effected->program()),"dry and effected tests use identical Program");
        vox::fx::offline::Settings render;render.sampleRate=48000;render.note=60;render.velocity=100.0f/127.0f;
        render.format=vox::fx::offline::WavFormat::float32;
        const auto capture=effected->captureProgramWav(render);
        check(capture.settings.effects[1].type==vox::fx::effects::Type::phaser &&
              std::abs(capture.settings.effects[0].wet-.7f)<1e-5f,"WAV capture includes FX and automation");
        std::vector<std::uint8_t> wav, capturedWav;
        check(effected->renderProgramWav(render,wav) && vox::fx::offline::renderWav(capture.program,capture.settings,capturedWav)
              && wav==capturedWav,"captured worker render matches controller WAV");
        effected->prepareToPlay(48000,512);dry->prepareToPlay(48000,512);
        juce::AudioBuffer<float> a(2,512),b(2,512);juce::MidiBuffer n1,n2;
        n1.addEvent(juce::MidiMessage::noteOn(1,60,static_cast<juce::uint8>(100)),0);
        n2.addEvent(juce::MidiMessage::noteOn(1,60,static_cast<juce::uint8>(100)),0);
        block(*effected,a,n1);block(*dry,b,n2);double difference=0;
        for(int i=0;i<512;++i)difference+=std::abs(a.getSample(0,i)-b.getSample(0,i));
        check(difference>1e-5,"serial shared effect changes processor output");
        if(wav.size()>=56+512*8)for(int i=0;i<512;++i){const auto at=static_cast<std::size_t>(56+i*8);
            const auto bits=static_cast<std::uint32_t>(wav[at])|(static_cast<std::uint32_t>(wav[at+1])<<8)|
                (static_cast<std::uint32_t>(wav[at+2])<<16)|(static_cast<std::uint32_t>(wav[at+3])<<24);
            check(std::bit_cast<float>(bits)==a.getSample(0,i),"controller WAV matches processor output");
        }
    }
    phase("pattern editor");
    {
        auto owner=std::make_unique<vox::fx::FxProcessor>();vox::fx::pattern::Pattern pattern;
        auto panelStorage8=std::make_unique<vox::fx::FxPanel>(*owner); auto& panel=*panelStorage8;
        panel.patternLengthSelector().setSelectedId(4,juce::sendNotificationSync);
        panel.patternBarSelector().setSelectedId(4,juce::sendNotificationSync);
        panel.patternStep(2).setToggleState(true,juce::sendNotificationSync);
        check(owner->patternSnapshot().bars==4 && owner->patternSnapshot().steps[50].enabled,"editor changes bar four step");
        panel.patternStepSelector().setSelectedId(3,juce::sendNotificationSync);
        panel.patternNoteSlider().setValue(72,juce::sendNotificationSync);
        panel.patternVelocitySlider().setValue(90,juce::sendNotificationSync);
        panel.patternProbabilitySlider().setValue(55,juce::sendNotificationSync);
        panel.patternRatchetSlider().setValue(3,juce::sendNotificationSync);
        const auto editedStep=owner->patternSnapshot().steps[50];
        check(editedStep.note==72 && editedStep.velocity==90 && editedStep.probability==55 && editedStep.ratchets==3,
              "editor changes selected step detail");
        panel.patternGridSelector().setSelectedId(8,juce::sendNotificationSync);
        panel.patternPageSelector().setSelectedId(3,juce::sendNotificationSync);
        panel.patternSwingSlider().setValue(.7,juce::sendNotificationSync);
        check(std::abs(owner->patternSnapshot().swing-.7f)<.001f,"editor controls pattern swing");
        panel.patternStep(2).setToggleState(true,juce::sendNotificationSync);
        check(owner->patternSnapshot().grid==vox::fx::pattern::Grid::thirtysecondTriplet &&
              owner->patternSnapshot().steps[178].enabled,"editor addresses 32nd triplet last-bar page");
        const auto beforePaste=owner->patternSnapshot();
        const auto command=[&](int id){panel.patternActionsSelector().setSelectedId(id,juce::sendNotificationSync);};
        command(5);
        panel.patternBarSelector().setSelectedId(2,juce::sendNotificationSync);
        command(6);
        const auto afterPaste=owner->patternSnapshot();
        check(afterPaste!=beforePaste&&std::equal(afterPaste.steps.begin()+48,afterPaste.steps.begin()+96,beforePaste.steps.begin()+144)&&
              std::equal(afterPaste.steps.begin()+144,afterPaste.steps.end(),beforePaste.steps.begin()+144)&&
              afterPaste.swing==beforePaste.swing&&afterPaste.seed==beforePaste.seed,
              "paste copies complete selected bar across pages without overwriting the source or global controls");
        command(3);check(owner->patternSnapshot()==beforePaste,"whole bar paste is one undo transaction");
        command(4);check(owner->patternSnapshot()==afterPaste,"redo restores the complete pasted bar");
        command(6);command(3);check(owner->patternSnapshot()==beforePaste,"identical paste does not insert a second history entry");
        panel.patternGridSelector().setSelectedId(1,juce::sendNotificationSync);
        const auto otherGrid=owner->patternSnapshot();
        command(6);check(owner->patternSnapshot()==otherGrid,"native editor rejects bar paste into a different grid");
        check(panel.patternPageSelector().getSelectedId()==1,"shorter grid reveals a valid page");
        panel.patternLengthSelector().setSelectedId(1,juce::sendNotificationSync);
        check(panel.patternBarSelector().getSelectedId()==1,"shorter pattern reveals a valid bar");
        panel.patternGridSelector().setSelectedId(1,juce::sendNotificationSync);
        panel.patternPageSelector().setSelectedId(1,juce::sendNotificationSync);
        panel.patternStepSelector().setSelectedId(16,juce::sendNotificationSync);
        check(panel.patternStepSelector().isEnabled() && !panel.patternNoteSlider().isEnabled(),
              "step selector remains usable when previous step is outside grid");
        panel.patternStepSelector().setSelectedId(1,juce::sendNotificationSync);
        check(panel.patternNoteSlider().isEnabled(),"valid step re-enables detail controls");
        panel.patternSelector().setSelectedId(8,juce::sendNotificationSync);
        check(vox::fx::pattern::factory(7,pattern)&&owner->patternSnapshot().seed==pattern.seed,"pattern selector installs real pattern");
        const auto beforeSwing=owner->patternSnapshot();
        {
            juce::Slider::ScopedDragNotification gesture(panel.patternSwingSlider());
            panel.patternSwingSlider().setValue(.31,juce::sendNotificationAsync);
            panel.patternSwingSlider().setValue(.58,juce::sendNotificationAsync);
        }
        panel.patternActionsSelector().setSelectedId(3,juce::sendNotificationSync);
        check(owner->patternSnapshot()==beforeSwing,"Pattern undo rolls an entire slider drag back together");
        panel.patternActionsSelector().setSelectedId(4,juce::sendNotificationSync);
        check(std::abs(owner->patternSnapshot().swing-.58f)<.001f,"Pattern redo restores the drag endpoint");
        panel.patternActionsSelector().setSelectedId(3,juce::sendNotificationSync);
        const auto patternProject=save(*owner);
        {
            juce::Slider::ScopedDragNotification gesture(panel.patternSwingSlider());
            panel.patternSwingSlider().setValue(.61,juce::sendNotificationAsync);
            owner->setStateInformation(patternProject.getData(),static_cast<int>(patternProject.getSize()));
            panel.patternSwingSlider().setValue(.64,juce::sendNotificationAsync);
        }
        check(owner->patternSnapshot()==beforeSwing,"stale Pattern slider after host restore cannot change the schedule");
        std::vector<std::uint8_t> exported,expected;
        check(owner->exportPatternMidi(120,exported)&&vox::fx::pattern::exportMidi(pattern,120,expected)&&exported==expected,
              "processor exports canonical trigger MIDI");
        auto saved=save(*owner);auto restored=std::make_unique<vox::fx::FxProcessor>();
        restored->setStateInformation(saved.getData(),static_cast<int>(saved.getSize()));
        std::vector<std::uint8_t> recalled;
        check(restored->exportPatternMidi(120,recalled)&&recalled==exported,"trigger pattern recalled in host state");
        auto document=vox::preset::decode(std::span(static_cast<const std::byte*>(saved.getData()),saved.getSize()),vox::preset::makeTag("VXFX"),1);
        for(auto& section:document.document.sections)if(section.tag==vox::fx::fxPatternSectionTag)section.data[0]=std::byte{4};
        const auto bad=vox::preset::encode(document.document);
        if(bad){restored->setStateInformation(bad->data(),static_cast<int>(bad->size()));
            check(restored->lastStateError().isNotEmpty()&&save(*restored)==saved,"invalid pattern restore is transactional");}
        else check(false,"encode invalid pattern state");
    }
    phase("pattern playback");
    {
        auto sequenced=std::make_unique<vox::fx::FxProcessor>();auto manual=std::make_unique<vox::fx::FxProcessor>();
        vox::fx::pattern::Pattern pattern;pattern.steps[0].enabled=true;
        check(sequenced->setPattern(pattern),"install sequencer pattern");
        sequenced->patternEnabledParameter().setValueNotifyingHost(1.0f);
        TestPlayHead head;juce::AudioPlayHead::PositionInfo pos;
        pos.setIsPlaying(true);pos.setTimeInSamples(0);pos.setPpqPosition(0.0);pos.setBpm(120.0);
        head.position=pos;sequenced->setPlayHead(&head);
        sequenced->prepareToPlay(48000,512);manual->prepareToPlay(48000,512);
        juce::AudioBuffer<float> a(2,512),b(2,512);juce::MidiBuffer noMidi,note;
        note.addEvent(juce::MidiMessage::noteOn(1,60,static_cast<juce::uint8>(100)),0);
        block(*sequenced,a,noMidi);block(*manual,b,note);
        for(int i=0;i<512;++i)check(a.getSample(0,i)==b.getSample(0,i),"host pattern downbeat matches MIDI trigger");
        using Clock=vox::fx::FxProcessor::PatternClock;
        check(sequenced->patternClock()==Clock::readyAssumed44,"missing signature is explicitly reported as assumed 4/4");
        const auto expectClock=[&](Clock value){head.position=pos;block(*sequenced,a,noMidi);check(sequenced->patternClock()==value,"pattern reports exact clock admission reason");};
        pos.setBpm(9);expectClock(Clock::invalidTempo);
        pos.setBpm(120);pos.setPpqPosition({});expectClock(Clock::invalidPpq);
        pos.setPpqPosition(0.0);pos.setTimeSignature(juce::AudioPlayHead::TimeSignature{3,4});expectClock(Clock::unsupportedMeter);
        pos.setIsPlaying(false);expectClock(Clock::stopped);
        head.position={};block(*sequenced,a,noMidi);
        check(sequenced->patternClock()==Clock::noPlayhead,"missing transport is not represented as playing");
        pos.setIsPlaying(true);pos.setTimeSignature(juce::AudioPlayHead::TimeSignature{4,4});expectClock(Clock::ready44);
        sequenced->releaseResources();check(sequenced->patternClock()==Clock::inactive,"release clears stale Pattern status");
        auto shifted=std::make_unique<vox::fx::FxProcessor>();auto manualShifted=std::make_unique<vox::fx::FxProcessor>();
        vox::fx::pattern::Pattern shiftedPattern;shiftedPattern.steps[1].enabled=true;
        check(shifted->setPattern(shiftedPattern),"install shifted pattern");
        shifted->patternEnabledParameter().setValueNotifyingHost(1.0f);
        TestPlayHead shiftedHead;juce::AudioPlayHead::PositionInfo shiftedPosition;
        shiftedPosition.setIsPlaying(true);shiftedPosition.setTimeInSamples(0);
        shiftedPosition.setPpqPosition(.25-100.0*120.0/(60.0*48000.0));shiftedPosition.setBpm(120.0);
        shiftedHead.position=shiftedPosition;shifted->setPlayHead(&shiftedHead);
        shifted->prepareToPlay(48000,512);manualShifted->prepareToPlay(48000,512);
        note.addEvent(juce::MidiMessage::noteOn(1,60,static_cast<juce::uint8>(100)),100);
#ifdef VOX_TEST_INTERPOSE_MALLOC
        voxProbeBegin();
#endif
        shifted->processBlock(a,noMidi);
#ifdef VOX_TEST_INTERPOSE_MALLOC
        const auto patternAllocations=voxProbeEnd();
        if(voxProbeAvailable())check(patternAllocations==0,"host pattern callback allocates nothing");
#endif
        block(*manualShifted,b,note);
        check(a.getMagnitude(0,0,100)==0 && a.getMagnitude(0,101,411)>0,"pattern trigger sample offset");
        for(int i=0;i<512;++i)check(a.getSample(0,i)==b.getSample(0,i),"shifted pattern matches MIDI trigger");
        auto state=save(*sequenced);auto restored=std::make_unique<vox::fx::FxProcessor>();
        restored->setStateInformation(state.getData(),static_cast<int>(state.getSize()));
        check(restored->patternEnabledParameter().get(),"pattern enable restored");
    }
    phase("modulation");
    {
        auto modulated=std::make_unique<vox::fx::FxProcessor>();auto dry=std::make_unique<vox::fx::FxProcessor>();
        auto panelStorage9=std::make_unique<vox::fx::FxPanel>(*modulated); auto& panel=*panelStorage9;
        panel.macroDepthSlider().setValue(-.5,juce::sendNotificationSync);
        check(modulated->macroConfig().count==1,"editor assigns typed macro gain route");
        check(modulated->setMacroGainRoute(0,-.5f,0),"assign macro to output gain");
        panel.macroValueSlider().setValue(1.0,juce::sendNotificationSync);
        panel.macroSelector().setSelectedId(2,juce::sendNotificationSync);
        check(modulated->macroParameter(0).get()==1.0f && modulated->macroParameter(1).get()==.5f,
              "switching macro editor preserves unrelated automation lanes");
        panel.macroSelector().setSelectedId(1,juce::sendNotificationSync);
        panel.macroDestinationSelector().setSelectedId(2,juce::sendNotificationSync);
        panel.macroDepthSlider().setValue(.5,juce::sendNotificationSync);
        check(modulated->macroConfig().count==2 && modulated->macroConfig().routes[1].destination==vox::fx::mod::Destination::pan,
              "editor adds pan without replacing gain route");
        auto panState=save(*modulated);auto panRecall=std::make_unique<vox::fx::FxProcessor>();
        panRecall->setStateInformation(panState.getData(),static_cast<int>(panState.getSize()));
        check(panRecall->macroConfig()==modulated->macroConfig(),"gain and pan routes recalled together");
        panel.macroDepthSlider().setValue(0,juce::sendNotificationSync);
        panel.macroDestinationSelector().setSelectedId(1,juce::sendNotificationSync);
        panel.macroSelector().setSelectedId(9,juce::sendNotificationSync);
        check(!panel.macroValueSlider().isEnabled(),"fixed LFO source does not expose fake macro value");
        panel.macroDepthSlider().setValue(.25,juce::sendNotificationSync);
        check(modulated->macroConfig().count==2,"editor assigns LFO gain route");
        panel.macroDepthSlider().setValue(0,juce::sendNotificationSync);
        panel.macroSelector().setSelectedId(1,juce::sendNotificationSync);
        check(modulated->outputParameter().get()==1.0f,"macro leaves host output automation base unchanged");
        auto state=save(*modulated);auto restored=std::make_unique<vox::fx::FxProcessor>();
        restored->setStateInformation(state.getData(),static_cast<int>(state.getSize()));
        check(restored->macroConfig().count==1&&restored->macroParameter(0).get()==1,"macro routes and values recalled");
        auto document=vox::preset::decode(std::span(static_cast<const std::byte*>(state.getData()),state.getSize()),vox::preset::makeTag("VXFX"),1);
        for(auto& section:document.document.sections)if(section.tag==vox::fx::fxMacroRouteSectionTag)section.data[0]=std::byte{3};
        const auto bad=vox::preset::encode(document.document);
        if(bad){restored->setStateInformation(bad->data(),static_cast<int>(bad->size()));
            check(restored->lastStateError().isNotEmpty()&&save(*restored)==state,"future macro routes rejected transactionally");}
        else check(false,"encode invalid macro state");
        modulated->prepareToPlay(48000,512);dry->prepareToPlay(48000,512);
        juce::AudioBuffer<float> a(2,512),b(2,512);juce::MidiBuffer ma,mb;
        ma.addEvent(juce::MidiMessage::noteOn(1,60,static_cast<juce::uint8>(100)),0);
        mb.addEvent(juce::MidiMessage::noteOn(1,60,static_cast<juce::uint8>(100)),0);
        block(*modulated,a,ma);block(*dry,b,mb);
        for(int i=0;i<512;++i)check(std::abs(a.getSample(0,i)-b.getSample(0,i)*.5f)<1e-7f,"macro route affects DSP");
        vox::fx::offline::Settings settings;settings.sampleRate=48000;settings.velocity=100.0f/127.0f;
        settings.format=vox::fx::offline::WavFormat::float32;std::vector<std::uint8_t> wav;
        check(modulated->renderProgramWav(settings,wav),"render macro-modulated WAV");
        if(wav.size()>=56+512*8)for(int i=0;i<512;++i){const auto at=static_cast<std::size_t>(56+i*8);
            const auto bits=static_cast<std::uint32_t>(wav[at])|(static_cast<std::uint32_t>(wav[at+1])<<8)|
                (static_cast<std::uint32_t>(wav[at+2])<<16)|(static_cast<std::uint32_t>(wav[at+3])<<24);
            check(std::bit_cast<float>(bits)==a.getSample(0,i),"macro WAV matches processor");}
    }
    phase("sample import and relink");
    {
        const std::vector<std::uint8_t> fixture{'R','I','F','F',40,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,1,0,
            0x80,0xbb,0,0,0,0x77,1,0,2,0,16,0,'d','a','t','a',4,0,0,0,0,0x40,0,0x40};
        const auto file=juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("vox-fx-processor-sample",".wav",false);
        check(file.replaceWithData(fixture.data(),fixture.size()),"create sample fixture");
        auto user=std::make_unique<vox::fx::FxProcessor>();user->setFamily(11);
        check(user->importSample(file)&&user->waitForSample(std::chrono::seconds(5))&&
              user->sampleStatus()==vox::fx::sample::Controller::Status::ready,"processor asynchronously imports sample");
        const auto sampleState=save(*user);
        {
            auto decoded=vox::preset::decode(std::span(static_cast<const std::byte*>(sampleState.getData()),sampleState.getSize()),vox::preset::makeTag("VXFX"),1);
            vox::fx::sample::Reference pending;pending.kind=vox::fx::sample::Reference::Kind::pending;
            pending.path=file.getFullPathName().toStdString();
            bool found=false;for(auto& section:decoded.document.sections)if(section.tag==vox::fx::fxSampleSectionTag){
                found=vox::fx::sample::referencecodec::encode(pending,section.data);}
            const auto pendingBytes=vox::preset::encode(decoded.document);
            check(found&&pendingBytes.has_value(),"construct pending sample project state");
            if(pendingBytes){
                auto comparison=std::make_unique<vox::fx::FxProcessor>();
                comparison->setStateInformation(pendingBytes->data(),static_cast<int>(pendingBytes->size()));
                check(comparison->sampleReference()==pending&&comparison->sampleStatus()==vox::fx::sample::Controller::Status::missing,
                      "pending project restore remains unavailable");
                check(comparison->storeComparison(0)&&comparison->useBuiltinSample()&&comparison->recallComparison(0)&&
                      comparison->sampleReference()==pending&&comparison->sampleStatus()==vox::fx::sample::Controller::Status::missing,
                      "A/B restores pending identity without trusting unhashed content");
                auto bad=decoded.document;for(auto& section:bad.sections)if(section.tag==vox::fx::fxSampleSectionTag)section.data[0]=std::byte{99};
                const auto invalid=vox::preset::encode(bad);const auto before=save(*comparison);
                if(invalid){comparison->setStateInformation(invalid->data(),static_cast<int>(invalid->size()));
                    check(comparison->lastStateError().isNotEmpty()&&save(*comparison)==before,"invalid sample state rejected transactionally");}
                else check(false,"encode invalid sample project state");
            }
        }
        user->prepareToPlay(48000,512);
        juce::AudioBuffer<float> sound(2,512);juce::MidiBuffer note;
        note.addEvent(juce::MidiMessage::noteOn(1,60,static_cast<juce::uint8>(100)),0);block(*user,sound,note);
        check(sound.getMagnitude(0,0,512)>0,"processor granular sample triggers audio");
        vox::fx::offline::Settings settings;settings.format=vox::fx::offline::WavFormat::float32;
        settings.velocity=100.0f/127.0f;std::vector<std::uint8_t> bytes;
        check(user->renderProgramWav(settings,bytes),"sample-backed processor WAV render");
        if(bytes.size()>=56+512*8)for(int i=0;i<512;++i){const auto at=static_cast<std::size_t>(56+i*8);
            const auto bits=static_cast<std::uint32_t>(bytes[at])|(static_cast<std::uint32_t>(bytes[at+1])<<8)|
                (static_cast<std::uint32_t>(bytes[at+2])<<16)|(static_cast<std::uint32_t>(bytes[at+3])<<24);
            check(std::bit_cast<float>(bits)==sound.getSample(0,i),"sample-backed WAV matches processor");}
        check(file.deleteFile(),"remove loaded sample fixture");
        auto missing=std::make_unique<vox::fx::FxProcessor>();missing->setStateInformation(sampleState.getData(),static_cast<int>(sampleState.getSize()));
        check(missing->waitForSample(std::chrono::seconds(5))&&missing->sampleStatus()==vox::fx::sample::Controller::Status::missing,
              "restored missing sample is explicit");
        missing->prepareToPlay(48000,512);note.addEvent(juce::MidiMessage::noteOn(1,60,static_cast<juce::uint8>(100)),0);
        block(*missing,sound,note);check(sound.getMagnitude(0,0,512)==0,"missing sample never silently falls back");
        const auto before=bytes;check(!missing->renderProgramWav(settings,bytes)&&bytes==before,"missing sample WAV fails transactionally");
        check(missing->setFamily(0),"select non-granular family despite missing sample");
        note.addEvent(juce::MidiMessage::noteOn(1,60,static_cast<juce::uint8>(100)),0);block(*missing,sound,note);
        check(sound.getMagnitude(0,0,512)>0,"missing sample does not suppress non-granular audio");
        check(missing->renderProgramWav(settings,bytes),"non-granular WAV ignores missing sample");
        missing->panic();block(*missing,sound,note);check(missing->setFamily(11),"return to granular family");
        auto samplePanelStorage10=std::make_unique<vox::fx::FxPanel>(*missing); auto& samplePanel=*samplePanelStorage10;
        check(samplePanel.sampleImportButton().isEnabled(),"sample import action exposed");
        samplePanel.sampleBuiltinButton().onClick();
        check(missing->sampleReference().kind==vox::fx::sample::Reference::Kind::builtin,"editor explicitly selects built-in sample");
        note.addEvent(juce::MidiMessage::noteOn(1,60,static_cast<juce::uint8>(100)),0);block(*missing,sound,note);
        check(sound.getMagnitude(0,0,512)>0,"explicit fallback triggers procedural sample");
    }
    phase("tempo capture");
    {
        auto tempoProcessor=std::make_unique<vox::fx::FxProcessor>();TestPlayHead host;
        juce::AudioPlayHead::PositionInfo position;position.setIsPlaying(true);position.setBpm(73.125);host.position=position;
        tempoProcessor->setPlayHead(&host);tempoProcessor->prepareToPlay(48000,512);
        juce::AudioBuffer<float> tempoAudio(2,512);juce::MidiBuffer tempoMidi;
        block(*tempoProcessor,tempoAudio,tempoMidi);
        check(tempoProcessor->captureProgramWav({}).settings.tempo==73.125&&tempoProcessor->observedTempo()==73.125,
              "offline and MIDI export snapshot valid host tempo");
        position.setBpm(std::numeric_limits<double>::quiet_NaN());host.position=position;
        block(*tempoProcessor,tempoAudio,tempoMidi);
        check(tempoProcessor->captureProgramWav({}).settings.tempo==120,"invalid host tempo uses documented default");
        tempoProcessor->setPlayHead(nullptr);
    }
    phase("bar-clock gate and capture");
    {
        auto musical=std::make_unique<vox::fx::FxProcessor>(); TestPlayHead head;
        musical->setPlayHead(&head); musical->prepareToPlay(8000,256);
        auto program=musical->program(); program.bars=1;program.layers[0].family=vox::fx::NoiseFamily::shepard;
        check(musical->setProgram(program),"processor bar program installed");
        juce::AudioBuffer<float> audio(2,256);juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1,60,static_cast<juce::uint8>(100)),0);
        block(*musical,audio,midi);
        check(musical->audioTelemetry().events==0,"bar note rejected without host position");
        std::int64_t sample=0;double ppq=0,energy=0;
        auto run=[&](int total,double bpm,bool trigger){
            while(total>0){const int n=std::min(total,256);audio.setSize(2,n,false,false,true);
                juce::AudioPlayHead::PositionInfo pos;pos.setIsPlaying(true);pos.setTimeInSamples(sample);
                pos.setPpqPosition(ppq);pos.setBpm(bpm);juce::AudioPlayHead::TimeSignature sig{4,4};pos.setTimeSignature(sig);head.position=pos;
                if(trigger){midi.addEvent(juce::MidiMessage::noteOn(1,60,static_cast<juce::uint8>(100)),0);trigger=false;}
                block(*musical,audio,midi);energy+=audio.getMagnitude(0,0,n);sample+=n;ppq+=n*bpm/(60.0*8000.0);total-=n;
            }
        };
        run(4000,120,true);check(energy>0 && musical->audioTelemetry().events==1,"processor renders tonal bar event");
        run(23999,60,false);check(musical->audioTelemetry().events==1,"processor tempo change retains event");
        run(2,60,false);check(musical->audioTelemetry().events==0,"processor bar event ends at boundary");
        musical->setPlayHead(nullptr);
    }
    p.prepareToPlay(48000, 512);
    auto meterProgram=p.program();meterProgram.panoramaCurve=vox::fx::timeline::Curve::constant(.25f);
    check(p.setProgram(meterProgram),"asymmetric stereo meter test program");
    juce::AudioBuffer<float> audio(2, 512);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 100);
#ifdef VOX_TEST_INTERPOSE_MALLOC
    voxProbeBegin();
#endif
    p.processBlock(audio, midi);
#ifdef VOX_TEST_INTERPOSE_MALLOC
    const auto allocations = voxProbeEnd();
    if (voxProbeAvailable()) check(allocations == 0, "no allocations on processor audio thread");
#endif
    check(midi.isEmpty(), "processor consumes MIDI without host MIDI output");
    midi.clear();
    check(audio.getMagnitude(0, 0, 100) == 0, "MIDI offset preserves leading silence");
    check(audio.getMagnitude(0, 101, 411) > 0, "MIDI trigger makes processor audio");
    const auto stereo=p.outputPeaks();
    check(stereo.left>stereo.right&&stereo.left==audio.getMagnitude(0,0,512)&&
          stereo.right==audio.getMagnitude(1,0,512)&&p.outputPeak()==stereo.left,
          "stereo meter reports distinct post-render channel peaks");
    midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
    block(p, audio, midi);
    check(audio.getMagnitude(0, 0, 512) > 0, "one-shot ignores note off");
    midi.addEvent(juce::MidiMessage::allSoundOff(1), 100);
    block(p, audio, midi);
    check(audio.getMagnitude(0, 350, 162) == 0, "CC120 fades to silence");
    midi.addEvent(juce::MidiMessage::noteOn(2, 61, static_cast<juce::uint8>(100)), 0);
    midi.addEvent(juce::MidiMessage::allNotesOff(1), 64);
    block(p, audio, midi);
    check(audio.getMagnitude(0, 350, 162) > 0, "channel-specific panic");
    midi.addEvent(juce::MidiMessage::allNotesOff(2), 0);
    block(p, audio, midi);
    check(audio.getMagnitude(0, 300, 212) == 0, "CC123 fades channel");
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);
    block(p, audio, midi);
    p.reset(); block(p, audio, midi);
    check(audio.getMagnitude(0, 300, 212) == 0, "reset fades active event");

    p.audition(); block(p, audio, midi);
    check(p.audioTelemetry().events>=1&&p.audioTelemetry().events<=12&&p.audioTelemetry().grains<=64,"audio telemetry reports bounded active pool");
    check(audio.getMagnitude(0, 0, 512) > 0, "editor audition triggers sound");
    midi.addEvent(juce::MidiMessage::noteOn(1,60,static_cast<juce::uint8>(100)),0);
    block(p,audio,midi);
    check(p.audioTelemetry().events==2,"host note and audition own distinct voices");
    p.stopAudition();block(p,audio,midi);
    check(p.audioTelemetry().events==1&&audio.getMagnitude(0,300,212)>0,"audition stop leaves the host note sounding");
    p.panic(); block(p, audio, midi);
    check(p.audioTelemetry().events==0&&p.audioTelemetry().grains==0,"audio telemetry reflects panic release");
    check(audio.getMagnitude(0, 300, 212) == 0, "editor panic fades sound");

    phase("bar transitions");
    {
        auto fx=std::make_unique<vox::fx::FxProcessor>();TestPlayHead host;
        fx->setPlayHead(&host);fx->prepareToPlay(8000,256);
        auto program=fx->program();program.mode=vox::fx::PlayMode::transition;program.bars=1;
        check(fx->setProgram(program),"configure processor Transition");
        // Start 256 samples before the target (PPQ 4.5), and trigger at offset 64.
        juce::AudioPlayHead::PositionInfo pos;
        pos.setIsPlaying(true);pos.setBpm(120);pos.setTimeSignature(juce::AudioPlayHead::TimeSignature{4,4});
        pos.setPpqPositionOfLastBarStart(.5);pos.setPpqPosition(4.5-256./4000);
        pos.setTimeInSamples(0);host.position=pos;
        juce::AudioBuffer<float> audio(2,256);juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1,60,.8f),64);
        block(*fx,audio,midi);
        check(audio.getMagnitude(0,0,64)==0 && audio.getMagnitude(0,65,191)>0,
              "aligned processor note honors its MIDI sample offset");
        pos.setTimeInSamples(256);pos.setPpqPosition(4.5);pos.setPpqPositionOfLastBarStart(4.5);host.position=pos;
        block(*fx,audio,midi);
        check(audio.getMagnitude(0,0,256)==0 && fx->audioTelemetry().events==0,
              "aligned processor note stops on shifted host bar line");
        pos.setTimeInSamples(512);pos.setPpqPosition(4.5+256./4000);pos.setTimeSignature(juce::AudioPlayHead::TimeSignature{3,4});host.position=pos;
        midi.addEvent(juce::MidiMessage::noteOn(1,60,.8f),64);block(*fx,audio,midi);
        check(fx->musicalClockUnavailable() && fx->audioTelemetry().events==0,"transition rejects unsupported meter");
        fx->setPlayHead(nullptr);
    }
    phase("voice capacity and telemetry");
    {
        auto busy=std::make_unique<vox::fx::FxProcessor>();busy->prepareToPlay(48000,512);
        juce::AudioBuffer<float> shortBlock(2,64);juce::MidiBuffer notes;
        for(int i=0;i<13;++i)notes.addEvent(juce::MidiMessage::noteOn(1,48+i,static_cast<juce::uint8>(100)),0);
        block(*busy,shortBlock,notes);
        check(busy->audioTelemetry().events==12&&busy->audioTelemetry().dropped==1,
              "voice pool telemetry reports simultaneous retirement exhaustion");
        busy->releaseResources();check(busy->audioTelemetry().events==0&&busy->audioTelemetry().dropped==0,
              "resource release resets voice telemetry");
    }
    phase("transport discontinuity");
    {
        auto transportOwner=std::make_unique<vox::fx::FxProcessor>(); auto& transport=*transportOwner; TestPlayHead playhead;
        transport.setPlayHead(&playhead); transport.prepareToPlay(48000, 512);
        juce::MidiBuffer events;
        playhead.set(true, 0);
        events.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);
        block(transport, audio, events);
        playhead.set(true, 512); block(transport, audio, events);
        check(audio.getMagnitude(0, 300, 212) > 0, "continuous transport preserves event");
        playhead.set(false, 1024); block(transport, audio, events);
        check(audio.getMagnitude(0, 300, 212) == 0, "transport stop fades event");
        playhead.set(true, 2000);
        events.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);
        block(transport, audio, events);
        playhead.set(true, 8000); block(transport, audio, events);
        check(audio.getMagnitude(0, 300, 212) == 0, "transport seek fades event");
        playhead.set(true, 8512);
        events.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);
        block(transport, audio, events);
        playhead.set(true, 1000); block(transport, audio, events);
        check(audio.getMagnitude(0, 300, 212) == 0, "loop jump fades event");
        transport.setPlayHead(nullptr);
    }

    phase("PPQ snapshots");
    {
        auto ppqProcessor=std::make_unique<vox::fx::FxProcessor>();TestPlayHead head;
        juce::AudioPlayHead::PositionInfo position;position.setIsPlaying(true);position.setPpqPosition(2.0);position.setBpm(120);
        head.position=position;ppqProcessor->setPlayHead(&head);ppqProcessor->prepareToPlay(48000,512);
        juce::MidiBuffer notes;notes.addEvent(juce::MidiMessage::noteOn(1,60,static_cast<juce::uint8>(100)),0);
        block(*ppqProcessor,audio,notes);
        position.setPpqPosition(2.0+512.0*120.0/(60.0*48000));head.position=position;block(*ppqProcessor,audio,notes);
        check(audio.getMagnitude(0,300,212)>0,"PPQ-only continuity preserves event");
        position.setPpqPosition(10);head.position=position;block(*ppqProcessor,audio,notes);
        check(audio.getMagnitude(0,300,212)==0,"PPQ-only seek fades event");ppqProcessor->setPlayHead(nullptr);
    }
    phase("source-family playback");
    {
        auto riserOwner=std::make_unique<vox::fx::FxProcessor>(); auto downOwner=std::make_unique<vox::fx::FxProcessor>(); auto& riser=*riserOwner; auto& down=*downOwner;
        riser.prepareToPlay(48000, 512); down.prepareToPlay(48000, 512);
        check(down.setFamily(1) && !down.setFamily(12), "bounded family selection");
        juce::AudioBuffer<float> a(2,512), b(2,512);
        juce::MidiBuffer ma, mb;
        ma.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);
        mb.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);
        block(riser, a, ma); block(down, b, mb);
        double difference = 0;
        for (int i = 0; i < 512; ++i) difference += std::abs(a.getSample(0, i) - b.getSample(0, i));
        check(b.getMagnitude(0, 0, 512) > 0 && difference > 1e-4, "family choice reaches real processor DSP");
        auto shepardOwner=std::make_unique<vox::fx::FxProcessor>(); auto& shepard=*shepardOwner;
        shepard.prepareToPlay(48000, 512); shepard.setFamily(2);
        juce::MidiBuffer mc;
        mc.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);
        block(shepard, b, mc);
        difference = 0;
        for (int i = 0; i < 512; ++i) difference += std::abs(a.getSample(0, i) - b.getSample(0, i));
        check(b.getMagnitude(0, 0, 512) > 0 && difference > 1e-4, "Shepard reaches processor DSP");
        auto impactOwner=std::make_unique<vox::fx::FxProcessor>(); auto& impact=*impactOwner;
        impact.prepareToPlay(48000, 512); impact.setFamily(3);
        juce::MidiBuffer md;
        md.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);
        block(impact, b, md);
        check(b.getMagnitude(0, 0, 512) > 0, "impact reaches processor DSP");
    }
    phase("layered Program");
    {
        auto layeredOwner=std::make_unique<vox::fx::FxProcessor>(); auto& layered=*layeredOwner; layered.prepareToPlay(48000, 512);
        auto program = layered.program(); program.layers[0].enabled = false;
        program.layers[1].enabled = true; program.layers[1].family = vox::fx::NoiseFamily::downlifter;
        program.layers[1].pan = -1.0f;
        check(layered.setProgram(program), "publish layered program");
        juce::MidiBuffer notes;
        notes.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);
        block(layered, audio, notes);
        check(audio.getMagnitude(0, 0, 512) > 0 && audio.getMagnitude(1, 0, 512) == 0,
              "published layer drives real stereo DSP");
        const auto layerState = save(layered);
        auto restoredLayeredOwner=std::make_unique<vox::fx::FxProcessor>(); auto& restoredLayered=*restoredLayeredOwner;
        restoredLayered.setStateInformation(layerState.getData(), static_cast<int>(layerState.getSize()));
        const auto recovered = restoredLayered.program();
        check(restoredLayered.lastStateError().isEmpty() && !recovered.layers[0].enabled &&
              recovered.layers[1].enabled && recovered.layers[1].family == vox::fx::NoiseFamily::downlifter &&
              recovered.layers[1].pan == -1.0f && save(restoredLayered) == layerState,
              "layer program restored and roundtrips");
        auto bad = vox::preset::decode(std::span(static_cast<const std::byte*>(layerState.getData()), layerState.getSize()),
                                       vox::preset::makeTag("VXFX"), 1);
        for (auto& section : bad.document.sections) if (section.tag == vox::fx::fxProgramSectionTag) section.data[0] = std::byte{2};
        const auto encodedBad = vox::preset::encode(bad.document);
        if (encodedBad) {
            restoredLayered.setStateInformation(encodedBad->data(), static_cast<int>(encodedBad->size()));
            check(restoredLayered.lastStateError().isNotEmpty() && save(restoredLayered) == layerState,
                  "future layered state rejected transactionally");
        } else check(false, "future program test encoded");
    }
    auto* gain = p.getParameters()[0];
    check(gain != nullptr, "output parameter exists");
    gain->setValueNotifyingHost(0.25f);
    p.durationParameter().setValueNotifyingHost(p.durationParameter().convertTo0to1(2.5f));
    p.setFamily(1);
    const auto saved = save(p);
    auto restoredOwner=std::make_unique<vox::fx::FxProcessor>(); auto& restored=*restoredOwner;
    restored.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    check(restored.lastStateError().isEmpty(), "valid state accepted");
    check(std::abs(restored.getParameters()[0]->getValue() - 0.25f) < 1e-6f, "parameter restored");
    check(std::abs(restored.durationParameter().get() - 2.5f) < 1e-4f, "duration restored");
    check(restored.family() == 1, "structural family restored");
    check(save(restored) == saved, "state deterministic roundtrip");
    auto damaged = saved;
    static_cast<unsigned char*>(damaged.getData())[20] ^= 1;
    restored.setStateInformation(damaged.getData(), static_cast<int>(damaged.getSize()));
    check(restored.lastStateError().isNotEmpty() && save(restored) == saved, "corrupt state rejected transactionally");
    auto decoded = vox::preset::decode(std::span(static_cast<const std::byte*>(saved.getData()), saved.getSize()),
                                       vox::preset::makeTag("VXFX"), 1);
    check(decoded.ok(), "decode state");
    if (decoded.ok()) {
        for (auto& section : decoded.document.sections)
            if (section.tag == vox::fx::fxParameterSectionTag) section.data[0] = std::byte{3};
        auto encoded = vox::preset::encode(decoded.document);
        if (encoded) {
            restored.setStateInformation(encoded->data(), static_cast<int>(encoded->size()));
            check(restored.lastStateError().isNotEmpty() && save(restored) == saved, "future FX section rejected transactionally");
        } else check(false, "encode future state");
    }
    phase("legacy parameter migration");
    {
        // FPRM v1 had only the output value. Restore it with the 4 s default.
        auto oldDoc = vox::preset::decode(std::span(static_cast<const std::byte*>(saved.getData()), saved.getSize()),
                                          vox::preset::makeTag("VXFX"), 1);
        auto& oldSections = oldDoc.document.sections;
        oldSections.erase(std::remove_if(oldSections.begin(), oldSections.end(), [] (const auto& section) {
            return section.tag == vox::fx::fxFamilySectionTag || section.tag == vox::fx::fxProgramSectionTag;
        }), oldSections.end());
        for (auto& section : oldSections) if (section.tag == vox::fx::fxParameterSectionTag) {
            section.data.resize(8); section.data[0] = std::byte{1};
        }
        auto old = vox::preset::encode(oldDoc.document);
        auto migratedOwner=std::make_unique<vox::fx::FxProcessor>(); auto& migrated=*migratedOwner;
        check(old.has_value(), "encode v1 state");
        if (old) {
            migrated.setStateInformation(old->data(), static_cast<int>(old->size()));
            check(migrated.lastStateError().isEmpty() && migrated.family() == 0 &&
                  std::abs(migrated.durationParameter().get() - 4.0f) < 1e-4f,
                  "v1 state migrates to four second duration");
        }
    }
    phase("duration capture");
    {
        auto durationTestOwner=std::make_unique<vox::fx::FxProcessor>(); auto& durationTest=*durationTestOwner; durationTest.prepareToPlay(48000, 512);
        durationTest.durationParameter().setValueNotifyingHost(0.0f);
        juce::MidiBuffer notes;
        notes.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);
        block(durationTest, audio, notes);
        for (int i = 0; i < 4; ++i) block(durationTest, audio, notes);
        check(audio.getMagnitude(0, 400, 112) == 0, "duration parameter affects triggered event");
    }
    restored.prepareToPlay(48000, 512);
    restored.getParameters()[0]->setValueNotifyingHost(0.0f);
    for (int i = 0; i < 2; ++i) block(restored, audio, midi);
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);
    block(restored, audio, midi);
    check(audio.getMagnitude(0, 0, 512) == 0, "host output gain controls DSP");
    phase("test cases finished; teardown follows");
    std::printf("FX processor: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
