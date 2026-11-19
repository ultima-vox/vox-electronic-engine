#include "Processor.h"
#include "FxEditor.h"
#include <cmath>
#include <bit>
#include <cstdio>
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
}
int main() {
    juce::ScopedJuceInitialiser_GUI gui;
    auto pOwner=std::make_unique<vox::fx::FxProcessor>(); auto& p=*pOwner;
    {
        vox::fx::FxPanel panel(p);
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
        check(p.setProgram(restored),"replace Program while curve drag is active");
        curve.mouseDrag(mouse({8.0f,80.0f}));curve.mouseUp(mouse({8.0f,80.0f}));
        check(p.program().layers[1].gainCurve.evaluate(.5f)==.4f,"stale drag cannot overwrite replaced Program");
        p.outputParameter().setValueNotifyingHost(1.0f);
        check(std::abs(panel.outputSlider().getValue() - 1.0) < 1e-6, "host updates editor knob");
    }
    {
        using namespace vox::fx;
        auto processor = std::make_unique<FxProcessor>();
        FxPanel panel(*processor);
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
    {
        auto generated = std::make_unique<vox::fx::FxProcessor>();
        vox::fx::FxPanel panel(*generated);
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
        vox::fx::FxPanel recalledPanel(*generatorRecall);
        check(recalledPanel.seedSlider().getValue()==123 && recalledPanel.layerLock(0).getToggleState(),"generator seed and locks recalled in editor");
        auto invalidGenerator=vox::preset::decode(std::span(static_cast<const std::byte*>(generatorSaved.getData()),generatorSaved.getSize()),vox::preset::makeTag("VXFX"),1);
        for(auto& section:invalidGenerator.document.sections)if(section.tag==vox::fx::fxGeneratorSectionTag)section.data[0]=std::byte{4};
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
    {
        auto effected=std::make_unique<vox::fx::FxProcessor>();
        auto dry=std::make_unique<vox::fx::FxProcessor>();
        vox::fx::FxPanel panel(*effected);
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
                  std::abs(migratedFx->effectWetParameter(0).get()-std::round(.7f*63)/63)<1e-5f &&
                  std::abs(migratedFx->effectAmountParameter(0).get()-std::round(.3f*127)/127)<1e-5f,
                  "FXSL v1 migrates type and old quantized automation values");}
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
    {
        auto owner=std::make_unique<vox::fx::FxProcessor>();vox::fx::pattern::Pattern pattern;
        vox::fx::FxPanel panel(*owner);
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
        panel.patternGridSelector().setSelectedId(1,juce::sendNotificationSync);
        panel.patternPageSelector().setSelectedId(1,juce::sendNotificationSync);
        panel.patternStepSelector().setSelectedId(16,juce::sendNotificationSync);
        check(panel.patternStepSelector().isEnabled() && !panel.patternNoteSlider().isEnabled(),
              "step selector remains usable when previous step is outside grid");
        panel.patternStepSelector().setSelectedId(1,juce::sendNotificationSync);
        check(panel.patternNoteSlider().isEnabled(),"valid step re-enables detail controls");
        panel.patternSelector().setSelectedId(8,juce::sendNotificationSync);
        check(vox::fx::pattern::factory(7,pattern)&&owner->patternSnapshot().seed==pattern.seed,"pattern selector installs real pattern");
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
        check(!sequenced->patternUnavailable(),"valid pattern host timing is available");
        pos.setBpm(9);head.position=pos;block(*sequenced,a,noMidi);
        check(sequenced->patternUnavailable(),"invalid scheduler tempo surfaced to UI");
        pos.setBpm(120);head.position=pos;block(*sequenced,a,noMidi);
        check(!sequenced->patternUnavailable(),"pattern status recovers when timing is valid");
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
    {
        auto modulated=std::make_unique<vox::fx::FxProcessor>();auto dry=std::make_unique<vox::fx::FxProcessor>();
        vox::fx::FxPanel panel(*modulated);
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
        for(auto& section:document.document.sections)if(section.tag==vox::fx::fxMacroRouteSectionTag)section.data[0]=std::byte{2};
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
        vox::fx::FxPanel samplePanel(*missing);
        check(samplePanel.sampleImportButton().isEnabled(),"sample import action exposed");
        samplePanel.sampleBuiltinButton().onClick();
        check(missing->sampleReference().kind==vox::fx::sample::Reference::Kind::builtin,"editor explicitly selects built-in sample");
        note.addEvent(juce::MidiMessage::noteOn(1,60,static_cast<juce::uint8>(100)),0);block(*missing,sound,note);
        check(sound.getMagnitude(0,0,512)>0,"explicit fallback triggers procedural sample");
    }
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
    check(p.outputPeak() == audio.getMagnitude(0, 0, 512), "meter follows actual output peak");
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
    p.panic(); block(p, audio, midi);
    check(p.audioTelemetry().events==0&&p.audioTelemetry().grains==0,"audio telemetry reflects panic release");
    check(audio.getMagnitude(0, 300, 212) == 0, "editor panic fades sound");

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
    std::printf("FX processor: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
