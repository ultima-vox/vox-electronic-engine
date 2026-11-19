#include "FxEditor.h"
#include "Processor.h"
#include "Tokens.h"
#include "Typography.h"

namespace vox::fx {
namespace {
mod::Source sourceForControl(int id) noexcept {
    if(id>=1&&id<=8)return static_cast<mod::Source>(static_cast<unsigned>(mod::Source::macro1)+static_cast<unsigned>(id-1));
    if(id==9)return mod::Source::lfo1;
    if(id==10)return mod::Source::lfo2;
    if(id==11)return mod::Source::random;
    return mod::Source::count;
}
}
GeneratorPanel::GeneratorPanel(FxProcessor& processor) : processor_(processor) {
    const char* names[]{"Style seed", "Active layers", "Intensity", "Energy", "Stereo"};
    for(std::size_t i=0;i<labels_.size();++i){labels_[i].setText(names[i],juce::dontSendNotification);addAndMakeVisible(labels_[i]);}
    for(int i=0;i<6;++i)style_.addItem("Style " + juce::String(i+1),i+1);
    for(int i=1;i<=4;++i)complexity_.addItem(juce::String(i),i);
    style_.setTitle("Generator style seed");complexity_.setTitle("Generator active layers");
    style_.onChange=[this]{const auto value=style_.getSelectedId()-1;
        (void)processor_.editGeneratorState([value](generator::ControllerState& s){s.style=value;});};
    complexity_.onChange=[this]{const auto value=complexity_.getSelectedId();
        (void)processor_.editGeneratorState([value](generator::ControllerState& s){s.complexity=value;});};
    auto configure=[this](juce::Slider& slider,const char* title){slider.setRange(0,1,.01);
        slider.setTitle(title);slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::TextBoxRight,false,50,24);addAndMakeVisible(slider);};
    configure(intensity_,"Generator intensity");configure(energy_,"Generator energy");configure(stereo_,"Generator stereo spread");
    intensity_.onValueChange=[this]{const auto v=static_cast<float>(intensity_.getValue());
        (void)processor_.editGeneratorState([v](generator::ControllerState& s){s.intensity=v;});};
    energy_.onValueChange=[this]{const auto v=static_cast<float>(energy_.getValue());
        (void)processor_.editGeneratorState([v](generator::ControllerState& s){s.energy=v;});};
    stereo_.onValueChange=[this]{const auto v=static_cast<float>(stereo_.getValue());
        (void)processor_.editGeneratorState([v](generator::ControllerState& s){s.stereo=v;});};
    addAndMakeVisible(style_);addAndMakeVisible(complexity_);addAndMakeVisible(close_);
    for(int i=0;i<4;++i)lockLayer_.addItem("Layer " + juce::String::charToString(static_cast<juce::juce_wchar>('A'+i)),i+1);
    lockLayer_.setSelectedId(1,juce::dontSendNotification);lockLayer_.setTitle("Layer for generator locks");
    lockLayer_.onChange=[this]{timerCallback();};addAndMakeVisible(lockLayer_);
    const char* lockNames[]{"Family","On","Time","Gain","Pan","Gain env","Pan env","Pitch env","Cutoff env"};
    const std::uint32_t bits[]{generator::family,generator::enabled,generator::timing,generator::gain,generator::pan,generator::gainCurve,generator::panCurve,generator::pitchCurve,generator::cutoffCurve};
    for(std::size_t i=0;i<fieldLocks_.size();++i){
        auto& button=fieldLocks_[i];button.setButtonText(lockNames[i]);button.setTitle("Lock " + juce::String(lockNames[i]));
        button.setTooltip("Preserve this field during Generate and Mutate");
        button.onClick=[this,i,bit=bits[i]]{const int layer=lockLayer_.getSelectedId()-1;if(layer<0||layer>=4)return;
            const bool locked=fieldLocks_[i].getToggleState();
            (void)processor_.editGeneratorState([layer,bit,locked](generator::ControllerState& s){
                auto& mask=s.locks.layers[static_cast<std::size_t>(layer)];if(locked)mask|=bit;else mask&=~bit;});};
        addAndMakeVisible(button);
    }
    close_.onClick=[this]{setVisible(false);};timerCallback();startTimerHz(15);
}
void GeneratorPanel::paint(juce::Graphics& g) {
    g.fillAll(vox::ui::tokens::colour::panelRaised);
    g.setColour(vox::ui::tokens::colour::border);g.drawRect(getLocalBounds(),1);
}
void GeneratorPanel::resized() {
    auto area=getLocalBounds().reduced(10);close_.setBounds(area.removeFromBottom(26).removeFromRight(75));
    for(std::size_t i=0;i<labels_.size();++i){auto row=area.removeFromTop(31);labels_[i].setBounds(row.removeFromLeft(100));
        switch(i){case 0:style_.setBounds(row);break;case 1:complexity_.setBounds(row);break;
            case 2:intensity_.setBounds(row);break;case 3:energy_.setBounds(row);break;case 4:stereo_.setBounds(row);break;default:break;}}
    auto locks=area.removeFromTop(27);lockLayer_.setBounds(locks.removeFromLeft(100));
    const auto width=locks.getWidth()/4;for(std::size_t i=0;i<4;++i)fieldLocks_[i].setBounds(locks.removeFromLeft(width));
    auto second=area.removeFromTop(27);second.removeFromLeft(100);
    const auto secondWidth=second.getWidth()/5;for(std::size_t i=4;i<fieldLocks_.size();++i)fieldLocks_[i].setBounds(second.removeFromLeft(secondWidth));
}
void GeneratorPanel::timerCallback() {
    if(!isShowing())return;
    const auto state=processor_.generatorState();
    style_.setSelectedId(state.style+1,juce::dontSendNotification);
    complexity_.setSelectedId(state.complexity,juce::dontSendNotification);
    auto sync=[](juce::Slider& slider,float value){if(!slider.isMouseButtonDown()&&!slider.hasKeyboardFocus(true))slider.setValue(value,juce::dontSendNotification);};
    sync(intensity_,state.intensity);sync(energy_,state.energy);sync(stereo_,state.stereo);
    const int layer=lockLayer_.getSelectedId()-1;
    if(layer>=0&&layer<4){const auto mask=state.locks.layers[static_cast<std::size_t>(layer)];
        const std::uint32_t bits[]{generator::family,generator::enabled,generator::timing,generator::gain,generator::pan,generator::gainCurve,generator::panCurve,generator::pitchCurve,generator::cutoffCurve};
        for(std::size_t i=0;i<fieldLocks_.size();++i)fieldLocks_[i].setToggleState((mask&bits[i])!=0,juce::dontSendNotification);}
}
ProgramCurvePanel::ProgramCurvePanel(FxProcessor& processor) : editor_(processor, std::nullopt) {
    const char* names[] { "Amplitude", "Pitch", "Cutoff", "Panorama" };
    for (int i=0; i<4; ++i) selector_.addItem(names[i], i+1);
    selector_.setTitle("Program trajectory");
    selector_.setSelectedId(1, juce::dontSendNotification);
    selector_.onChange = [this] {
        const int index = selector_.getSelectedId() - 1;
        if (index < 0 || index >= 4) return;
        editor_.setKind(static_cast<CurveKind>(index));
        refreshHint();
    };
    undo_.setTitle("Undo curve edit"); redo_.setTitle("Redo curve edit");
    reset_.setTooltip("Restore the selected trajectory to its neutral value; this edit can be undone");
    undo_.onClick = [this] { (void)editor_.undo(); };
    redo_.onClick = [this] { (void)editor_.redo(); };
    reset_.onClick = [this] { (void)editor_.resetCurve(); };
    hint_.setFont(vox::ui::typography::controlLabel());
    for (auto* child : std::array<juce::Component*,6>{ &selector_, &undo_, &redo_, &reset_, &editor_, &hint_ })
        addAndMakeVisible(*child);
    refreshHint();
}
void ProgramCurvePanel::refresh() { editor_.refresh(); }
void ProgramCurvePanel::refreshHint() {
    const char* unit = "Amplitude: 0% to 100%";
    switch (editor_.kind()) {
        case CurveKind::gain: break;
        case CurveKind::pitch: unit = "Pitch: -24 to +24 semitones before layer pitch"; break;
        case CurveKind::cutoff: unit = "Cutoff: below to above the family sweep; center is neutral"; break;
        case CurveKind::pan: unit = "Panorama: left to right offset; center is neutral"; break;
    }
    hint_.setText(juce::String(unit) + "  |  Time: 0-100% of event  |  Double-click to add, Delete to remove",
                  juce::dontSendNotification);
}
void ProgramCurvePanel::paint(juce::Graphics& g) {
    namespace c = vox::ui::tokens::colour;
    const auto bounds = getLocalBounds().toFloat().reduced(1.0f);
    g.setColour(c::panelRaised); g.fillRoundedRectangle(bounds, 6.0f);
    g.setColour(c::border); g.drawRoundedRectangle(bounds, 6.0f, 1.0f);
    g.setColour(c::text); g.setFont(vox::ui::typography::controlLabel());
    g.drawText("PROGRAM TRAJECTORIES / NEW EVENTS", getLocalBounds().reduced(12).removeFromTop(28).removeFromLeft(270),
               juce::Justification::centredLeft);
}
void ProgramCurvePanel::resized() {
    auto area = getLocalBounds().reduced(12);
    auto header = area.removeFromTop(28);
    header.removeFromLeft(270);
    selector_.setBounds(header.removeFromLeft(140).reduced(2));
    undo_.setBounds(header.removeFromLeft(60).reduced(2));
    redo_.setBounds(header.removeFromLeft(60).reduced(2));
    reset_.setBounds(header.removeFromLeft(100).reduced(2));
    hint_.setBounds(area.removeFromBottom(22));
    editor_.setBounds(area);
}
WavOptionsPanel::WavOptionsPanel() {
    rate_.addItem("44.1 kHz",1); rate_.addItem("48 kHz",2); rate_.addItem("96 kHz",3);
    format_.addItem("24-bit PCM",1); format_.addItem("32-bit float",2);
    rate_.setSelectedId(2,juce::dontSendNotification); format_.setSelectedId(1,juce::dontSendNotification);
    const char* names[]{"Sample rate", "Format", "Audition MIDI note", "Audition velocity"};
    std::array<juce::Component*,4> controls {&rate_, &format_, &note_, &velocity_};
    for (std::size_t i=0; i<controls.size(); ++i) {
        controls[i]->setTitle(names[i]);
        labels_[i].setText(names[i],juce::dontSendNotification);
        addAndMakeVisible(labels_[i]); addAndMakeVisible(*controls[i]);
    }
    for (auto* slider : { &note_, &velocity_ }) {
        slider->setSliderStyle(juce::Slider::LinearHorizontal);
        slider->setTextBoxStyle(juce::Slider::TextBoxRight,false,56,24);
    }
    note_.setRange(0,127,1); note_.setValue(60,juce::dontSendNotification);
    velocity_.setRange(1,127,1); velocity_.setValue(127,juce::dontSendNotification);
    note_.setTooltip("MIDI note number; Sequence uses its own pattern notes");
    velocity_.setTooltip("MIDI velocity; Sequence uses its own pattern velocities");
    close_.onClick=[this]{setVisible(false);};
    save_.onClick=[this]{setVisible(false);if(onSave)onSave(settings());};
    addAndMakeVisible(close_); addAndMakeVisible(save_);
}
offline::Settings WavOptionsPanel::settings() const {
    offline::Settings result;
    result.sampleRate=rate_.getSelectedId()==1?44100u:rate_.getSelectedId()==3?96000u:48000u;
    result.format=format_.getSelectedId()==2?offline::WavFormat::float32:offline::WavFormat::pcm24;
    result.note=static_cast<int>(note_.getValue()); result.velocity=static_cast<float>(velocity_.getValue()/127.0);
    return result;
}
void WavOptionsPanel::paint(juce::Graphics& g) {
    namespace c = vox::ui::tokens::colour;
    const auto bounds = getLocalBounds().toFloat().reduced(1.0f);
    g.setColour(c::panelRaised); g.fillRoundedRectangle(bounds,7.0f);
    g.setColour(c::border); g.drawRoundedRectangle(bounds,7.0f,1.0f);
    auto text=getLocalBounds().reduced(14);
    g.setColour(c::text); g.setFont(vox::ui::typography::controlLabel());
    g.drawText("WAV EXPORT / STEREO",text.removeFromTop(22),juce::Justification::centredLeft);
    g.drawText("One sound pass, or one Sequence cycle with its notes and velocities.",text.removeFromTop(22),juce::Justification::centredLeft);
}
void WavOptionsPanel::resized() {
    auto area=getLocalBounds().reduced(14); area.removeFromTop(48);
    std::array<juce::Component*,4> controls {&rate_, &format_, &note_, &velocity_};
    for(std::size_t i=0;i<controls.size();++i){auto row=area.removeFromTop(29);labels_[i].setBounds(row.removeFromLeft(143));controls[i]->setBounds(row.reduced(2));}
    auto footer=area.removeFromBottom(30); save_.setBounds(footer.removeFromRight(130).reduced(2));close_.setBounds(footer.removeFromRight(80).reduced(2));
}
FxPanel::FxPanel (FxProcessor& processor)
    : processor_ (processor), attachment_ (processor.outputParameter(), output_.getSlider()),
      durationAttachment_ (processor.durationParameter(), duration_.getSlider()), programCurves_(processor)
{
    addAndMakeVisible (output_);
    addAndMakeVisible (duration_);
    duration_.setDoubleClickResetValue (4.0);
    duration_.getSlider().setTitle ("Event duration in seconds");
    duration_.getSlider().setDescription ("Duration applied to newly triggered events");
    output_.setDoubleClickResetValue (1.0);
    output_.getSlider().setTitle ("Output gain");
    output_.getSlider().setDescription ("Master output gain");
    family_.addItem ("Noise Riser", 1);
    family_.addItem ("Downlifter", 2);
    family_.addItem ("Tonal Sweep / Shepard", 3);
    family_.addItem ("Impact / Boom", 4);
    family_.addItem ("Whoosh / Doppler", 5);
    family_.addItem ("Zap / Laser", 6);
    family_.addItem ("Psy Blip / Squelch", 7);
    family_.addItem ("Metallic / Resonant", 8);
    family_.addItem ("Texture / Drone", 9);
    family_.addItem ("Formant / Alien", 10);
    family_.addItem ("Glitch / Stutter", 11);
    family_.addItem ("Granular Cloud", 12);
    family_.setTitle ("Sound family");
    family_.setTooltip ("Family for newly triggered events");
    family_.setSelectedId (processor_.family() + 1, juce::dontSendNotification);
    family_.onChange = [this] { processor_.setFamily (family_.getSelectedId() - 1); };
    addAndMakeVisible (family_);
    eventBars_.addItem("Seconds",1); eventBars_.addItem("1 bar",2); eventBars_.addItem("2 bars",3);
    eventBars_.addItem("4 bars",4); eventBars_.addItem("8 bars",5); eventBars_.addItem("16 bars",6);
    eventBars_.setTitle("Event timeline");
    eventBars_.setTooltip("Live bar notes require playing host, PPQ, tempo and explicit 4/4. WAV uses a fixed tempo and starts on a bar line.");
    eventBars_.onChange=[this] { constexpr int values[]{0,1,2,4,8,16}; const int id=eventBars_.getSelectedId();
        if(id>=1&&id<=6)(void)processor_.setEventBars(values[id-1]); };
    const auto initialBars=processor_.program().bars;
    const int initialBarsId=initialBars==0?1:initialBars==1?2:initialBars==2?3:initialBars==4?4:initialBars==8?5:6;
    eventBars_.setSelectedId(initialBarsId,juce::dontSendNotification);
    addAndMakeVisible(eventBars_);
    playMode_.addItem("One Shot",1);playMode_.addItem("Gate",2);playMode_.addItem("Continuous",3);
    playMode_.addItem("Loop",4);playMode_.addItem("Sequence",5);playMode_.addItem("Transition",6);
    playMode_.setTitle("Play mode");playMode_.setTooltip("Transition in bars ends on a host bar line; a mid-bar note shortens the first bar. In seconds it is free-running. Gate, Continuous and Loop release on MIDI NoteOff. Preview plays one pass; Sequence uses Host Pattern.");
    triggerPolicy_.addItem("Overlap",1);triggerPolicy_.addItem("Ignore",2);triggerPolicy_.addItem("Replace",3);
    triggerPolicy_.setTitle("Repeated NoteOn policy");triggerPolicy_.setTooltip("Overlap voices, ignore a duplicate channel/note, or fade-replace that same channel/note");
    const auto initialProgram=processor_.program();
    playMode_.setSelectedId(static_cast<int>(initialProgram.mode)+1,juce::dontSendNotification);
    triggerPolicy_.setSelectedId(static_cast<int>(initialProgram.triggerPolicy)+1,juce::dontSendNotification);
    auto updatePlayback=[this]{const int m=playMode_.getSelectedId()-1,p=triggerPolicy_.getSelectedId()-1;
        if(m>=0&&m<=5&&p>=0&&p<=2)(void)processor_.setPlayback(static_cast<PlayMode>(m),static_cast<TriggerPolicy>(p));};
    playMode_.onChange=updatePlayback;triggerPolicy_.onChange=updatePlayback;
    addAndMakeVisible(playMode_);addAndMakeVisible(triggerPolicy_);
    clockStatus_.setFont(juce::FontOptions(10.0f)); addAndMakeVisible(clockStatus_);
    sampleImport_.setTooltip("Import a licensed mono or stereo WAV for granular layers; decoding is asynchronous");
    sampleBuiltin_.setTooltip("Explicitly select the built-in granular source");
    sampleStatus_.setTooltip("Granular sample status");sampleStatus_.setFont(juce::FontOptions(11.0f));
    sampleImport_.onClick=[this]{importSample();};
    sampleBuiltin_.onClick=[this]{(void)processor_.useBuiltinSample();};
    addAndMakeVisible(sampleImport_);addAndMakeVisible(sampleBuiltin_);addAndMakeVisible(sampleStatus_);
    for(int i=1;i<=8;++i)macroSelector_.addItem("M"+juce::String(i),i);
    macroSelector_.addItem("LFO 1",9);macroSelector_.addItem("LFO 2",10);macroSelector_.addItem("S&H",11);
    macroSelector_.setTitle("Modulation source selector");macroSelector_.setTooltip("Macros or fixed-rate LFO 1 (0.5 Hz), LFO 2 (1 Hz), S&H (4 Hz)");
    macroSelector_.setSelectedId(1,juce::dontSendNotification);
    macroDestination_.addItem("Gain",1);macroDestination_.addItem("Pan",2);
    macroDestination_.setSelectedId(1,juce::dontSendNotification);
    macroDestination_.setTitle("Modulation destination");macroDestination_.setTooltip("Route source to master gain or stereo balance");
    macroDestination_.onChange=[this]{refreshMacro();};
    macroValue_.setSliderStyle(juce::Slider::LinearHorizontal);macroValue_.setTextBoxStyle(juce::Slider::TextBoxBelow,false,48,18);
    macroValue_.setTitle("Macro value");macroValue_.setTooltip("Bipolar macro value: center is neutral");
    macroDepth_.setRange(-1,1,.01);macroDepth_.setSliderStyle(juce::Slider::LinearHorizontal);
    macroDepth_.setTextBoxStyle(juce::Slider::TextBoxBelow,false,48,18);
    macroDepth_.setTitle("Modulation route depth");macroDepth_.setTooltip("Assign this source to the selected destination; zero removes the route");
    macroDepth_.onValueChange=[this]{const auto source=sourceForControl(macroSelector_.getSelectedId());
        const auto target=macroDestination_.getSelectedId()==2?mod::Destination::pan:mod::Destination::gain;
        if(source!=mod::Source::count)(void)processor_.setMotionRoute(source,target,static_cast<float>(macroDepth_.getValue()),.01f);};
    macroSelector_.onChange=[this]{const int id=macroSelector_.getSelectedId();
        // Detach first: the new attachment's initial slider update must not
        // notify the previous macro's listener and overwrite its automation.
        macroAttachment_.reset();macroValue_.setEnabled(id>=1&&id<=8);
        if(id>=1&&id<=8)
            macroAttachment_=std::make_unique<juce::SliderParameterAttachment>(processor_.macroParameter(static_cast<std::size_t>(id-1)),macroValue_);
        refreshMacro();};
    macroAttachment_=std::make_unique<juce::SliderParameterAttachment>(processor_.macroParameter(0),macroValue_);
    addAndMakeVisible(macroSelector_);addAndMakeVisible(macroDestination_);addAndMakeVisible(macroValue_);addAndMakeVisible(macroDepth_);
    refreshMacro();
    addAndMakeVisible (audition_);
    addAndMakeVisible (panic_);
    const char* familyNames[]{"Noise Riser","Downlifter","Shepard","Impact","Whoosh","Laser","Squelch","Metallic","Texture","Formant","Glitch","Granular"};
    factory_.setTextWhenNothingSelected("Load seed program...");factory_.setTitle("Procedural seed program");
    factory_.setTooltip("Load an uncurated procedural layer seed; duration, effects, macros and pattern remain unchanged");
    for(int i=0;i<48;++i)factory_.addItem(juce::String(familyNames[i/4])+" "+juce::String(1+i%4),i+1);
    factory_.onChange=[this]{const int id=factory_.getSelectedId();generator::Result result;
        if(id>=1&&id<=48&&generator::factory(static_cast<std::size_t>(id-1),result))(void)processor_.setProgram(result.program);
        factory_.setSelectedId(0,juce::dontSendNotification);};
    addAndMakeVisible(factory_);
    comparison_.setTextWhenNothingSelected("Compare A/B");comparison_.setTitle("Session A/B comparisons");
    comparison_.setTooltip("Store or recall full session snapshots; recall changes host parameter values. Slots are not saved in the project.");
    comparison_.addItem("Store A",1);comparison_.addItem("Recall A (host values)",2);
    comparison_.addItem("Store B",3);comparison_.addItem("Recall B (host values)",4);
    comparison_.onChange=[this]{const auto id=comparison_.getSelectedId();
        if(id>=1&&id<=4){const auto slot=static_cast<std::size_t>((id-1)/2);
            const bool ok=(id&1)?processor_.storeComparison(slot):processor_.recallComparison(slot);
            if(!ok)juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                "Comparison unavailable","Store this comparison before recalling it, or check the current state.");}
        comparison_.setSelectedId(0,juce::dontSendNotification);};
    addAndMakeVisible(comparison_);
    midiExport_.setTitle("Export trigger pattern MIDI");
    midiExport_.setTooltip("Export one 4/4 pattern loop at the latest host tempo (120 BPM if unavailable); trigger notes, not audio");
    midiExport_.onClick=[this]{exportPattern();};addAndMakeVisible(midiExport_);
    wavExport_.setTitle("Export WAV");
    wavExport_.setTooltip("Choose stereo WAV options; click during rendering to cancel");
    wavOptions_.onSave=[this](offline::Settings settings){exportWav(settings);};
    addChildComponent(wavOptions_);
    wavExport_.onClick=[this]{if(wavJob_)wavJob_->cancel();else {wavOptions_.setVisible(true);wavOptions_.toFront(true);}};
    addAndMakeVisible(wavExport_);
    audition_.setTooltip ("Trigger the current program");
    panic_.setTooltip ("Fade all active events");
    audition_.onClick = [this] { processor_.audition(); };
    panic_.onClick = [this] { processor_.panic(); };
    seed_.setRange (1, 2147483647, 1);
    seed_.setValue (processor_.generatorState().seed, juce::dontSendNotification);
    seed_.setSliderStyle (juce::Slider::LinearHorizontal);
    seed_.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 85, 25);
    seed_.setTitle ("Generator seed");
    seed_.setTooltip ("Deterministic seed for Generate and Mutate");
    seed_.onValueChange=[this] {const auto seed=static_cast<std::uint32_t>(seed_.getValue());
        (void)processor_.editGeneratorState([seed](generator::ControllerState& state){state.seed=seed;});};
    addAndMakeVisible (seed_);
    generate_.setTooltip ("Generate four layers for the selected family and seed; host duration is unchanged");
    mutate_.setTooltip ("Mutate unlocked layer gain, pan and trajectories using the seed");
    generate_.onClick = [this] {
        const auto controls=processor_.generatorState();
        (void) processor_.generateProgram (controls.settings(processor_.family()), controls.locks);
    };
    mutate_.onClick = [this] {
        const auto controls=processor_.generatorState();
        (void) processor_.mutateProgram (controls.seed, controls.locks);
    };
    for(std::size_t i=0;i<4;++i) {
        layerLocks_[i].setButtonText(juce::String::charToString(static_cast<juce::juce_wchar>('A'+i)));
        layerLocks_[i].setTitle("Lock entire layer " + juce::String(static_cast<int>(i+1)));
        layerLocks_[i].setTooltip("Preserve this layer during Generate and Mutate; saved in the project");
        layerLocks_[i].onClick=[this,i]{const auto mask=layerLocks_[i].getToggleState()?static_cast<std::uint32_t>(generator::all):0u;
            (void)processor_.editGeneratorState([i,mask](generator::ControllerState& state){state.locks.layers[i]=mask;});};
        addAndMakeVisible(layerLocks_[i]);
    }
    refreshGenerator();
    addAndMakeVisible (generate_); addAndMakeVisible (mutate_);
    generatorSettings_.setTitle("Generator settings");generatorSettings_.setTooltip("Adjust generator shaping and per-field layer locks");
    generatorPanel_=std::make_unique<GeneratorPanel>(processor_);addChildComponent(*generatorPanel_);
    generatorSettings_.onClick=[this]{generatorPanel_->setVisible(true);generatorPanel_->toFront(true);};
    addAndMakeVisible(generatorSettings_);
    for (int i=0; i<4; ++i) {
        layers_[static_cast<std::size_t>(i)] = std::make_unique<LayerPanel>(processor_,i);
        addAndMakeVisible(*layers_[static_cast<std::size_t>(i)]);
    }
    addChildComponent(programCurves_);
    programCurvesButton_.setClickingTogglesState(true);
    programCurvesButton_.setTooltip("Switch between layer cards and Program amplitude, pitch, cutoff and panorama");
    programCurvesButton_.onClick = [this] {
        const bool show = programCurvesButton_.getToggleState();
        for (auto& layer : layers_) layer->setVisible(!show);
        if (show) programCurves_.refresh();
        programCurves_.setVisible(show);
        programCurvesButton_.setButtonText(show ? "Layer cards" : "Program curves");
    };
    addAndMakeVisible(programCurvesButton_);
    for (std::size_t i=0;i<4;++i) {
        auto& type=effectsType_[i];
        const char* names[]{"Bypass","Lowpass","Drive","Wavefold","Bitcrush","Ring","Gate","Phaser","Flanger","Delay","Chorus","Reverb"};
        static_assert(std::size(names)==static_cast<std::size_t>(effects::Type::count));
        for (std::size_t n=0;n<std::size(names);++n) type.addItem(names[n],static_cast<int>(n+1));
        type.setTitle("Shared effect slot " + juce::String(static_cast<int>(i+1)));
        type.onChange=[this,i] { auto s=processor_.effect(i);s.type=static_cast<effects::Type>(effectsType_[i].getSelectedId()-1);(void)processor_.setEffect(i,s); };
        addAndMakeVisible(type);
        for (auto* slider : {&effectsWet_[i],&effectsAmount_[i]}) {
            slider->setRange(0,1,.001);slider->setSliderStyle(juce::Slider::LinearHorizontal);
            slider->setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);addAndMakeVisible(*slider);
        }
        effectsWet_[i].setTitle("Slot " + juce::String(static_cast<int>(i+1)) + " wet mix");
        effectsWet_[i].setTooltip("Host-automatable wet mix");
        effectsAmount_[i].setTitle("Slot " + juce::String(static_cast<int>(i+1)) + " amount");
        effectsAmount_[i].setTooltip("Host-automatable effect amount");
        wetAttachments_[i]=std::make_unique<juce::SliderParameterAttachment>(processor_.effectWetParameter(i),effectsWet_[i]);
        amountAttachments_[i]=std::make_unique<juce::SliderParameterAttachment>(processor_.effectAmountParameter(i),effectsAmount_[i]);
        const auto s=processor_.effect(i);type.setSelectedId(static_cast<int>(s.type)+1,juce::dontSendNotification);
    }
    patternSelector_.setTextWhenNothingSelected("Load seed pattern...");
    patternSelector_.setTitle("Load trigger seed pattern");
    patternSelector_.setTooltip("Replaces the saved pattern; works when HOST PATTERN is enabled");
    for(int i=0;i<12;++i)patternSelector_.addItem("Seed pattern " + juce::String(i+1),i+1);
    patternSelector_.onChange=[this] {
        const auto id=patternSelector_.getSelectedId();pattern::Pattern selected;
        if(id>=1 && id<=12 && pattern::factory(static_cast<std::size_t>(id-1),selected)) (void)processor_.setPattern(selected);
        patternSelector_.setSelectedId(0,juce::dontSendNotification);
        refreshPattern();
    };
    patternEnabled_.setClickingTogglesState(true);
    patternEnabled_.setTitle("Enable host-synchronized trigger pattern");
    patternEnabled_.setTooltip("Requires a playing 4/4 host with PPQ and 10–1000 BPM; oversized blocks are rejected to bound callback work");
    patternAttachment_=std::make_unique<juce::ButtonParameterAttachment>(processor_.patternEnabledParameter(),patternEnabled_);
    addAndMakeVisible(patternSelector_);addAndMakeVisible(patternEnabled_);
    for(int i=1;i<=4;++i){patternLength_.addItem(juce::String(i)+" bars",i);patternBar_.addItem("Bar "+juce::String(i),i);}
    patternLength_.setTitle("Pattern length in bars");patternBar_.setTitle("Pattern bar to edit");
    patternBar_.setSelectedId(1,juce::dontSendNotification);
    patternLength_.onChange=[this] {const int id=patternLength_.getSelectedId();
        if(id>=1 && id<=4)processor_.editPattern([id](pattern::Pattern& p){p.bars=static_cast<std::uint8_t>(id);});
        refreshPattern();};
    patternBar_.onChange=[this] {refreshPattern();};
    const char* gridNames[]{"1/4","1/8","1/16","1/32","1/4T","1/8T","1/16T","1/32T"};
    for(int i=0;i<8;++i)patternGrid_.addItem(gridNames[i],i+1);
    patternGrid_.setTitle("Pattern grid");patternGrid_.setTooltip("Changing grid reinterprets step timing without resampling steps");
    patternGrid_.onChange=[this]{const int id=patternGrid_.getSelectedId();
        if(id>=1&&id<=8)processor_.editPattern([id](pattern::Pattern& p){p.grid=static_cast<pattern::Grid>(id-1);});
        refreshPattern();};
    for(int i=1;i<=3;++i)patternPage_.addItem("Page "+juce::String(i),i);
    patternPage_.setTitle("Step page within bar");patternPage_.setSelectedId(1,juce::dontSendNotification);
    patternPage_.onChange=[this]{refreshPattern();};
    patternSwing_.setRange(0,1,.01);patternSwing_.setSliderStyle(juce::Slider::LinearHorizontal);
    patternSwing_.setTextBoxStyle(juce::Slider::TextBoxLeft,false,40,20);
    patternSwing_.setTitle("Pattern swing");patternSwing_.setTooltip("Delay odd steps up to half their grid interval");
    patternSwing_.onValueChange=[this]{const float swing=static_cast<float>(patternSwing_.getValue());
        processor_.editPattern([swing](pattern::Pattern& p){p.swing=swing;});};
    addAndMakeVisible(patternSwing_);
    addAndMakeVisible(patternLength_);addAndMakeVisible(patternBar_);addAndMakeVisible(patternGrid_);addAndMakeVisible(patternPage_);
    for(int i=1;i<=16;++i)patternStepSelector_.addItem(juce::String(i),i);
    patternStepSelector_.setTitle("Step to edit");patternStepSelector_.setTooltip("Choose a step to edit its note, velocity, probability and ratchets");
    patternStepSelector_.setSelectedId(1,juce::dontSendNotification);
    patternStepSelector_.onChange=[this]{refreshPattern();};addAndMakeVisible(patternStepSelector_);
    const char* detailNames[]{"Step MIDI note", "Step velocity", "Step probability percent", "Step ratchet count"};
    const char* suffixes[]{" N"," V"," %"," x"};
    for(std::size_t i=0;i<patternDetails_.size();++i){auto& slider=patternDetails_[i];
        slider.setRange(i==0||i==2?0:1,i==0?127:i==1?127:i==2?100:4,1);
        slider.setSliderStyle(juce::Slider::LinearHorizontal);slider.setTextBoxStyle(juce::Slider::TextBoxBelow,false,48,20);
        slider.setTextValueSuffix(suffixes[i]);slider.setTitle(detailNames[i]);slider.setTooltip(detailNames[i]);
        slider.onValueChange=[this,i] {const int bar=patternBar_.getSelectedId(),step=patternStepSelector_.getSelectedId(),page=patternPage_.getSelectedId();
            const int value=static_cast<int>(patternDetails_[i].getValue());
            if(bar>=1&&bar<=4&&step>=1&&step<=16&&page>=1&&page<=3)processor_.editPattern([bar,step,page,i,value](pattern::Pattern& p){
                const auto steps=pattern::stepsPerBar(p.grid),local=static_cast<unsigned>((page-1)*16+step-1);
                if(bar>p.bars||local>=steps)return;
                auto& selected=p.steps[static_cast<std::size_t>(bar-1)*steps+local];
                switch(i){case 0:selected.note=static_cast<std::uint8_t>(value);break;
                    case 1:selected.velocity=static_cast<std::uint8_t>(value);break;
                    case 2:selected.probability=static_cast<std::uint8_t>(value);break;
                    case 3:selected.ratchets=static_cast<std::uint8_t>(value);break;default:break;}
            });};
        addAndMakeVisible(slider);
    }
    for(std::size_t i=0;i<patternSteps_.size();++i){auto& step=patternSteps_[i];
        step.setButtonText(juce::String(static_cast<int>(i+1)));step.setClickingTogglesState(true);
        step.setTitle("Pattern step "+juce::String(static_cast<int>(i+1)));
        step.setTooltip("Enable or disable this sixteenth-note step in the selected bar");
        step.onClick=[this,i] {const int bar=patternBar_.getSelectedId(),page=patternPage_.getSelectedId();const bool enabled=patternSteps_[i].getToggleState();
            if(bar>=1&&bar<=4&&page>=1&&page<=3)processor_.editPattern([bar,page,i,enabled](pattern::Pattern& p){
                const auto steps=pattern::stepsPerBar(p.grid);const auto local=static_cast<std::size_t>(page-1)*16+i;
                if(bar<=p.bars&&local<steps)p.steps[static_cast<std::size_t>(bar-1)*steps+local].enabled=enabled;});
            patternStepSelector_.setSelectedId(static_cast<int>(i+1),juce::dontSendNotification);refreshPattern();};
        addAndMakeVisible(step);
    }
    refreshPattern();
    startTimerHz (25);
}
FxPanel::~FxPanel() { stopTimer(); wavJob_.reset(); }
void FxPanel::paint (juce::Graphics& g)
{
    namespace c = vox::ui::tokens::colour;
    auto area = getLocalBounds().reduced (32);
    g.setColour (c::text);
    g.setFont (vox::ui::typography::sectionTitle());
    g.drawText ("PERFORM  /  SOUND FAMILY", area.removeFromTop (30), juce::Justification::centredLeft);
    g.setColour (c::textSecondary);
    g.setFont (vox::ui::typography::controlLabel());
    g.drawText ("One-shot event. Trigger with MIDI or audition.", area.removeFromTop (24), juce::Justification::centredLeft);
    auto macroLabel=getLocalBounds().reduced(32).withTrimmedTop(125).removeFromLeft(200).removeFromTop(12);
    macroLabel.translate(276,0);
    g.drawText("SOURCE   TARGET   VALUE   DEPTH",macroLabel,juce::Justification::centredLeft);
    auto labels=getLocalBounds().reduced(32).withTrimmedTop(197).removeFromTop(14);
    const int labelWidth=labels.getWidth()/4;
    for(int i=0;i<4;++i) {
        auto column=i==3?labels:labels.removeFromLeft(labelWidth);
        g.drawText("FX " + juce::String(i+1) + "            WET       AMOUNT",column.reduced(3,0),juce::Justification::centredLeft);
    }
    auto meter = getLocalBounds().reduced (32).removeFromBottom (55).removeFromLeft (320).toFloat();
    g.setColour (c::panelRaised); g.fillRoundedRectangle (meter, 4.0f);
    g.setColour (clipHold_?c::danger:c::accent); g.fillRoundedRectangle (meter.withWidth (meter.getWidth() * std::min(1.0f,shownPeak_)), 4.0f);
    g.setColour (c::text); g.setFont (vox::ui::typography::controlLabel());
    auto meterText=meter.toNearestInt().reduced(8);
    g.drawText(clipHold_?"OUTPUT PEAK  /  CLIP":"OUTPUT PEAK",meterText.removeFromTop(18),juce::Justification::centredLeft);
    g.drawText("EV "+juce::String(shownEvents_)+"/8+4  GR "+juce::String(shownGrains_)+"/64  DROP "+juce::String(static_cast<juce::int64>(shownDropped_)),
               meterText,juce::Justification::centredLeft);
}
void FxPanel::resized()
{
    auto area = getLocalBounds().reduced (32);
    programCurvesButton_.setBounds(area.withHeight(28).removeFromRight(154));
    area.removeFromTop (80);
    output_.setBounds (area.removeFromLeft (128).removeFromTop (140));
    duration_.setBounds (area.removeFromLeft (128).removeFromTop (140));
    area.removeFromLeft (20);
    auto familyArea = area.removeFromLeft (200);
    family_.setBounds (familyArea.removeFromTop (40));
    eventBars_.setBounds(familyArea.removeFromTop(25));
    auto modeRow=familyArea.removeFromTop(25);
    playMode_.setBounds(modeRow.removeFromLeft(modeRow.getWidth()/2).reduced(1));
    triggerPolicy_.setBounds(modeRow.reduced(1));
    clockStatus_.setBounds(familyArea.removeFromTop(12));
    auto macroRow=familyArea.removeFromTop(32);
    macroSelector_.setBounds(macroRow.removeFromLeft(49).reduced(1));
    macroDestination_.setBounds(macroRow.removeFromLeft(49).reduced(1));
    macroValue_.setBounds(macroRow.removeFromLeft(50).reduced(1));macroDepth_.setBounds(macroRow.reduced(1));
    auto sampleRow=familyArea.removeFromTop(23);
    sampleImport_.setBounds(sampleRow.removeFromLeft(58).reduced(1));
    sampleBuiltin_.setBounds(sampleRow.removeFromLeft(58).reduced(1));sampleStatus_.setBounds(sampleRow.reduced(1));
    area.removeFromLeft (20);
    auto buttons = area.removeFromLeft (160);
    audition_.setBounds (buttons.removeFromTop (32)); buttons.removeFromTop (4);
    panic_.setBounds (buttons.removeFromTop (32)); buttons.removeFromTop (4);
    factory_.setBounds (buttons.removeFromTop (32));

    area.removeFromLeft (20);
    auto generatorArea = area.removeFromLeft (180);
    auto seedRow=generatorArea.removeFromTop(30);
    generatorSettings_.setBounds(seedRow.removeFromRight(65).reduced(1));seed_.setBounds(seedRow);
    generatorArea.removeFromTop (5);
    generate_.setBounds (generatorArea.removeFromTop (35)); generatorArea.removeFromTop (5);
    mutate_.setBounds (generatorArea.removeFromTop (35));
    auto lockArea=area.removeFromLeft(100);
    auto topLocks=lockArea.removeFromTop(30), bottomLocks=lockArea.removeFromTop(30);
    layerLocks_[0].setBounds(topLocks.removeFromLeft(45));layerLocks_[1].setBounds(topLocks);
    layerLocks_[2].setBounds(bottomLocks.removeFromLeft(45));layerLocks_[3].setBounds(bottomLocks);
    lockArea.removeFromTop(4);comparison_.setBounds(lockArea.removeFromTop(24));
    lockArea.removeFromTop(4);auto exports=lockArea.removeFromTop(24);
    midiExport_.setBounds(exports.removeFromLeft(exports.getWidth()/2).reduced(1));wavExport_.setBounds(exports.reduced(1));
    auto fxRow=getLocalBounds().reduced(32).withTrimmedTop(197).removeFromTop(45);
    const int fxWidth=fxRow.getWidth()/4;
    for (std::size_t i=0;i<4;++i) {
        auto column=i==3?fxRow:fxRow.removeFromLeft(fxWidth);
        column=column.reduced(3).withTrimmedTop(14);effectsType_[i].setBounds(column.removeFromLeft(column.getWidth()*4/10));
        effectsWet_[i].setBounds(column.removeFromLeft(column.getWidth()/2));effectsAmount_[i].setBounds(column);
    }
    auto patternRow=getLocalBounds().reduced(32).removeFromBottom(55);
    patternRow.removeFromLeft(340);
    patternSelector_.setBounds(patternRow.removeFromLeft(190).reduced(0,8));
    patternRow.removeFromLeft(10);
    patternEnabled_.setBounds(patternRow.removeFromLeft(160).reduced(0,8));
    patternRow.removeFromLeft(8);patternStepSelector_.setBounds(patternRow.removeFromLeft(56).reduced(0,8));
    const int detailWidth=patternRow.getWidth()/4;
    for(std::size_t i=0;i<patternDetails_.size();++i){auto area=i==3?patternRow:patternRow.removeFromLeft(detailWidth);
        patternDetails_[i].setBounds(area.reduced(1));}
    auto stepRow=getLocalBounds().reduced(32).removeFromBottom(115).removeFromTop(35);
    patternLength_.setBounds(stepRow.removeFromLeft(85).reduced(2));
    patternBar_.setBounds(stepRow.removeFromLeft(80).reduced(2));
    patternGrid_.setBounds(stepRow.removeFromLeft(75).reduced(2));
    patternPage_.setBounds(stepRow.removeFromLeft(75).reduced(2));
    patternSwing_.setBounds(stepRow.removeFromLeft(75).reduced(2));
    const int stepWidth=stepRow.getWidth()/16;
    for(std::size_t i=0;i<patternSteps_.size();++i){auto area=i==15?stepRow:stepRow.removeFromLeft(stepWidth);
        patternSteps_[i].setBounds(area.reduced(2));}
    auto row = getLocalBounds().reduced(32);
    row.removeFromTop(247);
    row.removeFromBottom(120);
    generatorPanel_->setBounds(getLocalBounds().withSizeKeepingCentre(600,280));
    wavOptions_.setBounds(getLocalBounds().withSizeKeepingCentre(470,232));
    programCurves_.setBounds(row.reduced(3));
    const int width = row.getWidth() / 4;
    for (std::size_t i=0; i<layers_.size(); ++i) {
        auto card = i + 1 == layers_.size() ? row : row.removeFromLeft(width);
        layers_[i]->setBounds(card.reduced(3));
    }
}
void FxPanel::importSample()
{
    const auto state=processor_.sampleSnapshot();
    const bool relink=state.reference.kind==sample::Reference::Kind::external &&
        (state.status==sample::Controller::Status::missing || state.status==sample::Controller::Status::failed);
    sampleChooser_=std::make_unique<juce::FileChooser>(relink?"Relink the original WAV (SHA-256 must match)":"Choose a licensed WAV sample",juce::File{},"*.wav");
    sampleImport_.setEnabled(false);
    sampleChooser_->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,
        [safe=juce::Component::SafePointer<FxPanel>(this),relink,reference=state.reference](const juce::FileChooser& chooser){
            if(safe==nullptr)return;
            safe->sampleImport_.setEnabled(true);
            const auto file=chooser.getResult();if(file==juce::File{})return;
            const bool queued=relink?safe->processor_.relinkSample(file,reference):safe->processor_.importSample(file);
            if(!queued)
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                    "Sample request failed","The selected path is invalid or the sample selection changed while the chooser was open.");
        });
}
void FxPanel::exportPattern()
{
    // Capture before opening the chooser; later edits do not change the file.
    const auto pattern=processor_.patternSnapshot();
    const auto bpm=processor_.observedTempo();
    if(bpm<10.0||bpm>1000.0) {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
            "MIDI export failed","The captured host tempo is outside the supported MIDI export range (10–1000 BPM).");return;
    }
    exportChooser_=std::make_unique<juce::FileChooser>("Export trigger pattern as MIDI",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("VOX FX Pattern.mid"),"*.mid");
    midiExport_.setEnabled(false);wavExport_.setEnabled(false);
    exportChooser_->launchAsync(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles|
        juce::FileBrowserComponent::warnAboutOverwriting,
        [safe=juce::Component::SafePointer<FxPanel>(this),pattern,bpm](const juce::FileChooser& chooser){
            if(safe==nullptr)return;
            safe->midiExport_.setEnabled(true);safe->wavExport_.setEnabled(true);
            auto target=chooser.getResult();if(target==juce::File{})return;
            // Do not silently change the destination after the chooser's
            // overwrite confirmation, which could overwrite another file.
            if(!target.hasFileExtension("mid")){
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                    "MIDI export failed","Choose a filename ending in .mid.");return;
            }
            std::vector<std::uint8_t> bytes;
            bool success=pattern::exportMidi(pattern,bpm,bytes);
            if(success){
                success=fileexport::writeBytes(target,bytes);
            }
            juce::AlertWindow::showMessageBoxAsync(success?juce::MessageBoxIconType::InfoIcon:juce::MessageBoxIconType::WarningIcon,
                success?"MIDI exported":"MIDI export failed",
                success?("Trigger pattern exported at " + juce::String(bpm,2) + " BPM."):juce::String("Could not write the trigger pattern."));
        });
}
void FxPanel::exportWav(offline::Settings settings)
{
    // Capture a self-contained value while the processor is alive. The worker
    // never references the processor or editor and cannot touch the callback.
    std::shared_ptr<offline::Capture> capture;
    try { capture=std::make_shared<offline::Capture>(processor_.captureProgramWav(settings)); }
    catch(...) {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
            "WAV export failed","Could not capture the sound for export.");return;
    }
    if (offline::frameCount(capture->program, capture->settings, &capture->pattern) == 0) {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
            "WAV export failed", "This audition exceeds the 128 MiB WAV audio limit or has invalid settings.");return;
    }
    if (capture->program.mode==PlayMode::sequence) {
        pattern::FixedSchedule schedule;
        if (!pattern::scheduleFixed(capture->pattern,schedule) || schedule.count==0) {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                "WAV export failed", "The captured Sequence has no playable triggers.");return;
        }
    }
    if(capture->useUserSample && capture->sample.empty()) {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
            "WAV export failed","The selected granular sample is unavailable. Reimport it or choose the built-in source.");return;
    }
    exportChooser_=std::make_unique<juce::FileChooser>("Export VOX FX stereo WAV",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("VOX FX.wav"),"*.wav");
    midiExport_.setEnabled(false);wavExport_.setEnabled(false);
    exportChooser_->launchAsync(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles|
        juce::FileBrowserComponent::warnAboutOverwriting,
        [safe=juce::Component::SafePointer<FxPanel>(this),capture](const juce::FileChooser& chooser){
            if(safe==nullptr)return;
            safe->midiExport_.setEnabled(true);safe->wavExport_.setEnabled(true);
            const auto target=chooser.getResult();if(target==juce::File{})return;
            if(!target.hasFileExtension("wav")){
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                    "WAV export failed","Choose a filename ending in .wav.");return;
            }
            safe->wavExport_.setButtonText("STOP");
            try {
                safe->wavJob_=std::make_unique<fileexport::WavJob>(std::move(*capture),target);
            } catch(...) {
                safe->wavJob_.reset();safe->wavExport_.setButtonText("WAV");
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                    "WAV export failed","Could not start the render worker.");
            }
        });
}
void FxPanel::refreshPattern()
{
    const auto pattern=processor_.patternSnapshot();
    patternLength_.setSelectedId(pattern.bars,juce::dontSendNotification);
    patternGrid_.setSelectedId(static_cast<int>(pattern.grid)+1,juce::dontSendNotification);
    if(!patternSwing_.isMouseButtonDown()&&!patternSwing_.hasKeyboardFocus(true))
        patternSwing_.setValue(pattern.swing,juce::dontSendNotification);
    const int bar=std::clamp(patternBar_.getSelectedId(),1,4),page=std::clamp(patternPage_.getSelectedId(),1,3);
    const auto steps=pattern::stepsPerBar(pattern.grid);
    for(std::size_t i=0;i<patternSteps_.size();++i) {
        const auto local=static_cast<std::size_t>(page-1)*16+i;
        const bool inRange=bar<=pattern.bars && local<steps;
        patternSteps_[i].setEnabled(inRange);
        patternSteps_[i].setToggleState(inRange && pattern.steps[static_cast<std::size_t>(bar-1)*steps+local].enabled,juce::dontSendNotification);
    }
    const auto stepIndex=std::clamp(patternStepSelector_.getSelectedId(),1,16);
    const auto local=static_cast<std::size_t>(page-1)*16+static_cast<std::size_t>(stepIndex-1);
    const bool valid=bar<=pattern.bars && local<steps;
    const auto index=valid ? static_cast<std::size_t>(bar-1)*steps+local : 0;
    const auto& selected=pattern.steps[index];
    const int values[]{selected.note,selected.velocity,selected.probability,selected.ratchets};
    for(std::size_t i=0;i<patternDetails_.size();++i){auto& slider=patternDetails_[i];slider.setEnabled(valid);
        if(!slider.isMouseButtonDown() && !slider.hasKeyboardFocus(true))slider.setValue(values[i],juce::dontSendNotification);}
    // Keep the selector available when the current step is out of range so
    // the user can recover by selecting an existing step on this page.
    patternStepSelector_.setEnabled(bar<=pattern.bars && static_cast<unsigned>((page-1)*16)<steps);
}
void FxPanel::refreshGenerator()
{
    const auto controls=processor_.generatorState();
    if(!seed_.isMouseButtonDown()&&!seed_.hasKeyboardFocus(true))seed_.setValue(controls.seed,juce::dontSendNotification);
    for(std::size_t i=0;i<4;++i)layerLocks_[i].setToggleState(controls.locks.layers[i]==generator::all,juce::dontSendNotification);
}
void FxPanel::refreshMacro()
{
    const auto source=sourceForControl(macroSelector_.getSelectedId());if(source==mod::Source::count)return;
    const auto config=processor_.macroConfig();
    const auto destination=macroDestination_.getSelectedId()==2?mod::Destination::pan:mod::Destination::gain;
    float depth=0;for(std::size_t i=0;i<config.count;++i)
        if(config.routes[i].source==source&&config.routes[i].destination==destination){depth=config.routes[i].depth;break;}
    if(!macroDepth_.isMouseButtonDown()&&!macroDepth_.hasKeyboardFocus(true))macroDepth_.setValue(depth,juce::dontSendNotification);
}
void FxPanel::timerCallback()
{
    if(wavJob_){
        const auto result=wavJob_->result();
        if(result!=fileexport::Result::running){
            wavJob_.reset();wavExport_.setButtonText("WAV");
            if(result!=fileexport::Result::cancelled)juce::AlertWindow::showMessageBoxAsync(result==fileexport::Result::success?juce::MessageBoxIconType::InfoIcon:juce::MessageBoxIconType::WarningIcon,
                result==fileexport::Result::success?"WAV exported":"WAV export failed",
                result==fileexport::Result::success?"The captured audio was exported using the selected WAV settings.":"Could not render or write the WAV file.");
        }else {
            const int percent=static_cast<int>(wavJob_->progress()*100);
            wavExport_.setButtonText(wavJob_->cancelled()?"...":juce::String(percent)+"%");
        }
    }
    if (!isShowing()) return;
    const auto sample=processor_.sampleSnapshot();
    using Status=sample::Controller::Status;
    const char* text=sample.status==Status::builtin?"Built-in":sample.status==Status::loading?"Loading":
                     sample.status==Status::ready?"Ready":sample.status==Status::missing?"Missing":"Failed";
    sampleStatus_.setText(text,juce::dontSendNotification);
    const bool canRelink=sample.reference.kind==sample::Reference::Kind::external && (sample.status==Status::missing || sample.status==Status::failed);
    sampleImport_.setButtonText(canRelink?"Relink":"Import");
    sampleImport_.setTooltip(canRelink?"Locate the original sample; SHA-256 must match":"Import a licensed mono or stereo WAV sample");
    juce::String detail=sample.reference.path.empty()?juce::String("Built-in source"):juce::String::fromUTF8(sample.reference.path.c_str());
    if(sample.status==Status::failed)detail="Import or relink failed; retained: "+detail;
    if(sample.reference.kind==sample::Reference::Kind::pending&&sample.status==Status::missing)
        detail="Unverified import; reimport the WAV: "+detail;
    sampleStatus_.setTooltip(detail);
    // Block peak comes from rendered samples; visual decay is message-thread only.
    const auto program = processor_.program();
    patternEnabled_.setButtonText(processor_.patternUnavailable()?"PATTERN UNAVAILABLE":"HOST PATTERN");
    refreshPattern();refreshMacro();refreshGenerator();
    family_.setSelectedId (static_cast<int>(program.layers[0].family) + 1, juce::dontSendNotification);
    const int barsId=program.bars==0?1:program.bars==1?2:program.bars==2?3:program.bars==4?4:program.bars==8?5:6;
    eventBars_.setSelectedId(barsId,juce::dontSendNotification);
    playMode_.setSelectedId(static_cast<int>(program.mode)+1,juce::dontSendNotification);
    triggerPolicy_.setSelectedId(static_cast<int>(program.triggerPolicy)+1,juce::dontSendNotification);
    clockStatus_.setText(program.bars==0?"Seconds":processor_.musicalClockUnavailable()?"Host bar clock unavailable":(program.mode==PlayMode::transition ? "Ends on host bar line" : "Host bar clock ready"),juce::dontSendNotification);
    const auto fx=processor_.effectsSnapshot();
    for (std::size_t i=0;i<4;++i) {
        const auto s=effects::unpack(static_cast<std::uint16_t>(fx>>(i*16)));
        effectsType_[i].setSelectedId(static_cast<int>(s.type)+1,juce::dontSendNotification);
    }
    if (programCurves_.isVisible()) programCurves_.refresh();
    else for (std::size_t i=0; i<layers_.size(); ++i) layers_[i]->refresh(program.layers[i]);
    const auto telemetry=processor_.audioTelemetry();shownEvents_=telemetry.events;shownGrains_=telemetry.grains;shownDropped_=telemetry.dropped;
    clipHold_=processor_.consumeClip()?25u:(clipHold_?clipHold_-1:0u);
    shownPeak_ = std::max (processor_.outputPeak(), shownPeak_ * 0.88f);
    repaint();
}
} // namespace vox::fx
