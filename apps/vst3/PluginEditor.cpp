#include "PluginEditor.h"
#include "RackRoutingPanel.h"
#include "instrument/HostParameterSchema.h"
#include "ui/common/UiComponents.h"
#include "vox-ui/Tokens.h"
#include "vox-ui/Typography.h"
#include "vox-ui/VoxPaint.h"
#include "ui/InstrumentIdentity.h"
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
    vox::ui::paint::panel(g, bounds.toFloat());
}

constexpr int sectionHeader = 30;

// Page body: outer panel plus titled raised sections.
template <typename SectionList>
void pageWithSections(juce::Graphics& g, juce::Rectangle<int> bounds, const SectionList& sections)
{
    panel(g, bounds);
    for (const auto& [area, heading] : sections) {
        vox::ui::paint::panel(g, area.toFloat(), true);
        vox::ui::paint::sectionTitle(g, area.withHeight(sectionHeader).reduced(12, 0), heading,
                                     false, false);
    }
}

// Channel picker: Off, this slot's own channel and channels no other
// instrument owns (one instrument = one MIDI channel).
void fillChannelChoices(juce::ComboBox& combo, const VstEngineAudioProcessor& processor,
                        std::size_t slot, int current)
{
    if (combo.isPopupActive())
        return;
    combo.clear(juce::dontSendNotification);
    combo.addItem("OFF", 1);
    for (int channel = 1; channel <= 16; ++channel)
        if (channel == current || processor.channelAvailable(slot, channel))
            combo.addItem("CH" + juce::String(channel), channel + 1);
    combo.setSelectedId(current + 1, juce::dontSendNotification);
}

// Caption above a knob or field.
void caption(juce::Label& label, const juce::String& text)
{
    label.setText(text, juce::dontSendNotification);
    label.setFont(vox::ui::paint::spaced(10.0f, 0.12f, true));
    label.setJustificationType(juce::Justification::centred);
    label.setColour(juce::Label::textColourId, juce::Colour(0xff9ab2c5));
    label.setInterceptsMouseClicks(false, false);
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
  power.setBounds(getWidth() - 30, 0, 28, getHeight());
}
void VstEngineAudioProcessorEditor::RackRail::SlotRow::mouseDown(
    const juce::MouseEvent &event) {
  if (onSelect)
    onSelect(event.mods.isPopupMenu());
}
void VstEngineAudioProcessorEditor::RackRail::SlotRow::paint(
    juce::Graphics &g) {
  using namespace vox::ui::tokens;
  namespace paint = vox::ui::paint;
  const bool occupied = !summary.instrumentId.empty();
  const auto area = getLocalBounds().toFloat().reduced(1.0f);
  if (selected) {
    g.setGradientFill(juce::ColourGradient(
        colour::accent.withAlpha(0.16f), area.getX(), area.getY(),
        colour::panelRaised, area.getRight(), area.getY(), false));
    g.fillRoundedRectangle(area, radius::small);
    paint::selectedOutline(g, area);
  } else {
    g.setColour(occupied ? colour::control : colour::background.brighter(0.02f));
    g.fillRoundedRectangle(area, radius::small);
    g.setColour(occupied ? colour::border : colour::borderSubtle);
    g.drawRoundedRectangle(area.reduced(0.5f), radius::small, 1.0f);
  }
  if (hasKeyboardFocus(false) && !selected) {
    g.setColour(colour::accentDim);
    g.drawRoundedRectangle(area.reduced(0.5f), radius::small, 1.2f);
  }

  auto content = getLocalBounds().reduced(8, 0);
  content.removeFromRight(30); // power
  const auto big = juce::jlimit(12.0f, 15.0f, getHeight() * 0.36f);
  g.setFont(paint::spaced(big, 0.02f));
  g.setColour(selected ? colour::text
                       : (occupied ? colour::textSecondary : colour::textMuted));
  g.drawText(juce::String(static_cast<int>(index + 1)).paddedLeft('0', 2),
             content.removeFromLeft(24), juce::Justification::centredLeft);

  const auto channelArea = content.removeFromRight(40);
  if (occupied) {
    const auto thumbHeight = static_cast<float>(getHeight()) - 10.0f;
    auto thumb = content.removeFromLeft(juce::roundToInt(thumbHeight * 1.3f))
                     .toFloat()
                     .withSizeKeepingCentre(thumbHeight * 1.3f, thumbHeight);
    vstengine::ui::identity::drawThumbnail(
        g, thumb, vstengine::ui::identity::forInstrument(summary.instrumentId));
    if (!enabled) {
      g.setColour(colour::background.withAlpha(0.55f));
      g.fillRoundedRectangle(thumb, 3.0f);
    }
    content.removeFromLeft(8);
  }

  const bool twoLines = getHeight() >= 34;
  auto first = twoLines ? content.removeFromTop(getHeight() / 2 + 1)
                        : content;
  g.setFont(vox::ui::typography::makeFont(juce::jlimit(11.5f, 13.5f, big - 0.5f)));
  g.setColour(occupied ? (enabled ? colour::text : colour::textSecondary)
                       : colour::textMuted);
  g.drawFittedText(occupied ? juce::String(summary.name) : juce::String("Empty"),
                   first.withTrimmedBottom(twoLines ? 1 : 0),
                   twoLines ? juce::Justification::bottomLeft
                            : juce::Justification::centredLeft,
                   1, 0.85f);
  auto channelText = channelArea;
  g.setFont(vox::ui::typography::makeFont(11.5f));
  g.setColour(occupied ? colour::textSecondary : colour::textMuted);
  g.drawText(channel,
             twoLines ? channelText.removeFromTop(getHeight() / 2 + 1)
                            .withTrimmedBottom(1)
                      : channelText,
             twoLines ? juce::Justification::bottomRight
                      : juce::Justification::centredRight);
  if (twoLines && (mute || solo)) {
    g.setFont(paint::spaced(8.5f, 0.12f, true));
    g.setColour(mute ? colour::danger : colour::warning);
    g.drawText(mute ? "MUTE" : "SOLO", channelText.withTrimmedTop(2),
               juce::Justification::topRight);
  }
  if (occupied && twoLines) {
    const bool resolved =
        summary.resolution == vstengine::instrument::ResolutionStatus::resolved;
    g.setFont(vox::ui::typography::makeFont(10.5f));
    g.setColour(resolved ? colour::textSecondary : colour::warning);
    g.drawFittedText(resolved
                         ? juce::String(summary.vendor) + " " +
                               juce::String(summary.instrumentVersion) + ".0"
                         : juce::String("Unavailable / state preserved"),
                     content.withTrimmedTop(2), juce::Justification::topLeft, 1,
                     0.85f);
  }
}
VstEngineAudioProcessorEditor::RackRail::RackRail(VstEngineAudioProcessor& p) : processor(p)
{
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
  using namespace vox::ui::tokens;
  panel(g, getLocalBounds());
  g.setFont(vox::ui::paint::spaced(12.0f, 0.06f));
  g.setColour(colour::text);
  g.drawText("INSTRUMENT RACK", headingBounds, juce::Justification::centredLeft);
  g.setFont(vox::ui::typography::makeFont(11.0f));
  g.setColour(colour::textSecondary);
  g.drawText("16 slots", headingBounds.withTrimmedRight(32),
             juce::Justification::centredRight);
  if (artBounds.getHeight() >= 64) {
    auto art = artBounds.toFloat();
    vstengine::ui::identity::drawMountains(g, art, colour::accent);
    g.setColour(colour::text.withAlpha(0.85f));
    g.setFont(vox::ui::paint::spaced(9.5f, 0.5f));
    const auto lines = juce::StringArray{"SOUND", "FOR A DIFFERENT", "TOMORROW"};
    auto text = art.removeFromBottom(juce::jmin(54.0f, art.getHeight() * 0.5f))
                    .toNearestInt();
    const int lineHeight = text.getHeight() / 3;
    for (const auto &line : lines)
      g.drawText(line, text.removeFromTop(lineHeight),
                 juce::Justification::centred);
    g.setColour(colour::border);
    g.drawRoundedRectangle(artBounds.toFloat().reduced(0.5f), radius::small, 1.0f);
  }
}
void VstEngineAudioProcessorEditor::RackRail::resized()
{
  auto a = getLocalBounds().reduced(8, 6);
  headingBounds = a.removeFromTop(32);
  add.setBounds(headingBounds.withLeft(headingBounds.getRight() - 26)
                    .withSizeKeepingCentre(26, 24));
  a.removeFromTop(2);
  const int h = juce::jlimit(30, 46, (a.getHeight() - 70) / 16);
  for (auto &row : slots) {
    row.setBounds(a.removeFromTop(h).reduced(0, 2));
  }
  a.removeFromTop(6);
  artBounds = a;
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
  // Channels owned by another instrument are listed but disabled.
  for (int c = 1; c <= 16; ++c)
    channels.addItem(100 + c, "CH" + juce::String(c),
                     processor.channelAvailable(i, c),
                     slots[i].channel == "CH" + juce::String(c));
  menu.addSubMenu("MIDI channel", channels);
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
          if (!safe->processor.assignSlotChannel(i, r - 100, d))
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::WarningIcon, "MIDI channel", d);
        }
        if (safe)
          safe->refresh();
      });
}

VstEngineAudioProcessorEditor::InstrumentHeader::InstrumentHeader(VstEngineAudioProcessor& p)
    : processor(p)
{
    vstengine::ui::styleLabel(title, 26.0f, juce::Justification::bottomLeft,
                              vstengine::ui::colours::text);
    title.setFont(vox::ui::paint::spaced(27.0f, 0.02f, true));
    title.setMinimumHorizontalScale(0.7f);
    vstengine::ui::styleLabel(subtitle, 12.0f, juce::Justification::topLeft,
                              vox::ui::tokens::colour::textSecondary);
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
        if (!processor.assignSlotChannel(selected, requested, diagnostic))
            refresh();
        if (onModelChanged) onModelChanged();
    };
}

void VstEngineAudioProcessorEditor::InstrumentHeader::paint(juce::Graphics& g)
{
    panel(g, getLocalBounds());
    g.setFont(vox::ui::typography::makeFont(12.5f));
    g.setColour(vox::ui::tokens::colour::textSecondary);
    g.drawText("Channel", midiIn.getBounds().withX(midiIn.getX() - 62).withWidth(56),
               juce::Justification::centredRight);
}

void VstEngineAudioProcessorEditor::InstrumentHeader::resized()
{
    auto area = getLocalBounds().reduced(16, 8);
    const auto control = [](juce::Rectangle<int> r) {
        return r.withSizeKeepingCentre(r.getWidth(), 32);
    };
    midiIn.setBounds(control(area.removeFromRight(86)));
    area.removeFromRight(70); // "Channel" caption
    presetNext.setBounds(control(area.removeFromRight(34)));
    area.removeFromRight(4);
    preset.setBounds(control(area.removeFromRight(juce::jlimit(120, 200, area.getWidth() / 4))));
    area.removeFromRight(4);
    presetPrevious.setBounds(control(area.removeFromRight(34)));
    area.removeFromRight(10);
    instrument.setBounds(control(area.removeFromRight(juce::jlimit(120, 200, area.getWidth() / 3))));
    area.removeFromRight(12);
    title.setBounds(area.removeFromTop(area.getHeight() * 3 / 5));
    subtitle.setBounds(area);
}

void VstEngineAudioProcessorEditor::InstrumentHeader::bind(std::size_t slot)
{
    selected = slot;
    instrument.setName("rack.instrument.selector");
    preset.setName("rack.sound.preset");
    title.setName("rack.instrument.title");
    subtitle.setName("rack.instrument.subtitle");
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
  const int channel = state.routing.mode == vstengine::rack::RouteMode::off
                          ? 0
                          : state.routing.channel;
  const auto slotText = "Slot " + juce::String(static_cast<int>(selected + 1));
  const auto channelText = channel == 0 ? juce::String("MIDI off")
                                        : "CH" + juce::String(channel);
  subtitle.setText(descriptor != nullptr
                       ? slotText + "   |   " + channelText + "   |   " +
                             juce::String(descriptor->vendor) + " " +
                             juce::String(descriptor->instrumentVersion) + ".0"
                       : slotText + "   |   Choose an instrument to activate this slot",
                   juce::dontSendNotification);
  fillChannelChoices(midiIn, processor, selected, channel);
}

VstEngineAudioProcessorEditor::MacroPage::MacroPage(
    VstEngineAudioProcessor& p, juce::String heading, std::size_t visibleCount,
    InstrumentControls instrumentControls)
    : processor(p), count(juce::jlimit<std::size_t>(1, knobs.size(), visibleCount)),
      hasInstrumentControls(instrumentControls != InstrumentControls::none),
      layoutMode(instrumentControls == InstrumentControls::layout)
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
        labels[i].setFont(vox::ui::paint::spaced(11.0f, 0.12f, true));
        addChildComponent(knobs[i]); addChildComponent(labels[i]);
        knobs[i].setVisible(i < count); labels[i].setVisible(i < count);
    }
    if (hasInstrumentControls) {
        directView.setName(layoutMode ? "rack.sound.layout" : "rack.direct.controls");
        directView.setViewedComponent(&directContent, false);
        directView.setScrollBarsShown(true, false);
        addAndMakeVisible(directView);
        directEmpty.setName(layoutMode ? "rack.sound.empty" : "rack.direct.empty");
        vstengine::ui::styleLabel(directEmpty, 12.0f, juce::Justification::centredLeft,
                                  vstengine::ui::colours::mutedText);
        directEmpty.setText("No instrument controls", juce::dontSendNotification);
        addAndMakeVisible(directEmpty);
        directView.setVisible(!layoutMode);
        directEmpty.setVisible(!layoutMode);
    }
}

void VstEngineAudioProcessorEditor::MacroPage::paint(juce::Graphics& g)
{
    panel(g, getLocalBounds());
    if (!groupBounds.isEmpty())
        vox::ui::paint::panel(g, groupBounds.toFloat(), true);
    if (hasInstrumentControls && (!layoutMode || !directSectionStarts.empty())
        && directBounds.getHeight() >= sectionHeader) {
        vox::ui::paint::panel(g, directBounds.toFloat(), true);
        vox::ui::paint::sectionTitle(g, directBounds.withHeight(sectionHeader).reduced(12, 0),
                                     directHeading, false, false);
    }
}

void VstEngineAudioProcessorEditor::MacroPage::resized()
{
    auto area = getLocalBounds().reduced(18);
    title.setBounds(area.removeFromTop(26)); description.setBounds(area.removeFromTop(22));
    area.removeFromTop(14);
    // A declarative layout replaces the macros: the page shows exactly the
    // instrument's own controls.
    const bool showLayout = layoutMode && !directSectionStarts.empty();
    for (std::size_t i = 0; i < count; ++i) {
        knobs[i].setVisible(!showLayout); labels[i].setVisible(!showLayout);
    }
    directView.setVisible(!layoutMode || showLayout);
    if (showLayout) {
        groupBounds = {};
        directBounds = area;
        layoutInstrumentControls();
        return;
    }
    if (layoutMode) {
        directBounds = {};
        directEmpty.setVisible(false);
    }
    // With instrument controls present the macro knobs take a single row so
    // the controls section keeps at least one visible row at 1040 x 680.
    const bool compactMacros = hasInstrumentControls && !layoutMode && !directControls.empty();
    const int columns = compactMacros ? static_cast<int>(count)
                        : count <= 6 ? static_cast<int>(count) : 4;
    const int rows = static_cast<int>((count + columns - 1) / columns);
    // Knobs keep an instrument-panel scale instead of filling the page.
    const int cellWidth = juce::jmin(150, (area.getWidth() - (compactMacros ? 24 : 0)) / columns);
    const int knobSize = juce::jlimit(56, 104, cellWidth - 34);
    const int cellHeight = 26 + knobSize + 28;
    groupBounds = area.withSizeKeepingCentre(cellWidth * columns + 24, cellHeight * rows + 24)
                      .withY(area.getY());
    const auto inner = groupBounds.reduced(12);
    for (std::size_t i = 0; i < count; ++i) {
        auto cell = juce::Rectangle<int>(inner.getX() + static_cast<int>(i % columns) * cellWidth,
            inner.getY() + static_cast<int>(i / columns) * cellHeight, cellWidth, cellHeight);
        labels[i].setBounds(cell.removeFromTop(26));
        knobs[i].setBounds(cell.withSizeKeepingCentre(juce::jmax(knobSize, 76), cell.getHeight()));
    }
    if (!hasInstrumentControls || layoutMode)
        return;
    area.removeFromTop(juce::jmin(area.getHeight(), groupBounds.getHeight() + 10));
    directBounds = area;
    layoutInstrumentControls();
}

void VstEngineAudioProcessorEditor::MacroPage::layoutInstrumentControls()
{
    constexpr int cellWidth = 96, cellHeight = 110;
    if (directBounds.getHeight() < sectionHeader + 24) {
        directView.setBounds({});
        directEmpty.setBounds({});
        return;
    }
    auto body = directBounds.withTrimmedTop(sectionHeader).reduced(12, 6);
    directEmpty.setBounds(body.withHeight(juce::jmin(24, body.getHeight())));
    directEmpty.setVisible(directControls.empty());
    directView.setVisible(!directControls.empty());
    directView.setBounds(body);
    const int width = juce::jmax(cellWidth, body.getWidth() - directView.getScrollBarThickness());
    const int columns = juce::jmax(1, width / cellWidth);
    const int cell = width / columns;
    const int rows = static_cast<int>((directControls.size() + static_cast<std::size_t>(columns) - 1)
                                      / static_cast<std::size_t>(columns));
    // Cell origin of every control: one grid for the generic list, or each
    // layout section under its own title, starting on a new row.
    std::vector<juce::Point<int>> origins(directControls.size());
    int height = rows * cellHeight;
    if (directSectionStarts.empty()) {
        for (std::size_t i = 0; i < directControls.size(); ++i)
            origins[i] = { static_cast<int>(i % static_cast<std::size_t>(columns)) * cell,
                           static_cast<int>(i / static_cast<std::size_t>(columns)) * cellHeight };
    } else {
        constexpr int titleHeight = 24;
        int y = 0;
        for (std::size_t s = 0; s < directSectionStarts.size(); ++s) {
            const auto first = directSectionStarts[s];
            const auto last = s + 1 < directSectionStarts.size() ? directSectionStarts[s + 1]
                                                                 : directControls.size();
            directSectionTitles[s]->setBounds(4, y, width - 8, titleHeight);
            y += titleHeight;
            for (auto i = first; i < last; ++i)
                origins[i] = { static_cast<int>((i - first) % static_cast<std::size_t>(columns)) * cell,
                               y + static_cast<int>((i - first) / static_cast<std::size_t>(columns)) * cellHeight };
            y += static_cast<int>((last - first + static_cast<std::size_t>(columns) - 1)
                                  / static_cast<std::size_t>(columns)) * cellHeight;
        }
        height = y;
    }
    directContent.setSize(width, height);
    for (std::size_t i = 0; i < directControls.size(); ++i) {
        auto bounds = juce::Rectangle<int>(origins[i].x, origins[i].y, cell, cellHeight).reduced(4, 2);
        directLabels[i]->setBounds(bounds.removeFromTop(24));
        auto *control = directControls[i].get();
        if (dynamic_cast<juce::Slider *>(control) != nullptr)
            control->setBounds(bounds.withSizeKeepingCentre(juce::jmin(bounds.getWidth(), 80),
                                                            bounds.getHeight()));
        else if (dynamic_cast<juce::ToggleButton *>(control) != nullptr)
            control->setBounds(bounds.withSizeKeepingCentre(juce::jmin(bounds.getWidth(), 40), 28));
        else
            control->setBounds(bounds.withSizeKeepingCentre(bounds.getWidth(), 26));
    }
}

void VstEngineAudioProcessorEditor::MacroPage::bindInstrumentControls(std::size_t slot)
{
    // Attachments capture the pool parameter's range at construction and the
    // range follows the bound instrument, so every bind rebuilds the section.
    directSliderAttachments.clear();
    directButtonAttachments.clear();
    directComboAttachments.clear();
    directControls.clear();
    directLabels.clear();
    directSectionTitles.clear();
    directSectionStarts.clear();
    const auto *descriptor = processor.instrumentRack().descriptor(slot);
    auto &state = processor.parameters();
    // One real control bound to the parameter's pool endpoint; its widget
    // follows the parameter type (a layout's kind was checked to match it).
    const auto add = [&](const vstengine::instrument::ParameterDescriptor &parameter,
                         const std::string &caption_) {
        if (parameter.poolIndex < 0 ||
            parameter.poolIndex >= static_cast<std::int16_t>(
                                       vstengine::instrument::poolParametersPerSlot))
            return;
        const auto id = vstengine::instrument::hostparams::poolId(
            slot, static_cast<std::size_t>(parameter.poolIndex));
        if (state.getParameter(id) == nullptr)
            return;
        auto text = juce::String(parameter.name);
        if (!parameter.unit.empty())
            text << " (" << juce::String(parameter.unit) << ")";
        auto label = std::make_unique<juce::Label>();
        caption(*label, caption_.empty() ? text.toUpperCase() : juce::String(caption_));
        label->setTooltip(text);
        std::unique_ptr<juce::Component> control;
        switch (parameter.type) {
        case vstengine::instrument::ParameterType::boolean: {
            auto button = std::make_unique<juce::ToggleButton>();
            button->setTooltip(text);
            directButtonAttachments.push_back(
                std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
                    state, id, *button));
            control = std::move(button);
            break;
        }
        case vstengine::instrument::ParameterType::choice: {
            auto combo = std::make_unique<juce::ComboBox>();
            int itemId = 1;
            for (const auto &choice : parameter.choices)
                combo->addItem(juce::String(choice), itemId++);
            combo->setTooltip(text);
            directComboAttachments.push_back(
                std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
                    state, id, *combo));
            control = std::move(combo);
            break;
        }
        case vstengine::instrument::ParameterType::floating:
        case vstengine::instrument::ParameterType::integer:
        default: {
            auto slider = std::make_unique<juce::Slider>();
            knob(*slider);
            slider->setTooltip(text);
            directSliderAttachments.push_back(
                std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
                    state, id, *slider));
            control = std::move(slider);
            break;
        }
        }
        control->setName(id);
        directContent.addAndMakeVisible(*label);
        directContent.addAndMakeVisible(*control);
        directLabels.push_back(std::move(label));
        directControls.push_back(std::move(control));
    };
    directHeading = "INSTRUMENT CONTROLS";
    if (descriptor != nullptr && descriptor->directParameters) {
        if (const auto layout = layoutMode ? descriptor->soundLayout : nullptr) {
            directHeading = juce::String(descriptor->name).toUpperCase();
            for (const auto &section : layout->sections) {
                auto title = std::make_unique<juce::Label>();
                caption(*title, juce::String(section.title));
                title->setJustificationType(juce::Justification::centredLeft);
                title->setColour(juce::Label::textColourId, vox::ui::tokens::colour::accent);
                directContent.addAndMakeVisible(*title);
                directSectionTitles.push_back(std::move(title));
                directSectionStarts.push_back(directControls.size());
                for (const auto &control : section.controls)
                    for (const auto &parameter : descriptor->parameters)
                        if (parameter.id == control.parameter)
                            add(parameter, control.label);
            }
        } else if (!layoutMode) {
            for (const auto &parameter : descriptor->parameters)
                add(parameter, {});
        }
    }
    directView.setViewPosition(0, 0);
    resized();
    repaint();
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
        labels[macro].setText(label.toUpperCase(), juce::dontSendNotification);
        knobs[macro].setTooltip("Macro " + juce::String(static_cast<int>(macro + 1)) + ": " + label);
        // An empty slot has nothing to control: its macros are inert.
        knobs[macro].setName("rack." + title.getText().toLowerCase() + ".macro." + juce::String(static_cast<int>(macro + 1)));
        knobs[macro].setEnabled(descriptor != nullptr);
        attachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            processor.parameters(), vstengine::instrument::hostparams::macroId(slot, macro), knobs[macro]));
    }
    if (hasInstrumentControls)
        bindInstrumentControls(slot);
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
  addChildComponent(grid);
  grid.onEdit = [this](const vstengine::pattern::Pattern &edited, juce::String &why) {
    std::string diagnostic;
    const bool ok = processor.instrumentRack().setPattern(processor.selectedSlotIndex(),
                                                          edited, diagnostic);
    why = diagnostic;
    return ok;
  };
  grid.onFactory = [this](std::size_t index, juce::String &why) {
    std::string diagnostic;
    const bool ok = processor.instrumentRack().loadFactoryPattern(processor.selectedSlotIndex(),
                                                                  index, diagnostic);
    why = diagnostic;
    return ok;
  };
  grid.onReload = [this](vstengine::pattern::Pattern &out) {
    return processor.instrumentRack().pattern(processor.selectedSlotIndex(), out);
  };
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

void VstEngineAudioProcessorEditor::PatternPage::paint(juce::Graphics& g)
{
    pageWithSections(g, getLocalBounds(), sections);
}
void VstEngineAudioProcessorEditor::PatternPage::resized()
{
    auto area = getLocalBounds().reduced(12);
    const auto generator = area.removeFromTop(sectionHeader + 42);
    sections = {{generator, "GENERATOR"}};
    auto tools = generator.withTrimmedTop(sectionHeader).reduced(10, 0).withTrimmedBottom(8);
    profile.setBounds(tools.removeFromLeft(220).reduced(2));
    generate.setBounds(tools.removeFromLeft(105).reduced(2));
    mutate.setBounds(tools.removeFromLeft(145).reduced(2));
    status.setBounds(tools.reduced(8, 0));
    area.removeFromTop(10);
    sequencer.setBounds(area);
    if (gridMode) {
      sections = {{getLocalBounds().reduced(12), "PATTERN"}};
      grid.setBounds(getLocalBounds().reduced(12).withTrimmedTop(sectionHeader));
    }
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
    const auto slot = processor.selectedSlotIndex();
    const auto* descriptor = processor.instrumentRack().descriptor(slot);
    const bool internal = descriptor != nullptr && descriptor->patternContent != nullptr
        && descriptor->patternContent->caps.playback == VOX_PATTERN_PLAYBACK_INTERNAL;
    if (internal != gridMode || descriptor != gridDescriptor || slot != gridSlot) {
      gridMode = internal;
      gridDescriptor = descriptor;
      gridSlot = slot;
      for (auto *c : std::array<juce::Component *, 5>{&profile, &generate, &mutate, &status, &sequencer})
        c->setVisible(!gridMode);
      grid.setVisible(gridMode);
      if (gridMode)
        grid.setContent(descriptor->patternContent);
      else
        grid.setContent(nullptr);
      resized();
    } else if (gridMode) {
      grid.reload(); // the same instrument: keep the shown bar
    }
    if (gridMode)
      return;
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
    vstengine::ui::styleLabel(channelHint, 11.0f, juce::Justification::centredLeft,
                              vstengine::ui::colours::mutedText);
    channelHint.setText("One instrument, one channel. Only free channels are listed.",
                        juce::dontSendNotification);
    for (auto* slider : { &level, &pan }) knob(*slider);
    pan.getProperties().set("vox.bipolar", true);
    caption(levelLabel, "LEVEL"); caption(panLabel, "PAN");
    const std::array<juce::Component*, 12> components {
        &title, &status, &midiIn, &channelHint, &enabled, &mute, &solo, &locked,
        &level, &pan, &levelLabel, &panLabel
    };
    for (auto* component : components) addAndMakeVisible(component);
    midiIn.onChange = [this] { assign(midiIn.getSelectedId() - 1); };
}
void VstEngineAudioProcessorEditor::RoutingPage::paint(juce::Graphics& g)
{
    pageWithSections(g, getLocalBounds(), sections);
}
void VstEngineAudioProcessorEditor::RoutingPage::resized()
{
    auto area = getLocalBounds().reduced(18);
    title.setBounds(area.removeFromTop(30));
    area.removeFromTop(8);
    auto top = area.removeFromTop(sectionHeader + 52);
    const auto input = top.removeFromLeft(juce::jmin(420, top.getWidth() / 2 - 6));
    top.removeFromLeft(12);
    const auto state = top;
    area.removeFromTop(12);
    const auto mix = area.removeFromTop(sectionHeader + 150).withWidth(juce::jmin(320, area.getWidth()));
    sections = {{input, "MIDI INPUT"}, {state, "SLOT STATE"}, {mix, "MIX"}};

    auto route = input.withTrimmedTop(sectionHeader).reduced(10, 10);
    midiIn.setBounds(route.removeFromLeft(110).reduced(2));
    route.removeFromLeft(10);
    channelHint.setBounds(route);

    auto toggles = state.withTrimmedTop(sectionHeader).reduced(10, 10);
    const int toggle = toggles.getWidth() / 4;
    for (auto* button : { &enabled, &mute, &solo, &locked })
        button->setBounds(toggles.removeFromLeft(toggle).reduced(2));

    auto knobs = mix.withTrimmedTop(sectionHeader).reduced(14, 6);
    for (auto [slider, label] : { std::pair { &level, &levelLabel }, std::pair { &pan, &panLabel } }) {
        auto cell = knobs.removeFromLeft(knobs.getWidth() >= 260 ? 140 : knobs.getWidth() / 2);
        label->setBounds(cell.removeFromTop(18));
        slider->setBounds(cell.withSizeKeepingCentre(100, cell.getHeight()));
    }
    area.removeFromTop(8);
    status.setBounds(area.removeFromTop(28));
}
void VstEngineAudioProcessorEditor::RoutingPage::assign(int channel)
{
    juce::String diagnostic;
    const bool ok = processor.assignSlotChannel(selected, channel, diagnostic);
    status.setText(ok ? "Routing updated" : diagnostic, juce::dontSendNotification);
    fillChannelChoices(midiIn, processor, selected, currentChannel());
}
int VstEngineAudioProcessorEditor::RoutingPage::currentChannel() const
{
    return juce::roundToInt(processor.parameters()
                                .getRawParameterValue(slotId(selected, "MidiIn").c_str())
                                ->load());
}
void VstEngineAudioProcessorEditor::RoutingPage::bind(std::size_t slot)
{
    selected = slot; sliderAttachments.clear(); buttonAttachments.clear();
    fillChannelChoices(midiIn, processor, slot, currentChannel());
    auto& apvts = processor.parameters();
    auto attachSlider = [&](juce::Slider& slider, std::string_view suffix) {
        sliderAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            apvts, slotId(slot, suffix), slider));
    };
    auto attachButton = [&](juce::Button& button, std::string_view suffix) {
        buttonAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            apvts, slotId(slot, suffix), button));
    };
    attachButton(enabled, "Enabled"); attachButton(mute, "Mute");
    attachButton(solo, "Solo"); attachButton(locked, "Lock");
    attachSlider(level, "Level"); attachSlider(pan, "Pan");
}

VstEngineAudioProcessorEditor::ZonesPage::ZonesPage(VstEngineAudioProcessor& p)
    : processor(p), ranges(p.parameters())
{
    title.setText("KEY / VELOCITY ZONES", juce::dontSendNotification);
    vstengine::ui::styleLabel(title, 15.0f, juce::Justification::centredLeft, vstengine::ui::colours::primary);
    caption(transposeLabel, "SEMITONES");
    transpose.setSliderStyle(juce::Slider::IncDecButtons);
    transpose.setTextBoxStyle(juce::Slider::TextBoxAbove, false, 80, 24);
    addAndMakeVisible(title); addAndMakeVisible(ranges); addAndMakeVisible(transposeLabel); addAndMakeVisible(transpose);
}
void VstEngineAudioProcessorEditor::ZonesPage::paint(juce::Graphics& g)
{
    pageWithSections(g, getLocalBounds(), sections);
}
void VstEngineAudioProcessorEditor::ZonesPage::resized()
{
    auto area = getLocalBounds().reduced(18); title.setBounds(area.removeFromTop(28));
    area.removeFromTop(8);
    auto row = area.removeFromTop(juce::jmin(area.getHeight(), sectionHeader + 200));
    const auto side = row.removeFromRight(150);
    row.removeFromRight(12);
    sections = {{row, "KEY / VELOCITY RANGE"}, {side, "TRANSPOSE"}};
    ranges.setBounds(row.withTrimmedTop(sectionHeader).reduced(10, 6));
    auto inner = side.withTrimmedTop(sectionHeader).reduced(14, 8);
    transposeLabel.setBounds(inner.removeFromTop(18));
    transpose.setBounds(inner.removeFromTop(64));
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
    caption(seedLabel, "SEED"); addAndMakeVisible(seedLabel);
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
void VstEngineAudioProcessorEditor::AdvancedPage::paint(juce::Graphics& g)
{
    pageWithSections(g, getLocalBounds(), sections);
}
void VstEngineAudioProcessorEditor::AdvancedPage::resized()
{
    auto area = getLocalBounds().reduced(18); title.setBounds(area.removeFromTop(28));
    description.setBounds(area.removeFromTop(22));
    area.removeFromTop(8);
    auto row = area.removeFromTop(sectionHeader + 60);
    const int half = (row.getWidth() - 12) / 2;
    const auto source = row.removeFromLeft(half);
    row.removeFromLeft(12);
    const auto fx = row;
    area.removeFromTop(12);
    const auto generation = area.removeFromTop(sectionHeader + 140);
    sections = {{source, "HOST MIDI SOURCE"}, {fx, "GLOBAL FX"}, {generation, "GENERATION"}};
    midiMode.setBounds(source.withTrimmedTop(sectionHeader).reduced(12, 14).withWidth(220));
    effectsPreset.setBounds(fx.withTrimmedTop(sectionHeader).reduced(12, 14).withWidth(240));
    auto gen = generation.withTrimmedTop(sectionHeader).reduced(14, 6);
    auto seedCell = gen.removeFromLeft(120);
    seedLabel.setBounds(seedCell.removeFromTop(18));
    seed.setBounds(seedCell.withSizeKeepingCentre(100, seedCell.getHeight()));
    gen.removeFromLeft(16);
    auto actions = gen.withSizeKeepingCentre(gen.getWidth(), 34).withX(gen.getX());
    generateAll.setBounds(actions.removeFromLeft(140).reduced(2));
    mutateAll.setBounds(actions.removeFromLeft(140).reduced(2));
    actions.removeFromLeft(12);
    status.setBounds(actions);
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
      rackRail(p), instrumentHeader(p), soundPage(p, "SOUND", 6, MacroPage::InstrumentControls::layout),
      patternPage(p), routingPage(p), zonesPage(p), macrosPage(p, "MACROS", 8, MacroPage::InstrumentControls::all),
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
  // VOX keybed: cool ivory keys, ink black keys, cyan key-down state.
  keyboard.setColour(juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour(0xffe3e9ee));
  keyboard.setColour(juce::MidiKeyboardComponent::blackNoteColourId, juce::Colour(0xff081018));
  keyboard.setColour(juce::MidiKeyboardComponent::keySeparatorLineColourId, juce::Colour(0xff6d7f8c));
  keyboard.setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId, juce::Colour(0xcc00ddf5));
  keyboard.setColour(juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, juce::Colour(0x4000ddf5));
  keyboard.setColour(juce::MidiKeyboardComponent::shadowColourId, juce::Colour(0x5506121d));
  keyboard.setColour(juce::MidiKeyboardComponent::upDownButtonBackgroundColourId, juce::Colour(0xff0a1722));
  keyboard.setColour(juce::MidiKeyboardComponent::upDownButtonArrowColourId, juce::Colour(0xff9ab2c5));
  keyboard.setColour(juce::MidiKeyboardComponent::textLabelColourId, juce::Colour(0xff587487));
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
