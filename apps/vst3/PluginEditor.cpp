#include "PluginEditor.h"
#include "instrument/HostParameterSchema.h"
#include "ui/common/UiComponents.h"
#include <algorithm>
#include <cstdio>

using Page = vstengine::ui::MainNavigation::Page;

namespace {
std::string slotId(std::size_t slot, std::string_view suffix)
{
    char prefix[16] {};
    std::snprintf(prefix, sizeof(prefix), "slot%02zu", slot + 1);
    return std::string(prefix) + std::string(suffix);
}

void knob(juce::Slider& slider)
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 20);
}

void panel(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    auto area = bounds.toFloat().reduced(0.5f);
    g.setColour(vstengine::ui::colours::panel);
    g.fillRoundedRectangle(area, vstengine::ui::metrics::corner);
    g.setColour(vstengine::ui::colours::border);
    g.drawRoundedRectangle(area, vstengine::ui::metrics::corner, 1.0f);
}

juce::String routeName(const vstengine::rack::PersistentSlotState& state)
{
    return state.routing.mode == vstengine::rack::RouteMode::off
        ? "OFF" : "CH" + juce::String(state.routing.channel);
}
} // namespace

void VstEngineAudioProcessorEditor::SequenceCallbacks::onCopy() { processor.sequence().copyTo(clipboard); copied = true; }
void VstEngineAudioProcessorEditor::SequenceCallbacks::onPaste() { if (copied) processor.sequence().paste(clipboard); }
void VstEngineAudioProcessorEditor::SequenceCallbacks::onRotateLeft() { processor.sequence().rotateLeft(); }
void VstEngineAudioProcessorEditor::SequenceCallbacks::onRotateRight() { processor.sequence().rotateRight(); }
void VstEngineAudioProcessorEditor::SequenceCallbacks::onReverse() { processor.sequence().reverse(); }
void VstEngineAudioProcessorEditor::SequenceCallbacks::onShiftLeft() { processor.sequence().shiftLeft(); }
void VstEngineAudioProcessorEditor::SequenceCallbacks::onShiftRight() { processor.sequence().shiftRight(); }
void VstEngineAudioProcessorEditor::SequenceCallbacks::onTransposeUp() { processor.sequence().transpose(1); }
void VstEngineAudioProcessorEditor::SequenceCallbacks::onTransposeDown() { processor.sequence().transpose(-1); }
void VstEngineAudioProcessorEditor::SequenceCallbacks::onOctaveUp() { processor.sequence().octaveUp(); }
void VstEngineAudioProcessorEditor::SequenceCallbacks::onOctaveDown() { processor.sequence().octaveDown(); }
void VstEngineAudioProcessorEditor::SequenceCallbacks::onMutate()
{
    juce::String diagnostic;
    (void) processor.mutateSelectedPattern(true, diagnostic);
}
void VstEngineAudioProcessorEditor::SequenceCallbacks::onClear() { processor.sequence().clearSelected(); }
void VstEngineAudioProcessorEditor::SequenceCallbacks::onSequenceChanged() { processor.publishSequenceForAudio(); }

VstEngineAudioProcessorEditor::RackRail::RackRail(VstEngineAudioProcessor& p) : processor(p)
{
    title.setText("INSTRUMENT RACK", juce::dontSendNotification);
    vstengine::ui::styleLabel(title, 12.0f, juce::Justification::centredLeft,
                              vstengine::ui::colours::mutedText);
    addAndMakeVisible(title);
    for (std::size_t i = 0; i < slots.size(); ++i) {
        vstengine::ui::styleButton(slots[i]);
        slots[i].setClickingTogglesState(false);
        slots[i].onClick = [this, i] { if (onSelected) onSelected(i); };
        addAndMakeVisible(slots[i]);
    }
}

void VstEngineAudioProcessorEditor::RackRail::paint(juce::Graphics& g) { panel(g, getLocalBounds()); }

void VstEngineAudioProcessorEditor::RackRail::resized()
{
    auto area = getLocalBounds().reduced(8);
    title.setBounds(area.removeFromTop(25));
    const int height = juce::jmax(24, area.getHeight() / static_cast<int>(slots.size()));
    for (auto& slot : slots) slot.setBounds(area.removeFromTop(height).reduced(0, 1));
}

void VstEngineAudioProcessorEditor::RackRail::refresh()
{
    const auto& states = processor.instrumentRack().state();
    const auto runtime = processor.instrumentRack().runtimeState();
    const auto selected = processor.selectedSlotIndex();
    for (std::size_t i = 0; i < slots.size(); ++i) {
        const auto* descriptor = processor.instrumentRack().descriptor(i);
        auto name = descriptor != nullptr ? juce::String(descriptor->name) : "Empty +";
        auto stateFlags = states[i].mute ? " M" : states[i].solo ? " S" : "";
        auto activity = runtime[i].ownedNotes != 0 ? "  *" : "";
        const auto level = descriptor != nullptr
            ? "  " + juce::String(states[i].level, 1) : juce::String();
        slots[i].setButtonText(juce::String(static_cast<int>(i + 1)).paddedLeft('0', 2)
            + "  " + name + "  " + routeName(states[i]) + level + stateFlags + activity);
        slots[i].setToggleState(i == selected, juce::dontSendNotification);
        slots[i].setColour(juce::TextButton::buttonColourId,
            i == selected ? vstengine::ui::colours::primary.darker(0.68f)
                          : vstengine::ui::colours::panelRaised);
    }
}

VstEngineAudioProcessorEditor::InstrumentHeader::InstrumentHeader(VstEngineAudioProcessor& p)
    : processor(p)
{
    vstengine::ui::styleLabel(title, 18.0f, juce::Justification::centredLeft,
                              vstengine::ui::colours::text);
    vstengine::ui::styleLabel(subtitle, 11.0f, juce::Justification::centredLeft,
                              vstengine::ui::colours::mutedText);
    midiIn.addItem("OFF", 1);
    for (int channel = 1; channel <= 16; ++channel)
        midiIn.addItem("CH" + juce::String(channel), channel + 1);
    int item = 1;
    for (const auto* descriptor : processor.availableInstruments())
        instrument.addItem(descriptor->name, item++);
    const std::array<juce::Component*, 7> components {
        &title, &subtitle, &instrument, &preset, &midiIn, &presetPrevious, &presetNext
    };
    for (auto* component : components)
        addAndMakeVisible(component);
    for (auto* button : { &presetPrevious, &presetNext }) vstengine::ui::styleButton(*button);
    preset.setTextWhenNothingSelected("Sound preset");
    instrument.onChange = [this] {
        const auto descriptors = processor.availableInstruments();
        const auto index = instrument.getSelectedItemIndex();
        if (index < 0 || index >= static_cast<int>(descriptors.size())) return;
        juce::String diagnostic;
        processor.loadSlotInstrument(selected, descriptors[static_cast<std::size_t>(index)]->id,
                                     diagnostic);
        bind(selected);
        if (onModelChanged) onModelChanged();
    };
    preset.onChange = [this] {
        const auto index = preset.getSelectedItemIndex();
        if (index < 0 || index >= static_cast<int>(presetIds.size())) return;
        juce::String diagnostic;
        processor.applySelectedSoundPreset(presetIds[static_cast<std::size_t>(index)], diagnostic);
    };
    auto stepPreset = [this](int delta) {
        if (preset.getNumItems() == 0) return;
        const auto next = (preset.getSelectedItemIndex() + delta + preset.getNumItems())
            % preset.getNumItems();
        preset.setSelectedItemIndex(next, juce::sendNotificationAsync);
    };
    presetPrevious.onClick = [stepPreset] { stepPreset(-1); };
    presetNext.onClick = [stepPreset] { stepPreset(1); };
    midiIn.onChange = [this] {
        juce::String diagnostic;
        const int requested = midiIn.getSelectedId() - 1;
        if (!processor.assignSlotChannel(selected, requested,
                VstEngineAudioProcessor::ChannelConflictAction::reject, diagnostic))
            refresh();
        if (onModelChanged) onModelChanged();
    };
}

void VstEngineAudioProcessorEditor::InstrumentHeader::paint(juce::Graphics& g) { panel(g, getLocalBounds()); }

void VstEngineAudioProcessorEditor::InstrumentHeader::resized()
{
    auto area = getLocalBounds().reduced(14, 9);
    auto identity = area.removeFromLeft(245);
    title.setBounds(identity.removeFromTop(26)); subtitle.setBounds(identity);
    instrument.setBounds(area.removeFromLeft(190).reduced(4, 8));
    presetPrevious.setBounds(area.removeFromLeft(38).reduced(2, 8));
    preset.setBounds(area.removeFromLeft(190).reduced(4, 8));
    presetNext.setBounds(area.removeFromLeft(38).reduced(2, 8));
    midiIn.setBounds(area.removeFromRight(90).reduced(4, 8));
}

void VstEngineAudioProcessorEditor::InstrumentHeader::bind(std::size_t slot)
{
    selected = slot;
    const auto& state = processor.instrumentRack().state()[selected];
    const auto descriptors = processor.availableInstruments();
    int descriptorIndex = 0;
    for (std::size_t i = 0; i < descriptors.size(); ++i)
        if (descriptors[i]->id == state.instrumentId) descriptorIndex = static_cast<int>(i + 1);
    instrument.setSelectedId(descriptorIndex, juce::dontSendNotification);
    preset.clear(juce::dontSendNotification); presetIds.clear();
    for (const auto& content : processor.selectedContent())
        if (content.kind == vstengine::instrument::ContentKind::soundPreset) {
            presetIds.push_back(content.id);
            preset.addItem(content.name, static_cast<int>(presetIds.size()));
        }
    refresh();
}

void VstEngineAudioProcessorEditor::InstrumentHeader::refresh()
{
    const auto& state = processor.instrumentRack().state()[selected];
    const auto* descriptor = processor.instrumentRack().descriptor(selected);
    title.setText(descriptor != nullptr ? descriptor->name : "Empty slot", juce::dontSendNotification);
    subtitle.setText(descriptor != nullptr
        ? juce::String(descriptor->vendor) + "  /  slot " + juce::String(static_cast<int>(selected + 1))
        : "Choose instrument to activate slot", juce::dontSendNotification);
    const int channel = state.routing.mode == vstengine::rack::RouteMode::off ? 0 : state.routing.channel;
    midiIn.setSelectedId(channel + 1, juce::dontSendNotification);
}

VstEngineAudioProcessorEditor::MacroPage::MacroPage(
    VstEngineAudioProcessor& p, juce::String heading, std::size_t visibleCount)
    : processor(p), count(juce::jlimit<std::size_t>(1, knobs.size(), visibleCount))
{
    title.setText(heading, juce::dontSendNotification);
    description.setText(heading == "SOUND"
        ? "Primary performance controls"
        : "Stable host automation macros and instrument mappings", juce::dontSendNotification);
    vstengine::ui::styleLabel(title, 15.0f, juce::Justification::centredLeft,
                              vstengine::ui::colours::primary);
    vstengine::ui::styleLabel(description, 12.0f, juce::Justification::centredLeft,
                              vstengine::ui::colours::mutedText);
    addAndMakeVisible(title); addAndMakeVisible(description);
    for (std::size_t i = 0; i < knobs.size(); ++i) {
        knob(knobs[i]);
        vstengine::ui::styleLabel(labels[i], 11.0f, juce::Justification::centred,
                                  vstengine::ui::colours::text);
        addChildComponent(knobs[i]); addChildComponent(labels[i]);
        knobs[i].setVisible(i < count); labels[i].setVisible(i < count);
    }
}

void VstEngineAudioProcessorEditor::MacroPage::paint(juce::Graphics& g) { panel(g, getLocalBounds()); }

void VstEngineAudioProcessorEditor::MacroPage::resized()
{
    auto area = getLocalBounds().reduced(18);
    title.setBounds(area.removeFromTop(26)); description.setBounds(area.removeFromTop(25));
    area.removeFromTop(12);
    const int columns = count <= 6 ? static_cast<int>(count) : 4;
    const int rows = static_cast<int>((count + columns - 1) / columns);
    const int cellWidth = area.getWidth() / columns;
    const int cellHeight = area.getHeight() / rows;
    for (std::size_t i = 0; i < count; ++i) {
        auto cell = juce::Rectangle<int>(area.getX() + static_cast<int>(i % columns) * cellWidth,
            area.getY() + static_cast<int>(i / columns) * cellHeight, cellWidth, cellHeight).reduced(5);
        labels[i].setBounds(cell.removeFromTop(24)); knobs[i].setBounds(cell);
    }
}

void VstEngineAudioProcessorEditor::MacroPage::bind(std::size_t slot)
{
    attachments.clear();
    const auto* descriptor = processor.instrumentRack().descriptor(slot);
    for (std::size_t macro = 0; macro < count; ++macro) {
        juce::String label(vstengine::instrument::hostparams::macroLabels[macro].data());
        if (descriptor != nullptr)
            for (const auto& parameter : descriptor->parameters)
                if (parameter.preferredMacro == static_cast<std::int8_t>(macro)) {
                    label = parameter.name;
                    break;
                }
        labels[macro].setText(label, juce::dontSendNotification);
        knobs[macro].setTooltip("Macro " + juce::String(static_cast<int>(macro + 1)) + ": " + label);
        attachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            processor.parameters(), vstengine::instrument::hostparams::macroId(slot, macro), knobs[macro]));
    }
}

VstEngineAudioProcessorEditor::PatternPage::PatternPage(VstEngineAudioProcessor& p)
    : processor(p), callbacks(p), sequencer(p.sequence(), &callbacks)
{
    profile.setTextWhenNothingSelected("Generator profile");
    vstengine::ui::styleButton(generate, true); vstengine::ui::styleButton(mutate);
    vstengine::ui::styleLabel(status, 12.0f, juce::Justification::centredLeft,
                              vstengine::ui::colours::mutedText);
    const std::array<juce::Component*, 5> components {
        &profile, &generate, &mutate, &status, &sequencer
    };
    for (auto* component : components) addAndMakeVisible(component);
    generate.onClick = [this] {
        const auto index = profile.getSelectedItemIndex();
        if (index < 0 || index >= static_cast<int>(profileIds.size())) return;
        juce::String diagnostic;
        processor.generateSelectedPattern(profileIds[static_cast<std::size_t>(index)], diagnostic);
        status.setText(diagnostic.isEmpty() ? "Pattern generated" : diagnostic, juce::dontSendNotification);
        refresh();
    };
    mutate.onClick = [this] {
        juce::String diagnostic;
        processor.mutateSelectedPattern(true, diagnostic);
        status.setText(diagnostic.isEmpty() ? "Selected steps mutated" : diagnostic, juce::dontSendNotification);
        refresh();
    };
}

void VstEngineAudioProcessorEditor::PatternPage::paint(juce::Graphics& g) { panel(g, getLocalBounds()); }
void VstEngineAudioProcessorEditor::PatternPage::resized()
{
    auto area = getLocalBounds().reduced(12);
    auto tools = area.removeFromTop(38);
    profile.setBounds(tools.removeFromLeft(220).reduced(2));
    generate.setBounds(tools.removeFromLeft(105).reduced(2));
    mutate.setBounds(tools.removeFromLeft(145).reduced(2));
    status.setBounds(tools.reduced(8, 0));
    sequencer.setBounds(area.reduced(0, 6));
}
void VstEngineAudioProcessorEditor::PatternPage::bind(std::size_t)
{
    profile.clear(juce::dontSendNotification); profileIds.clear();
    for (const auto& content : processor.selectedContent())
        if (content.kind == vstengine::instrument::ContentKind::generatorProfile) {
            profileIds.push_back(content.id);
            profile.addItem(content.name, static_cast<int>(profileIds.size()));
        }
    if (!profileIds.empty()) profile.setSelectedItemIndex(0, juce::dontSendNotification);
    refresh();
}
void VstEngineAudioProcessorEditor::PatternPage::refresh()
{
    const auto* descriptor = processor.instrumentRack().descriptor(processor.selectedSlotIndex());
    const bool supported = descriptor != nullptr
        && (descriptor->capabilities & vstengine::instrument::Capability::sequence) != 0;
    sequencer.setSequence(processor.sequence());
    sequencer.setSlideEnabled((processor.selectedSequenceFieldMask() & VOX_SEQUENCE_SLIDE) != 0);
    sequencer.setEnabled(supported); generate.setEnabled(supported && !profileIds.empty());
    mutate.setEnabled(supported); sequencer.refreshFromModel();
    if (!supported) status.setText("Selected instrument has no sequence capability", juce::dontSendNotification);
}
void VstEngineAudioProcessorEditor::PatternPage::setPlayHead(int step) { sequencer.setPlayHeadPosition(step); }

VstEngineAudioProcessorEditor::RoutingPage::RoutingPage(VstEngineAudioProcessor& p) : processor(p)
{
    title.setText("ROUTING / MIX", juce::dontSendNotification);
    vstengine::ui::styleLabel(title, 15.0f, juce::Justification::centredLeft, vstengine::ui::colours::primary);
    vstengine::ui::styleLabel(status, 12.0f, juce::Justification::centredLeft, vstengine::ui::colours::mutedText);
    midiIn.addItem("OFF", 1);
    for (int channel = 1; channel <= 16; ++channel) midiIn.addItem("CH" + juce::String(channel), channel + 1);
    for (auto* slider : { &level, &pan }) knob(*slider);
    for (auto* button : { &swap, &move, &layerAction }) vstengine::ui::styleButton(*button);
    const std::array<juce::Component*, 13> components {
        &title, &status, &midiIn, &layer, &enabled, &mute, &solo, &locked,
        &level, &pan, &swap, &move, &layerAction
    };
    for (auto* component : components) addAndMakeVisible(component);
    midiIn.onChange = [this] { pendingChannel = midiIn.getSelectedId() - 1; assign(VstEngineAudioProcessor::ChannelConflictAction::reject); };
    swap.onClick = [this] { assign(VstEngineAudioProcessor::ChannelConflictAction::swap); };
    move.onClick = [this] { assign(VstEngineAudioProcessor::ChannelConflictAction::move); };
    layerAction.onClick = [this] { assign(VstEngineAudioProcessor::ChannelConflictAction::layer); };
}
void VstEngineAudioProcessorEditor::RoutingPage::paint(juce::Graphics& g) { panel(g, getLocalBounds()); }
void VstEngineAudioProcessorEditor::RoutingPage::resized()
{
    auto area = getLocalBounds().reduced(20);
    title.setBounds(area.removeFromTop(30));
    auto route = area.removeFromTop(42);
    midiIn.setBounds(route.removeFromLeft(120).reduced(2));
    swap.setBounds(route.removeFromLeft(80).reduced(2)); move.setBounds(route.removeFromLeft(80).reduced(2));
    layerAction.setBounds(route.removeFromLeft(85).reduced(2));
    auto toggles = area.removeFromTop(40);
    for (auto* button : { &enabled, &mute, &solo, &locked, &layer }) button->setBounds(toggles.removeFromLeft(100));
    auto controls = area.removeFromTop(150);
    level.setBounds(controls.removeFromLeft(130)); pan.setBounds(controls.removeFromLeft(130));
    status.setBounds(area.removeFromTop(28));
}
void VstEngineAudioProcessorEditor::RoutingPage::assign(VstEngineAudioProcessor::ChannelConflictAction action)
{
    juce::String diagnostic;
    const bool ok = processor.assignSlotChannel(selected, pendingChannel, action, diagnostic);
    status.setText(ok ? "Routing updated" : diagnostic, juce::dontSendNotification);
    const auto& state = processor.instrumentRack().state()[selected];
    midiIn.setSelectedId((state.routing.mode == vstengine::rack::RouteMode::off ? 0 : state.routing.channel) + 1,
                         juce::dontSendNotification);
}
void VstEngineAudioProcessorEditor::RoutingPage::bind(std::size_t slot)
{
    selected = slot; sliderAttachments.clear(); buttonAttachments.clear();
    const auto& state = processor.instrumentRack().state()[slot];
    pendingChannel = state.routing.mode == vstengine::rack::RouteMode::off ? 0 : state.routing.channel;
    midiIn.setSelectedId(pendingChannel + 1, juce::dontSendNotification);
    auto& apvts = processor.parameters();
    auto attachSlider = [&](juce::Slider& slider, std::string_view suffix) {
        sliderAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            apvts, slotId(slot, suffix), slider));
    };
    auto attachButton = [&](juce::Button& button, std::string_view suffix) {
        buttonAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            apvts, slotId(slot, suffix), button));
    };
    attachButton(layer, "Layer"); attachButton(enabled, "Enabled"); attachButton(mute, "Mute");
    attachButton(solo, "Solo"); attachButton(locked, "Lock");
    attachSlider(level, "Level"); attachSlider(pan, "Pan");
}

VstEngineAudioProcessorEditor::ZonesPage::ZonesPage(VstEngineAudioProcessor& p)
    : processor(p), ranges(p.parameters())
{
    title.setText("KEY / VELOCITY ZONES", juce::dontSendNotification);
    transposeLabel.setText("Transpose", juce::dontSendNotification);
    vstengine::ui::styleLabel(title, 15.0f, juce::Justification::centredLeft, vstengine::ui::colours::primary);
    vstengine::ui::styleLabel(transposeLabel, 11.0f, juce::Justification::centred, vstengine::ui::colours::text);
    transpose.setSliderStyle(juce::Slider::IncDecButtons);
    transpose.setTextBoxStyle(juce::Slider::TextBoxAbove, false, 80, 24);
    addAndMakeVisible(title); addAndMakeVisible(ranges); addAndMakeVisible(transposeLabel); addAndMakeVisible(transpose);
}
void VstEngineAudioProcessorEditor::ZonesPage::paint(juce::Graphics& g) { panel(g, getLocalBounds()); }
void VstEngineAudioProcessorEditor::ZonesPage::resized()
{
    auto area = getLocalBounds().reduced(18); title.setBounds(area.removeFromTop(28));
    auto side = area.removeFromRight(130).reduced(12);
    transposeLabel.setBounds(side.removeFromTop(24)); transpose.setBounds(side.removeFromTop(72));
    ranges.setBounds(area.reduced(0, 8));
}
void VstEngineAudioProcessorEditor::ZonesPage::bind(std::size_t slot)
{
    ranges.bind(slot);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters(), slotId(slot, "Transpose"), transpose);
}

VstEngineAudioProcessorEditor::AdvancedPage::AdvancedPage(VstEngineAudioProcessor& p) : processor(p)
{
    title.setText("ADVANCED", juce::dontSendNotification);
    description.setText("Host MIDI source, deterministic generation, global FX", juce::dontSendNotification);
    vstengine::ui::styleLabel(title, 15.0f, juce::Justification::centredLeft, vstengine::ui::colours::primary);
    vstengine::ui::styleLabel(description, 12.0f, juce::Justification::centredLeft, vstengine::ui::colours::mutedText);
    vstengine::ui::styleLabel(status, 12.0f, juce::Justification::centredLeft, vstengine::ui::colours::status);
    midiMode.addItemList({ "AUTO", "PIANO ROLL", "GENERATOR", "BOTH" }, 1);
    effectsPreset.addItemList({ "Clean", "Psy Drive", "Wide Motion", "Lo-Fi", "Deep Space" }, 1);
    effectsPreset.setTextWhenNothingSelected("Global FX preset"); knob(seed);
    vstengine::ui::styleButton(generateAll, true); vstengine::ui::styleButton(mutateAll);
    const std::array<juce::Component*, 8> components {
        &title, &description, &status, &midiMode, &effectsPreset, &seed,
        &generateAll, &mutateAll
    };
    for (auto* component : components)
        addAndMakeVisible(component);
    generateAll.onClick = [this] { juce::String d; processor.generateAllPatterns(d); status.setText(d, juce::dontSendNotification); };
    mutateAll.onClick = [this] { juce::String d; processor.mutateAllPatterns(d); status.setText(d, juce::dontSendNotification); };
    effectsPreset.onChange = [this] {
        static constexpr std::array ids { "clean", "psy-drive", "wide-motion", "lo-fi", "deep-space" };
        const auto index = effectsPreset.getSelectedItemIndex();
        if (index >= 0) processor.applyInternalEffectsPreset(ids[static_cast<std::size_t>(index)]);
    };
    modeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.parameters(), "midiMode", midiMode);
    seedAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.parameters(), "rngSeed", seed);
}
void VstEngineAudioProcessorEditor::AdvancedPage::paint(juce::Graphics& g) { panel(g, getLocalBounds()); }
void VstEngineAudioProcessorEditor::AdvancedPage::resized()
{
    auto area = getLocalBounds().reduced(22); title.setBounds(area.removeFromTop(28));
    description.setBounds(area.removeFromTop(26));
    auto controls = area.removeFromTop(130);
    midiMode.setBounds(controls.removeFromLeft(180).removeFromTop(38).reduced(2));
    effectsPreset.setBounds(controls.removeFromLeft(210).removeFromTop(38).reduced(2));
    seed.setBounds(controls.removeFromLeft(120));
    auto actions = area.removeFromTop(42);
    generateAll.setBounds(actions.removeFromLeft(140).reduced(2)); mutateAll.setBounds(actions.removeFromLeft(140).reduced(2));
    status.setBounds(area.removeFromTop(30));
}
void VstEngineAudioProcessorEditor::AdvancedPage::bind(std::size_t slot)
{
    const auto& runtime = processor.instrumentRack().runtimeState()[slot];
    status.setText("Slot " + juce::String(static_cast<int>(slot + 1)) + " / dropped MIDI "
        + juce::String(runtime.droppedMidiEvents), juce::dontSendNotification);
}

VstEngineAudioProcessorEditor::VstEngineAudioProcessorEditor(VstEngineAudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p),
      header(p.parameters(), {
          [this] { cycleGlobalPreset(-1); }, [this] { cycleGlobalPreset(1); },
          [this] { saveGlobalPreset(); }, [&p] { p.requestPanic(); },
          [this] { showPage(Page::advanced); }, [this](int index) { loadGlobalPreset(index); } }),
      rackRail(p), instrumentHeader(p), soundPage(p, "SOUND", 6), patternPage(p),
      routingPage(p), zonesPage(p), macrosPage(p, "MACROS", 8), advancedPage(p),
      pages { &soundPage, &patternPage, &routingPage, &zonesPage, &macrosPage, &advancedPage },
      keyboard(p.keyboardState(), juce::MidiKeyboardComponent::horizontalKeyboard)
{
    setLookAndFeel(&lookAndFeel);
    addAndMakeVisible(header); addAndMakeVisible(rackRail); addAndMakeVisible(instrumentHeader);
    addAndMakeVisible(navigation); addAndMakeVisible(keyboard);
    for (auto* page : pages) addChildComponent(page);
    rackRail.onSelected = [this](std::size_t slot) { bindSelection(slot); };
    instrumentHeader.onModelChanged = [this] { bindSelection(processor.selectedSlotIndex()); };
    navigation.onPageChanged = [this](Page page) { showPage(page); };
    keyboard.setAvailableRange(24, 96);
    setResizable(true, true); setResizeLimits(1040, 680, 1600, 1000); setSize(1240, 800);
    bindSelection(0); refreshGlobalPresets(); showPage(Page::sound); startTimerHz(20);
}

VstEngineAudioProcessorEditor::~VstEngineAudioProcessorEditor()
{
    stopTimer(); setLookAndFeel(nullptr);
}

void VstEngineAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(vstengine::ui::colours::background);
}

void VstEngineAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();
    header.setBounds(area.removeFromTop(58));
    auto keyboardArea = area.removeFromBottom(92).reduced(10, 8);
    keyboard.setBounds(keyboardArea);
    keyboard.setKeyWidth(static_cast<float>(keyboardArea.getWidth()) / 43.0f);
    auto rail = area.removeFromLeft(245).reduced(10, 8);
    rackRail.setBounds(rail);
    area = area.reduced(6, 8);
    instrumentHeader.setBounds(area.removeFromTop(66));
    navigation.setBounds(area.removeFromTop(44).reduced(0, 4));
    for (auto* page : pages) page->setBounds(area);
}

void VstEngineAudioProcessorEditor::bindSelection(std::size_t slot)
{
    processor.selectSlot(slot);
    instrumentHeader.bind(slot); soundPage.bind(slot); patternPage.bind(slot);
    routingPage.bind(slot); zonesPage.bind(slot); macrosPage.bind(slot); advancedPage.bind(slot);
    rackRail.refresh();
}

void VstEngineAudioProcessorEditor::showPage(Page page)
{
    const auto index = static_cast<std::size_t>(page);
    if (index >= pages.size()) return;
    for (std::size_t i = 0; i < pages.size(); ++i) pages[i]->setVisible(i == index);
    if (navigation.getCurrentPage() != page) navigation.setCurrentPage(page);
}

void VstEngineAudioProcessorEditor::cycleGlobalPreset(int delta)
{
    auto* manager = processor.presetManager();
    if (manager == nullptr) return;
    const auto presets = manager->getPresets();
    if (presets.isEmpty()) return;
    int current = -1;
    for (int i = 0; i < presets.size(); ++i)
        if (presets.getReference(i).name == manager->getCurrentPresetName()) current = i;
    const int next = (current + delta + presets.size()) % presets.size();
    manager->loadPreset(presets.getReference(next));
    bindSelection(processor.selectedSlotIndex());
    refreshGlobalPresets();
}

void VstEngineAudioProcessorEditor::loadGlobalPreset(int index)
{
    auto* manager = processor.presetManager();
    if (manager == nullptr) return;
    const auto presets = manager->getPresets();
    if (index < 0 || index >= presets.size()) return;
    manager->loadPreset(presets.getReference(index));
    bindSelection(processor.selectedSlotIndex());
    refreshGlobalPresets();
}

void VstEngineAudioProcessorEditor::saveGlobalPreset()
{
    if (auto* manager = processor.presetManager()) {
        const auto stamp = juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S");
        manager->saveFullPreset("User " + stamp);
        refreshGlobalPresets();
    }
}

void VstEngineAudioProcessorEditor::refreshGlobalPresets()
{
    auto* manager = processor.presetManager();
    if (manager == nullptr) return;
    const auto presets = manager->getPresets();
    juce::StringArray names;
    int selected = -1;
    for (int i = 0; i < presets.size(); ++i) {
        names.add(presets.getReference(i).name);
        if (presets.getReference(i).name == manager->getCurrentPresetName()) selected = i;
    }
    header.setPresetEntries(names, selected);
    header.setPresetName(manager->getCurrentPresetName());
}

void VstEngineAudioProcessorEditor::showPageForTesting(Page page) { showPage(page); }
Page VstEngineAudioProcessorEditor::currentPageForTesting() const noexcept { return navigation.getCurrentPage(); }
float VstEngineAudioProcessorEditor::keyboardKeyWidthForTesting(Page) const noexcept { return keyboard.getKeyWidth(); }
int VstEngineAudioProcessorEditor::keyboardComponentWidthForTesting(Page) const noexcept { return keyboard.getWidth(); }
juce::Rectangle<int> VstEngineAudioProcessorEditor::rackBoundsForTesting() const noexcept { return rackRail.getBounds(); }
juce::Rectangle<int> VstEngineAudioProcessorEditor::workspaceBoundsForTesting() const noexcept { return pages[static_cast<std::size_t>(navigation.getCurrentPage())]->getBounds(); }
juce::Rectangle<int> VstEngineAudioProcessorEditor::keyboardBoundsForTesting() const noexcept { return keyboard.getBounds(); }

void VstEngineAudioProcessorEditor::timerCallback()
{
    processor.publishSequenceForAudio(); rackRail.refresh(); instrumentHeader.refresh();
    patternPage.setPlayHead(processor.getCurrentPlayHeadStep());
    header.setMidiActivity(processor.hasRecentMidiActivity());
    header.setCpuLoad(processor.currentCpuLoad());
    if (auto* manager = processor.presetManager())
        header.setPresetName(manager->getCurrentPresetName());
}

juce::AudioProcessorEditor* VstEngineAudioProcessor::createEditor()
{
    return new VstEngineAudioProcessorEditor(*this);
}
