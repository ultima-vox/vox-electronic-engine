#include "FxEditor.h"
#include "Processor.h"
#include "content/FactoryCandidates.h"
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
constexpr const char* familyControlNames[12][3]{
    {"Noise colour", "Sweep bias", "Exciter"},
    {"Tone balance", "Fall motion", "Noise skirt"},
    {"Partial spread", "Rotation", "Partial density"},
    {"Body", "Pitch drop", "Noise hit"},
    {"Air", "Doppler", "Delay mix"},
    {"Beam body", "Sweep range", "Click"},
    {"Resonance", "Sweep range", "Noise skirt"},
    {"Mode spread", "Mode motion", "Strike density"},
    {"Detune", "Texture motion", "Noise colour"},
    {"Vowel spacing", "Vowel motion", "Exciter"},
    {"Slice edge", "Read motion", "Slice level"},
    {"Grain level", "Pitch motion", "Grain density"}
};
}
GeneratorPanel::GeneratorPanel(FxProcessor& processor) : processor_(processor) {
    const char* names[]{"Style seed", "Active layers", "Intensity", "Energy", "Stereo", "Sound role"};
    for(std::size_t i=0;i<labels_.size();++i){labels_[i].setText(names[i],juce::dontSendNotification);addAndMakeVisible(labels_[i]);}
    for(int i=0;i<6;++i)style_.addItem(std::array<const char*,6>{"Psy Goa","Techno","Progressive","Ambient","Cinematic","Experimental"}[i],i+1);
    const char* roles[]{"Impact","Riser","Fall","Sweep","Zap","Rhythm","Atmosphere","Stinger"};
    for(int i=0;i<8;++i)role_.addItem(roles[i],i+1);
    role_.onChange=[this]{const auto id=role_.getSelectedId();if(id>0)(void)processor_.editGeneratorState([id](auto& state){state.role=static_cast<unsigned>(id-1);});};
    addAndMakeVisible(role_);
    const char* groups[]{"Global amplitude","Global pitch","Global cutoff","Global pan","Key / scale","Mode / retrigger","Bar length","FX slot types","Motion routes / clocks","Trigger pattern","External sample reference"};
    for(int i=0;i<11;++i)graphLock_.addItem(groups[i],i+1);
    graphLock_.setSelectedId(1,juce::dontSendNotification);graphLock_.onChange=[this]{timerCallback();};
    lockGroup_.onClick=[this]{const int id=graphLock_.getSelectedId();if(id<1||id>11)return;const auto bit=1u<<(id-1);
        const bool enabled=lockGroup_.getToggleState();(void)processor_.editGeneratorState([=](auto& state){if(enabled)state.graphLocks|=bit;else state.graphLocks&=~bit;});};
    fullGenerate_.setTooltip("Generate the selected role, replacing only unlocked structural fields. Host automation is not changed.");
    fullMutate_.setTooltip("Deterministically vary the unlocked existing structural values at the current seed.");
    const auto doSound=[this](bool mutation){const bool applied=processor_.generateSound(mutation);
        graphStatus_.setText(applied?u8("Sound graph updated · host values unchanged"):"No structural change (check locks / seed)",juce::dontSendNotification);};
    fullGenerate_.onClick=[doSound]{doSound(false);};fullMutate_.onClick=[doSound]{doSound(true);};
    for(auto* c:std::array<juce::Component*,5>{&graphLock_,&lockGroup_,&fullGenerate_,&fullMutate_,&graphStatus_})addAndMakeVisible(*c);
    complexity_.addItem("1 layer",1);complexity_.addItem("2 layers",2);
    complexity_.addItem("3 layers",3);complexity_.addItem("4 layers",4);
    style_.setTitle("Generator style seed");complexity_.setTitle("Generator active layers");
    style_.onChange=[this]{const auto value=style_.getSelectedId()-1;
        (void)processor_.editGeneratorState([value](generator::ControllerState& s){s.style=value;});};
    complexity_.onChange=[this]{const auto value=complexity_.getSelectedId();
        (void)processor_.editGeneratorState([value](generator::ControllerState& s){s.complexity=value;});};
    for (auto* dial : { &intensity_, &energy_, &stereo_ }) dial->getSlider().setRange (0, 1, .01);
    { const auto state=processor_.generatorState();
      intensity_.getSlider().setValue(state.intensity,juce::dontSendNotification);
      energy_.getSlider().setValue(state.energy,juce::dontSendNotification);
      stereo_.getSlider().setValue(state.stereo,juce::dontSendNotification); }
    intensity_.getSlider().onValueChange=[this]{const auto v=static_cast<float>(intensity_.getSlider().getValue());
        (void)processor_.editGeneratorState([v](generator::ControllerState& s){s.intensity=v;});};
    energy_.getSlider().onValueChange=[this]{const auto v=static_cast<float>(energy_.getSlider().getValue());
        (void)processor_.editGeneratorState([v](generator::ControllerState& s){s.energy=v;});};
    stereo_.getSlider().onValueChange=[this]{const auto v=static_cast<float>(stereo_.getSlider().getValue());
        (void)processor_.editGeneratorState([v](generator::ControllerState& s){s.stereo=v;});};
    // style_/complexity_/intensity_/energy_/stereo_ and fieldLocks_ are
    // re-parented into the PERFORM Program-Generator module (reference
    // composition); this overlay keeps role/graph locks and status.
    addAndMakeVisible(close_);
    for(int i=0;i<4;++i)lockLayer_.addItem("Layer " + juce::String::charToString(static_cast<juce::juce_wchar>('A'+i)),i+1);
    lockLayer_.setSelectedId(1,juce::dontSendNotification);lockLayer_.setTitle("Layer for generator locks");
    lockLayer_.onChange=[this]{timerCallback();};addAndMakeVisible(lockLayer_);
    const char* lockNames[]{"Family","On","Time","Gain","Pan","Gain env","Pan env","Pitch env","Cutoff env","Filters","Transpose","Character"};
    const std::uint32_t bits[]{generator::family,generator::enabled,generator::timing,generator::gain,generator::pan,generator::gainCurve,generator::panCurve,generator::pitchCurve,generator::cutoffCurve,generator::filters,generator::transpose,generator::character};
    for(std::size_t i=0;i<fieldLocks_.size();++i){
        auto& button=fieldLocks_[i];button.setButtonText(lockNames[i]);button.setTitle("Lock " + juce::String(lockNames[i]));
        button.setTooltip("Preserve this field during Generate and Mutate");
        button.onClick=[this,i,bit=bits[i]]{const int layer=lockLayer_.getSelectedId()-1;if(layer<0||layer>=4)return;
            const bool locked=fieldLocks_[i].getToggleState();
            (void)processor_.editGeneratorState([layer,bit,locked](generator::ControllerState& s){
                auto& mask=s.locks.layers[static_cast<std::size_t>(layer)];if(locked)mask|=bit;else mask&=~bit;});};
        // fieldLocks_ live in the PERFORM module; the overlay edits lockLayer_. 
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
        // style_/complexity_/intensity_/energy_/stereo_ live in the PERFORM
        // Program-Generator module now; the overlay must not re-position them.
        switch(i){case 0:if(style_.getParentComponent()==this)style_.setBounds(row);break;
            case 1:if(complexity_.getParentComponent()==this)complexity_.setBounds(row);break;
            case 5:role_.setBounds(row);break;default:break;}}
    auto locks=area.removeFromTop(27);lockLayer_.setBounds(locks.removeFromLeft(100));
    const auto width=locks.getWidth()/4;for(std::size_t i=0;i<4;++i)fieldLocks_[i].setBounds(locks.removeFromLeft(width));
    auto second=area.removeFromTop(27);second.removeFromLeft(100);
    const auto secondWidth=second.getWidth()/8;for(std::size_t i=4;i<fieldLocks_.size();++i)fieldLocks_[i].setBounds(second.removeFromLeft(secondWidth));
    auto graph=area.removeFromTop(30);graphLock_.setBounds(graph.removeFromLeft(224).reduced(2));lockGroup_.setBounds(graph.reduced(2));
    auto commands=area.removeFromTop(31);fullGenerate_.setBounds(commands.removeFromLeft(137).reduced(2));fullMutate_.setBounds(commands.removeFromLeft(133).reduced(2));graphStatus_.setBounds(commands.reduced(2));
}
void GeneratorPanel::timerCallback() {
    if(!isShowing())return;
    const auto state=processor_.generatorState();
    style_.setSelectedId(state.style+1,juce::dontSendNotification);
    complexity_.setSelectedId(state.complexity,juce::dontSendNotification);
    role_.setSelectedId(static_cast<int>(state.role)+1,juce::dontSendNotification);
    const int group=graphLock_.getSelectedId();if(group>=1&&group<=11)
        lockGroup_.setToggleState((state.graphLocks&(1u<<(group-1)))!=0,juce::dontSendNotification);
    auto sync=[](vox::ui::VoxKnob& dial,float value){auto& slider=dial.getSlider();
        if(!slider.isMouseButtonDown()&&!slider.hasKeyboardFocus(true))slider.setValue(value,juce::dontSendNotification);};
    sync(intensity_,state.intensity);sync(energy_,state.energy);sync(stereo_,state.stereo);
    const int layer=lockLayer_.getSelectedId()-1;
    if(layer>=0&&layer<4){const auto mask=state.locks.layers[static_cast<std::size_t>(layer)];
        const std::uint32_t bits[]{generator::family,generator::enabled,generator::timing,generator::gain,generator::pan,generator::gainCurve,generator::panCurve,generator::pitchCurve,generator::cutoffCurve,generator::filters,generator::transpose,generator::character};
        for(std::size_t i=0;i<fieldLocks_.size();++i)fieldLocks_[i].setToggleState((mask&bits[i])!=0,juce::dontSendNotification);}
}
ProgramCurvePanel::ProgramCurvePanel(FxProcessor& processor) : editor_(processor, std::nullopt) {
    // Per-layer trajectory editors complete the 20 timeline targets.
    for (std::size_t i = 0; i < layerEditors_.size(); ++i)
        layerEditors_[i] = std::make_unique<CurveEditor>(processor, i);
    const char* names[] { "Amplitude", "Pitch", "Cutoff", "Panorama" };
    for (int i=0; i<4; ++i) selector_.addItem(names[i], i+1);
    selector_.setTitle("Program trajectory");
    selector_.setSelectedId(1, juce::dontSendNotification);
    selector_.onChange = [this] {
        const int index = selector_.getSelectedId() - 1;
        if (index < 0 || index >= 4) return;
        activeEditor().setKind(static_cast<CurveKind>(index));
        refreshHint();
    };
    // 20 timeline targets: 4 trajectory kinds across Program + Layer A..D.
    scope_.addItem("Program", 1);
    for (int i = 0; i < 4; ++i)
        scope_.addItem(juce::String("Layer ") + juce::String::charToString(static_cast<juce::juce_wchar>('A' + i)), i + 2);
    scope_.setTitle("Timeline target scope");
    scope_.setSelectedId(1, juce::dontSendNotification);
    scope_.onChange = [this] { applyScope(); };
    for (auto& editor : layerEditors_) addChildComponent(*editor);
    undo_.setTitle("Undo curve edit"); redo_.setTitle("Redo curve edit");
    reset_.setTooltip("Restore the selected trajectory to its neutral value; this edit can be undone");
    undo_.onClick = [this] { (void)activeEditor().undo(); };
    redo_.onClick = [this] { (void)activeEditor().redo(); };
    reset_.onClick = [this] { (void)activeEditor().resetCurve(); };
    hint_.setFont(vox::ui::typography::controlLabel());
    for (auto* child : std::array<juce::Component*,7>{ &scope_, &selector_, &undo_, &redo_, &reset_, &editor_, &hint_ })
        addAndMakeVisible(*child);
    refreshHint();
}
CurveEditor& ProgramCurvePanel::editor() noexcept { return activeEditor(); }
CurveEditor& ProgramCurvePanel::activeEditor() noexcept {
    const auto scope = static_cast<std::size_t> (juce::jlimit (0, 4, scope_.getSelectedId() - 1));
    return scope == 0 ? editor_ : *layerEditors_[scope - 1];
}
void ProgramCurvePanel::applyScope() {
    for (auto& editor : layerEditors_) editor->setVisible (false);
    editor_.setVisible (scope_.getSelectedId() <= 1);
    activeEditor().setKind (static_cast<CurveKind> (juce::jlimit (0, 3, selector_.getSelectedId() - 1)));
    activeEditor().refresh();
    refreshHint();
}
void ProgramCurvePanel::refresh() { activeEditor().refresh(); }
void ProgramCurvePanel::refreshHint() {
    const char* unit = "Amplitude: 0% to 100%";
    switch (activeEditor().kind()) {
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
    header.removeFromLeft(160);
    scope_.setBounds(header.removeFromLeft(110).reduced(2));
    selector_.setBounds(header.removeFromLeft(140).reduced(2));
    undo_.setBounds(header.removeFromLeft(60).reduced(2));
    redo_.setBounds(header.removeFromLeft(60).reduced(2));
    reset_.setBounds(header.removeFromLeft(100).reduced(2));
    hint_.setBounds(area.removeFromBottom(22));
    editor_.setBounds(area);
    for (auto& editor : layerEditors_) editor->setBounds(area);
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
    prepare_.onClick=[this]{setVisible(false);if(onPrepareDrag)onPrepareDrag(settings());};
    addAndMakeVisible(close_); addAndMakeVisible(save_); addAndMakeVisible(prepare_);
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
    auto footer=area.removeFromBottom(30); save_.setBounds(footer.removeFromRight(130).reduced(2));prepare_.setBounds(footer.removeFromRight(130).reduced(2));close_.setBounds(footer.removeFromRight(80).reduced(2));
}
namespace {
constexpr const char* motionSources[]{"LFO 1","LFO 2","Event step","Sample & hold","Event ADSR","Event phase","Velocity","Dry signal follower","Macro 1","Macro 2","Macro 3","Macro 4","Macro 5","Macro 6","Macro 7","Macro 8"};
const juce::String motionDestinations[]{"Master gain","Master balance",u8("Voice pitch (±12 st)"),u8("Voice cutoff (±2 oct)"),u8("Filter Q (×¼–4)"),u8("Grain rate (×½–2)"),"Layer gain","Layer pan","Stereo width","FX 1 wet","FX 1 amount","FX 2 wet","FX 2 amount","FX 3 wet","FX 3 amount","FX 4 wet","FX 4 amount"};
static_assert(std::size(motionSources)==static_cast<unsigned>(mod::Source::count));
static_assert(std::size(motionDestinations)==static_cast<unsigned>(mod::Destination::count));
}
MotionPanel::MotionPanel(FxProcessor& processor):processor_(processor){
    title_.setText(u8("MOTION — VOICE AND MIX ROUTES"),juce::dontSendNotification);
    title_.setFont(vox::ui::typography::controlLabel());
    routeHelp_.setText("Depth is signed. Event sources target voices; dry follower targets mix/FX.",juce::dontSendNotification);
    rows_.setRowHeight(25);rows_.setMultipleSelectionEnabled(false);
    for(unsigned i=0;i<std::size(motionSources);++i)source_.addItem(motionSources[i],static_cast<int>(i+1));
    for(unsigned i=0;i<std::size(motionDestinations);++i)destination_.addItem(motionDestinations[i],static_cast<int>(i+1));
    source_.setTitle("Route source");destination_.setTitle("Route destination");
    const auto slider=[](juce::Slider& s,double a,double b,double step,const char* title,const char* suffix){
        s.setRange(a,b,step);s.setTitle(title);s.setTextValueSuffix(suffix);
        s.setSliderStyle(juce::Slider::LinearHorizontal);s.setTextBoxStyle(juce::Slider::TextBoxRight,false,90,23);
    };
    slider(depth_,-100,100,.01,"Signed route depth"," %");
    slider(smooth_,0,1000,1,"Route smoothing time"," ms");
    slider(randomRate_,.03125,16,0,"Sample and hold rate"," Hz");
    slider(randomSeed_,0,4294967295.,1,"Sample and hold seed","");
    randomSeed_.setNumDecimalPlacesToDisplay(0);
    for(unsigned i=0;i<2;++i){
        sync_[i].setButtonText("Tempo sync");
        slider(lfoRate_[i],.03125,16,0,i==0?"LFO 1 rate":"LFO 2 rate"," Hz");
        lfoRate_[i].setNumDecimalPlacesToDisplay(4);
        sync_[i].setTooltip("Cycles per quarter note at host BPM, with continuous phase. Free-running phase; 120 BPM when host tempo is unavailable.");
        sync_[i].onClick=[this,i]{lfoRate_[i].setTextValueSuffix(sync_[i].getToggleState()?" cyc/qn":" Hz");};
        addAndMakeVisible(sync_[i]);addAndMakeVisible(lfoRate_[i]);
    }
    randomRate_.setNumDecimalPlacesToDisplay(4);
    shapeTitle_.setText(u8("EVENT SHAPE — eight equal phase steps (0–100%)"),juce::dontSendNotification);
    for(std::size_t i=0;i<steps_.size();++i){
        auto& step=steps_[i];step.setRange(0,100,.01);step.setTitle("Step "+juce::String(static_cast<int>(i+1)));
        step.setTooltip(step.getTitle());step.setSliderStyle(juce::Slider::LinearVertical);
        step.setTextBoxStyle(juce::Slider::TextBoxBelow,false,57,19);addAndMakeVisible(step);
    }
    constexpr const char* names[]{"Attack","Decay","Sustain","Release"};
    for(std::size_t i=0;i<adsr_.size();++i){
        auto& a=adsr_[i];slider(a,i==3?5:0,i==2?100:2000,.01,names[i],i==2?" %":" ms");addAndMakeVisible(a);
    }
    customAdsr_.setTooltip("Custom NoteOff release applies to held voices with an ADSR route. Panic still fades in 5 ms.");
    shapeApply_.onClick=[this]{commit(3);};resetShape_.onClick=[this]{commit(4);};
    for(auto* c:std::array<juce::Component*,4>{&shapeTitle_,&customAdsr_,&shapeApply_,&resetShape_})addAndMakeVisible(*c);
    apply_.onClick=[this]{commit(0);};remove_.onClick=[this]{commit(1);};clock_.onClick=[this]{commit(2);};
    undoButton_.onClick=[this]{stepHistory(false);};redoButton_.onClick=[this]{stepHistory(true);};
    undoButton_.setTitle("Undo Motion edit");redoButton_.setTitle("Redo Motion edit");
    undoButton_.setTooltip("Undo a committed route, clock or event-shape edit in this editor");
    redoButton_.setTooltip("Redo a committed Motion edit; a new edit clears redo");
    setWantsKeyboardFocus(true);
    reload_.onClick=[this]{refresh();};close_.onClick=[this]{setVisible(false);if(onClosed)onClosed();};
    const std::array<juce::Component*,17> components{&title_,&routeHelp_,&rows_,&source_,&destination_,&depth_,&smooth_,&randomRate_,&randomSeed_,&status_,&apply_,&remove_,&clock_,&reload_,&close_,&undoButton_,&redoButton_};
    for(auto* c:components)addAndMakeVisible(*c);
    refresh(0);
}
int MotionPanel::getNumRows(){return static_cast<int>(shown_.config.count)+(shown_.config.count<64?1:0);}
void MotionPanel::setEmbedded(bool embedded) noexcept { close_.setVisible(!embedded); }
void MotionPanel::paintListBoxItem(int row,juce::Graphics& g,int w,int h,bool selected){
    namespace c=vox::ui::tokens::colour;
    if(selected)g.fillAll(c::border);
    g.setColour(c::text);g.setFont(juce::FontOptions(12.f));
    if(row<0||row>=getNumRows())return;
    juce::String text;
    if(static_cast<unsigned>(row)==shown_.config.count)text="+ Add route";
    else {const auto r=shown_.config.routes[static_cast<unsigned>(row)];
        text=juce::String(row+1)+". "+motionSources[static_cast<unsigned>(r.source)]+u8(" → ")+motionDestinations[static_cast<unsigned>(r.destination)]+
            "   "+juce::String(r.depth*100,1)+"% / "+juce::String(r.smoothingSeconds*1000,0)+" ms";}
    g.drawFittedText(text,8,0,w-16,h,juce::Justification::centredLeft,1);
}
void MotionPanel::selectedRowsChanged(int row){
    const bool existing=row>=0&&static_cast<unsigned>(row)<shown_.config.count;
    const auto r=existing?shown_.config.routes[static_cast<unsigned>(row)]:mod::Route{mod::Source::macro1,mod::Destination::gain,.25f,.01f};
    source_.setSelectedId(static_cast<int>(r.source)+1,juce::dontSendNotification);
    destination_.setSelectedId(static_cast<int>(r.destination)+1,juce::dontSendNotification);
    depth_.setValue(r.depth*100,juce::dontSendNotification);smooth_.setValue(r.smoothingSeconds*1000,juce::dontSendNotification);
    remove_.setEnabled(existing);apply_.setEnabled(row>=0);
}
void MotionPanel::refresh(int row){
    if(row<0)row=std::max(0,rows_.getSelectedRow());
    const auto current=processor_.versionedMotion();
    if(shown_.revision!=current.revision||shown_.config!=current.config){undoMotion_.clear();redoMotion_.clear();}
    shown_=current;
    undoButton_.setEnabled(!undoMotion_.empty());redoButton_.setEnabled(!redoMotion_.empty());
    rows_.updateContent();row=std::clamp(row,0,getNumRows()-1);rows_.selectRow(row);selectedRowsChanged(row);
    for(unsigned i=0;i<2;++i){const auto lfo=shown_.config.lfos[i];
        lfoRate_[i].setValue(lfo.rate,juce::dontSendNotification);sync_[i].setToggleState(lfo.sync,juce::dontSendNotification);
        lfoRate_[i].setTextValueSuffix(lfo.sync?" cyc/qn":" Hz");}
    randomRate_.setValue(shown_.config.randomRate,juce::dontSendNotification);randomSeed_.setValue(shown_.config.randomSeed,juce::dontSendNotification);
    const auto& e=shown_.config.event;
    for(std::size_t i=0;i<steps_.size();++i)steps_[i].setValue(e.steps[i]*100,juce::dontSendNotification);
    const double values[]{e.attack*1000,e.decay*1000,e.sustain*100,e.release*1000};
    for(std::size_t i=0;i<adsr_.size();++i)adsr_[i].setValue(values[i],juce::dontSendNotification);
    customAdsr_.setToggleState(e.customAdsr,juce::dontSendNotification);
    status_.setText(juce::String(shown_.config.count)+" / 64 routes",juce::dontSendNotification);
}
void MotionPanel::commit(int action){
    const int row=rows_.getSelectedRow();auto next=shown_.config;
    if(action==0){
        const mod::Route route{static_cast<mod::Source>(source_.getSelectedId()-1),static_cast<mod::Destination>(destination_.getSelectedId()-1),
            static_cast<float>(depth_.getValue()*.01),static_cast<float>(smooth_.getValue()*.001)};
        if(row<0||!next.put(static_cast<std::size_t>(row),route)){
            status_.setText("Unsupported source/target pairing, or no free row.",juce::dontSendNotification);return;}
    }else if(action==1){if(row<0||!next.erase(static_cast<std::size_t>(row)))return;}
    else if(action==2) {
        for(unsigned i=0;i<2;++i)next.lfos[i]={static_cast<float>(lfoRate_[i].getValue()),sync_[i].getToggleState()};
        next.randomRate=static_cast<float>(randomRate_.getValue());next.randomSeed=static_cast<std::uint32_t>(randomSeed_.getValue());
    }else if(action==3){
        for(std::size_t i=0;i<steps_.size();++i)next.event.steps[i]=static_cast<float>(steps_[i].getValue()*.01);
        next.event.customAdsr=customAdsr_.getToggleState();
        next.event.attack=static_cast<float>(adsr_[0].getValue()*.001);next.event.decay=static_cast<float>(adsr_[1].getValue()*.001);
        next.event.sustain=static_cast<float>(adsr_[2].getValue()*.01);next.event.release=static_cast<float>(adsr_[3].getValue()*.001);
    }else next.event={};
    if(next==shown_.config){refresh(row);return;}
    const auto previous=shown_.config;
    if(!publishMotion(next))return;
    if(undoMotion_.size()==32)undoMotion_.pop_front();
    undoMotion_.push_back(previous);redoMotion_.clear();
    refresh(row);
}
bool MotionPanel::publishMotion(const macro::Config& next){
    std::uint64_t revision=0;
    if(!processor_.replaceMotionIfCurrent(shown_,next,&revision)){
        refresh();status_.setText("Program or Motion changed. Old edit was not applied.",juce::dontSendNotification);return false;
    }
    shown_={next,revision};
    return true;
}
void MotionPanel::stepHistory(bool redo){
    auto& from=redo?redoMotion_:undoMotion_;
    auto& to=redo?undoMotion_:redoMotion_;
    if(from.empty())return;
    const auto previous=shown_.config;
    if(!publishMotion(from.back()))return;
    to.push_back(previous);from.pop_back();
    refresh();
}
bool MotionPanel::keyPressed(const juce::KeyPress& key){
    const auto mods=key.getModifiers();
    if(!mods.isCommandDown()||mods.isAltDown())return false;
    const auto code=key.getKeyCode();
    if(code=='Z'||code=='z'){stepHistory(mods.isShiftDown());return true;}
    if(code=='Y'||code=='y'){stepHistory(true);return true;}
    return false;
}
void MotionPanel::paint(juce::Graphics& g){
    namespace c=vox::ui::tokens::colour;
    g.fillAll(c::panelRaised);g.setColour(c::border);g.drawRect(getLocalBounds());g.setColour(c::text);
    g.setFont(juce::FontOptions(12.f));
    for(const auto& slider:adsr_)g.drawText(slider.getTitle(),slider.getBounds().translated(0,-21),juce::Justification::centredLeft);
    for(auto entry:std::array<std::pair<const char*,juce::Component*>,6>{{{"Depth",&depth_},{"Smoothing",&smooth_},
        {"LFO 1",&lfoRate_[0]},{"LFO 2",&lfoRate_[1]},{"S&H rate",&randomRate_},{"S&H seed",&randomSeed_}}}){
        const auto r=entry.second->getBounds();g.drawText(entry.first,r.getX()-86,r.getY(),82,r.getHeight(),juce::Justification::centredLeft);}
}
void MotionPanel::resized(){
    auto a=getLocalBounds().reduced(12);title_.setBounds(a.removeFromTop(23));routeHelp_.setBounds(a.removeFromTop(24));
    auto footer=a.removeFromBottom(32);
    close_.setBounds(footer.removeFromRight(76).reduced(2));reload_.setBounds(footer.removeFromRight(76).reduced(2));
    redoButton_.setBounds(footer.removeFromRight(76).reduced(2));undoButton_.setBounds(footer.removeFromRight(76).reduced(2));
    status_.setBounds(footer);
    auto shape=a.removeFromBottom(161);
    shapeTitle_.setBounds(shape.removeFromTop(20));
    auto meters=shape.removeFromTop(68);
    for(auto& step:steps_)step.setBounds(meters.removeFromLeft(getWidth()/9).reduced(2));
    shape.removeFromTop(17);auto envelope=shape.removeFromTop(28);
    for(auto& slider:adsr_){
        const auto width=(getWidth()-24)/4;
        slider.setBounds(envelope.removeFromLeft(width).reduced(2));
    }
    customAdsr_.setBounds(shape.removeFromLeft(163));shapeApply_.setBounds(shape.removeFromLeft(113).reduced(2));resetShape_.setBounds(shape.removeFromLeft(113).reduced(2));
    rows_.setBounds(a.removeFromLeft(a.getWidth()/2).reduced(2));auto form=a.reduced(6,0);
    source_.setBounds(form.removeFromTop(30).reduced(2));destination_.setBounds(form.removeFromTop(30).reduced(2));
    depth_.setBounds(form.removeFromTop(29).withTrimmedLeft(86).reduced(2));smooth_.setBounds(form.removeFromTop(29).withTrimmedLeft(86).reduced(2));
    auto buttons=form.removeFromTop(30);apply_.setBounds(buttons.removeFromLeft(120).reduced(2));remove_.setBounds(buttons.removeFromLeft(120).reduced(2));
    form.removeFromTop(7);
    for(unsigned i=0;i<2;++i){auto r=form.removeFromTop(28);sync_[i].setBounds(r.removeFromRight(103));lfoRate_[i].setBounds(r.withTrimmedLeft(86).reduced(2));}
    randomRate_.setBounds(form.removeFromTop(27).withTrimmedLeft(86).reduced(2));randomSeed_.setBounds(form.removeFromTop(27).withTrimmedLeft(86).reduced(2));
    clock_.setBounds(form.removeFromTop(29).removeFromRight(134).reduced(2));
}
SourcePanel::SourcePanel(FxProcessor& processor):processor_(processor) {
    constexpr const char* roots[]{"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    constexpr const char* scales[]{"Chromatic","Major","Natural minor","Dorian","Phrygian"};
    const auto options=[](juce::ComboBox& box,const auto& names){for(std::size_t i=0;i<std::size(names);++i)box.addItem(names[i],static_cast<int>(i+1));};
    constexpr const char* layerNames[]{"Layer A","Layer B","Layer C","Layer D"};
    options(layer_,layerNames);options(root_,roots);options(scale_,scales);
    layer_.setTitle("Layer to edit");root_.setTitle("Program root");scale_.setTitle("Program input scale");
    root_.setTooltip("Quantize incoming pitched source notes to the nearest scale degree; chromatic leaves the note unchanged");
    layer_.setSelectedId(1,juce::dontSendNotification);layer_.onChange=[this]{refresh();};
    root_.onChange=[this]{editKey();};scale_.onChange=[this]{editKey();};
    for(auto* box:{&layer_,&root_,&scale_})addAndMakeVisible(*box);
    const auto slider=[this](StructuralSlider& control, double min, double max, double step, const char* title,const char* units) {
        control.setRange(min,max,step);control.setTitle(title);control.setTextValueSuffix(units);
        control.setSliderStyle(juce::Slider::LinearHorizontal);control.setTextBoxStyle(juce::Slider::TextBoxRight,false,85,24);
        control.onDragStart=[this]{dragging_=true;};
        control.onDragEnd=[this]{dragging_=false;refresh();};
        addAndMakeVisible(control);
    };
    slider(transpose_,-24,24,1,"Layer transpose"," st");
    transpose_.onStructuralChange=[this]{const auto value=static_cast<float>(transpose_.getValue());editLayer([=](NoiseLayer& l){l.transpose=value;});};
    slider(character_,-100,100,.1,"Source character (relative to legacy)"," %");
    character_.onStructuralChange=[this]{const auto value=static_cast<float>(character_.getValue()*.01);editLayer([=](NoiseLayer& l){l.character=value;});};
    addAndMakeVisible(characterLabel_);
    for(std::size_t i=0;i<familyControls_.size();++i){
        slider(familyControls_[i],-100,100,.1,"Family control"," %");
        familyControls_[i].onStructuralChange=[this,i]{
            const auto value=static_cast<float>(familyControls_[i].getValue()*.01);
            editLayer([i,value](NoiseLayer& l){
                auto& controls=l.familyControls;
                if(i==0)controls.primary=value;else if(i==1)controls.motion=value;else controls.density=value;
            });
        };
        familyControlLabels_[i].setJustificationType(juce::Justification::centredLeft);
        familyControlLabels_[i].setFont(vox::ui::typography::controlLabel());
        addAndMakeVisible(familyControlLabels_[i]);
    }
    slider(grainLength_,10,180,.1,"Grain length"," ms");slider(grainPosition_,0,100,.1,"Grain start"," %");
    grainLength_.onStructuralChange=[this]{const auto value=static_cast<float>(grainLength_.getValue());editLayer([=](auto& l){l.grains.milliseconds=value;});};
    slider(grainTravel_,-100,100,1,"Grain travel across the layer timeline"," %");
    // JUCE sliders opt out of focus by default; these fixed-grain
    // controls support local arrow-key editing in the Source panel.
    for(auto* control:{&grainLength_,&grainPosition_,&grainTravel_}){
        control->setWantsKeyboardFocus(true);
        control->setTextBoxStyle(juce::Slider::TextBoxRight,false,64,24);
    }
    grainTravel_.setDoubleClickReturnValue(true,0);
    grainTravel_.setTooltip("Move new grain starts along the source over the layer timeline. Stops at either edge. Does not change pitch or reverse. Unavailable while Scatter is on.");
    grainTravel_.onStructuralChange=[this]{const auto value=static_cast<float>(grainTravel_.getValue()*.01);
        editLayer([value](auto& l){if(l.family==NoiseFamily::granular&&l.grains.position>=0)l.grains.travel=value;});};
    const auto setPosition=[this]{const float value=scatter_.getToggleState()?-1.f:static_cast<float>(grainPosition_.getValue()*.01);
        editLayer([=](auto& l){l.grains.position=value;});};
    grainPosition_.onStructuralChange=setPosition;scatter_.onClick=setPosition;
    reverse_.onClick=[this]{const auto checked=reverse_.getToggleState();editLayer([=](auto& l){l.grains.reverse=checked;});};
    scatter_.setTooltip("Use a seeded random grain start instead of a fixed percentage of the source");
    addAndMakeVisible(scatter_);addAndMakeVisible(reverse_);
    // Keep JUCE's Tab/Shift+Tab traversal inside this overlay instead of
    // mixing the explicit Source order with the underlying editor controls.
    setFocusContainerType(juce::Component::FocusContainerType::keyboardFocusContainer);
    setTitle("Layer Source Editor");
    setDescription("Choose a layer, then Tab through its source controls. Escape closes this editor.");
    constexpr const char* grids[]{"1/8","1/16","1/32"};
    constexpr const char* reads[]{"Alt. 1x / 2x","Forward","Reverse"};
    options(sliceGrid_,grids);options(sliceRead_,reads);
    sliceGrid_.setTitle("Glitch slice tempo division");
    sliceGrid_.setTooltip("Event-local clock starts with the note; follows tempo, not host PPQ or the Trigger Pattern.");
    sliceRead_.setTitle("Glitch slice read direction and speed");
    sliceRead_.setTooltip("Play the procedural capture forward, backward, or forward at alternating 1x and 2x rates.");
    sliceGrid_.onChange=[this]{const auto id=sliceGrid_.getSelectedId();
        if(id>=1&&id<=3)editGlitch([id](auto& s){s.grid=static_cast<source::SliceGrid>(id-1);});};
    sliceRead_.onChange=[this]{const auto id=sliceRead_.getSelectedId();
        if(id>=1&&id<=3)editGlitch([id](auto& s){s.read=static_cast<source::SliceRead>(id-1);});};
    sliceGrid_.setName("Slice grid");sliceRead_.setName("Slice read");
    addAndMakeVisible(sliceGrid_);addAndMakeVisible(sliceRead_);
    for(unsigned i=0;i<sliceGate_.size();++i){
        auto& gate=sliceGate_[i];gate.setButtonText(juce::String(i+1));gate.setTitle("Play Glitch slice "+juce::String(i+1));
        gate.setTooltip("Enable this slice in the repeating four-step source cycle. Filters and shared FX may still ring on rests.");
        gate.onClick=[this,i]{const auto on=sliceGate_[i].getToggleState();
            editGlitch([i,on](auto& s){s.gates=static_cast<std::uint8_t>(on?s.gates|(1u<<i):s.gates&~(1u<<i));});};
        addAndMakeVisible(gate);
    }
    sliceReset_.setTooltip("Restore default 1/16 divisions, alternating rate and legacy slice gates.");
    sliceReset_.onClick=[this]{editGlitch([](auto& s){s={};});};addAndMakeVisible(sliceReset_);
    constexpr const char* types[]{"Off","Low-pass","High-pass","Band-pass","Notch"};
    for(std::size_t i=0;i<filters_.size();++i){
        auto& f=filters_[i];options(f.type,types);f.slope.addItem("12 dB/oct",1);f.slope.addItem("24 dB/oct",2);
        f.type.setTitle("Layer filter type");f.slope.setTitle("Filter stages");
        f.slope.setTooltip("12 or 24 dB/oct low/high pass; one or two stages for band-pass and notch");
        f.slope.onChange=[this,i]{const int id=filters_[i].slope.getSelectedId();if(id==1||id==2)editLayer([=](NoiseLayer& l){l.filters[i].poles=id==1?2:4;});};
        f.type.onChange=[this,i]{const int id=filters_[i].type.getSelectedId();if(id>=1&&id<=5)editLayer([=](NoiseLayer& l){l.filters[i].type=static_cast<source::FilterType>(id-1);});};
        slider(f.hz,20,20000,1,"Filter cutoff"," Hz");f.hz.setSkewFactorFromMidPoint(1000);
        f.hz.setTooltip("Cutoff is limited to 45% of the host rate during rendering");
        slider(f.q,.5,4,.001,"Resonance Q", " Q");
        f.hz.onStructuralChange=[this,i]{const auto value=static_cast<float>(filters_[i].hz.getValue());editLayer([=](NoiseLayer& l){l.filters[i].hz=value;});};
        f.q.onStructuralChange=[this,i]{const auto value=static_cast<float>(filters_[i].q.getValue());editLayer([=](NoiseLayer& l){l.filters[i].q=value;});};
        addAndMakeVisible(f.type);addAndMakeVisible(f.slope);
    }
    close_.onClick=[this]{close();};addAndMakeVisible(close_);
    int order=0;
    const auto tab=[&](juce::Component& child){child.setExplicitFocusOrder(++order);};
    tab(layer_);tab(root_);tab(scale_);tab(transpose_);tab(character_);
    for(auto& control:familyControls_)tab(control);
    for(auto& f:filters_){tab(f.type);tab(f.slope);tab(f.hz);tab(f.q);}
    tab(grainLength_);tab(grainPosition_);tab(grainTravel_);tab(scatter_);tab(reverse_);
    tab(sliceGrid_);tab(sliceRead_);for(auto& gate:sliceGate_)tab(gate);tab(sliceReset_);tab(close_);
    refresh();
}
void SourcePanel::open(juce::Component& returnFocus){
    returnFocus_=&returnFocus;
    refresh();setVisible(true);toFront(false);
    if(isShowing()&&getPeer()!=nullptr)layer_.grabKeyboardFocus();
}
void SourcePanel::close(){
    dragging_=false;setVisible(false);
    if(auto* target=returnFocus_.getComponent();target&&target->isShowing()&&target->isEnabled()&&target->getPeer()!=nullptr)
        target->grabKeyboardFocus();
    returnFocus_=nullptr;
}
bool SourcePanel::keyPressed(const juce::KeyPress& key){
    if(isVisible()&&key==juce::KeyPress::escapeKey){close();return true;}
    return false;
}
template <typename Edit> void SourcePanel::editGlitch(Edit edit){
    editLayer([&](auto& l){if(l.family==NoiseFamily::glitch)edit(l.glitch);});
}
template <typename Edit> void SourcePanel::editLayer(Edit edit){
    const int index=layer_.getSelectedId()-1;
    if(index>=0 && index<4) (void)processor_.editLayerIfRevision(static_cast<std::size_t>(index),revision_,edit);
    refresh();
}
void SourcePanel::editKey(){
    const int root=root_.getSelectedId()-1,scale=scale_.getSelectedId()-1;
    if(root>=0&&root<12&&scale>=0&&scale<5)
        (void)processor_.editTonality({static_cast<source::Scale>(scale),static_cast<std::uint8_t>(root)});
    refresh();
}
void SourcePanel::refresh(){
    const auto snapshot=processor_.versionedProgram();
    // Keep the old token while an in-flight drag crosses a restore. All
    // further callbacks from that drag remain rejected until mouse-up.
    if(dragging_ && revision_!=snapshot.revision)return;
    revision_=snapshot.revision;
    const auto& layer=snapshot.program.layers[static_cast<std::size_t>(std::clamp(layer_.getSelectedId()-1,0,3))];
    root_.setSelectedId(snapshot.program.tonality.root+1,juce::dontSendNotification);
    scale_.setSelectedId(static_cast<int>(snapshot.program.tonality.scale)+1,juce::dontSendNotification);
    root_.setEnabled(snapshot.program.tonality.scale!=source::Scale::chromatic);
    constexpr const char* characterNames[]{"Noise brightness","Tonal blend","Partial brightness","Sub level","Doppler depth","Sweep range","Resonance","Upper modes","Detune","Vowel spacing","Slice edge","Grain density"};
    const auto familyIndex=static_cast<unsigned>(layer.family);
    const auto* name=characterNames[familyIndex];
    characterLabel_.setText(name,juce::dontSendNotification);character_.setTitle(name);
    if(!character_.isMouseButtonDown())character_.setValue(layer.character*100,juce::dontSendNotification);
    for(std::size_t i=0;i<familyControls_.size();++i){
        familyControlLabels_[i].setText(familyControlNames[familyIndex][i],juce::dontSendNotification);
        familyControls_[i].setTitle(familyControlNames[familyIndex][i]);
        const auto value=i==0?layer.familyControls.primary:i==1?layer.familyControls.motion:layer.familyControls.density;
        if(!familyControls_[i].isMouseButtonDown())familyControls_[i].setValue(value*100,juce::dontSendNotification);
    }
    if(!transpose_.isMouseButtonDown())transpose_.setValue(layer.transpose,juce::dontSendNotification);
    const bool glitch=layer.family==NoiseFamily::glitch;
    const bool granular=layer.family==NoiseFamily::granular;
    // A restore/family change can hide the focused control. Keep focus in
    // Source, but do not take focus from the host or another editor panel.
    if(isShowing()&&getPeer()!=nullptr&&hasKeyboardFocus(true)){
        const auto focused=[](const juce::Component& c){return c.hasKeyboardFocus(true);};
        const bool hidesSlice=!glitch&&(focused(sliceGrid_)||focused(sliceRead_)||focused(sliceReset_)||
            std::any_of(sliceGate_.begin(),sliceGate_.end(),focused));
        const bool hidesGrain=(!granular&&(focused(grainLength_)||focused(scatter_)||focused(reverse_)))||
            ((!granular||layer.grains.position<0)&&(focused(grainPosition_)||focused(grainTravel_)));
        if(hidesSlice||hidesGrain)layer_.grabKeyboardFocus();
    }
    for(auto* c:std::array<juce::Component*,3>{&sliceGrid_,&sliceRead_,&sliceReset_})c->setVisible(glitch);
    sliceGrid_.setSelectedId(static_cast<int>(layer.glitch.grid)+1,juce::dontSendNotification);
    sliceRead_.setSelectedId(static_cast<int>(layer.glitch.read)+1,juce::dontSendNotification);
    for(unsigned i=0;i<sliceGate_.size();++i){sliceGate_[i].setVisible(glitch);sliceGate_[i].setToggleState(layer.glitch.gate(i),juce::dontSendNotification);}
    for(auto* c:std::array<juce::Component*,5>{&grainLength_,&grainPosition_,&grainTravel_,&scatter_,&reverse_})c->setVisible(granular);
    scatter_.setToggleState(layer.grains.position<0,juce::dontSendNotification);reverse_.setToggleState(layer.grains.reverse,juce::dontSendNotification);
    grainPosition_.setEnabled(layer.grains.position>=0);grainTravel_.setEnabled(layer.grains.position>=0);
    if(!grainTravel_.isMouseButtonDown())grainTravel_.setValue(layer.grains.travel*100,juce::dontSendNotification);
    if(!grainPosition_.isMouseButtonDown())grainPosition_.setValue(std::max(0.f,layer.grains.position)*100,juce::dontSendNotification);
    if(!grainLength_.isMouseButtonDown())grainLength_.setValue(layer.grains.milliseconds,juce::dontSendNotification);
    for(std::size_t i=0;i<filters_.size();++i){
        const auto& value=layer.filters[i];auto& f=filters_[i];
        f.type.setSelectedId(static_cast<int>(value.type)+1,juce::dontSendNotification);
        f.slope.setSelectedId(value.poles==2?1:2,juce::dontSendNotification);
        if(!f.hz.isMouseButtonDown())f.hz.setValue(value.hz,juce::dontSendNotification);
        if(!f.q.isMouseButtonDown())f.q.setValue(value.q,juce::dontSendNotification);
        const bool on=value.type!=source::FilterType::off;f.hz.setEnabled(on);f.q.setEnabled(on);f.slope.setEnabled(on);
    }
}
void SourcePanel::paint(juce::Graphics& g){
    namespace c=vox::ui::tokens::colour;
    g.fillAll(c::panelRaised);g.setColour(c::border);g.drawRect(getLocalBounds());
    g.setColour(c::text);g.setFont(vox::ui::typography::controlLabel());
    auto a=getLocalBounds().reduced(14);
    g.drawText(u8("SOURCE / KEY    •    Applies to new events"),a.removeFromTop(21),juce::Justification::centredLeft);
    a.removeFromTop(72);
    g.drawText("FILTER     TYPE                     SLOPE                   CUTOFF                                           RESONANCE",a.removeFromTop(20),juce::Justification::centredLeft);
    g.drawText("A",a.removeFromTop(31),juce::Justification::centredLeft);
    g.drawText("B",a.removeFromTop(31),juce::Justification::centredLeft);
    const auto labels=a.removeFromTop(22);
    const auto label=[&](const juce::String& text,const juce::Component& child){
        g.drawText(text,labels.withX(child.getX()).withWidth(getWidth()-child.getX()),juce::Justification::centredLeft);
    };
    if(sliceGrid_.isVisible()){
        label("Slice grid",sliceGrid_);label("Read",sliceRead_);label(u8("Repeat gates 1–4"),sliceGate_[0]);
    }else if(grainLength_.isVisible()){
        label("Grain length",grainLength_);label("Start",grainPosition_);label("Travel",grainTravel_);
    }
}
void SourcePanel::resized(){
    auto a=getLocalBounds().reduced(14);a.removeFromTop(21);
    auto first=a.removeFromTop(32);
    layer_.setBounds(first.removeFromLeft(142).reduced(2));
    // root_ lives in the PERFORM Program-Generator selects row (reference
    // PITCH KEY); the source overlay must not re-position it.
    if(root_.getParentComponent()==this)root_.setBounds(first.removeFromLeft(78).reduced(2));else first.removeFromLeft(78);
    scale_.setBounds(first.removeFromLeft(145).reduced(2));
    auto voice=a.removeFromTop(36);
    transpose_.setBounds(voice.removeFromLeft(248).reduced(2));
    characterLabel_.setBounds(voice.removeFromLeft(108).reduced(2));character_.setBounds(voice.reduced(2));
    auto family=a.removeFromTop(38);
    for(std::size_t i=0;i<familyControls_.size();++i){
        auto cell=family.removeFromLeft(family.getWidth()/(static_cast<int>(familyControls_.size()-i)));
        familyControlLabels_[i].setBounds(cell.removeFromTop(16).reduced(2,0));
        familyControls_[i].setBounds(cell.reduced(2,0));
    }
    a.removeFromTop(8);
    for(auto& f:filters_){auto row=a.removeFromTop(31);row.removeFromLeft(28);
        f.type.setBounds(row.removeFromLeft(118).reduced(2));f.slope.setBounds(row.removeFromLeft(110).reduced(2));
        f.hz.setBounds(row.removeFromLeft(row.getWidth()/2).reduced(2));f.q.setBounds(row.reduced(2));}
    a.removeFromTop(20);auto grains=a.removeFromTop(32);
    grainLength_.setBounds(grains.removeFromLeft(148).reduced(2));grainPosition_.setBounds(grains.removeFromLeft(148).reduced(2));
    grainTravel_.setBounds(grains.removeFromLeft(148).reduced(2));
    scatter_.setBounds(grains.removeFromLeft(90));reverse_.setBounds(grains.removeFromLeft(90));
    auto slices=grainLength_.getBounds().getUnion(reverse_.getBounds());
    sliceGrid_.setBounds(slices.removeFromLeft(98).reduced(2,0));sliceRead_.setBounds(slices.removeFromLeft(126).reduced(2,0));
    for(auto& gate:sliceGate_)gate.setBounds(slices.removeFromLeft(55).reduced(2,0));
    sliceReset_.setBounds(slices.removeFromLeft(110).reduced(1,0));
    close_.setBounds(a.removeFromBottom(28).removeFromRight(92));
}
namespace {
constexpr const char* browserRoles[]{"Impact","Riser","Fall","Sweep","Zap","Rhythm","Atmosphere","Stinger","Untagged"};
constexpr const char* browserModes[]{"One Shot","Gate","Continuous","Loop","Sequence","Transition"};
juce::String lowerQuery(const portable::Tags& tags,PlayMode mode){
    return (juce::String(tags.name+" "+tags.style+" "+tags.mood+" "+tags.use)+" "+browserRoles[tags.role]+" "+browserModes[static_cast<unsigned>(mode)]).toLowerCase();
}
juce::String presetKey(source::Tonality key,bool pitched){
    if(!pitched)return "Noise only";
    if(key.scale==source::Scale::chromatic)return "Chromatic pitch";
    constexpr const char* roots[]{"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    return juce::String(roots[key.root])+" "+content::scaleName(key.scale);
}
}
PresetBrowser::PresetBrowser(FxProcessor& processor,juce::File root):processor_(processor),root_(std::move(root)){
    title_.setText(u8("PRESETS — REVIEW CATALOG / USER LIBRARY"),juce::dontSendNotification);
    title_.setFont(vox::ui::typography::controlLabel());
    search_.setTitle("Search name, role, style, mood, use case or mode");search_.setTextToShowWhenEmpty(u8("Search words…"),juce::Colours::grey);
    scope_.addItem("All",1);scope_.addItem("Review only",2);scope_.addItem("User only",3);scope_.setSelectedId(1,juce::dontSendNotification);
    role_.addItem("All roles",1);editRole_.setTitle("User preset role");
    for(int i=0;i<9;++i){role_.addItem(browserRoles[i],i+2);editRole_.addItem(browserRoles[i],i+1);}
    mode_.addItem("All modes",1);for(int i=0;i<6;++i)mode_.addItem(browserModes[i],i+2);
    role_.setSelectedId(1,juce::dontSendNotification);mode_.setSelectedId(1,juce::dontSendNotification);
    const auto field=[](juce::TextEditor& e,const char* hint){e.setTitle(hint);e.setTextToShowWhenEmpty(hint,juce::Colours::grey);e.setInputRestrictions(128);};
    field(name_,"User preset name");field(style_,"Style / genre tags");field(mood_,"Mood tags");field(use_,"Use-case tags");
    search_.onTextChange=[this]{filter();};scope_.onChange=[this]{filter();};role_.onChange=[this]{filter();};mode_.onChange=[this]{filter();};onlyFavorites_.onClick=[this]{filter();};
    constexpr const char* facets[]{"All styles","All moods","All uses"};
    std::array facetsControls{&styleFacet_,&moodFacet_,&useFacet_};
    for(std::size_t i=0;i<facetsControls.size();++i){facetsControls[i]->setTitle(facets[i]);facetsControls[i]->onChange=[this]{filter();};}
    const auto fillFacet=[this](juce::ComboBox& c,std::initializer_list<juce::String> choices){
        int id=1;for(const auto& label:choices)c.addItem(label,id++);c.setSelectedId(1,juce::dontSendNotification);c.onChange=[this]{filter();};};
    fillFacet(durationFacet_,{"All durations",u8("Short ≤1 s"),"Long >1 s","Bars / Sequence","Host duration"});
    fillFacet(tonalFacet_,{"All tonality","Pitch-responsive","Noise only"});
    durationFacet_.setTitle("Filter by duration");tonalFacet_.setTitle("Filter by pitch classification");
    rows_.setRowHeight(28);rows_.setMultipleSelectionEnabled(false);
    detail_.setMinimumHorizontalScale(1);detail_.setJustificationType(juce::Justification::topLeft);
    load_.setTooltip("Load structure; host automation stays unchanged");
    play_.setTooltip("Render up to 12 seconds, then temporarily replace this plug-in's real-time output. Stop before recording. Review uses authored audition settings; user previews use current host bases.");
    save_.setTooltip("Save the current structure as a new file in the user library; no existing file is overwritten");
    tags_.setTooltip("Update only metadata/favorite of the selected user preset");
    load_.onClick=[this]{load();};play_.onClick=[this]{runPreview();};stop_.onClick=[this]{stopPreview();report("Preview stopped");};
    save_.onClick=[this]{save(false);};tags_.onClick=[this]{save(true);};remove_.onClick=[this]{erase();};
    openFile_.onClick=[this]{stopPreview();if(looseFile)looseFile(false);};saveFile_.onClick=[this]{if(looseFile)looseFile(true);};
    close_.onClick=[this]{stopPreview();setVisible(false);if(onClosed)onClosed();};
    const std::array<juce::Component*,28> controls{&rows_,&search_,&name_,&style_,&mood_,&use_,&scope_,&role_,&mode_,&editRole_,&onlyFavorites_,&favorite_,&title_,&detail_,&status_,&load_,&play_,&stop_,&save_,&tags_,&remove_,&openFile_,&saveFile_,
        &styleFacet_,&moodFacet_,&useFacet_,&durationFacet_,&tonalFacet_};
    for(auto* c:controls)addAndMakeVisible(*c);addAndMakeVisible(close_);
}
PresetBrowser::~PresetBrowser(){stopTimer();stopPreview();}
void PresetBrowser::open(){scan();setVisible(true);toFront(true);}
void PresetBrowser::setEmbedded(bool embedded){close_.setVisible(!embedded);}
int PresetBrowser::getNumRows(){return static_cast<int>(visible_.size());}
const PresetBrowser::Row* PresetBrowser::selected() const noexcept{
    const auto index=rows_.getSelectedRow();return index>=0&&static_cast<std::size_t>(index)<visible_.size()?&all_[visible_[static_cast<std::size_t>(index)]]:nullptr;
}
void PresetBrowser::report(juce::String text){status_.setText(std::move(text),juce::dontSendNotification);}
void PresetBrowser::scan(){
    if(indexJob_)return;
    visible_.clear();all_.clear();
    for(std::size_t i=0;i<std::size(content::reviewEntries);++i){const auto& e=content::reviewEntries[i];
        const auto authored=content::makePreview(e,48000);
        const auto bars=e.mode==PlayMode::sequence?authored.pattern.bars:e.bars;
        const auto seconds=bars?0.f:e.seconds;
        const auto key=presetKey(authored.program.tonality,e.pitchedSource);
        portable::Tags tags{std::string(e.name),std::string(e.style),std::string(e.mood),std::string(content::useCase(e)),static_cast<unsigned>(e.role),false};
        auto notes=juce::String(browserModes[static_cast<unsigned>(e.mode)])+u8("  •  ")+juce::String(e.bpm,0)+u8(" BPM  •  ")+
            (bars?juce::String(bars)+" bars":juce::String(seconds,2)+" s")+"\n"+
            key+u8("  •  Built-in assets  •  Review only\n")+juce::String(tags.use);
        auto searchable=lowerQuery(tags,e.mode)+" "+key.toLowerCase();
        all_.push_back({std::move(tags),e.mode,static_cast<int>(i),{},std::move(notes),bars,seconds,e.pitchedSource,key,std::move(searchable)});
    }
    indexStatus_=u8("Scanning user presets…");
    try{indexJob_=std::make_unique<library::IndexJob>(root_);startTimerHz(20);}
    catch(...){indexStatus_="Could not start user scan";}
    rebuild();
}
void PresetBrowser::upsert(const library::IndexEntry& entry){
    const auto key=presetKey(entry.key,entry.pitched);
    const auto length=entry.bars?juce::String(entry.bars)+" bars":juce::String("Host duration");
    const auto details=juce::String(browserModes[static_cast<unsigned>(entry.mode)])+u8("  •  ")+length+u8("  •  ")+key+"\n"+
        (entry.asset==sample::Reference::Kind::builtin?"Built-in source":entry.asset==sample::Reference::Kind::pending?u8("Unverified sample — reimport WAV"):u8("External WAV · SHA verified on Preview/Load"));
    auto searchable=lowerQuery(entry.tags,entry.mode)+" "+key.toLowerCase();
    Row row{entry.tags,entry.mode,-1,entry.file,details,entry.bars,0,entry.pitched,key,std::move(searchable)};
    const auto at=std::find_if(all_.begin(),all_.end(),[&](const Row& r){return r.factory<0&&r.file==entry.file;});
    if(at==all_.end())all_.push_back(std::move(row));else *at=std::move(row);
}
void PresetBrowser::rebuild(const juce::File& select){
    visible_.clear();
    std::sort(all_.begin(),all_.end(),[](const Row& a,const Row& b){
        if((a.factory>=0)!=(b.factory>=0))return a.factory>=0;
        const int name=juce::String(a.tags.name).compareIgnoreCase(juce::String(b.tags.name));
        return name!=0?name<0:a.factory>=0?a.factory<b.factory:a.file.getFullPathName()<b.file.getFullPathName();
    });
    const auto fill=[this](juce::ComboBox& c,const char* title,auto member){
        const auto previous=c.getSelectedId()>1?c.getText():juce::String{};
        juce::StringArray choices;
        for(const auto& r:all_)if(!(r.tags.*member).empty())choices.addIfNotAlreadyThere(juce::String(r.tags.*member),true);
        choices.sort(true);c.clear(juce::dontSendNotification);c.addItem(title,1);
        int id=2,selected=1;for(const auto& label:choices){c.addItem(label,id);if(label.equalsIgnoreCase(previous))selected=id;++id;}
        c.setSelectedId(selected,juce::dontSendNotification);
    };
    fill(styleFacet_,"All styles",&portable::Tags::style);fill(moodFacet_,"All moods",&portable::Tags::mood);fill(useFacet_,"All uses",&portable::Tags::use);
    filter();
    if(select!=juce::File{})for(std::size_t i=0;i<visible_.size();++i)if(all_[visible_[i]].file==select){rows_.selectRow(static_cast<int>(i));break;}
}
void PresetBrowser::filter(){
    const auto* old=selected();
    const auto oldFile=old?old->file:juce::File{};const int oldFactory=old?old->factory:-1;
    visible_.clear();
    const auto sameTag=[](const juce::ComboBox& c,const std::string& value){return c.getSelectedId()<=1||c.getText().equalsIgnoreCase(juce::String(value));};
    const auto terms=juce::StringArray::fromTokens(search_.getText().toLowerCase(),false);
    for(std::size_t i=0;i<all_.size();++i){const auto& row=all_[i];
        if((scope_.getSelectedId()==2&&row.factory<0)||(scope_.getSelectedId()==3&&row.factory>=0)||
           (role_.getSelectedId()>1&&static_cast<unsigned>(role_.getSelectedId()-2)!=row.tags.role)||
           (mode_.getSelectedId()>1&&mode_.getSelectedId()-2!=static_cast<int>(row.mode))||
           (onlyFavorites_.getToggleState()&&!row.tags.favorite)||
           !sameTag(styleFacet_,row.tags.style)||!sameTag(moodFacet_,row.tags.mood)||!sameTag(useFacet_,row.tags.use))continue;
        const int durationClass=row.bars?4:row.seconds==0?5:row.seconds<=1?2:3;
        const int tonalClass=row.pitched?2:3;
        if((durationFacet_.getSelectedId()>1&&durationFacet_.getSelectedId()!=durationClass)||
           (tonalFacet_.getSelectedId()>1&&tonalFacet_.getSelectedId()!=tonalClass))continue;
        const auto& searchable=row.searchable;
        if(std::any_of(terms.begin(),terms.end(),[&](const juce::String& t){return !searchable.contains(t);}))continue;
        visible_.push_back(i);
    }
    rows_.updateContent();
    int select=visible_.empty()?-1:0;
    for(std::size_t i=0;i<visible_.size();++i){const auto& row=all_[visible_[i]];
        if((oldFactory>=0&&row.factory==oldFactory)||(oldFile!=juce::File{}&&row.file==oldFile)){select=static_cast<int>(i);break;}}
    const auto previous=rows_.getSelectedRow();
    if(select<0)rows_.deselectAllRows();else rows_.selectRow(select);
    if(previous==select)selectedRowsChanged(select);
    report(juce::String(static_cast<int>(visible_.size()))+" shown"+(indexStatus_.isEmpty()?"":u8(" · ")+indexStatus_));
}
void PresetBrowser::paintListBoxItem(int row,juce::Graphics& g,int w,int h,bool selectedRow){
    if(row<0||static_cast<std::size_t>(row)>=visible_.size())return;
    const auto& item=all_[visible_[static_cast<std::size_t>(row)]];
    namespace c=vox::ui::tokens::colour;
    if(selectedRow)g.fillAll(c::border);g.setColour(c::text);g.setFont(juce::FontOptions(13.f));
    const auto line=juce::String(item.factory>=0?"REVIEW  ":"USER  ")+juce::String(item.tags.name)+u8("  ·  ")+browserRoles[item.tags.role]+
        (item.tags.style.empty()?"":u8("  ·  ")+juce::String(item.tags.style))+(item.tags.favorite?u8("  ★"):"");
    g.drawFittedText(line,8,0,w-16,h,juce::Justification::centredLeft,1);
}
void PresetBrowser::selectedRowsChanged(int){
    if(armed_||(!audition_&&processor_.previewBus().playing()))stopPreview();
    const auto* row=selected();const bool user=row&&row->factory<0;
    load_.setEnabled(row!=nullptr);play_.setEnabled(row!=nullptr);save_.setEnabled(!indexJob_);
    tags_.setEnabled(user&&!indexJob_);remove_.setEnabled(user&&!indexJob_);
    detail_.setText(row?row->details:juce::String("No matching presets"),juce::dontSendNotification);
    name_.setText(user?juce::String(row->tags.name):juce::String{},false);
    style_.setText(user?juce::String(row->tags.style):juce::String{},false);mood_.setText(user?juce::String(row->tags.mood):juce::String{},false);
    use_.setText(user?juce::String(row->tags.use):juce::String{},false);
    editRole_.setSelectedId(user?static_cast<int>(row->tags.role)+1:9,juce::dontSendNotification);
    favorite_.setToggleState(user&&row->tags.favorite,juce::dontSendNotification);
}
portable::Tags PresetBrowser::editorTags() const {
    return {name_.getText().trim().toStdString(),style_.getText().trim().toStdString(),mood_.getText().trim().toStdString(),
        use_.getText().trim().toStdString(),static_cast<unsigned>(editRole_.getSelectedId()-1),favorite_.getToggleState()};
}
void PresetBrowser::load(){
    const auto* row=selected();if(!row)return;
    stopPreview();portable::Sound sound;
    const bool ok=row->factory>=0?processor_.loadReviewCandidate(static_cast<std::size_t>(row->factory)):
        library::read(row->file,sound)&&processor_.applyPortableSound(sound);
    report(ok?u8("Loaded structure · host values unchanged"):"Preset is unavailable or invalid");
}
void PresetBrowser::save(bool tagsOnly){
    if(indexJob_)return;
    const auto metadata=editorTags();if(!metadata.valid()){report("Enter a name and tags of at most 128 UTF-8 bytes each");return;}
    const auto& root=root_;
    if(root.isSymbolicLink()||root.createDirectory().failed()){report("User library folder unavailable");return;}
    portable::Sound sound;
    auto file=root.getChildFile(juce::Uuid().toString()+".voxfx");
    if(tagsOnly){const auto* row=selected();if(!row||row->factory>=0)return;file=row->file;
        if(!library::managedFile(root,file)||!library::read(file,sound)){report("User preset has changed or is missing");return;}}
    else {if(file.exists()){report("User preset ID collision; try again");return;}sound=processor_.portableSnapshot();}
    if(!library::write(file,sound,&metadata)){report("Could not save user preset");return;}
    upsert(library::describe(file,metadata,sound));rebuild(file);report(tagsOnly?"User tags saved":"Saved current structure in User Presets");
}
void PresetBrowser::erase(){
    const auto* row=selected();if(!row||row->factory>=0)return;
    const auto file=row->file;
    juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::WarningIcon,"Delete user preset",
        "Permanently delete the selected user preset file? Projects and immutable review entries will not change.","Delete","Cancel",this,
        juce::ModalCallbackFunction::create([safe=juce::Component::SafePointer<PresetBrowser>(this),file](int result){
            if(!safe||result==0)return;
            if(!library::managedFile(safe->root_,file)||!file.existsAsFile()||!file.deleteFile()){safe->report("Could not delete user preset");return;}
            std::erase_if(safe->all_,[&](const Row& row){return row.factory<0&&row.file==file;});
            safe->visible_.clear();safe->rebuild();safe->report("User preset deleted");}));
}
void PresetBrowser::runPreview(){
    const auto* row=selected();if(!row)return;
    if(audition_){report("Previous preview is still finishing; stop or wait");return;}
    processor_.previewBus().stop();
    previewTicket_=processor_.previewBus().ticket();
    offline::Capture capture;sample::Reference reference;
    if(row->factory>=0)capture=content::makePreview(content::reviewEntries[static_cast<std::size_t>(row->factory)],preview::Job::rate);
    else {portable::Sound sound;if(!library::read(row->file,sound)){report("User preset unavailable");return;}
        capture=processor_.capturePresetPreview(sound);reference=std::move(sound.sample);}
    try{audition_=std::make_unique<preview::Job>(std::move(capture),std::move(reference));armed_=true;startTimerHz(20);report(u8("Rendering preview…"));}
    catch(...){report("Could not start preview worker");}
}
void PresetBrowser::stopPreview(){armed_=false;if(audition_)audition_->cancel();processor_.previewBus().stop();}
void PresetBrowser::timerCallback(){pollBackground();}
void PresetBrowser::pollBackground(){
    if(indexJob_&&indexJob_->ready()){
        auto result=indexJob_->take();indexJob_.reset();
        for(const auto& item:result.entries)upsert(item);
        indexStatus_=result.unavailable?"User library unavailable":juce::String{};
        if(result.invalid)indexStatus_+=(indexStatus_.isEmpty()?"":u8(" · "))+juce::String(result.invalid)+" invalid user presets skipped";
        if(result.limited)indexStatus_+=(indexStatus_.isEmpty()?"":u8(" · "))+juce::String("User scan limited to 512 files");
        visible_.clear();rebuild();
    }
    if(audition_){const auto result=audition_->result();if(result==preview::Result::loading)return;
        const bool started=armed_&&result==preview::Result::ready&&audition_->publish(processor_.previewBus(),previewTicket_);
        if(armed_&&previewTicket_!=processor_.previewBus().ticket())report("Preview cancelled by restore, panic or offline processing");
        else if(armed_)report(started?u8("Preview ready · host audio must be running"):"Preview unavailable (asset, empty schedule or audio bounds)");
        audition_.reset();armed_=false;
        if(!started){if(!indexJob_)stopTimer();return;}
    }
    if(!indexJob_&&!processor_.previewBus().playing())stopTimer();
}
void PresetBrowser::paint(juce::Graphics& g){namespace c=vox::ui::tokens::colour;g.fillAll(c::panelRaised);g.setColour(c::border);g.drawRect(getLocalBounds());}
void PresetBrowser::resized(){
    auto a=getLocalBounds().reduced(12);title_.setBounds(a.removeFromTop(23));
    auto filters=a.removeFromTop(34);search_.setBounds(filters.removeFromLeft(261).reduced(2));scope_.setBounds(filters.removeFromLeft(117).reduced(2));
    role_.setBounds(filters.removeFromLeft(121).reduced(2));mode_.setBounds(filters.removeFromLeft(129).reduced(2));onlyFavorites_.setBounds(filters.reduced(2));
    auto facets=a.removeFromTop(32);
    for(auto* c:{&styleFacet_,&moodFacet_,&useFacet_,&durationFacet_,&tonalFacet_})c->setBounds(facets.removeFromLeft(177).reduced(2));
    auto footer=a.removeFromBottom(32);close_.setBounds(footer.removeFromRight(71).reduced(2));saveFile_.setBounds(footer.removeFromRight(103).reduced(2));
    openFile_.setBounds(footer.removeFromRight(103).reduced(2));status_.setBounds(footer.reduced(2));
    rows_.setBounds(a.removeFromLeft(a.getWidth()*55/100).reduced(2));auto form=a.reduced(8,2);
    detail_.setBounds(form.removeFromTop(69));auto audition=form.removeFromTop(31);
    play_.setBounds(audition.removeFromLeft(102).reduced(2));stop_.setBounds(audition.removeFromLeft(73).reduced(2));load_.setBounds(audition.removeFromLeft(82).reduced(2));
    form.removeFromTop(10);name_.setBounds(form.removeFromTop(29).reduced(2));style_.setBounds(form.removeFromTop(29).reduced(2));
    mood_.setBounds(form.removeFromTop(29).reduced(2));use_.setBounds(form.removeFromTop(29).reduced(2));
    auto tagsRow=form.removeFromTop(29);editRole_.setBounds(tagsRow.removeFromLeft(147).reduced(2));favorite_.setBounds(tagsRow.reduced(2));
    auto files=form.removeFromTop(32);save_.setBounds(files.removeFromLeft(102).reduced(2));tags_.setBounds(files.removeFromLeft(98).reduced(2));remove_.setBounds(files.removeFromLeft(84).reduced(2));
}
template <typename Edit> void FxPanel::editPattern(Edit edit){
    auto next=displayedPattern_;edit(next);
    if(next==displayedPattern_)return;
    if(processor_.replacePatternIfCurrent({displayedPattern_,patternRevision_},next)){
        if(!patternDragging_)notePatternUndo(displayedPattern_);
        displayedPattern_=next;
    }
    if(!patternDragging_)refreshPattern();
}
void FxPanel::notePatternUndo(const pattern::Pattern& previous){
    if(patternUndo_.size()==32)patternUndo_.pop_front();
    patternUndo_.push_back(previous);patternRedo_.clear();
}
void FxPanel::finishPatternDrag(){
    patternDragging_=false;
    const auto state=processor_.versionedPattern();
    if(state.revision==patternRevision_&&state.pattern==displayedPattern_&&displayedPattern_!=patternDragBefore_)
        notePatternUndo(patternDragBefore_);
    refreshPattern();
}
void FxPanel::patternCommand(int id){
    if(patternDragging_)return;
    const auto previous=displayedPattern_;const auto revision=patternRevision_;
    refreshPattern();
    if((id==2||id==6||(id>=7&&id<=9))&&(previous!=displayedPattern_||revision!=patternRevision_))return;
    const juce::String prefix="VOXFX-PATTERN/1:";
    if(id==1){
        std::vector<std::byte> bytes;
        if(pattern::codec::encode(displayedPattern_,bytes))
            juce::SystemClipboard::copyTextToClipboard(prefix+juce::Base64::toBase64(bytes.data(),bytes.size()));
    }else if(id==2){
        const auto text=juce::SystemClipboard::getTextFromClipboard();
        juce::MemoryOutputStream bytes;pattern::Pattern value;
        if(text.length()>8192||!text.startsWith(prefix)||!juce::Base64::convertFromBase64(bytes,text.substring(prefix.length()))||
           !pattern::codec::decode({static_cast<const std::byte*>(bytes.getData()),bytes.getDataSize()},value)){
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,"Pattern paste rejected","Copy a VOX FX Pattern first.");
            return;
        }
        editPattern([&](auto& p){p=value;});
    }else if(id==5||id==6){
        const auto bar=static_cast<unsigned>(patternBar_.getSelectedId()-1);
        if(id==5){
            (void)pattern::edit::copyBar(displayedPattern_,bar,copiedPatternBar_);
            refreshPattern();
        }else editPattern([&](auto& p){(void)pattern::edit::pasteBar(p,bar,copiedPatternBar_);});
    }else if(id>=7&&id<=9){
        const auto bar=static_cast<unsigned>(patternBar_.getSelectedId()-1);
        editPattern([&](auto& p){(void)pattern::edit::varyBar(p,bar,static_cast<pattern::edit::Variation>(id-7));});
    }else if(id==3||id==4){
        auto& from=id==3?patternUndo_:patternRedo_;
        auto& to=id==3?patternRedo_:patternUndo_;
        if(!from.empty()&&processor_.replacePatternIfCurrent({displayedPattern_,patternRevision_},from.back())){
            to.push_back(displayedPattern_);displayedPattern_=from.back();from.pop_back();
        }
        refreshPattern();
    }
}
FxPanel::FxPanel (FxProcessor& processor,SavePicker picker,ExternalDrag drag)
    : processor_ (processor),savePicker_(std::move(picker)),externalDrag_(std::move(drag)), attachment_ (processor.outputParameter(), output_.getSlider()),
      durationAttachment_ (processor.durationParameter(), duration_.getSlider()), programCurves_(processor), sourcePanel_(processor), motionPanel_(processor), browser_(processor),
      eventChart_ (processor)
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
    clockStatus_.setTitle("Host bar clock");clockStatus_.setFont(juce::FontOptions(10.0f));addAndMakeVisible(clockStatus_);
    fxTailStatus_.setTitle("FX tail activity");fxTailStatus_.setFont(juce::FontOptions(10.0f));
    fxTailStatus_.setJustificationType(juce::Justification::centredRight);
    fxTailStatus_.setTooltip("Measured FX-only output above -90 dBFS, after voices stop and before preset preview. Not an estimate of remaining tail time.");
    fxTailStatus_.setDescription(fxTailStatus_.getTooltip());addAndMakeVisible(fxTailStatus_);
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
    addAndMakeVisible (audition_);addAndMakeVisible(stopAudition_);
    addAndMakeVisible (panic_);
    const char* familyNames[]{"Noise Riser","Downlifter","Shepard","Impact","Whoosh","Laser","Squelch","Metallic","Texture","Formant","Glitch","Granular"};
    factory_.setTextWhenNothingSelected("Load seed program...");factory_.setTitle("Procedural seed program");
    factory_.setTooltip("Load an uncurated procedural layer seed; duration, effects, macros and pattern remain unchanged");
    for(int i=0;i<48;++i)factory_.addItem(juce::String(familyNames[i/4])+" "+juce::String(1+i%4),i+1);
    factory_.onChange=[this]{const int id=factory_.getSelectedId();generator::Result result;
        if(id>=1&&id<=48&&generator::factory(static_cast<std::size_t>(id-1),result)&&processor_.setProgram(result.program))
            family_.setSelectedId(processor_.family()+1,juce::dontSendNotification);
        factory_.setSelectedId(0,juce::dontSendNotification);};
    addAndMakeVisible(factory_);
    comparison_.setTextWhenNothingSelected("Compare A/B");comparison_.setTitle("Project A/B comparisons");
    comparison_.setTooltip("Store or recall project snapshots; recall explicitly changes host parameter values. Both slots are saved in the project.");
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
    midiExport_.onClick=[this]{if(processor_.fileExportStatus())processor_.cancelFileExport(fileexport::Kind::midi);else exportPattern();};addAndMakeVisible(midiExport_);
    wavExport_.setTitle("Export WAV");
    wavExport_.setTooltip("Choose stereo WAV options; click during rendering to cancel");
    wavOptions_.onSave=[this](offline::Settings settings){exportWav(settings);};
    wavOptions_.onPrepareDrag=[this](offline::Settings settings){exportWav(settings,true);};
    prepareMidiDrag_.setTooltip("Prepare a fixed-tempo MIDI snapshot, then drag it from the adjacent handle; Save remains separate.");
    prepareMidiDrag_.onClick=[this]{exportPattern(true);};
    addAndMakeVisible(prepareMidiDrag_);
    dragGrip_.setTitle("Prepared file drag handle");
    dragGrip_.setWantsKeyboardFocus(false);
    dragGrip_.setTooltip("Drag a prepared WAV or MIDI snapshot. Copy imports into the DAW project; use Save for a lasting file path.");
    dragGrip_.setDescription(dragGrip_.getTooltip());
    dragGrip_.begin=[this]{beginFileDrag();};
    addAndMakeVisible(dragGrip_);
    dragWarning_.setText(u8("Copy into project · Save for a lasting file"),juce::dontSendNotification);
    dragWarning_.setJustificationType(juce::Justification::centredRight);
    dragWarning_.setFont(juce::FontOptions(12.0f));
    dragWarning_.setTooltip("Temporary files may expire. Availability is checked when dragging; the OS may still read later after that check. Copy imports into the project; handed-off files are retained even after canceled drops.");
    addAndMakeVisible(dragWarning_);
    refreshExportControls();
    addChildComponent(wavOptions_);
    wavExport_.onClick=[this]{if(processor_.fileExportStatus())processor_.cancelFileExport(fileexport::Kind::wav);else if(!exportChooserOpen_&&!processor_.dragInProgress()){wavOptions_.setVisible(true);wavOptions_.toFront(true);}};
    addAndMakeVisible(wavExport_);
    audition_.setTooltip ("Trigger a finite audition of the current voice. Bar programs require the host bar clock.");
    stopAudition_.setTitle("Stop audition");stopAudition_.setTooltip("Fade audition voices and stop preset preview without stopping MIDI or Pattern voices");
    stopAudition_.onClick=[this]{processor_.stopAudition();};
    panic_.setTooltip ("Fade all active events");
    audition_.onClick = [this] { processor_.audition(); };
    panic_.onClick = [this] { keyboard_.releaseAllNotes(); processor_.panic(); };
    seed_.setRange (1, 2147483647, 1);
    seed_.setValue (processor_.generatorState().seed, juce::dontSendNotification);
    seed_.setSliderStyle (juce::Slider::LinearBar);
    seed_.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 40, 24);
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
        const auto layerIndex=static_cast<std::size_t>(i);
        layers_[layerIndex]->onEditSource=[this,layerIndex]{
            sourcePanel_.layerSelector().setSelectedId(static_cast<int>(layerIndex)+1,juce::sendNotificationSync);
            sourcePanel_.open(*this);
        };
    }
    addChildComponent(programCurves_);
    programCurvesButton_.setClickingTogglesState(true);
    programCurvesButton_.setTooltip("Switch between layer cards and Program amplitude, pitch, cutoff and panorama");
    programCurvesButton_.onClick = [this] {
        // Toggle contract (tests + users): on = Program curves workspace,
        // off = layer cards. Visibility is owned by the workspace shell.
        setWorkspace(programCurvesButton_.getToggleState() ? Workspace::timeline : Workspace::design);
    };
    addAndMakeVisible(programCurvesButton_);
    addChildComponent(sourcePanel_);
    sourceButton_.setTooltip("Edit Program key, per-layer pitch and filters, and Glitch or Granular source controls");
    sourceButton_.onClick=[this]{sourcePanel_.open(sourceButton_);};
    addChildComponent(motionPanel_);
    motionPanel_.onClosed=[this]{ setWorkspace(Workspace::perform); };
    browser_.onClosed=[this]{ setWorkspace(Workspace::perform); };
    browser_.setEmbedded(true);
    motionPanel_.setEmbedded(true);
    addAndMakeVisible(sourceButton_);
    addChildComponent(browser_);
    presets_.setTooltip("Browse the immutable review catalog and your tagged user presets");
    presets_.onClick=[this]{browser_.open();};
    browser_.looseFile=[this](bool saving){choosePreset(saving);};
    addAndMakeVisible(presets_);
    for (std::size_t i=0;i<4;++i) {
        auto& type=effectsType_[i];
        const char* names[]{"Bypass","Lowpass","Drive","Wavefold","Bitcrush","Ring","Gate","Phaser","Flanger","Delay","Chorus","Reverb","Tempo Delay (1/8)"};
        static_assert(std::size(names)==static_cast<std::size_t>(effects::Type::count));
        for (std::size_t n=0;n<std::size(names);++n) type.addItem(names[n],static_cast<int>(n+1));
        type.setTitle("Shared effect slot " + juce::String(static_cast<int>(i+1)));
        type.setTooltip("Tempo Delay: eighth-note tap at host BPM (120 if unavailable, 30 BPM minimum); Amount is feedback");
        type.onChange=[this,i] { auto s=processor_.effect(i);s.type=static_cast<effects::Type>(effectsType_[i].getSelectedId()-1);(void)processor_.setEffect(i,s);refreshEffectSemantics(i); };
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
        refreshEffectSemantics(i);
    }
    patternSelector_.setTextWhenNothingSelected("Load seed pattern...");
    patternSelector_.setTitle("Load trigger seed pattern");
    patternSelector_.setTooltip("Replaces the saved pattern; works when HOST PATTERN is enabled");
    for(int i=0;i<12;++i)patternSelector_.addItem("Seed pattern " + juce::String(i+1),i+1);
    patternSelector_.onChange=[this] {
        const auto id=patternSelector_.getSelectedId();pattern::Pattern selected;
        if(id>=1 && id<=12 && pattern::factory(static_cast<std::size_t>(id-1),selected)) editPattern([&](auto& p){p=selected;});
        patternSelector_.setSelectedId(0,juce::dontSendNotification);
        refreshPattern();
    };
    patternEnabled_.setClickingTogglesState(true);
    patternEnabled_.setTitle("Enable host-synchronized trigger pattern");
    patternEnabled_.setTooltip(u8("Requires a playing 4/4 host with PPQ and 10–1000 BPM; oversized blocks are rejected to bound callback work"));
    patternAttachment_=std::make_unique<juce::ButtonParameterAttachment>(processor_.patternEnabledParameter(),patternEnabled_);
    addAndMakeVisible(patternSelector_);addAndMakeVisible(patternEnabled_);
    for(int i=1;i<=4;++i){patternLength_.addItem(juce::String(i)+" bars",i);patternBar_.addItem("Bar "+juce::String(i),i);}
    patternLength_.setTitle("Pattern length in bars");patternBar_.setTitle("Pattern bar to edit");
    patternBar_.setSelectedId(1,juce::dontSendNotification);
    patternLength_.onChange=[this] {const int id=patternLength_.getSelectedId();
        if(id>=1 && id<=4)editPattern([id](pattern::Pattern& p){p.bars=static_cast<std::uint8_t>(id);});
        refreshPattern();};
    patternBar_.onChange=[this] {refreshPattern();};
    const char* gridNames[]{"1/4","1/8","1/16","1/32","1/4T","1/8T","1/16T","1/32T"};
    for(int i=0;i<8;++i)patternGrid_.addItem(gridNames[i],i+1);
    patternGrid_.setTitle("Pattern grid");patternGrid_.setTooltip("Changing grid reinterprets step timing without resampling steps");
    patternGrid_.onChange=[this]{const int id=patternGrid_.getSelectedId();
        if(id>=1&&id<=8)editPattern([id](pattern::Pattern& p){p.grid=static_cast<pattern::Grid>(id-1);});
        refreshPattern();};
    for(int i=1;i<=3;++i)patternPage_.addItem("Page "+juce::String(i),i);
    patternPage_.setTitle("Step page within bar");patternPage_.setSelectedId(1,juce::dontSendNotification);
    patternPage_.onChange=[this]{refreshPattern();};
    patternSwing_.setRange(0,1,.01);patternSwing_.setSliderStyle(juce::Slider::LinearBar);
    patternSwing_.setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);
    patternSwing_.setTitle("Pattern swing");patternSwing_.setTooltip("Delay odd steps up to half their grid interval");
    patternSwing_.onStructuralChange=[this]{const float swing=static_cast<float>(patternSwing_.getValue());
        editPattern([swing](pattern::Pattern& p){p.swing=swing;});};
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
        slider.setSliderStyle(juce::Slider::LinearBar);slider.setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);
        slider.setTextValueSuffix(suffixes[i]);slider.setTitle(detailNames[i]);slider.setTooltip(detailNames[i]);
        slider.onStructuralChange=[this,i] {const int bar=patternBar_.getSelectedId(),step=patternStepSelector_.getSelectedId(),page=patternPage_.getSelectedId();
            const int value=static_cast<int>(patternDetails_[i].getValue());
            if(bar>=1&&bar<=4&&step>=1&&step<=16&&page>=1&&page<=3)editPattern([bar,step,page,i,value](pattern::Pattern& p){
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
        step.setTooltip("Enable or disable this step on the selected bar and page");
        step.onClick=[this,i] {const int bar=patternBar_.getSelectedId(),page=patternPage_.getSelectedId();const bool enabled=patternSteps_[i].getToggleState();
            if(bar>=1&&bar<=4&&page>=1&&page<=3)editPattern([bar,page,i,enabled](pattern::Pattern& p){
                const auto steps=pattern::stepsPerBar(p.grid);const auto local=static_cast<std::size_t>(page-1)*16+i;
                if(bar<=p.bars&&local<steps)p.steps[static_cast<std::size_t>(bar-1)*steps+local].enabled=enabled;});
            patternStepSelector_.setSelectedId(static_cast<int>(i+1),juce::dontSendNotification);refreshPattern();};
        addAndMakeVisible(step);
    }
    patternActions_.setTitle("Pattern clipboard, selected bar variations, undo and redo");
    patternActions_.setTooltip("Copy the whole Pattern or the selected bar. Bar copy lasts until this editor closes; paste requires the same grid. Shift or reverse the selected bar without changing other bars.");patternActions_.setTextWhenNothingSelected(u8("Edit…"));
    patternActions_.addItem("Copy Pattern",1);patternActions_.addItem("Paste Pattern",2);
    patternActions_.addItem("Copy selected bar",5);patternActions_.addItem("Paste into selected bar",6);
    patternActions_.addSeparator();
    patternActions_.addItem("Shift bar one step earlier",7);patternActions_.addItem("Shift bar one step later",8);
    patternActions_.addItem("Reverse bar step order",9);
    patternActions_.addSeparator();
    patternActions_.addItem("Undo edit",3);patternActions_.addItem("Redo edit",4);
    patternActions_.onChange=[this]{const auto id=patternActions_.getSelectedId();patternActions_.setSelectedId(0,juce::dontSendNotification);patternCommand(id);};
    addAndMakeVisible(patternActions_);
    const auto bindPatternDrag=[this](juce::Slider& slider){
        slider.onDragStart=[this]{patternDragBefore_=displayedPattern_;patternDragging_=true;};
        slider.onDragEnd=[this]{finishPatternDrag();};
    };
    bindPatternDrag(patternSwing_);for(auto& control:patternDetails_)bindPatternDrag(control);
    refreshPattern();
    // Workspace shell: tab strip, section header, persistent keyboard.
    workspaceTabs_.setTabs({
        {"design","DESIGN"},{"perform","PERFORM"},{"timeline","TIMELINE"},{"motion","MOTION"},
        {"process","PROCESS"},{"trigger","TRIGGER"},{"presets","PRESETS"},{"advanced","ADVANCED"}});
    workspaceTabs_.onTabChanged=[this](int index){
        if(!syncingTabs_&&index>=0&&index<workspaceCount)setWorkspace(static_cast<Workspace>(index));};
    addAndMakeVisible(workspaceTabs_);
    addAndMakeVisible(workspaceHeader_);
    // UI keyboard -> bounded processor queue (channel uiNoteChannel); the
    // audio thread consumes it and never echoes it into the host MIDI path.
    keyboard_.onNoteOn=[this](int note,float velocity){
        const bool queued=processor_.pushUiNote({1,static_cast<std::uint8_t>(note),
            static_cast<std::uint8_t>(juce::jlimit(1,127,int(std::lround(velocity*127.0f))))});
        // Fail closed: a dropped press must not leave a held visual.
        if(!queued)keyboard_.releaseAllNotes();
        else if(processor_.program().mode==PlayMode::oneShot)keyboard_.pulseNote(note);};
    keyboard_.onNoteOff=[this](int note){
        (void)processor_.pushUiNote({0,static_cast<std::uint8_t>(note),0});};
    keyboard_.onAllNotesOff=[this]{processor_.cancelUiNotes();};
    addAndMakeVisible(keyboard_);
    keyboard_.setAccent(vox::ui::tokens::colour::fx::accent);
    for (auto* module : std::array<vox::ui::VoxModule*,12>{ &generatorModule_, &previewModule_,
                 &macrosModule_, &designModule_, &timelineModule_, &motionModule_, &flowModule_,
                 &slotsModule_, &outputModule_, &patternModule_, &presetsModule_, &advancedModule_ })
    {
        addChildComponent(*module);
        // Module chrome sits behind the controls it hosts: it is a backdrop,
        // not an opaque sibling on top of them.
        module->toBack();
    }
    for (auto& label : moduleLabels_) {
        label.setJustificationType(juce::Justification::centredLeft);
        label.setColour(juce::Label::textColourId, vox::ui::tokens::colour::textSecondary);
        label.setFont(vox::ui::typography::eyebrow());
        label.setInterceptsMouseClicks(false,false);
        addChildComponent(label);
    }
    for (auto* label : { &previewName_, &previewMeta_ }) {
        label->setJustificationType(juce::Justification::centredLeft);
        label->setInterceptsMouseClicks(false,false);
        addChildComponent(*label);
    }
    previewName_.setColour(juce::Label::textColourId, vox::ui::tokens::colour::text);
    previewName_.setFont(vox::ui::typography::sectionTitle());
    previewMeta_.setColour(juce::Label::textColourId, vox::ui::tokens::colour::textMuted);
    previewMeta_.setFont(vox::ui::typography::mono(10.0f));
    flowTrack_.setInterceptsMouseClicks(false,false);
    addChildComponent(flowTrack_);
    programEyebrow_.setText("PROGRAM", juce::dontSendNotification);
    programEyebrow_.setColour(juce::Label::textColourId, vox::ui::tokens::colour::textMuted);
    programEyebrow_.setFont(vox::ui::typography::eyebrow());
    programEyebrow_.setJustificationType(juce::Justification::centredRight);
    programName_.setFont(vox::ui::typography::sectionTitle());
    programName_.setJustificationType(juce::Justification::centredRight);
    for (auto* label : { &programEyebrow_, &programName_ }) {
        label->setInterceptsMouseClicks(false,false);
        addAndMakeVisible(*label);
    }
    eventChart_.setInterceptsMouseClicks(false,false);
    addChildComponent(eventChart_);
    // The FX product selects the mint FX accent (reference [data-theme=fx]);
    // every shared component paints through the theme references.
    vox::ui::tokens::colour::selectFxTheme();
    keyboard_.setAccent (vox::ui::tokens::colour::fx::accent);
    // Recipe controls re-parented into the PERFORM Program-Generator module.
    generatorPanel().styleSelector().setSelectedId (processor_.generatorState().style + 1, juce::dontSendNotification);
    generatorPanel().complexitySelector().setSelectedId (processor_.generatorState().complexity, juce::dontSendNotification);
    // (reference composition). They are GeneratorPanel members; the panel
    // owns them visually now, the overlay keeps its own controls.
    for (auto* control : std::array<juce::Component*,18>{
             &generatorPanel().styleSelector(), &generatorPanel().complexitySelector(),
             &generatorPanel().intensitySlider(), &generatorPanel().energySlider(),
             &generatorPanel().stereoSlider(),
             &generatorPanel().fieldLock(0), &generatorPanel().fieldLock(1),
             &generatorPanel().fieldLock(2), &generatorPanel().fieldLock(3),
             &generatorPanel().fieldLock(4), &generatorPanel().fieldLock(5),
             &generatorPanel().fieldLock(6), &generatorPanel().fieldLock(7),
             &generatorPanel().fieldLock(8), &generatorPanel().fieldLock(9),
             &generatorPanel().fieldLock(10), &generatorPanel().fieldLock(11),
             &sourcePanel_.rootSelector() })
        addChildComponent (control);
    // Recipe dials are VoxKnobs constructed Small (reference dial size).
    // Eight visible macro knobs bound to the real host macro parameters.
    for(std::size_t i=0;i<macroKnobs_.size();++i){
        const auto macroNames=std::array<juce::String,8> { "Intensity", "Motion", "Brightness",
            "Space", "Texture", "Length", "Impact", "Width" }; // reference M1..M8 captions
        const auto name=juce::String("M")+juce::String(static_cast<int>(i+1))+u8(" \u00b7 ")+macroNames[i];
        macroKnobs_[i].setLabel(name);
        macroKnobs_[i].setTooltip(name+u8(" — host-automatable macro value with automation readback"));
        macroKnobs_[i].getSlider().setTitle(name);
        macroKnobs_[i].setDoubleClickResetValue(0.5); // host macro default (0..1)
        macroKnobAttachments_[i]=std::make_unique<juce::SliderParameterAttachment>(
            processor_.macroParameter(i),macroKnobs_[i].getSlider(),nullptr);
        addAndMakeVisible(macroKnobs_[i]);
    }
    setWorkspace(Workspace::perform);
    startTimerHz (25);
}
FxPanel::~FxPanel()
{
    // Editor close must never strand a keyboard note: release exactly the
    // notes this control started (the processor queue outlives the panel).
    keyboard_.releaseAllNotes();
    stopTimer();
    exportChooserOpen_=false;
    exportChooser_.reset();
}
void FxPanel::paint (juce::Graphics& g)
{
    namespace c = vox::ui::tokens::colour;
    namespace t = vox::ui::tokens;
    g.fillAll (c::background);
    // Reference product identity (PR #160 shell): raised icon square, mono
    // eyebrow, display title — painted, no WebView, no image assets.
    {
        auto header = getLocalBounds().removeFromTop (52).reduced (16, 4);
        auto icon = header.removeFromLeft (30).removeFromBottom (30).translated (0, -1);
        g.setColour (c::panelRaised);
        g.fillRoundedRectangle (icon.toFloat(), 6.0f);
        g.setColour (c::fx::accent);
        g.drawRoundedRectangle (icon.toFloat().reduced (0.5f), 6.0f, 1.0f);
        g.setFont (vox::ui::typography::mono (12.0f));
        g.drawText ("FX", icon, juce::Justification::centred);
        auto id = header.removeFromLeft (240);
        g.setColour (c::textSecondary);
        g.setFont (vox::ui::typography::eyebrow());
        g.drawText ("INSTRUMENT 02 / EVENT DESIGN", id.removeFromTop (13), juce::Justification::centredLeft);
        g.setColour (c::text);
        g.setFont (vox::ui::typography::productTitle());
        g.drawText ("VOX FX", id.removeFromTop (24), juce::Justification::centredLeft);
    }
    // Module-hosted labels/summary/flow track are child components (they
    // must paint above the module chrome).
    if (workspace_ == Workspace::motion && !macroLabelBounds_.isEmpty()) {
        g.setColour (c::textSecondary);
        g.setFont (vox::ui::typography::eyebrow());
        g.drawText ("SOURCE        TARGET                          VALUE        DEPTH",
                    macroLabelBounds_, juce::Justification::centredLeft);
    }
    if (workspace_ == Workspace::process && !fxLabelBounds_.isEmpty()) {
        g.setColour (c::textSecondary);
        g.setFont (vox::ui::typography::eyebrow());
        auto rest = fxLabelBounds_;
        const int labelWidth = fxLabelBounds_.getWidth() / 4;
        for (int i = 0; i < 4; ++i) {
            auto column = i < 3 ? rest.removeFromLeft (labelWidth) : rest;
            g.drawText ("FX " + juce::String (i + 1) + "            WET       AMOUNT",
                        column.reduced (4, 0), juce::Justification::centredLeft);
        }
    }
    // Real stereo peak / clip meter from rendered block samples.
    if (meterBounds_.isEmpty()) return;
    auto meter = meterBounds_;
    g.setColour (c::control);
    g.fillRoundedRectangle (meter.toFloat(), 5.0f);
    g.setColour (c::border);
    g.drawRoundedRectangle (meter.toFloat().reduced (0.5f), 5.0f, 1.0f);
    auto text = meter.reduced (7, 4);
    g.setColour (c::text);
    g.setFont (vox::ui::typography::mono (10.0f));
    auto titleRow = text.removeFromTop (13);
    g.drawText (clipHold_ ? "STEREO PEAK / CLIP" : "STEREO PEAK", titleRow,
                juce::Justification::centredLeft);
    g.setColour (c::textMuted);
    g.drawText ("EV " + juce::String (shownEvents_) + "/8+4  GR " + juce::String (shownGrains_) +
                    "/64  DROP " + juce::String (static_cast<juce::int64> (shownDropped_)),
                titleRow, juce::Justification::centredRight);
    const auto trackHeight = juce::jlimit (4, 10, text.getHeight() / static_cast<int> (shownPeaks_.size()) - 3);
    for (std::size_t ch = 0; ch < shownPeaks_.size(); ++ch) {
        auto row = text.removeFromTop (trackHeight);
        g.setColour (c::text);
        g.drawText (ch == 0 ? "L" : "R", row.removeFromLeft (12), juce::Justification::centredLeft);
        const auto db = juce::Decibels::gainToDecibels (shownPeaks_[ch], -60.f);
        auto track = row.reduced (0, 1).toFloat();
        g.setColour (c::background);
        g.fillRoundedRectangle (track, 2.f);
        g.setColour (clipHold_ ? c::danger : c::fx::accent);
        g.fillRoundedRectangle (track.withWidth (track.getWidth() * juce::jlimit (0.f, 1.f, (db + 60.f) / 60.f)), 2.f);
    }
}
void FxPanel::resized()
{
    auto bounds = getLocalBounds();
    // Chrome row 1: product identity (painted) + meter/clock/panic.
    auto top = bounds.removeFromTop (52).reduced (16, 4);
    panic_.setBounds (top.removeFromRight (88).reduced (2, 0));
    top.removeFromRight (8);
    clockStatus_.setBounds (top.removeFromRight (150).reduced (4, 0));
    meterBounds_ = top.removeFromRight (250).reduced (2);
    top.removeFromRight (10);
    // Reference header right side: PROGRAM eyebrow + strong program name.
    auto programArea = top.removeFromRight (200);
    programEyebrow_.setBounds (programArea.removeFromTop (14));
    programName_.setBounds (programArea);
    // Chrome row 2: zone tabs (reference fx-nav) span the content width.
    auto tabsRow = bounds.removeFromTop (38).reduced (16, 2);
    workspaceTabs_.setBounds (tabsRow);
    // Persistent native performance keyboard along the bottom (owner request).
    keyboard_.setBounds (bounds.removeFromBottom (118).reduced (16, 8));
    // Central workspace band composed of reference modules.
    auto central = bounds.reduced (16, 6);
    macroLabelBounds_ = {}; fxLabelBounds_ = {};
    previewSummaryBounds_ = {}; flowTrackBounds_ = {};
    paintLabels_.clear();
    generatorModule_.clearWells();
    // VoxModule::bodyArea() is module-local; panel children are placed in
    // panel coordinates, so translate by the module position.
    const auto bodyOf = [this] (vox::ui::VoxModule& module) {
        auto body = module.bodyArea();
        body.translate (module.getX(), module.getY());
        return body;
    };
    const auto band = central;
    { auto area = band; // DESIGN
        auto row = area.removeFromTop (30);
        sourceButton_.setBounds (row.removeFromLeft (150).reduced (2));
        programCurvesButton_.setBounds (row.removeFromLeft (180).reduced (2));
        auto locks = area.removeFromTop (28);
        for (std::size_t i = 0; i < layerLocks_.size(); ++i)
            layerLocks_[i].setBounds (locks.removeFromLeft (110).reduced (2));
        area.removeFromTop (4);
        designModule_.setBounds (area);
        auto body = bodyOf (designModule_);
        const int width = body.getWidth() / 4;
        for (std::size_t i = 0; i < layers_.size(); ++i) {
            auto card = i + 1 == layers_.size() ? body : body.removeFromLeft (width);
            layers_[i]->setBounds (card.reduced (3));
        }
    }
    { auto area = band; // PERFORM — Program Generator | Event Preview; Macros below
        auto columns = area.removeFromTop (std::max (0, area.getHeight() - 152));
        // Both modules fill their column: dense two-column grid per the
        // reference (no vast empty lower workspace).
        auto generatorCol = columns.removeFromLeft (columns.getWidth() * 55 / 100);
        columns.removeFromLeft (10);
        previewModule_.setBounds (columns);
        generatorModule_.setBounds (generatorCol);
        macrosModule_.setBounds (area.removeFromBottom (132).removeFromTop (132 - 10));

        auto body = bodyOf (generatorModule_);
        auto selectsBody = body.removeFromTop (46);
        auto selectsLabels = selectsBody.removeFromTop (14);
        const auto selectWidth = selectsBody.getWidth() / 3;
        for (int i = 0; i < 3; ++i)
            paintLabels_.push_back ({ selectsLabels.removeFromLeft (selectWidth).reduced (2, 0),
                std::array<juce::String,3> { "CATEGORY", "STYLE", "PITCH KEY" }[static_cast<std::size_t>(i)], 1 });
        family_.setBounds (selectsBody.removeFromLeft (selectWidth).reduced (2, 0));
        generatorPanel().styleSelector().setBounds (selectsBody.removeFromLeft (selectWidth).reduced (2, 0));
        sourcePanel_.rootSelector().setBounds (selectsBody.reduced (2, 0));
        body.removeFromTop (6);
        auto seedRow = body.removeFromTop (48); // 14 label strip + 32 controls + 2 pad
        const auto seedLabels = seedRow.removeFromTop (14);
        auto seedControls = seedRow;
        auto seedArea = seedControls.removeFromLeft (seedControls.getWidth() * 44 / 100);
        paintLabels_.push_back ({ seedArea.removeFromLeft (46), "SEED", 1 });
        seed_.setBounds (seedArea.removeFromBottom (30).reduced (1, 3));
        seedControls.removeFromLeft (8);
        auto lengthArea = seedControls.removeFromLeft (seedControls.getWidth() * 40 / 100);
        paintLabels_.push_back ({ lengthArea.removeFromLeft (62), "LENGTH", 1 });
        eventBars_.setBounds (lengthArea.removeFromBottom (30).reduced (1, 3));
        duration_.setBounds (seedControls.removeFromRight (104).removeFromBottom (34).reduced (0, 0));
        body.removeFromTop (6);
        auto recipes = body.removeFromTop (std::max (96, body.getHeight() - (4 + 14 + 2 + 26 + 26 + 4 + 34 + 18)));
        generatorModule_.addWell (recipes.translated (-generatorModule_.getX(), -generatorModule_.getY())
                                              .reduced (0, 2));
        const auto recipeWidth = recipes.getWidth() / 4;
        for (int i = 0; i < 4; ++i) {
            auto slot = i == 3 ? recipes : recipes.removeFromLeft (recipeWidth);
            if (i == 1) paintLabels_.push_back ({ slot.removeFromTop (14).reduced (2, 0), "COMPLEXITY", 1 });
            else slot.removeFromTop (14); // VoxKnob paints its own dial label
            if (i == 1) generatorPanel().complexitySelector().setBounds (slot.removeFromTop (34).reduced (10, 3));
            else if (i == 0) generatorPanel().intensitySlider().setBounds (slot.removeFromTop (70).reduced (14, 0));
            else if (i == 2) generatorPanel().energySlider().setBounds (slot.removeFromTop (70).reduced (14, 0));
            else generatorPanel().stereoSlider().setBounds (slot.removeFromTop (70).reduced (14, 0));
        }
        body.removeFromTop (4);
        auto locksBody = body.removeFromTop (14 + 2 + 26 + 26);
        paintLabels_.push_back ({ locksBody.removeFromTop (14).reduced (2, 0), "KEEP ON GENERATE", 1 });
        locksBody.removeFromTop (2);
        auto locks = locksBody.removeFromTop (26);
        for (int i = 0; i < 6; ++i)
            generatorPanel().fieldLock (static_cast<std::size_t> (i)).setBounds (locks.removeFromLeft (locks.getWidth() / 6).reduced (2));
        auto locks2 = locksBody.removeFromTop (26);
        for (int i = 6; i < 12; ++i)
            generatorPanel().fieldLock (static_cast<std::size_t> (i)).setBounds (locks2.removeFromLeft (locks2.getWidth() / 6).reduced (2));
        body.removeFromTop (4);
        auto actions = body.removeFromTop (34);
        generate_.setBounds (actions.removeFromLeft (160).reduced (2));
        mutate_.setBounds (actions.removeFromLeft (120).reduced (2));
        actions.removeFromLeft (6);
        generatorSettings_.setBounds (actions.removeFromLeft (110).reduced (2));
        paintLabels_.push_back ({ actions.removeFromRight (actions.getWidth() - 396), u8("Creates editable Program data \u00b7 host automation unchanged"), 1 });

        auto previewBody = bodyOf (previewModule_);
        previewSummaryBounds_ = previewBody.removeFromTop (66);
        paintLabels_.push_back ({ previewSummaryBounds_.removeFromTop (14).reduced (2, 0), "SELECTED PROGRAM", 1 });
        previewName_.setBounds (previewSummaryBounds_.removeFromTop (30));
        previewMeta_.setBounds (previewSummaryBounds_);
        previewBody.removeFromTop (10);
        paintLabels_.push_back ({ previewBody.removeFromTop (14).reduced (2, 0), "EVENT PHASE", 1 });
        const auto configured = processor_.durationParameter().get();
        paintLabels_.push_back ({ previewBody.removeFromTop (14).reduced (2, 0),
            juce::String (configured, 2) + " s configured", 1 });
        previewBody.removeFromTop (6);
        paintLabels_.push_back ({ previewBody.removeFromTop (14).reduced (2, 0), "MODE / TRIGGER POLICY", 1 });
        playMode_.setBounds (previewBody.removeFromTop (28).reduced (2));
        previewBody.removeFromTop (4);
        triggerPolicy_.setBounds (previewBody.removeFromTop (28).reduced (2));
        previewBody.removeFromTop (8);
        paintLabels_.push_back ({ previewBody.removeFromTop (14).reduced (2, 0), "LOAD SEED PROGRAM / COMPARE", 1 });
        factory_.setBounds (previewBody.removeFromTop (30).reduced (2));
        previewBody.removeFromTop (4);
        comparison_.setBounds (previewBody.removeFromTop (30).reduced (2));
        previewBody.removeFromTop (10);
        auto previewActions = previewBody.removeFromBottom (34);
        audition_.setBounds (previewActions.removeFromLeft (140).reduced (2));
        stopAudition_.setBounds (previewActions.removeFromLeft (110).reduced (2));
        // The event chart (real program trajectory) fills the remaining band.
        eventChart_.setVisible (workspace_ == Workspace::perform);
        eventChart_.setBounds (previewBody);

        auto macrosBody = bodyOf (macrosModule_);
        auto well = macrosBody.removeFromBottom (110).reduced (10, 8); // grouped well
        const int knobWidth = well.getWidth() / static_cast<int> (macroKnobs_.size());
        for (std::size_t i = 0; i < macroKnobs_.size(); ++i) {
            auto slot = i + 1 == macroKnobs_.size() ? well : well.removeFromLeft (knobWidth);
            macroKnobs_[i].setBounds (slot.reduced (2));
        }
    }
    { auto area = band; // TIMELINE
        timelineModule_.setBounds (area);
        programCurves_.setBounds (bodyOf (timelineModule_));
    }
    { auto area = band; // MOTION
        motionModule_.setBounds (area);
        auto body = bodyOf (motionModule_);
        macroLabelBounds_ = body.removeFromTop (14).reduced (4, 0);
        auto macro = body.removeFromTop (34);
        macroSelector_.setBounds (macro.removeFromLeft (74).reduced (2));
        macroDestination_.setBounds (macro.removeFromLeft (180).reduced (2));
        macroValue_.setBounds (macro.removeFromLeft (110).reduced (2));
        macroDepth_.setBounds (macro.reduced (2));
        body.removeFromTop (4);
        motionPanel_.setBounds (body);
    }
    { auto area = band; // PROCESS
        flowModule_.setBounds (area.removeFromTop (64));
        flowTrackBounds_ = bodyOf (flowModule_);
        area.removeFromTop (8);
        auto slotsRow = area.removeFromTop (170);
        slotsModule_.setBounds (slotsRow);
        auto slotsBody = bodyOf (slotsModule_);
        fxLabelBounds_ = slotsBody.removeFromTop (14);
        auto fxRow = slotsBody.removeFromTop (120);
        const int fxWidth = fxRow.getWidth() / 4;
        for (std::size_t i = 0; i < 4; ++i) {
            auto column = i == 3 ? fxRow : fxRow.removeFromLeft (fxWidth);
            column = column.reduced (4);
            effectsType_[i].setBounds (column.removeFromLeft (column.getWidth() * 4 / 10));
            effectsWet_[i].setBounds (column.removeFromLeft (column.getWidth() / 2));
            effectsAmount_[i].setBounds (column);
        }
        area.removeFromTop (8);
        outputModule_.setBounds (area);
        flowTrack_.setVisible (workspace_ == Workspace::process);
        flowTrack_.setBounds (bodyOf (flowModule_));
        auto outputBody = bodyOf (outputModule_);
        output_.setBounds (outputBody.removeFromLeft (140).reduced (2));
        fxTailStatus_.setBounds (outputBody.removeFromRight (260));
    }
    { auto area = band; // TRIGGER
        patternModule_.setBounds (area);
        auto body = bodyOf (patternModule_);
        auto selectsBody = body.removeFromTop (46);
        auto selectsLabels = selectsBody.removeFromTop (14);
        auto selects = selectsBody;
        paintLabels_.push_back ({ selectsLabels.removeFromLeft (96).reduced (2, 0), "TRIGGER MODE", 5 });
        patternActions_.setBounds (selects.removeFromLeft (96).reduced (2, 0));
        selects.removeFromLeft (8);
        paintLabels_.push_back ({ selectsLabels.removeFromLeft (150).reduced (2, 0), "TRIGGER PATTERN RATE", 5 });
        patternSelector_.setBounds (selects.removeFromLeft (150).reduced (2, 0));
        selects.removeFromLeft (8);
        paintLabels_.push_back ({ selectsLabels.removeFromLeft (130).reduced (2, 0), "TRIGGER PATTERN BARS", 5 });
        patternLength_.setBounds (selects.removeFromLeft (110).reduced (2, 0));
        selects.removeFromLeft (8);
        selectsLabels.removeFromLeft (110); // keep label strip in sync (no extra title)
        patternBar_.setBounds (selects.removeFromLeft (100).reduced (2, 0));
        selects.removeFromLeft (8);
        patternPage_.setBounds (selects.removeFromLeft (100).reduced (2, 0));
        selects.removeFromLeft (8);
        patternEnabled_.setBounds (selects.removeFromLeft (170).reduced (2, 0));
        // Step option strip (note/velocity/probability/ratchet) + grid row.
        auto stepOpts = body.removeFromTop (30);
        patternStepSelector_.setBounds (stepOpts.removeFromLeft (90).reduced (2));
        stepOpts.removeFromLeft (8);
        patternGrid_.setBounds (stepOpts.removeFromLeft (100).reduced (2));
        stepOpts.removeFromLeft (8);
        patternSwing_.setBounds (stepOpts.removeFromLeft (stepOpts.getWidth() * 40 / 100).reduced (2));
        auto details = body.removeFromTop (30);
        const int detailWidth = details.getWidth() / 4;
        for (std::size_t i = 0; i < patternDetails_.size(); ++i) {
            auto slot = i == 3 ? details : details.removeFromLeft (detailWidth);
            paintLabels_.push_back ({ slot.removeFromTop (12).reduced (2, 0),
                std::array<juce::String,4> { "NOTE", "VELOCITY", "PROBABILITY", "RATCHET" }[i], 5 });
            patternDetails_[i].setBounds (slot.reduced (2));
        }
        auto stepRow = body.removeFromTop (54);
        const int stepWidth = stepRow.getWidth() / 16;
        for (std::size_t i = 0; i < patternSteps_.size(); ++i) {
            auto slot = i == 15 ? stepRow : stepRow.removeFromLeft (stepWidth);
            patternSteps_[i].setBounds (slot.reduced (2));
        }
        auto exportRow = body.removeFromTop (34);
        midiExport_.setBounds (exportRow.removeFromLeft (150).reduced (2));
    }
    { auto area = band; // PRESETS
        presetsModule_.setBounds (area);
        browser_.setBounds (bodyOf (presetsModule_));
    }
    { auto area = band; // ADVANCED
        advancedModule_.setBounds (area);
        auto body = bodyOf (advancedModule_);
        auto sampleRow = body.removeFromTop (34);
        sampleImport_.setBounds (sampleRow.removeFromLeft (130).reduced (2));
        sampleBuiltin_.setBounds (sampleRow.removeFromLeft (130).reduced (2));
        sampleStatus_.setBounds (sampleRow.reduced (2));
        auto exportRow = body.removeFromTop (34);
        wavExport_.setBounds (exportRow.removeFromLeft (130).reduced (2));
        auto transfer = body.removeFromTop (34);
        dragGrip_.setBounds (transfer.removeFromRight (190).reduced (2));
        prepareMidiDrag_.setBounds (transfer.removeFromRight (180).reduced (2));
        dragWarning_.setBounds (transfer.reduced (4, 0));
    }
    // Overlay panels keep their existing centred geometry.
    generatorPanel_->setBounds (getLocalBounds().withSizeKeepingCentre (768, 372));
    sourcePanel_.setBounds (getLocalBounds().withSizeKeepingCentre (665, 301));
    wavOptions_.setBounds (getLocalBounds().withSizeKeepingCentre (470, 232));
    syncModuleLabels();
}
void FxPanel::syncModuleLabels()
{
    for (auto& label : moduleLabels_) label.setVisible (false);
    std::size_t used = 0;
    for (const auto& entry : paintLabels_) {
        if (used >= moduleLabels_.size()) break;
        auto& label = moduleLabels_[used++];
        label.setText (entry.text, juce::dontSendNotification);
        label.setBounds (entry.bounds);
        label.setVisible (static_cast<int> (workspace_) == entry.workspace);
        label.toFront (false);
    }
    previewName_.setVisible (workspace_ == Workspace::perform);
    previewMeta_.setVisible (workspace_ == Workspace::perform);
    const auto family = family_.getSelectedId() > 0
        ? family_.getItemText (family_.getSelectedId() - 1) : juce::String ("--");
    previewName_.setText (family, juce::dontSendNotification);
    programName_.setText (family, juce::dontSendNotification);
    previewMeta_.setText (playMode_.getItemText (juce::jmax (0, playMode_.getSelectedId() - 1)) + " / " +
        triggerPolicy_.getItemText (juce::jmax (0, triggerPolicy_.getSelectedId() - 1)) +
        u8("  \u00b7  Live DSP event engine"), juce::dontSendNotification);
    flowTrack_.setVisible (workspace_ == Workspace::process);
}
void FxPanel::EventChart::paint (juce::Graphics& g)
{
    namespace c = vox::ui::tokens::colour;
    const auto program = processor.program();
    auto area = getLocalBounds();
    auto phaseStrip = area.removeFromBottom (20).toFloat().reduced (0, 4);
    const auto chart = area.toFloat().reduced (1);
    // Grid: 5 vertical / 4 horizontal hairlines (reference event chart).
    g.setColour (c::borderSubtle);
    for (int i = 1; i < 5; ++i)
        g.drawVerticalLine (static_cast<int> (chart.getX() + chart.getWidth() * i / 5.0f),
                            chart.getY(), chart.getBottom());
    for (int i = 1; i < 4; ++i)
        g.drawHorizontalLine (static_cast<int> (chart.getY() + chart.getHeight() * i / 4.0f),
                              chart.getX(), chart.getRight());
    // Real program data: the program-wide amplitude trajectory.
    juce::Path path;
    constexpr int steps = 96;
    for (int i = 0; i <= steps; ++i)
    {
        const float phase = i / static_cast<float> (steps);
        const float value = juce::jlimit (0.0f, 1.0f, program.amplitudeCurve.evaluate (phase));
        const float x = chart.getX() + phase * chart.getWidth();
        const float y = chart.getBottom() - value * chart.getHeight();
        if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
    }
    g.setColour (c::themeAccent);
    g.strokePath (path, juce::PathStrokeType (2.0f));
    // Enabled layer spans as dim markers across the chart top.
    g.setColour (c::themeAccentDim);
    for (const auto& layer : program.layers)
        if (layer.enabled)
            g.drawHorizontalLine (static_cast<int> (chart.getY()) + 3,
                chart.getX() + layer.timing.offset * chart.getWidth(),
                chart.getX() + (layer.timing.offset + layer.timing.span) * chart.getWidth());
    // Configured phase strip: equal segments, no fake fill.
    g.setColour (c::border);
    for (int i = 0; i < 24; ++i)
    {
        const auto segment = phaseStrip.removeFromLeft (phaseStrip.getWidth() / (24 - i));
        g.fillRoundedRectangle (segment.reduced (1, 6), 2.0f);
    }
}

void FxPanel::FlowTrack::paint (juce::Graphics& g)
{
    namespace c = vox::ui::tokens::colour;
    namespace ty = vox::ui::typography;
    auto track = getLocalBounds().reduced (2, 4);
    auto chip = [&] (juce::String label, bool active) {
        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText (ty::badge(), label, 0.0f, 0.0f);
        const auto bounds = track.removeFromLeft (
            static_cast<int> (glyphs.getBoundingBox (0, -1, true).getWidth()) + 20);
        g.setColour (c::control);
        g.fillRoundedRectangle (bounds.toFloat(), 5.0f);
        g.setColour (active ? c::fx::accent.withAlpha (0.55f) : c::border);
        g.drawRoundedRectangle (bounds.toFloat().reduced (0.5f), 5.0f, 1.0f);
        g.setColour (active ? c::fx::accent : c::textSecondary);
        g.setFont (ty::badge());
        g.drawText (label, bounds, juce::Justification::centred);
        track.removeFromLeft (16);
        g.setColour (c::textMuted);
        g.drawText (">", track.removeFromLeft (12), juce::Justification::centred);
    };
    chip ("A-D SUM", false);
    for (int i = 0; i < 4; ++i)
        chip (slots[i], active[i]);
    chip ("MASTER", false);
}

namespace {
juce::String workspaceTitle (FxPanel::Workspace workspace)
{
    using W = FxPanel::Workspace;
    switch (workspace) {
    case W::design:   return u8("DESIGN  ·  LAYERS A-D");
    case W::perform:  return u8("PERFORM  ·  PROGRAM / MODE / MACROS");
    case W::timeline: return u8("TIMELINE  ·  20 PROGRAM CURVES");
    case W::motion:   return u8("MOTION  ·  LFO / STEP / ADSR / MATRIX");
    case W::process:  return u8("PROCESS  ·  FOUR REAL FX SLOTS");
    case W::trigger:  return u8("TRIGGER / PATTERN  ·  1-4 BARS");
    case W::presets:  return u8("PRESETS  ·  FACTORY / USER / A-B");
    case W::advanced: return u8("ADVANCED  ·  SAMPLE / EXPORT / DIAGNOSTICS");
    case W::count:    break;
    }
    return "";
}
} // namespace
void FxPanel::setWorkspace (Workspace workspace)
{
    if (static_cast<int> (workspace) < 0 || static_cast<int> (workspace) >= workspaceCount) return;
    workspace_ = workspace;
    resized(); // relayout: per-workspace geometry, labels and wells are rebuilt
    applyWorkspaceVisibility();
    syncingTabs_ = true;
    workspaceTabs_.setSelectedIndex (static_cast<int> (workspace_));
    syncingTabs_ = false;
    const bool timeline = workspace_ == Workspace::timeline;
    programCurvesButton_.setToggleState (timeline, juce::dontSendNotification);
    programCurvesButton_.setButtonText (timeline ? "Layer cards" : "Program curves");
    if (timeline) programCurves_.refresh();
    workspaceHeader_.setTitle (workspaceTitle (workspace_));
    repaint();
    resized();
}
void FxPanel::applyWorkspaceVisibility()
{
    // Fail-safe: hide everything first, then mark the live workspace's
    // controls. Retired or not-yet-laid-out controls can never leak through.
    for (auto* child : getChildren()) child->setVisible (false);
    const auto show = [this] (juce::Component& component, bool visible) { component.setVisible (visible); };
    const bool design = workspace_ == Workspace::design;
    const bool perform = workspace_ == Workspace::perform;
    const bool timeline = workspace_ == Workspace::timeline;
    const bool motion = workspace_ == Workspace::motion;
    const bool process = workspace_ == Workspace::process;
    const bool trigger = workspace_ == Workspace::trigger;
    const bool presets = workspace_ == Workspace::presets;
    const bool advanced = workspace_ == Workspace::advanced;
    // DESIGN: four layer cards, locks, source and curves entry points.
    for (auto& layer : layers_) show (*layer, design);
    for (auto& lock : layerLocks_) show (lock, design);
    show (sourceButton_, design);
    show (programCurvesButton_, design); // Design entry point only; the tab strip owns TIMELINE
    show (programCurves_, timeline);
    // PERFORM: reference composition — Program Generator (category/style,
    // seed/length, recipe dials, locks, generate), Event Preview (program
    // select/A-B/audition), Performance Macros (8 host knobs).
    show (generatorModule_, perform); show (previewModule_, perform);
    show (macrosModule_, perform);
    show (family_, perform);
    show (sourcePanel_.rootSelector(), perform);
    show (generatorPanel().styleSelector(), perform);
    show (seed_, perform); show (duration_, perform); show (eventBars_, perform);
    show (generatorPanel().intensitySlider(), perform);
    show (generatorPanel().complexitySelector(), perform);
    show (generatorPanel().energySlider(), perform);
    show (generatorPanel().stereoSlider(), perform);
    for (std::size_t i = 0; i < 12; ++i) show (generatorPanel().fieldLock (i), perform);
    show (generate_, perform); show (mutate_, perform); show (generatorSettings_, perform);
    show (factory_, perform); show (comparison_, perform);
    show (playMode_, perform); show (triggerPolicy_, perform);
    show (audition_, perform); show (stopAudition_, perform);
    for (auto& knob : macroKnobs_) show (knob, perform);
    show (designModule_, design); show (timelineModule_, timeline);
    // Routing editor (assignment/readback) lives with Motion; the eight
    // performance knobs stay in PERFORM.
    show (macroSelector_, motion); show (macroDestination_, motion);
    show (macroValue_, motion); show (macroDepth_, motion);
    // MOTION / PROCESS / TRIGGER / PRESETS / ADVANCED. Motion and Presets
    // are real embedded workspace panels now (no single-button overlays).
    show (motionModule_, motion);
    show (motionPanel_, motion);
    show (flowModule_, process); show (slotsModule_, process); show (outputModule_, process);
    show (output_, process);
    show (patternModule_, trigger);
    show (presetsModule_, presets);
    show (browser_, presets);
    if (workspace_ == Workspace::motion) motionPanel_.refresh();
    if (workspace_ == Workspace::presets) browser_.open();
    for (std::size_t i = 0; i < effectsType_.size(); ++i) {
        show (effectsType_[i], process);
        show (effectsWet_[i], process);
        show (effectsAmount_[i], process);
    }
    show (fxTailStatus_, process);
    show (patternActions_, trigger); show (patternSelector_, trigger);
    show (patternEnabled_, trigger); show (patternStepSelector_, trigger);
    for (auto& detail : patternDetails_) show (detail, trigger);
    show (patternSwing_, trigger);
    show (patternLength_, trigger); show (patternBar_, trigger);
    show (patternGrid_, trigger); show (patternPage_, trigger);
    for (auto& step : patternSteps_) show (step, trigger);
    show (midiExport_, trigger);
    show (advancedModule_, advanced);
    show (sampleImport_, advanced); show (sampleBuiltin_, advanced);
    show (sampleStatus_, advanced);
    show (wavExport_, advanced);
    show (dragGrip_, advanced); show (prepareMidiDrag_, advanced);
    show (dragWarning_, advanced);
    // Always visible: chrome, keyboard, panic, clock and overlays themselves.
    show (workspaceTabs_, true);
    show (workspaceHeader_, false); // identity is painted in the chrome row
    show (panic_, true);
    show (clockStatus_, true);
    show (keyboard_, true);
    syncModuleLabels();
}
void FxPanel::focusLost (juce::Component::FocusChangeType)
{
    // Losing focus (clicking the DAW, tabbing away) never strands a note.
    keyboard_.releaseAllNotes();
}
void FxPanel::choosePreset(bool saving){
    presetChooser_=std::make_unique<juce::FileChooser>(saving?"Save VOX FX user sound":"Load VOX FX user sound",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("VOX FX.voxfx"),"*.voxfx");
    presets_.setEnabled(false);
    const int flags=saving?(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::warnAboutOverwriting):juce::FileBrowserComponent::openMode;
    presetChooser_->launchAsync(flags|juce::FileBrowserComponent::canSelectFiles,
        [safe=juce::Component::SafePointer<FxPanel>(this),saving](const juce::FileChooser& chooser){
            if(!safe)return;
            safe->presets_.setEnabled(true);
            const auto file=chooser.getResult();if(file==juce::File{})return;
            bool ok=false;
            try {
                if(file.hasFileExtension("voxfx")&&saving){
                    std::vector<std::byte> bytes;
                    if(safe->processor_.encodePortablePreset(bytes))ok=library::writePresetBytes(file,bytes);
                }else if(file.hasFileExtension("voxfx")&&file.getSize()>0&&file.getSize()<=static_cast<juce::int64>(portable::maxBytes)){
                    juce::FileInputStream input(file);
                    if(input.openedOk()){
                        std::vector<std::byte> data(portable::maxBytes+1);
                        const auto count=input.read(data.data(),static_cast<int>(data.size()));
                        if(count>0&&static_cast<std::size_t>(count)<=portable::maxBytes&&input.isExhausted())
                            ok=safe->processor_.restorePortablePreset(std::span(data).first(static_cast<std::size_t>(count)));
                    }
                }
            }catch(...){ok=false;}
            if(!ok)juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,"Preset operation failed",
                "Choose a valid .voxfx user sound. No project change was applied on a rejected load.");
        });
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
void FxPanel::chooseExportFile(fileexport::Kind kind,SaveResult accept)
{
    if(exportChooserOpen_||processor_.fileExportStatus()||processor_.dragInProgress())return;
    const bool wav=kind==fileexport::Kind::wav;
    const auto format=juce::String(wav?"WAV":"MIDI");
    const auto extension=juce::String(wav?"wav":"mid");
    const auto safe=juce::Component::SafePointer<FxPanel>(this);
    const auto ticket=++exportChooserTicket_;
    exportChooserOpen_=true;
    refreshExportControls();
    auto complete=[safe,ticket,format,extension,accept=std::move(accept)](juce::File target){
        if(safe==nullptr||!safe->exportChooserOpen_||safe->exportChooserTicket_!=ticket)return;
        ++safe->exportChooserTicket_; // invalidate duplicate completions immediately
        if(target!=juce::File{}){
            if(!target.hasFileExtension(extension))
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                    format+" export failed","Choose a filename ending in ."+extension+".");
            else {
                try {accept(std::move(target));}
                catch (...) {
                    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                        format+" export failed","Could not submit the selected file.");
                }
            }
        }
        if(safe!=nullptr){safe->exportChooserOpen_=false;safe->refreshExportControls();}
    };
    try {
        if(savePicker_){savePicker_(kind,std::move(complete));return;}
        exportChooser_=std::make_unique<juce::FileChooser>(wav?"Export VOX FX stereo WAV to a new file":"Export trigger pattern as a new MIDI file",
            juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile(wav?"VOX FX.wav":"VOX FX Pattern.mid"),"*."+extension);
        exportChooser_->launchAsync(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles,
            [complete=std::move(complete)](const juce::FileChooser& chooser){complete(chooser.getResult());});
    } catch (...) {
        if(safe!=nullptr&&safe->exportChooserTicket_==ticket){
            ++safe->exportChooserTicket_;safe->exportChooserOpen_=false;safe->refreshExportControls();
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,format+" export failed","Could not open the save dialog.");
        }
    }
}
void FxPanel::exportPattern(bool drag)
{
    if(exportChooserOpen_||processor_.fileExportStatus()||processor_.dragInProgress())return;
    // Capture before opening the chooser; later edits do not change the file.
    const auto pattern=processor_.patternSnapshot();
    const auto bpm=processor_.observedTempo();
    if(bpm<10.0||bpm>1000.0) {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
            "MIDI export failed",u8("The captured host tempo is outside the supported MIDI export range (10–1000 BPM)."));return;
    }
    if(drag){
        if(!processor_.prepareMidiDrag(pattern,bpm))
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,"MIDI drag preparation failed","Could not start the file worker.");
        dragFailed_=false;refreshExportControls();return;
    }
    chooseExportFile(fileexport::Kind::midi,[this,pattern,bpm](juce::File target){
        if(!processor_.startMidiExport(pattern,bpm,std::move(target)))
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,"MIDI export failed","Could not start the file worker.");
    });
}
void FxPanel::exportWav(offline::Settings settings,bool drag)
{
    if(exportChooserOpen_||processor_.fileExportStatus()||processor_.dragInProgress())return;
    // Capture a self-contained value while the processor is alive. The worker
    // never references the processor or editor and cannot touch the callback.
    std::shared_ptr<offline::Capture> capture;
    try { capture=std::make_shared<offline::Capture>(processor_.captureProgramWav(settings)); }
    catch(...) {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
            "WAV export failed","Could not capture the sound for export.");return;
    }
    if (offline::frameCount(*capture) == 0) {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
            "WAV export failed", "This audition exceeds the 128 MiB WAV audio limit or has invalid settings.");return;
    }
    if (capture->program.mode==PlayMode::sequence||capture->patternEnabled) {
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
    if(drag){
        if(!processor_.prepareWavDrag(std::move(*capture)))
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,"WAV drag preparation failed","Could not start the render worker.");
        dragFailed_=false;refreshExportControls();return;
    }
    chooseExportFile(fileexport::Kind::wav,[this,capture](juce::File target){
        if(!processor_.startWavExport(std::move(*capture),std::move(target)))
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                "WAV export failed","Could not start the render worker.");
    });
}
void FxPanel::beginFileDrag()
{
    if(exportChooserOpen_||processor_.fileExportStatus()){
        refreshExportControls();
        return;
    }
    // The offer performs the one fresh filesystem validation for this gesture.
    const auto prepared=processor_.cachedPreparedDragState();
    const bool available=prepared==FxProcessor::PreparedDragState::wav||prepared==FxProcessor::PreparedDragState::midi;
    if(!available){
        refreshExportControls();
        return;
    }
    const auto safe=juce::Component::SafePointer<FxPanel>(this);
    // Copy the test launcher before entering any native/nested dispatch. Only
    // the processor's value lease survives the callback, not this component.
    const auto launch=externalDrag_;
    const bool started=processor_.offerPreparedDrag([safe,launch](const juce::File& file,std::function<void()> done){
        if(safe==nullptr)return false;
        if(launch)return launch(file,safe->dragGrip_,std::move(done));
        if(safe->dragGrip_.getPeer()==nullptr)return false;
        return juce::DragAndDropContainer::performExternalDragDropOfFiles({file.getFullPathName()},false,
                                                                         &safe->dragGrip_,std::move(done));
    });
    if(safe!=nullptr){safe->dragFailed_=!started;safe->refreshExportControls();}
}
void FxPanel::refreshPattern()
{
    if(patternDragging_)return;
    const auto state=processor_.versionedPattern();
    if(state.revision!=patternRevision_||state.pattern!=displayedPattern_){patternUndo_.clear();patternRedo_.clear();}
    patternRevision_=state.revision;displayedPattern_=state.pattern;
    patternActions_.setItemEnabled(3,!patternUndo_.empty());patternActions_.setItemEnabled(4,!patternRedo_.empty());
    const auto& pattern=displayedPattern_;
    patternLength_.setSelectedId(pattern.bars,juce::dontSendNotification);
    patternGrid_.setSelectedId(static_cast<int>(pattern.grid)+1,juce::dontSendNotification);
    if(!patternSwing_.isMouseButtonDown()&&!patternSwing_.hasKeyboardFocus(true))
        patternSwing_.setValue(pattern.swing,juce::dontSendNotification);
    const auto steps=pattern::stepsPerBar(pattern.grid);
    const int pages=static_cast<int>((steps+15)/16);
    const int bar=std::clamp(patternBar_.getSelectedId(),1,static_cast<int>(pattern.bars));
    const int page=std::clamp(patternPage_.getSelectedId(),1,pages);
    for(int i=1;i<=4;++i)patternBar_.setItemEnabled(i,i<=pattern.bars);
    for(int i=1;i<=3;++i)patternPage_.setItemEnabled(i,i<=pages);
    patternBar_.setSelectedId(bar,juce::dontSendNotification);
    patternPage_.setSelectedId(page,juce::dontSendNotification);
    patternActions_.setItemEnabled(6,copiedPatternBar_.grid==pattern.grid);
    for(std::size_t i=0;i<patternSteps_.size();++i) {
        const auto local=static_cast<std::size_t>(page-1)*16+i;
        const bool inRange=bar<=pattern.bars && local<steps;
        const auto stepNumber=juce::String(static_cast<int>(local+1));
        const auto title="Bar "+juce::String(bar)+", step "+stepNumber;
        if(patternSteps_[i].getTitle()!=title)patternSteps_[i].setTitle(title);
        if(patternSteps_[i].getButtonText()!=stepNumber)patternSteps_[i].setButtonText(stepNumber);
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
void FxPanel::refreshEffectSemantics(std::size_t index)
{
    if(index>=effectsType_.size())return;
    const auto type=static_cast<effects::Type>(std::max(0,effectsType_[index].getSelectedId()-1));
    const auto prefix="Slot "+juce::String(static_cast<int>(index+1))+" ";
    effectsAmount_[index].setTitle(prefix+effects::amountLabel(type));
    effectsAmount_[index].setTooltip(effects::amountHelp(type));
    effectsWet_[index].setTooltip("Host-automatable wet mix for the selected effect type");
}
void FxPanel::refreshExportControls()
{
    using Kind=fileexport::Kind;
    using DragState=FxProcessor::PreparedDragState;
    const auto status=processor_.fileExportStatus();
    const bool wavBusy=status&&status->kind==Kind::wav, midiBusy=status&&status->kind==Kind::midi;
    // This refresh runs at 25 Hz; never probe the filesystem here. The next
    // explicit drag gesture validates the payload immediately before offering.
    const auto dragState=processor_.cachedPreparedDragState();
    const bool ready=dragState==DragState::wav||dragState==DragState::midi;
    const bool unavailable=dragState==DragState::unavailable;
    const bool active=dragState==DragState::active;
    const bool preparing=status&&status->purpose==fileexport::Purpose::drag;
    midiExport_.setEnabled(!exportChooserOpen_&&!wavBusy&&!active);
    wavExport_.setEnabled(!exportChooserOpen_&&!midiBusy&&!active);
    prepareMidiDrag_.setEnabled(!exportChooserOpen_&&!status&&!active);
    dragGrip_.setEnabled(!exportChooserOpen_&&!status&&ready&&!active);
    const auto dragLabel=active?u8("Dragging…"):preparing?u8("Preparing drag…"):unavailable?"Snapshot unavailable":ready?
        (dragFailed_?u8("Drag failed · retry"):dragState==DragState::wav?"Drag WAV snapshot":"Drag MIDI snapshot"):
        "No drag file";
    if(dragGrip_.getButtonText()!=dragLabel)dragGrip_.setButtonText(dragLabel);
    const auto dragHelp=unavailable?
        "The prepared temporary file is missing, incomplete, or unreadable. It is not recreated automatically. Use Prepare MIDI drag or WAV > Prepare drag to explicitly render a replacement; Save remains separate.":
        "Drag a prepared WAV or MIDI snapshot. Copy imports into the DAW project; use Save for a lasting file path.";
    if(dragGrip_.getTooltip()!=dragHelp){dragGrip_.setTooltip(dragHelp);dragGrip_.setDescription(dragHelp);}
    const auto warning=unavailable?(preparing?u8("Preparing replacement snapshot…"):u8("Snapshot unavailable · prepare again")):
        u8("Copy into project · Save for a lasting file");
    if(dragWarning_.getText()!=warning)dragWarning_.setText(warning,juce::dontSendNotification);
    const auto warningHelp=unavailable?
        "The temporary drag file is no longer available. VOX FX will not silently recreate it or delete a file already handed to the OS. Prepare a fresh WAV or MIDI snapshot explicitly.":
        "Temporary files may expire. Availability is checked when dragging; the OS may still read later after that check. Copy imports into the project; handed-off files are retained even after canceled drops.";
    if(dragWarning_.getTooltip()!=warningHelp)dragWarning_.setTooltip(warningHelp);
    if(status){
        const auto label=status->committing?juce::String(u8("Saving…")):status->cancelling?juce::String(u8("Cancelling…")):
            wavBusy?juce::String(static_cast<int>(status->progress*100))+"%":juce::String("Cancel");
        (wavBusy?wavExport_:midiExport_).setButtonText(label);
    }
    if(!wavBusy&&wavExport_.getButtonText()!="WAV")wavExport_.setButtonText("WAV");
    if(!midiBusy&&midiExport_.getButtonText()!="MIDI")midiExport_.setButtonText("MIDI");
}
void FxPanel::timerCallback()
{
    using Kind=fileexport::Kind;
    if(const auto done=processor_.takeFileExportResult();done&&done->result!=fileexport::Result::cancelled){
        const bool ok=done->result==fileexport::Result::success;
        const bool wav=done->kind==Kind::wav;
        const juce::String format=wav?"WAV":"MIDI";
        if(done->purpose==fileexport::Purpose::drag){
            if(!ok)juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                format+" drag preparation failed","Could not write a temporary export file. Save to a different location instead.");
        } else juce::AlertWindow::showMessageBoxAsync(ok?juce::MessageBoxIconType::InfoIcon:juce::MessageBoxIconType::WarningIcon,
            format+(ok?" exported":" export failed"),ok?(wav?"The captured audio was exported.":"The captured trigger pattern and tempo were exported."):
            done->result==fileexport::Result::destinationExists?"A file or folder already exists at this path. Choose another name or location.":
            "Could not render or write the selected file.");
    }
    refreshExportControls();
    if (!isShowing()) return;
    keyboard_.setExternalNotes(processor_.hostNoteMask());
    keyboard_.setEventMode(processor_.program().mode==PlayMode::oneShot);
    for (std::size_t i = 0; i < 4; ++i) {
        flowTrack_.active[i] = effectsType_[i].getSelectedId() > 1;
        flowTrack_.slots[i] = juce::String ("0") + juce::String (static_cast<int> (i + 1)) + " " +
            effectsType_[i].getItemText (juce::jmax (0, effectsType_[i].getSelectedId() - 1));
    }
    if (workspace_ == Workspace::perform) {
        const auto family = family_.getSelectedId() > 0
            ? family_.getItemText (family_.getSelectedId() - 1) : juce::String ("--");
        previewName_.setText (family, juce::dontSendNotification);
        previewMeta_.setText (playMode_.getItemText (juce::jmax (0, playMode_.getSelectedId() - 1)) + " / " +
            triggerPolicy_.getItemText (juce::jmax (0, triggerPolicy_.getSelectedId() - 1)) +
            u8("  \u00b7  Live DSP event engine"), juce::dontSendNotification);
    }
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
    using Clock=FxProcessor::PatternClock;
    const auto clock=processor_.patternClock();
    juce::String patternLabel="HOST PATTERN";
    juce::String patternHelp="Use host PPQ to play the trigger Pattern. Sequence mode also uses this Pattern.";
    switch(clock){
        case Clock::inactive: break;
        case Clock::ready44: patternHelp="Host Pattern: synchronized in 4/4.";break;
        case Clock::readyAssumed44: patternLabel=u8("PATTERN · 4/4*");patternHelp="Time signature unavailable. Pattern assumes 4/4 for compatibility.";break;
        case Clock::noPlayhead: patternLabel=u8("PATTERN · NO CLOCK");patternHelp="The host did not report transport position.";break;
        case Clock::stopped: patternLabel=u8("PATTERN · STOPPED");patternHelp="Start the host transport to play the Pattern.";break;
        case Clock::invalidPpq: patternLabel=u8("PATTERN · NO PPQ");patternHelp="Host beat position is missing or outside the supported range.";break;
        case Clock::invalidTempo: patternLabel=u8("PATTERN · BPM");patternHelp="Host Pattern requires a valid tempo from 10 to 1000 BPM.";break;
        case Clock::unsupportedMeter: patternLabel=u8("PATTERN · METER");patternHelp="Host Pattern does not play in a reported time signature other than 4/4.";break;
        case Clock::waitingForState: patternLabel=u8("PATTERN · WAITING");patternHelp="Waiting for the new Program and Pattern state.";break;
        case Clock::blockRejected: patternLabel=u8("PATTERN · BLOCK");patternHelp="This host block cannot be scheduled within the Pattern work limit.";break;
    }
    if(patternEnabled_.getButtonText()!=patternLabel)patternEnabled_.setButtonText(patternLabel);
    if(patternEnabled_.getTooltip()!=patternHelp){patternEnabled_.setTooltip(patternHelp);patternEnabled_.setDescription(patternHelp);}
    refreshPattern();refreshMacro();refreshGenerator();
    family_.setSelectedId (static_cast<int>(program.layers[0].family) + 1, juce::dontSendNotification);
    const int barsId=program.bars==0?1:program.bars==1?2:program.bars==2?3:program.bars==4?4:program.bars==8?5:6;
    eventBars_.setSelectedId(barsId,juce::dontSendNotification);
    playMode_.setSelectedId(static_cast<int>(program.mode)+1,juce::dontSendNotification);
    triggerPolicy_.setSelectedId(static_cast<int>(program.triggerPolicy)+1,juce::dontSendNotification);
    using Bars=FxProcessor::BarClock;
    juce::String barLabel="Seconds",barHelp="Free-running seconds events do not require host PPQ.";
    if(program.bars!=0)switch(processor_.barClock()){
        case Bars::noPosition: barLabel="No host bar clock";barHelp="Host did not supply transport position.";break;
        case Bars::stopped: barLabel="Bar clock: stopped";barHelp="Start transport to trigger bar events.";break;
        case Bars::missingMeter: barLabel="Bar clock: no meter";barHelp="Bar events require the host to report 4/4 explicitly.";break;
        case Bars::unsupportedMeter: barLabel="Bar clock: needs 4/4";barHelp="Reported meter is unsupported for bar events. Use seconds or change to 4/4.";break;
        case Bars::invalidTempo: barLabel="Bar clock: invalid BPM";barHelp="Bar events require finite host tempo from 1 to 1000 BPM.";break;
        case Bars::invalidPpq: barLabel="Bar clock: invalid PPQ";barHelp="Host beat position or bar origin is missing, non-finite or out of range.";break;
        case Bars::assumedOrigin:
            barLabel=program.mode==PlayMode::transition?"Bar line: PPQ-zero grid":"Host bar clock ready";
            barHelp="Host supplied 4/4 and PPQ but not bar start. Transition anchors its bar grid at PPQ zero.";break;
        case Bars::ready44:
            barLabel=program.mode==PlayMode::transition?"Ends on host bar line":"Host bar clock ready";
            barHelp="Bar events use the host's reported 4/4 clock. Transition ends its dry event on the target bar; wet effects may ring on.";break;
    }
    clockStatus_.setText(barLabel,juce::dontSendNotification);
    if(clockStatus_.getTooltip()!=barHelp){clockStatus_.setTooltip(barHelp);clockStatus_.setDescription(barHelp);}
    const auto fx=processor_.effectsSnapshot();
    for (std::size_t i=0;i<4;++i) {
        const auto s=effects::unpack(static_cast<std::uint16_t>(fx>>(i*16)));
        effectsType_[i].setSelectedId(static_cast<int>(s.type)+1,juce::dontSendNotification);
        refreshEffectSemantics(i);
    }
    if (sourcePanel_.isVisible()) sourcePanel_.refresh();
    if (programCurves_.isVisible()) programCurves_.refresh();
    else for (std::size_t i=0; i<layers_.size(); ++i) layers_[i]->refresh(program.layers[i]);
    const auto telemetry=processor_.audioTelemetry();shownEvents_=telemetry.events;shownGrains_=telemetry.grains;shownDropped_=telemetry.dropped;
    clipHold_=processor_.consumeClip()?25u:(clipHold_?clipHold_-1:0u);
    fxTailHold_=processor_.consumeFxTail()?12u:(fxTailHold_?fxTailHold_-1:0u);
    fxTailStatus_.setText(fxTailHold_?"FX TAIL":"",juce::dontSendNotification);
    const auto peaks=processor_.outputPeaks();
    shownPeaks_[0]=std::max(peaks.left,shownPeaks_[0]*.88f);
    shownPeaks_[1]=std::max(peaks.right,shownPeaks_[1]*.88f);
    repaint();
}
} // namespace vox::fx
