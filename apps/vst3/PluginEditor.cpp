#include "PluginEditor.h"
#include "RackRoutingPanel.h"
#include "instrument/HostParameterSchema.h"
#include "ui/common/UiComponents.h"
#include "vox-ui/Tokens.h"
#include "vox-ui/Typography.h"
#include <algorithm>
#include <cmath>
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

} // namespace

void VstEngineAudioProcessorEditor::SequenceCallbacks::onCopy() {
  edit.value.copyTo(clipboard);
  copied = true;
}
void VstEngineAudioProcessorEditor::SequenceCallbacks::onPaste() {
  if (copied)
    edit.value.paste(clipboard);
}
void VstEngineAudioProcessorEditor::SequenceCallbacks::onRotateLeft() {
  edit.value.rotateLeft();
}
void VstEngineAudioProcessorEditor::SequenceCallbacks::onRotateRight() {
  edit.value.rotateRight();
}
void VstEngineAudioProcessorEditor::SequenceCallbacks::onReverse() {
  edit.value.reverse();
}
void VstEngineAudioProcessorEditor::SequenceCallbacks::onShiftLeft() {
  edit.value.shiftLeft();
}
void VstEngineAudioProcessorEditor::SequenceCallbacks::onShiftRight() {
  edit.value.shiftRight();
}
void VstEngineAudioProcessorEditor::SequenceCallbacks::onTransposeUp() {
  edit.value.transpose(1);
}
void VstEngineAudioProcessorEditor::SequenceCallbacks::onTransposeDown() {
  edit.value.transpose(-1);
}
void VstEngineAudioProcessorEditor::SequenceCallbacks::onOctaveUp() {
  edit.value.octaveUp();
}
void VstEngineAudioProcessorEditor::SequenceCallbacks::onOctaveDown() {
  edit.value.octaveDown();
}
void VstEngineAudioProcessorEditor::SequenceCallbacks::onMutate()
{
    juce::String diagnostic;
    (void) processor.mutateSelectedPattern(true, diagnostic);
    edit = processor.sequenceEditSnapshot();
}
void VstEngineAudioProcessorEditor::SequenceCallbacks::onClear() {
  edit.value.clearSelected();
}
void VstEngineAudioProcessorEditor::SequenceCallbacks::onSequenceChanged() {
  if (!processor.commitSequenceEdit(edit))
    edit = processor.sequenceEditSnapshot();
}

VstEngineAudioProcessorEditor::RackRail::SlotRow::SlotRow() {
  power.setClickingTogglesState(true);
  power.setTooltip("Enable or disable this slot; independent from selection");
  addAndMakeVisible(power);
  setMouseCursor(juce::MouseCursor::PointingHandCursor);
  setWantsKeyboardFocus(true);
}
void VstEngineAudioProcessorEditor::RackRail::SlotRow::resized() {
  power.setBounds(getWidth() - 27, 2, 25, getHeight() - 4);
}
void VstEngineAudioProcessorEditor::RackRail::SlotRow::mouseDown(
    const juce::MouseEvent &event) {
  if (onSelect)
    onSelect(event.mods.isPopupMenu());
}
void VstEngineAudioProcessorEditor::RackRail::SlotRow::paint(
    juce::Graphics &g) {
  using namespace vox::ui::tokens;
  const auto area = getLocalBounds().toFloat().reduced(0.5f);
  g.setColour(selected ? colour::accent.withAlpha(0.12f) : colour::control);
  g.fillRoundedRectangle(area, 3.0f);
  g.setColour(selected ? colour::accent : colour::border);
  g.drawRoundedRectangle(area, 3.0f, selected ? 1.4f : 0.7f);
  auto left = getLocalBounds().reduced(6, 3);
  g.setFont(vox::ui::typography::makeFont(10.0f));
  g.setColour(colour::textSecondary);
  g.drawText(juce::String(static_cast<int>(index + 1)).paddedLeft('0', 2),
             left.removeFromLeft(18), juce::Justification::centredLeft);
  const auto icon = left.removeFromLeft(32).reduced(1, 2);
  const bool occupied = !summary.instrumentId.empty();
  if (occupied) {
    const auto tint = juce::Colour::fromHSV(
        static_cast<float>(juce::String(summary.instrumentId).hashCode() &
                           255) /
            255.0f,
        0.6f, 0.85f, 1.0f);
    g.setColour(tint.withAlpha(0.14f));
    g.fillRoundedRectangle(icon.toFloat(), 3.0f);
    juce::Path wave;
    wave.startNewSubPath(static_cast<float>(icon.getX()),
                         static_cast<float>(icon.getCentreY()));
    for (int x = 0; x < icon.getWidth(); ++x)
      wave.lineTo(static_cast<float>(icon.getX() + x),
                  static_cast<float>(icon.getCentreY()) +
                      std::sin(x * 0.65f) * std::sin(x * 0.16f) *
                          icon.getHeight() * 0.35f);
    g.setColour(tint);
    g.strokePath(wave, juce::PathStrokeType(1.1f));
  }
  left.removeFromLeft(5);
  left.removeFromRight(25);
  const auto name =
      occupied ? juce::String(summary.name) : juce::String("Empty");
  g.setColour(enabled ? colour::text : colour::textMuted);
  g.setFont(vox::ui::typography::makeFont(12.0f));
  auto first = left.removeFromTop(juce::jmax(17, left.getHeight() / 2));
  const auto status = channel + (mute ? " M" : solo ? " S" : "");
  g.drawText(status, first.removeFromRight(39),
             juce::Justification::centredRight);
  g.drawFittedText(name, first, juce::Justification::centredLeft, 1);
  if (occupied) {
    const bool resolved =
        summary.resolution == vstengine::instrument::ResolutionStatus::resolved;
    g.setFont(vox::ui::typography::makeFont(8.5f));
    g.setColour(resolved ? colour::textSecondary : colour::warning);
    g.drawFittedText(resolved
                         ? juce::String(summary.vendor) + "  " +
                               juce::String(summary.instrumentVersion) + ".0"
                         : "Unavailable / preserved",
                     left, juce::Justification::centredLeft, 1);
  }
}
VstEngineAudioProcessorEditor::RackRail::RackRail(VstEngineAudioProcessor& p) : processor(p)
{
  title.setText("INSTRUMENT RACK       16 slots", juce::dontSendNotification);
  vstengine::ui::styleLabel(title, 10.0f, juce::Justification::centredLeft,
                            vstengine::ui::colours::mutedText);
  addAndMakeVisible(title);
  addAndMakeVisible(add);
  add.setTooltip("Load an instrument into the first empty slot");
  add.onClick = [this] {
    const auto summaries = processor.rackSlotSummaries();
    for (std::size_t i = 0; i < summaries.size(); ++i)
      if (summaries[i].instrumentId.empty()) {
        showCatalog(i);
        return;
      }
    juce::AlertWindow::showMessageBoxAsync(
        juce::MessageBoxIconType::InfoIcon, "Rack full",
        "All 16 slots are occupied. Use a slot's context menu to replace or "
        "unload an instrument.");
  };
  for (std::size_t i = 0; i < slots.size(); ++i) {
    auto &row = slots[i];
    row.index = i;
    row.setName("Rack slot " + juce::String(static_cast<int>(i + 1)));
    row.power.setName(slotId(i, "Enabled"));
    row.onSelect = [this, i](bool popup) {
      if (popup)
        showSlotMenu(i);
      else {
        if (onSelected)
          onSelected(i);
        if (slots[i].summary.instrumentId.empty())
          showCatalog(i);
      }
    };
    addAndMakeVisible(row);
    powerAttachments.push_back(
        std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            processor.parameters(), slotId(i, "Enabled"), row.power));
  }
}
void VstEngineAudioProcessorEditor::RackRail::paint(juce::Graphics &g) {
  panel(g, getLocalBounds());
  const auto bottom = slots.back().getBottom() + 12;
  if (getHeight() - bottom > 30) {
    g.setColour(vox::ui::tokens::colour::accentDim);
    g.setFont(vox::ui::typography::makeFont(9.0f));
    g.drawText("S O U N D   F O R   A   D I F F E R E N T", 8, bottom,
               getWidth() - 16, 15, juce::Justification::centred);
    g.drawText("T O M O R R O W", 8, bottom + 17, getWidth() - 16, 15,
               juce::Justification::centred);
  }
}
void VstEngineAudioProcessorEditor::RackRail::resized()
{
  auto a = getLocalBounds().reduced(6);
  auto heading = a.removeFromTop(28);
  add.setBounds(heading.removeFromRight(24).reduced(1));
  title.setBounds(heading);
  const int h = juce::jlimit(29, 42, (a.getHeight() - 42) / 16);
  for (auto &row : slots)
    row.setBounds(a.removeFromTop(h).reduced(0, 1));
}
void VstEngineAudioProcessorEditor::RackRail::refresh()
{
  const auto summaries = processor.rackSlotSummaries();
  for (std::size_t i = 0; i < slots.size(); ++i) {
    auto &row = slots[i];
    row.summary = summaries[i];
    row.selected = i == processor.selectedSlotIndex();
    const auto value = [this, i](const char *suffix) {
      return processor.parameters()
          .getRawParameterValue(slotId(i, suffix).c_str())
          ->load();
    };
    row.enabled = value("Enabled") >= 0.5f;
    row.mute = value("Mute") >= 0.5f;
    row.solo = value("Solo") >= 0.5f;
    const int channel = juce::roundToInt(value("MidiIn"));
    row.channel = channel == 0 ? "OFF" : "CH" + juce::String(channel);
    row.power.setEnabled(!row.summary.instrumentId.empty());
    row.repaint();
  }
}
void VstEngineAudioProcessorEditor::RackRail::loadInstrument(
    std::size_t i, const std::string &id) {
  juce::String diagnostic;
  if (!processor.loadSlotInstrument(i, id, diagnostic))
    juce::AlertWindow::showMessageBoxAsync(
        juce::MessageBoxIconType::WarningIcon, "Instrument not loaded",
        diagnostic);
  else if (id.empty()) {
    if (auto *p =
            processor.parameters().getParameter(slotId(i, "MidiIn").c_str())) {
      p->beginChangeGesture();
      p->setValueNotifyingHost(p->convertTo0to1(0.0f));
      p->endChangeGesture();
    }
  } else {
    const auto channel = processor.parameters()
                             .getRawParameterValue(slotId(i, "MidiIn").c_str())
                             ->load();
    if (channel == 0) {
      const int free = processor.nextFreeChannel();
      if (free > 0)
        (void)processor.assignSlotChannel(
            i, free, VstEngineAudioProcessor::ChannelConflictAction::reject,
            diagnostic);
    }
    if (auto *p =
            processor.parameters().getParameter(slotId(i, "Enabled").c_str())) {
      p->beginChangeGesture();
      p->setValueNotifyingHost(1.0f);
      p->endChangeGesture();
    }
  }
  refresh();
  if (onSelected)
    onSelected(i);
}
void VstEngineAudioProcessorEditor::RackRail::showCatalog(std::size_t i) {
  juce::PopupMenu menu;
  std::vector<std::string> ids;
  for (const auto *d : processor.availableInstruments()) {
    ids.push_back(d->id);
    menu.addItem(static_cast<int>(ids.size()), d->name + " / " + d->vendor);
  }
  if (ids.empty())
    menu.addItem(1, "No installed instruments", false);
  juce::Component::SafePointer<RackRail> safe(this);
  menu.showMenuAsync(
      juce::PopupMenu::Options().withTargetComponent(&slots[i]),
      [safe, i, ids = std::move(ids)](int result) {
        if (safe && result > 0 && result <= static_cast<int>(ids.size()))
          safe->loadInstrument(i, ids[static_cast<std::size_t>(result - 1)]);
      });
}
void VstEngineAudioProcessorEditor::RackRail::showSlotMenu(std::size_t i) {
  juce::PopupMenu menu;
  menu.addItem(1, "Load / replace instrument...");
  menu.addItem(2, "Unload instrument", !slots[i].summary.instrumentId.empty());
  menu.addSeparator();
  menu.addItem(3, "Mute", true, slots[i].mute);
  menu.addItem(4, "Solo", true, slots[i].solo);
  juce::PopupMenu channels;
  channels.addItem(100, "OFF", true, slots[i].channel == "OFF");
  for (int c = 1; c <= 16; ++c)
    channels.addItem(100 + c, "CH" + juce::String(c), true,
                     slots[i].channel == "CH" + juce::String(c));
  menu.addSubMenu("MIDI input (conflicts require Routing)", channels);
  juce::Component::SafePointer<RackRail> safe(this);
  menu.showMenuAsync(
      juce::PopupMenu::Options().withTargetComponent(&slots[i]),
      [safe, i](int r) {
        if (!safe || r == 0)
          return;
        if (r == 1)
          safe->showCatalog(i);
        else if (r == 2)
          safe->loadInstrument(i, {});
        else if (r == 3 || r == 4) {
          if (auto *p = safe->processor.parameters().getParameter(
                  slotId(i, r == 3 ? "Mute" : "Solo").c_str())) {
            p->beginChangeGesture();
            p->setValueNotifyingHost(p->getValue() < 0.5f ? 1.0f : 0.0f);
            p->endChangeGesture();
          }
        } else if (r >= 100 && r <= 116) {
          juce::String d;
          if (!safe->processor.assignSlotChannel(
                  i, r - 100,
                  VstEngineAudioProcessor::ChannelConflictAction::reject, d))
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::WarningIcon, "MIDI routing conflict",
                d);
        }
        if (safe)
          safe->refresh();
      });
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
    instrument.setName("rack.instrument.selector");
    preset.setName("rack.sound.preset");
    const auto state = processor.instrumentRack().snapshotState()[selected];
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
  const auto state = processor.instrumentRack().snapshotState()[selected];
  const auto *descriptor = processor.instrumentRack().descriptor(selected);
  title.setText(descriptor != nullptr ? descriptor->name : "Empty slot",
                juce::dontSendNotification);
  subtitle.setText(descriptor != nullptr
                       ? juce::String(descriptor->vendor) + "  /  slot " +
                             juce::String(static_cast<int>(selected + 1))
                       : "Choose instrument to activate slot",
                   juce::dontSendNotification);
  const int channel = state.routing.mode == vstengine::rack::RouteMode::off
                          ? 0
                          : state.routing.channel;
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

VstEngineAudioProcessorEditor::PatternPage::PatternPage(
    VstEngineAudioProcessor &p)
    : processor(p), callbacks(p), sequencer(callbacks.edit.value, &callbacks) {
  profile.setName("rack.generator.profile");
  profile.setTextWhenNothingSelected("Generator profile");
  vstengine::ui::styleButton(generate, true);
  vstengine::ui::styleButton(mutate);
  vstengine::ui::styleLabel(status, 12.0f, juce::Justification::centredLeft,
                            vstengine::ui::colours::mutedText);
  const std::array<juce::Component *, 5> components{
      &profile, &generate, &mutate, &status, &sequencer};
  for (auto *component : components)
    addAndMakeVisible(component);
  generate.onClick =
      [this] {
        const auto index = profile.getSelectedItemIndex();
        if (index < 0 || index >= static_cast<int>(profileIds.size()))
          return;
        juce::String diagnostic;
        processor.generateSelectedPattern(profileIds[static_cast<std::size_t>(index)], diagnostic);
        status.setText(diagnostic.isEmpty() ? "Pattern generated" : diagnostic, juce::dontSendNotification);
        refresh();
      };
  mutate.onClick = [this] {
    juce::String diagnostic;
    processor.mutateSelectedPattern(true, diagnostic);
    status.setText(diagnostic.isEmpty() ? "Selected steps mutated" : diagnostic,
                   juce::dontSendNotification);
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
    callbacks.edit = processor.sequenceEditSnapshot();
    sequencer.setSequence(callbacks.edit.value);
    sequencer.setSlideEnabled((processor.selectedSequenceFieldMask() & VOX_SEQUENCE_SLIDE) != 0);
    sequencer.setEnabled(supported); generate.setEnabled(supported && !profileIds.empty());
    mutate.setEnabled(supported); sequencer.refreshFromModel();
    if (!supported) status.setText("Selected instrument has no sequence capability", juce::dontSendNotification);
}
void VstEngineAudioProcessorEditor::PatternPage::synchronize() {
  const auto latest = processor.sequenceEditSnapshot();
  if (latest.slot != callbacks.edit.slot ||
      latest.revision != callbacks.edit.revision)
    refresh();
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
    const auto state = processor.instrumentRack().snapshotState()[selected];
    midiIn.setSelectedId((state.routing.mode == vstengine::rack::RouteMode::off ? 0 : state.routing.channel) + 1,
                         juce::dontSendNotification);
}
void VstEngineAudioProcessorEditor::RoutingPage::bind(std::size_t slot)
{
    selected = slot; sliderAttachments.clear(); buttonAttachments.clear();
    const auto state = processor.instrumentRack().snapshotState()[slot];
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
  const auto runtime = processor.instrumentRack().runtimeState()[slot];
  status.setText("Slot " + juce::String(static_cast<int>(slot + 1)) +
                     " / dropped MIDI " +
                     juce::String(runtime.droppedMidiEvents),
                 juce::dontSendNotification);
}

VstEngineAudioProcessorEditor::VstEngineAudioProcessorEditor(
    VstEngineAudioProcessor &p)
    : AudioProcessorEditor(&p), processor(p),
      header(p.parameters(),
             {[this] { cycleGlobalPreset(-1); },
              [this] { cycleGlobalPreset(1); }, [this] { saveGlobalPreset(); },
              [&p] { p.requestPanic(); }, [this] { showRackSettings(); },
              [this](int index) { loadGlobalPreset(index); }}),
      rackRail(p), instrumentHeader(p), soundPage(p, "SOUND", 6),
      patternPage(p), routingPage(p), zonesPage(p), macrosPage(p, "MACROS", 8),
      advancedPage(p), pages{&soundPage, &patternPage, &routingPage,
                             &zonesPage, &macrosPage,  &advancedPage},
      keyboard(p.keyboardState(),
               juce::MidiKeyboardComponent::horizontalKeyboard) {
  setLookAndFeel(&lookAndFeel);
  addAndMakeVisible(header);
  addAndMakeVisible(rackRail);
  addAndMakeVisible(instrumentHeader);
  addAndMakeVisible(navigation);
  addAndMakeVisible(keyboard);
  for (auto *page : pages)
    addChildComponent(page);
  rackRail.onSelected = [this](std::size_t slot) { bindSelection(slot); };
  instrumentHeader.onModelChanged = [this] {
    bindSelection(processor.selectedSlotIndex());
  };
  navigation.onPageChanged = [this](Page page) { showPage(page); };
  keyboard.setAvailableRange(24, 96);
  setResizable(true, true);
  setResizeLimits(1040, 680, 1600, 1000);
  setSize(1240, 800);
  bindSelection(0);
  refreshGlobalPresets();
  showPage(Page::sound);
  startTimerHz(20);
}

VstEngineAudioProcessorEditor::~VstEngineAudioProcessorEditor()
{
  stopTimer();
  if (routingWindow)
    delete routingWindow.getComponent();
  setLookAndFeel(nullptr);
}

void VstEngineAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(vstengine::ui::colours::background);
}

void VstEngineAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();
    header.setBounds(area.removeFromTop(58));
    auto rail = area.removeFromLeft(245).reduced(8, 6);
    rackRail.setBounds(rail);
    auto keyboardArea = area.removeFromBottom(92).reduced(10, 8);
    keyboard.setBounds(keyboardArea);
    keyboard.setKeyWidth(static_cast<float>(keyboardArea.getWidth()) / 43.0f);

    area = area.reduced(6, 8);
    instrumentHeader.setBounds(area.removeFromTop(66));
    navigation.setBounds(area.removeFromTop(44).reduced(0, 4));
    for (auto* page : pages) page->setBounds(area);
}

void VstEngineAudioProcessorEditor::bindSelection(std::size_t slot)
{
    processor.selectSlot(slot);
    const auto state = processor.instrumentRack().snapshotState()[slot];
    boundSlot = slot;
    boundSlotId = state.slotId;
    boundInstrument = state.instrumentId;
    boundDescriptor = processor.instrumentRack().descriptor(slot);
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

void VstEngineAudioProcessorEditor::showOperationError(
    const juce::String &message) {
  juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                         "Rack operation failed", message);
}
void VstEngineAudioProcessorEditor::showRackSettings() {
  juce::PopupMenu menu, modes;
  auto *mode = processor.parameters().getParameter("midiMode");
  const auto current =
      juce::roundToInt(mode->convertFrom0to1(mode->getValue()));
  const juce::StringArray names{"Auto", "Host MIDI", "Internal patterns",
                                "Host + internal"};
  for (int i = 0; i < names.size(); ++i)
    modes.addItem(10 + i, names[i], true, current == i);
  menu.addSubMenu("MIDI source", modes);
  juce::PopupMenu sizes;
  sizes.addItem(30, "Compact 1040 x 680");
  sizes.addItem(31, "Reference 1240 x 800");
  sizes.addItem(32, "Large 1500 x 920");
  menu.addSubMenu("Editor size", sizes);
  menu.addItem(50, "Rack audio routing / sends / ducking...");
  menu.addItem(51, "Export selected pattern MIDI...");
  menu.addSeparator();
  menu.addItem(40, "About / host information");
  juce::Component::SafePointer<VstEngineAudioProcessorEditor> safe(this);
  menu.showMenuAsync(
      juce::PopupMenu::Options().withTargetComponent(&header), [safe](int r) {
        if (!safe)
          return;
        if (r >= 10 && r < 14) {
          if (auto *p = safe->processor.parameters().getParameter("midiMode")) {
            p->beginChangeGesture();
            p->setValueNotifyingHost(
                p->convertTo0to1(static_cast<float>(r - 10)));
            p->endChangeGesture();
          }
        } else if (r == 30)
          safe->setSize(1040, 680);
        else if (r == 31)
          safe->setSize(1240, 800);
        else if (r == 32)
          safe->setSize(1500, 920);
        else if (r == 50) {
          if (safe->routingWindow) {
            safe->routingWindow->toFront(true);
            return;
          }
          juce::DialogWindow::LaunchOptions options;
          options.content.setOwned(new RackRoutingPanel(safe->processor));
          options.dialogTitle = "VOX / Rack Routing";
          options.dialogBackgroundColour = vstengine::ui::colours::background;
          options.componentToCentreAround = safe.getComponent();
          options.useNativeTitleBar = true;
          options.resizable = true;
          safe->routingWindow = options.launchAsync();
        } else if (r == 51) {
          safe->exportSelectedPattern();
        } else if (r == 40)
          juce::AlertWindow::showMessageBoxAsync(
              juce::MessageBoxIconType::InfoIcon, "Vox Electronic Engine",
              juce::String("Windows x64 VST3 / offline generation\nHost: ") +
                  juce::PluginHostType().getHostDescription() +
                  "\nSample rate: " +
                  juce::String(safe->processor.getSampleRate(), 0) +
                  " Hz\nMaximum block: " +
                  juce::String(safe->processor.getBlockSize()) +
                  " samples\n16 instrument slots / independent slot power and "
                  "MIDI routing");
      });
}

void VstEngineAudioProcessorEditor::exportSelectedPattern() {
  if (exportChooserActive)
    return;
  juce::MemoryBlock bytes;
  juce::String diagnostic;
  const auto slot = processor.selectedSlotIndex();
  if (!processor.serializeSlotPatternMidi(slot, bytes, diagnostic)) {
    showOperationError(diagnostic);
    return;
  }
  // Snapshot bytes before opening the dialog: later selection/generation or
  // host restore cannot silently switch the sound/pattern being exported.
  const auto suggested =
      juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
          .getChildFile("Vox-Slot-" + juce::String(static_cast<int>(slot + 1)) +
                        ".mid");
  exportChooser = std::make_unique<juce::FileChooser>(
      "Export slot pattern MIDI", suggested, "*.mid");
  exportChooserActive = true;
  juce::Component::SafePointer<VstEngineAudioProcessorEditor> safe(this);
  exportChooser->launchAsync(
      juce::FileBrowserComponent::saveMode |
          juce::FileBrowserComponent::canSelectFiles |
          juce::FileBrowserComponent::warnAboutOverwriting,
      [safe, bytes = std::move(bytes)](const juce::FileChooser &chooser) {
        if (!safe)
          return;
        safe->exportChooserActive = false;
        auto file = chooser.getResult();
        if (file == juce::File{})
          return;
        if (!file.hasFileExtension("mid;midi")) {
          safe->showOperationError("Choose a filename ending in .mid or .midi");
          return;
        }
        juce::String diagnostic;
        if (!safe->processor.writePatternMidiFile(file, bytes, diagnostic))
          safe->showOperationError(diagnostic);
      });
}

void VstEngineAudioProcessorEditor::cycleGlobalPreset(int delta)
{
    auto* manager = processor.presetManager();
    if (manager == nullptr) return;
    auto presets = manager->getPresets();
    for (int i = presets.size(); --i >= 0;)
      if (presets.getReference(i).kind !=
          vstengine::PresetManager::PresetKind::full)
        presets.remove(i);
    if (presets.isEmpty()) return;
    int current = -1;
    for (int i = 0; i < presets.size(); ++i)
        if (presets.getReference(i).name == manager->getCurrentPresetName()) current = i;
    const int next = (current + delta + presets.size()) % presets.size();
    const auto result = manager->loadPreset(presets.getReference(next));
    if (!result) {
      showOperationError(result.message);
      return;
    }
    bindSelection(processor.selectedSlotIndex());
    refreshGlobalPresets();
}

void VstEngineAudioProcessorEditor::loadGlobalPreset(int index)
{
    auto* manager = processor.presetManager();
    if (manager == nullptr) return;
    auto presets = manager->getPresets();
    for (int i = presets.size(); --i >= 0;)
      if (presets.getReference(i).kind !=
          vstengine::PresetManager::PresetKind::full)
        presets.remove(i);
    if (index < 0 || index >= presets.size()) return;
    const auto result = manager->loadPreset(presets.getReference(index));
    if (!result) {
      showOperationError(result.message);
      return;
    }
    bindSelection(processor.selectedSlotIndex());
    refreshGlobalPresets();
}

void VstEngineAudioProcessorEditor::saveGlobalPreset()
{
  auto dialog = std::make_shared<juce::AlertWindow>(
      "Save Rack scene", "Save all slots, patterns, routing and effects.",
      juce::MessageBoxIconType::NoIcon);
  dialog->addTextEditor(
      "name", "Rack " + juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S"),
      "Name");
  dialog->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
  dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
  juce::Component::SafePointer<VstEngineAudioProcessorEditor> safe(this);
  dialog->enterModalState(
      true, juce::ModalCallbackFunction::create([safe, dialog](int r) {
        if (!safe || r != 1)
          return;
        if (auto *manager = safe->processor.presetManager()) {
          const auto result = manager->saveFullPreset(
              dialog->getTextEditorContents("name").trim());
          if (!result)
            safe->showOperationError(result.message);
          else
            safe->refreshGlobalPresets();
        }
      }),
      false);
}

void VstEngineAudioProcessorEditor::refreshGlobalPresets()
{
    auto* manager = processor.presetManager();
    if (manager == nullptr) return;
    auto presets = manager->getPresets();
    for (int i = presets.size(); --i >= 0;)
      if (presets.getReference(i).kind !=
          vstengine::PresetManager::PresetKind::full)
        presets.remove(i);
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
  processor.serviceKeyboardUi();
  const auto selected = processor.selectedSlotIndex();
  const auto state = processor.instrumentRack().snapshotState()[selected];
  if (selected != boundSlot || state.slotId != boundSlotId ||
      state.instrumentId != boundInstrument ||
      processor.instrumentRack().descriptor(selected) != boundDescriptor)
    bindSelection(selected);
  rackRail.refresh();
  instrumentHeader.refresh();
  patternPage.synchronize();
  patternPage.setPlayHead(processor.getCurrentPlayHeadStep());
  header.setMidiActivity(processor.hasRecentMidiActivity());
  header.setCpuLoad(processor.currentCpuLoad());
  header.setTransportActive(processor.transportIsPlaying());
  const auto peaks = processor.consumeOutputPeaks();
  header.setOutputPeaks(peaks[0], peaks[1]);
  if (auto *manager = processor.presetManager())
    header.setPresetName(manager->getCurrentPresetName());
}

juce::AudioProcessorEditor* VstEngineAudioProcessor::createEditor()
{
    return new VstEngineAudioProcessorEditor(*this);
}
