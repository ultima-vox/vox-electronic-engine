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
}
GeneratorPanel::GeneratorPanel(FxProcessor& processor) : processor_(processor) {
    const char* names[]{"Style seed", "Active layers", "Intensity", "Energy", "Stereo", "Sound role"};
    for(std::size_t i=0;i<labels_.size();++i){labels_[i].setText(names[i],juce::dontSendNotification);addAndMakeVisible(labels_[i]);}
    for(int i=0;i<6;++i)style_.addItem("Style " + juce::String(i+1),i+1);
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
        graphStatus_.setText(applied?"Sound graph updated · host values unchanged":"No structural change (check locks / seed)",juce::dontSendNotification);};
    fullGenerate_.onClick=[doSound]{doSound(false);};fullMutate_.onClick=[doSound]{doSound(true);};
    for(auto* c:std::array<juce::Component*,5>{&graphLock_,&lockGroup_,&fullGenerate_,&fullMutate_,&graphStatus_})addAndMakeVisible(*c);
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
    const char* lockNames[]{"Family","On","Time","Gain","Pan","Gain env","Pan env","Pitch env","Cutoff env","Filters","Transpose","Character"};
    const std::uint32_t bits[]{generator::family,generator::enabled,generator::timing,generator::gain,generator::pan,generator::gainCurve,generator::panCurve,generator::pitchCurve,generator::cutoffCurve,generator::filters,generator::transpose,generator::character};
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
            case 2:intensity_.setBounds(row);break;case 3:energy_.setBounds(row);break;case 4:stereo_.setBounds(row);break;case 5:role_.setBounds(row);break;default:break;}}
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
    auto sync=[](juce::Slider& slider,float value){if(!slider.isMouseButtonDown()&&!slider.hasKeyboardFocus(true))slider.setValue(value,juce::dontSendNotification);};
    sync(intensity_,state.intensity);sync(energy_,state.energy);sync(stereo_,state.stereo);
    const int layer=lockLayer_.getSelectedId()-1;
    if(layer>=0&&layer<4){const auto mask=state.locks.layers[static_cast<std::size_t>(layer)];
        const std::uint32_t bits[]{generator::family,generator::enabled,generator::timing,generator::gain,generator::pan,generator::gainCurve,generator::panCurve,generator::pitchCurve,generator::cutoffCurve,generator::filters,generator::transpose,generator::character};
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
constexpr const char* motionDestinations[]{"Master gain","Master balance","Voice pitch (±12 st)","Voice cutoff (±2 oct)","Filter Q (×¼–4)","Grain rate (×½–2)","Layer gain","Layer pan","Stereo width","FX 1 wet","FX 1 amount","FX 2 wet","FX 2 amount","FX 3 wet","FX 3 amount","FX 4 wet","FX 4 amount"};
static_assert(std::size(motionSources)==static_cast<unsigned>(mod::Source::count));
static_assert(std::size(motionDestinations)==static_cast<unsigned>(mod::Destination::count));
}
MotionPanel::MotionPanel(FxProcessor& processor):processor_(processor){
    title_.setText("MOTION — VOICE AND MIX ROUTES",juce::dontSendNotification);
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
    shapeTitle_.setText("EVENT SHAPE — eight equal phase steps (0–100%)",juce::dontSendNotification);
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
    reload_.onClick=[this]{refresh();};close_.onClick=[this]{setVisible(false);};
    const std::array<juce::Component*,15> components{&title_,&routeHelp_,&rows_,&source_,&destination_,&depth_,&smooth_,&randomRate_,&randomSeed_,&status_,&apply_,&remove_,&clock_,&reload_,&close_};
    for(auto* c:components)addAndMakeVisible(*c);
    refresh(0);
}
int MotionPanel::getNumRows(){return static_cast<int>(shown_.config.count)+(shown_.config.count<64?1:0);}
void MotionPanel::paintListBoxItem(int row,juce::Graphics& g,int w,int h,bool selected){
    namespace c=vox::ui::tokens::colour;
    if(selected)g.fillAll(c::border);
    g.setColour(c::text);g.setFont(juce::FontOptions(12.f));
    if(row<0||row>=getNumRows())return;
    juce::String text;
    if(static_cast<unsigned>(row)==shown_.config.count)text="+ Add route";
    else {const auto r=shown_.config.routes[static_cast<unsigned>(row)];
        text=juce::String(row+1)+". "+motionSources[static_cast<unsigned>(r.source)]+" → "+motionDestinations[static_cast<unsigned>(r.destination)]+
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
    shown_=processor_.versionedMotion();
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
    if(!processor_.replaceMotionIfCurrent(shown_,next)){
        refresh();status_.setText("Program or routes changed. Old edit was not applied.",juce::dontSendNotification);return;}
    refresh(row);
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
    close_.setBounds(footer.removeFromRight(76).reduced(2));reload_.setBounds(footer.removeFromRight(76).reduced(2));status_.setBounds(footer);
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
    slider(grainLength_,10,180,.1,"Grain length"," ms");slider(grainPosition_,0,100,.1,"Grain start"," %");
    grainLength_.onStructuralChange=[this]{const auto value=static_cast<float>(grainLength_.getValue());editLayer([=](auto& l){l.grains.milliseconds=value;});};
    const auto setPosition=[this]{const float value=scatter_.getToggleState()?-1.f:static_cast<float>(grainPosition_.getValue()*.01);
        editLayer([=](auto& l){l.grains.position=value;});};
    grainPosition_.onStructuralChange=setPosition;scatter_.onClick=setPosition;
    reverse_.onClick=[this]{const auto checked=reverse_.getToggleState();editLayer([=](auto& l){l.grains.reverse=checked;});};
    scatter_.setTooltip("Use a seeded random grain start instead of a fixed percentage of the source");
    addAndMakeVisible(scatter_);addAndMakeVisible(reverse_);
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
    close_.onClick=[this]{setVisible(false);};addAndMakeVisible(close_);refresh();
}
template <typename Edit> void SourcePanel::editLayer(Edit edit){
    const int index=layer_.getSelectedId()-1;
    if(index>=0 && index<4) (void)processor_.editLayerIfRevision(static_cast<std::size_t>(index),revision_,edit);
    refresh();
}
void SourcePanel::editKey(){
    const int root=root_.getSelectedId()-1,scale=scale_.getSelectedId()-1;
    if(root>=0&&root<12&&scale>=0&&scale<5)
        (void)processor_.editTonalityIfRevision({static_cast<source::Scale>(scale),static_cast<std::uint8_t>(root)},revision_);
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
    const auto* name=characterNames[static_cast<unsigned>(layer.family)];
    characterLabel_.setText(name,juce::dontSendNotification);character_.setTitle(name);
    if(!character_.isMouseButtonDown())character_.setValue(layer.character*100,juce::dontSendNotification);
    if(!transpose_.isMouseButtonDown())transpose_.setValue(layer.transpose,juce::dontSendNotification);
    const bool granular=layer.family==NoiseFamily::granular;
    for(auto* c:std::array<juce::Component*,4>{&grainLength_,&grainPosition_,&scatter_,&reverse_})c->setVisible(granular);
    scatter_.setToggleState(layer.grains.position<0,juce::dontSendNotification);reverse_.setToggleState(layer.grains.reverse,juce::dontSendNotification);
    grainPosition_.setEnabled(layer.grains.position>=0);
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
    g.drawText("SOURCE / KEY    •    Applies to new events",a.removeFromTop(21),juce::Justification::centredLeft);
    a.removeFromTop(72);
    g.drawText("FILTER     TYPE                     SLOPE                   CUTOFF                                           RESONANCE",a.removeFromTop(20),juce::Justification::centredLeft);
    g.drawText("A",a.removeFromTop(31),juce::Justification::centredLeft);
    g.drawText("B",a.removeFromTop(31),juce::Justification::centredLeft);
    if(grainLength_.isVisible())g.drawText("GRAIN      Length                                Start",a.removeFromTop(22),juce::Justification::centredLeft);
}
void SourcePanel::resized(){
    auto a=getLocalBounds().reduced(14);a.removeFromTop(21);
    auto first=a.removeFromTop(32);
    layer_.setBounds(first.removeFromLeft(142).reduced(2));root_.setBounds(first.removeFromLeft(78).reduced(2));scale_.setBounds(first.removeFromLeft(145).reduced(2));
    auto voice=a.removeFromTop(36);
    transpose_.setBounds(voice.removeFromLeft(248).reduced(2));
    characterLabel_.setBounds(voice.removeFromLeft(108).reduced(2));character_.setBounds(voice.reduced(2));a.removeFromTop(24);
    for(auto& f:filters_){auto row=a.removeFromTop(31);row.removeFromLeft(28);
        f.type.setBounds(row.removeFromLeft(118).reduced(2));f.slope.setBounds(row.removeFromLeft(110).reduced(2));
        f.hz.setBounds(row.removeFromLeft(row.getWidth()/2).reduced(2));f.q.setBounds(row.reduced(2));}
    a.removeFromTop(20);auto grains=a.removeFromTop(32);
    grainLength_.setBounds(grains.removeFromLeft(207).reduced(2));grainPosition_.setBounds(grains.removeFromLeft(194).reduced(2));
    scatter_.setBounds(grains.removeFromLeft(104));reverse_.setBounds(grains.removeFromLeft(93));
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
    title_.setText("PRESETS — REVIEW CATALOG / USER LIBRARY",juce::dontSendNotification);
    title_.setFont(vox::ui::typography::controlLabel());
    search_.setTitle("Search name, role, style, mood, use case or mode");search_.setTextToShowWhenEmpty("Search words…",juce::Colours::grey);
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
    const auto fillFacet=[this](juce::ComboBox& c,std::initializer_list<const char*> choices){
        int id=1;for(auto* label:choices)c.addItem(label,id++);c.setSelectedId(1,juce::dontSendNotification);c.onChange=[this]{filter();};};
    fillFacet(durationFacet_,{"All durations","Short ≤1 s","Long >1 s","Bars / Sequence","Host duration"});
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
    close_.onClick=[this]{stopPreview();setVisible(false);};
    const std::array<juce::Component*,28> controls{&rows_,&search_,&name_,&style_,&mood_,&use_,&scope_,&role_,&mode_,&editRole_,&onlyFavorites_,&favorite_,&title_,&detail_,&status_,&load_,&play_,&stop_,&save_,&tags_,&remove_,&openFile_,&saveFile_,
        &styleFacet_,&moodFacet_,&useFacet_,&durationFacet_,&tonalFacet_};
    for(auto* c:controls)addAndMakeVisible(*c);addAndMakeVisible(close_);
}
PresetBrowser::~PresetBrowser(){stopTimer();stopPreview();}
void PresetBrowser::open(){scan();setVisible(true);toFront(true);}
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
        auto notes=juce::String(browserModes[static_cast<unsigned>(e.mode)])+"  •  "+juce::String(e.bpm,0)+" BPM  •  "+
            (bars?juce::String(bars)+" bars":juce::String(seconds,2)+" s")+"\n"+
            key+"  •  Built-in assets  •  Review only\n"+juce::String(tags.use);
        auto searchable=lowerQuery(tags,e.mode)+" "+key.toLowerCase();
        all_.push_back({std::move(tags),e.mode,static_cast<int>(i),{},std::move(notes),bars,seconds,e.pitchedSource,key,std::move(searchable)});
    }
    indexStatus_="Scanning user presets…";
    try{indexJob_=std::make_unique<library::IndexJob>(root_);startTimerHz(20);}
    catch(...){indexStatus_="Could not start user scan";}
    rebuild();
}
void PresetBrowser::upsert(const library::IndexEntry& entry){
    const auto key=presetKey(entry.key,entry.pitched);
    const auto length=entry.bars?juce::String(entry.bars)+" bars":juce::String("Host duration");
    const auto details=juce::String(browserModes[static_cast<unsigned>(entry.mode)])+"  •  "+length+"  •  "+key+"\n"+
        (entry.asset==sample::Reference::Kind::builtin?"Built-in source":entry.asset==sample::Reference::Kind::pending?"Unverified sample — reimport WAV":"External WAV · SHA verified on Preview/Load");
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
    report(juce::String(static_cast<int>(visible_.size()))+" shown"+(indexStatus_.isEmpty()?"":" · "+indexStatus_));
}
void PresetBrowser::paintListBoxItem(int row,juce::Graphics& g,int w,int h,bool selectedRow){
    if(row<0||static_cast<std::size_t>(row)>=visible_.size())return;
    const auto& item=all_[visible_[static_cast<std::size_t>(row)]];
    namespace c=vox::ui::tokens::colour;
    if(selectedRow)g.fillAll(c::border);g.setColour(c::text);g.setFont(juce::FontOptions(13.f));
    const auto line=juce::String(item.factory>=0?"REVIEW  ":"USER  ")+juce::String(item.tags.name)+"  ·  "+browserRoles[item.tags.role]+
        (item.tags.style.empty()?"":"  ·  "+juce::String(item.tags.style))+(item.tags.favorite?"  ★":"");
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
    report(ok?"Loaded structure · host values unchanged":"Preset is unavailable or invalid");
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
    try{audition_=std::make_unique<preview::Job>(std::move(capture),std::move(reference));armed_=true;startTimerHz(20);report("Rendering preview…");}
    catch(...){report("Could not start preview worker");}
}
void PresetBrowser::stopPreview(){armed_=false;if(audition_)audition_->cancel();processor_.previewBus().stop();}
void PresetBrowser::timerCallback(){pollBackground();}
void PresetBrowser::pollBackground(){
    if(indexJob_&&indexJob_->ready()){
        auto result=indexJob_->take();indexJob_.reset();
        for(const auto& item:result.entries)upsert(item);
        indexStatus_=result.unavailable?"User library unavailable":juce::String{};
        if(result.invalid)indexStatus_+=(indexStatus_.isEmpty()?"":" · ")+juce::String(result.invalid)+" invalid user presets skipped";
        if(result.limited)indexStatus_+=(indexStatus_.isEmpty()?"":" · ")+juce::String("User scan limited to 512 files");
        visible_.clear();rebuild();
    }
    if(audition_){const auto result=audition_->result();if(result==preview::Result::loading)return;
        const bool started=armed_&&result==preview::Result::ready&&audition_->publish(processor_.previewBus(),previewTicket_);
        if(armed_&&previewTicket_!=processor_.previewBus().ticket())report("Preview cancelled by restore, panic or offline processing");
        else if(armed_)report(started?"Preview ready · host audio must be running":"Preview unavailable (asset, empty schedule or audio bounds)");
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
    if((id==2||id==6)&&(previous!=displayedPattern_||revision!=patternRevision_))return;
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
      durationAttachment_ (processor.durationParameter(), duration_.getSlider()), programCurves_(processor), sourcePanel_(processor), motionPanel_(processor), browser_(processor)
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
    dragGrip_.begin=[this]{beginFileDrag();};
    addAndMakeVisible(dragGrip_);
    dragWarning_.setText("Copy into project · Save for a lasting file",juce::dontSendNotification);
    dragWarning_.setJustificationType(juce::Justification::centredRight);
    dragWarning_.setFont(juce::FontOptions(12.0f));
    dragWarning_.setTooltip("Dragged files live in OS temp and may be cleared. VOX FX keeps handed-off files even after canceled drops; copy them into the project before cleaning temp files.");
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
    addChildComponent(sourcePanel_);
    sourceButton_.setTooltip("Edit Program key and per-layer pitch and filters");
    sourceButton_.onClick=[this]{sourcePanel_.refresh();sourcePanel_.setVisible(true);sourcePanel_.toFront(true);};
    addChildComponent(motionPanel_);
    motionButton_.setTooltip("Open the native 64-route modulation editor");
    motionButton_.onClick=[this]{motionPanel_.refresh();motionPanel_.setVisible(true);motionPanel_.toFront(true);};
    addAndMakeVisible(motionButton_);
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
        if(id>=1 && id<=12 && pattern::factory(static_cast<std::size_t>(id-1),selected)) editPattern([&](auto& p){p=selected;});
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
    patternSwing_.setRange(0,1,.01);patternSwing_.setSliderStyle(juce::Slider::LinearHorizontal);
    patternSwing_.setTextBoxStyle(juce::Slider::TextBoxLeft,false,40,20);
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
        slider.setSliderStyle(juce::Slider::LinearHorizontal);slider.setTextBoxStyle(juce::Slider::TextBoxBelow,false,48,20);
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
    patternActions_.setTitle("Pattern and bar clipboard, undo and redo");
    patternActions_.setTooltip("Copy the whole Pattern or the selected bar. Bar copy lasts until this editor closes; paste requires the same grid.");patternActions_.setTextWhenNothingSelected("Edit…");
    patternActions_.addItem("Copy Pattern",1);patternActions_.addItem("Paste Pattern",2);
    patternActions_.addItem("Copy selected bar",5);patternActions_.addItem("Paste into selected bar",6);
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
    startTimerHz (25);
}
FxPanel::~FxPanel()
{
    stopTimer();
    exportChooserOpen_=false;
    exportChooser_.reset();
}
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
    g.setColour(c::text);g.setFont(vox::ui::typography::controlLabel());
    auto text=meter.toNearestInt().reduced(8,3);
    g.drawText(clipHold_?"STEREO PEAK  /  CLIP":"STEREO PEAK",text.removeFromTop(16),juce::Justification::centredLeft);
    for(std::size_t ch=0;ch<shownPeaks_.size();++ch){
        auto row=text.removeFromTop(9);
        g.setColour(c::text);g.drawText(ch==0?"L":"R",row.removeFromLeft(15),juce::Justification::centredLeft);
        // Map -60..0 dBFS to the bar; clip is latched from actual samples.
        const auto db=juce::Decibels::gainToDecibels(shownPeaks_[ch],-60.f);
        auto track=row.reduced(0,2).toFloat();
        g.setColour(c::panel);g.fillRoundedRectangle(track,2.f);
        g.setColour(clipHold_?c::danger:c::accent);
        g.fillRoundedRectangle(track.withWidth(track.getWidth()*juce::jlimit(0.f,1.f,(db+60.f)/60.f)),2.f);
    }
    g.setColour(c::text);
    g.drawText("EV "+juce::String(shownEvents_)+"/8+4  GR "+juce::String(shownGrains_)+"/64  DROP "+juce::String(static_cast<juce::int64>(shownDropped_)),
               text,juce::Justification::centredLeft);
}
void FxPanel::resized()
{
    auto area = getLocalBounds().reduced (32);
    auto actions=area.withHeight(28);
    programCurvesButton_.setBounds(actions.removeFromRight(154));
    sourceButton_.setBounds(actions.removeFromRight(130).reduced(2));
    presets_.setBounds(actions.removeFromRight(141).reduced(2));
    motionButton_.setBounds(actions.removeFromRight(90).reduced(2));
    auto transfer=area.withTrimmedTop(30).removeFromTop(26);
    dragGrip_.setBounds(transfer.removeFromRight(152).reduced(2));
    prepareMidiDrag_.setBounds(transfer.removeFromRight(130).reduced(2));
    dragWarning_.setBounds(transfer.removeFromRight(246).reduced(4,0));
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
    auto auditionRow=buttons.removeFromTop(32);buttons.removeFromTop(4);
    stopAudition_.setBounds(auditionRow.removeFromRight(58).reduced(1,0));
    audition_.setBounds(auditionRow.reduced(1,0));
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
    patternActions_.setBounds(patternRow.removeFromLeft(80).reduced(2,8));
    patternRow.removeFromLeft(260);
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
    generatorPanel_->setBounds(getLocalBounds().withSizeKeepingCentre(768,372));
    sourcePanel_.setBounds(getLocalBounds().withSizeKeepingCentre(665,301));
    motionPanel_.setBounds(getLocalBounds().withSizeKeepingCentre(848,580));
    browser_.setBounds(getLocalBounds().withSizeKeepingCentre(920,456));
    wavOptions_.setBounds(getLocalBounds().withSizeKeepingCentre(470,232));
    programCurves_.setBounds(row.reduced(3));
    const int width = row.getWidth() / 4;
    for (std::size_t i=0; i<layers_.size(); ++i) {
        auto card = i + 1 == layers_.size() ? row : row.removeFromLeft(width);
        layers_[i]->setBounds(card.reduced(3));
    }
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
                    if(safe->processor_.encodePortablePreset(bytes)){
                        std::vector<std::uint8_t> data;data.reserve(bytes.size());
                        for(auto b:bytes)data.push_back(std::to_integer<std::uint8_t>(b));
                        ok=fileexport::writeBytes(file,data);
                    }
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
        exportChooser_=std::make_unique<juce::FileChooser>(wav?"Export VOX FX stereo WAV":"Export trigger pattern as MIDI",
            juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile(wav?"VOX FX.wav":"VOX FX Pattern.mid"),"*."+extension);
        exportChooser_->launchAsync(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles|
            juce::FileBrowserComponent::warnAboutOverwriting,
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
            "MIDI export failed","The captured host tempo is outside the supported MIDI export range (10–1000 BPM).");return;
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
    if(exportChooserOpen_||processor_.fileExportStatus()||!processor_.preparedDragKind())return;
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
void FxPanel::refreshExportControls()
{
    using Kind=fileexport::Kind;
    const auto status=processor_.fileExportStatus();
    const bool wavBusy=status&&status->kind==Kind::wav, midiBusy=status&&status->kind==Kind::midi;
    const auto ready=processor_.preparedDragKind();
    const bool active=processor_.dragInProgress();
    midiExport_.setEnabled(!exportChooserOpen_&&!wavBusy&&!active);
    wavExport_.setEnabled(!exportChooserOpen_&&!midiBusy&&!active);
    prepareMidiDrag_.setEnabled(!exportChooserOpen_&&!status&&!active);
    dragGrip_.setEnabled(!exportChooserOpen_&&!status&&ready&&!active);
    const auto dragLabel=active?"Dragging…":ready?(dragFailed_?"Drag failed · retry":*ready==Kind::wav?"Drag WAV snapshot":"Drag MIDI snapshot"):
        (status&&status->purpose==fileexport::Purpose::drag?"Preparing drag…":"No drag file");
    if(dragGrip_.getButtonText()!=dragLabel)dragGrip_.setButtonText(dragLabel);
    if(status){
        const auto label=status->committing?juce::String("Saving…"):status->cancelling?juce::String("Cancelling…"):
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
            "Could not render or write the selected file.");
    }
    refreshExportControls();
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
    using Clock=FxProcessor::PatternClock;
    const auto clock=processor_.patternClock();
    const char* patternLabel="HOST PATTERN";
    const char* patternHelp="Use host PPQ to play the trigger Pattern. Sequence mode also uses this Pattern.";
    switch(clock){
        case Clock::inactive: break;
        case Clock::ready44: patternHelp="Host Pattern: synchronized in 4/4.";break;
        case Clock::readyAssumed44: patternLabel="PATTERN · 4/4*";patternHelp="Time signature unavailable. Pattern assumes 4/4 for compatibility.";break;
        case Clock::noPlayhead: patternLabel="PATTERN · NO CLOCK";patternHelp="The host did not report transport position.";break;
        case Clock::stopped: patternLabel="PATTERN · STOPPED";patternHelp="Start the host transport to play the Pattern.";break;
        case Clock::invalidPpq: patternLabel="PATTERN · NO PPQ";patternHelp="Host beat position is missing or outside the supported range.";break;
        case Clock::invalidTempo: patternLabel="PATTERN · BPM";patternHelp="Host Pattern requires a valid tempo from 10 to 1000 BPM.";break;
        case Clock::unsupportedMeter: patternLabel="PATTERN · METER";patternHelp="Host Pattern does not play in a reported time signature other than 4/4.";break;
        case Clock::waitingForState: patternLabel="PATTERN · WAITING";patternHelp="Waiting for the new Program and Pattern state.";break;
        case Clock::blockRejected: patternLabel="PATTERN · BLOCK";patternHelp="This host block cannot be scheduled within the Pattern work limit.";break;
    }
    if(patternEnabled_.getButtonText()!=patternLabel)patternEnabled_.setButtonText(patternLabel);
    if(patternEnabled_.getTooltip()!=patternHelp){patternEnabled_.setTooltip(patternHelp);patternEnabled_.setDescription(patternHelp);}
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
    if (sourcePanel_.isVisible()) sourcePanel_.refresh();
    if (programCurves_.isVisible()) programCurves_.refresh();
    else for (std::size_t i=0; i<layers_.size(); ++i) layers_[i]->refresh(program.layers[i]);
    const auto telemetry=processor_.audioTelemetry();shownEvents_=telemetry.events;shownGrains_=telemetry.grains;shownDropped_=telemetry.dropped;
    clipHold_=processor_.consumeClip()?25u:(clipHold_?clipHold_-1:0u);
    const auto peaks=processor_.outputPeaks();
    shownPeaks_[0]=std::max(peaks.left,shownPeaks_[0]*.88f);
    shownPeaks_[1]=std::max(peaks.right,shownPeaks_[1]*.88f);
    repaint();
}
} // namespace vox::fx
