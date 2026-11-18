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
FxPanel::FxPanel (FxProcessor& processor)
    : processor_ (processor), attachment_ (processor.outputParameter(), output_.getSlider()),
      durationAttachment_ (processor.durationParameter(), duration_.getSlider())
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
    family_.addItem ("Granular Cloud (built-in source)", 12);
    family_.setTitle ("Sound family");
    family_.setTooltip ("Family for newly triggered events");
    family_.setSelectedId (processor_.family() + 1, juce::dontSendNotification);
    family_.onChange = [this] { processor_.setFamily (family_.getSelectedId() - 1); };
    addAndMakeVisible (family_);
    for(int i=1;i<=8;++i)macroSelector_.addItem("M"+juce::String(i),i);
    macroSelector_.addItem("LFO 1",9);macroSelector_.addItem("LFO 2",10);macroSelector_.addItem("S&H",11);
    macroSelector_.setTitle("Modulation source selector");macroSelector_.setTooltip("Macros or fixed-rate LFO 1 (0.5 Hz), LFO 2 (1 Hz), S&H (4 Hz)");
    macroSelector_.setSelectedId(1,juce::dontSendNotification);
    macroValue_.setSliderStyle(juce::Slider::LinearHorizontal);macroValue_.setTextBoxStyle(juce::Slider::TextBoxBelow,false,48,18);
    macroValue_.setTitle("Macro value");macroValue_.setTooltip("Bipolar macro value: center is neutral");
    macroDepth_.setRange(-1,1,.01);macroDepth_.setSliderStyle(juce::Slider::LinearHorizontal);
    macroDepth_.setTextBoxStyle(juce::Slider::TextBoxBelow,false,48,18);
    macroDepth_.setTitle("Macro to master gain depth");macroDepth_.setTooltip("Assign this macro to master gain; zero removes the route");
    macroDepth_.onValueChange=[this]{const auto source=sourceForControl(macroSelector_.getSelectedId());
        if(source!=mod::Source::count)(void)processor_.setMotionGainRoute(source,static_cast<float>(macroDepth_.getValue()),.01f);};
    macroSelector_.onChange=[this]{const int id=macroSelector_.getSelectedId();
        // Detach first: the new attachment's initial slider update must not
        // notify the previous macro's listener and overwrite its automation.
        macroAttachment_.reset();macroValue_.setEnabled(id>=1&&id<=8);
        if(id>=1&&id<=8)
            macroAttachment_=std::make_unique<juce::SliderParameterAttachment>(processor_.macroParameter(static_cast<std::size_t>(id-1)),macroValue_);
        refreshMacro();};
    macroAttachment_=std::make_unique<juce::SliderParameterAttachment>(processor_.macroParameter(0),macroValue_);
    addAndMakeVisible(macroSelector_);addAndMakeVisible(macroValue_);addAndMakeVisible(macroDepth_);
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
    mutate_.setTooltip ("Mutate layer gains, pans and gain curves using the seed");
    generate_.onClick = [this] {
        generator::Settings settings;
        settings.family = processor_.family(); settings.complexity = 4;
        const auto controls=processor_.generatorState();settings.seed=controls.seed;
        (void) processor_.generateProgram (settings, controls.locks);
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
    for (int i=0; i<4; ++i) {
        layers_[static_cast<std::size_t>(i)] = std::make_unique<LayerPanel>(processor_,i);
        addAndMakeVisible(*layers_[static_cast<std::size_t>(i)]);
    }
    for (std::size_t i=0;i<4;++i) {
        auto& type=effectsType_[i];
        const char* names[]{"Bypass","Lowpass","Drive","Wavefold","Bitcrush","Ring","Gate","Phaser"};
        for (int n=0;n<7;++n) type.addItem(names[n],n+1);
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
    patternEnabled_.setTooltip("Requires playing host with PPQ and BPM; 4/4 patterns only");
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
    g.drawText("SOURCE        VALUE       GAIN DEPTH",macroLabel,juce::Justification::centredLeft);
    auto labels=getLocalBounds().reduced(32).withTrimmedTop(185).removeFromTop(14);
    const int labelWidth=labels.getWidth()/4;
    for(int i=0;i<4;++i) {
        auto column=i==3?labels:labels.removeFromLeft(labelWidth);
        g.drawText("FX " + juce::String(i+1) + "            WET       AMOUNT",column.reduced(3,0),juce::Justification::centredLeft);
    }
    auto meter = getLocalBounds().reduced (32).removeFromBottom (55).removeFromLeft (320).toFloat();
    g.setColour (c::panelRaised); g.fillRoundedRectangle (meter, 4.0f);
    g.setColour (c::accent); g.fillRoundedRectangle (meter.withWidth (meter.getWidth() * shownPeak_), 4.0f);
    g.setColour (c::text); g.setFont (vox::ui::typography::controlLabel());
    g.drawText ("OUTPUT PEAK", meter.toNearestInt().reduced (8), juce::Justification::centredLeft);
}
void FxPanel::resized()
{
    auto area = getLocalBounds().reduced (32);
    area.removeFromTop (80);
    output_.setBounds (area.removeFromLeft (128).removeFromTop (140));
    duration_.setBounds (area.removeFromLeft (128).removeFromTop (140));
    area.removeFromLeft (20);
    auto familyArea = area.removeFromLeft (200);
    family_.setBounds (familyArea.removeFromTop (40));
    familyArea.removeFromTop(5);familyArea.removeFromTop(12);auto macroRow=familyArea.removeFromTop(36);
    macroSelector_.setBounds(macroRow.removeFromLeft(58).reduced(1));
    macroValue_.setBounds(macroRow.removeFromLeft(65).reduced(1));macroDepth_.setBounds(macroRow.reduced(1));
    area.removeFromLeft (20);
    auto buttons = area.removeFromLeft (160);
    audition_.setBounds (buttons.removeFromTop (32)); buttons.removeFromTop (4);
    panic_.setBounds (buttons.removeFromTop (32)); buttons.removeFromTop (4);
    factory_.setBounds (buttons.removeFromTop (32));
    area.removeFromLeft (20);
    auto generatorArea = area.removeFromLeft (180);
    seed_.setBounds (generatorArea.removeFromTop (30)); generatorArea.removeFromTop (5);
    generate_.setBounds (generatorArea.removeFromTop (35)); generatorArea.removeFromTop (5);
    mutate_.setBounds (generatorArea.removeFromTop (35));
    auto lockArea=area.removeFromLeft(100);
    auto topLocks=lockArea.removeFromTop(30), bottomLocks=lockArea.removeFromTop(30);
    layerLocks_[0].setBounds(topLocks.removeFromLeft(45));layerLocks_[1].setBounds(topLocks);
    layerLocks_[2].setBounds(bottomLocks.removeFromLeft(45));layerLocks_[3].setBounds(bottomLocks);
    auto fxRow=getLocalBounds().reduced(32).withTrimmedTop(185).removeFromTop(45);
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
    row.removeFromTop(235);
    row.removeFromBottom(120);
    const int width = row.getWidth() / 4;
    for (std::size_t i=0; i<layers_.size(); ++i) {
        auto card = i + 1 == layers_.size() ? row : row.removeFromLeft(width);
        layers_[i]->setBounds(card.reduced(3));
    }
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
    for(std::size_t i=0;i<4;++i)layerLocks_[i].setToggleState(controls.locks.layers[i]!=0,juce::dontSendNotification);
}
void FxPanel::refreshMacro()
{
    const auto source=sourceForControl(macroSelector_.getSelectedId());if(source==mod::Source::count)return;
    const auto config=processor_.macroConfig();
    float depth=0;for(std::size_t i=0;i<config.count;++i)if(config.routes[i].source==source){depth=config.routes[i].depth;break;}
    if(!macroDepth_.isMouseButtonDown()&&!macroDepth_.hasKeyboardFocus(true))macroDepth_.setValue(depth,juce::dontSendNotification);
}
void FxPanel::timerCallback()
{
    if (!isShowing()) return;
    // Block peak comes from rendered samples; visual decay is message-thread only.
    const auto program = processor_.program();
    refreshPattern();refreshMacro();refreshGenerator();
    family_.setSelectedId (static_cast<int>(program.layers[0].family) + 1, juce::dontSendNotification);
    const auto fx=processor_.effectsSnapshot();
    for (std::size_t i=0;i<4;++i) {
        const auto s=effects::unpack(static_cast<std::uint16_t>(fx>>(i*16)));
        effectsType_[i].setSelectedId(static_cast<int>(s.type)+1,juce::dontSendNotification);
    }
    for (std::size_t i=0; i<layers_.size(); ++i) layers_[i]->refresh(program.layers[i]);
    shownPeak_ = std::max (processor_.outputPeak(), shownPeak_ * 0.88f);
    repaint();
}
} // namespace vox::fx
