#include "PluginEditor.h"
#include "instrument/HostParameterSchema.h"
#include "ui/common/UiComponents.h"
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

void VstEngineAudioProcessorEditor::SoundPage::HeroBanner::setLayout(
    Layout newLayout, juce::String instrumentName)
{
    layout = newLayout;
    name = std::move(instrumentName);
    repaint();
}

void VstEngineAudioProcessorEditor::SoundPage::HeroBanner::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto cyan = vstengine::ui::colours::primary;
    const auto acidGreen = juce::Colour::fromRGB(72, 210, 132);
    const auto accent = layout == Layout::acid ? acidGreen : cyan;

    juce::ColourGradient gradient(vstengine::ui::colours::panelRaised,
                                  bounds.getTopLeft(),
                                  vstengine::ui::colours::panel,
                                  bounds.getBottomRight(), false);
    gradient.addColour(0.72, accent.withAlpha(0.08f));
    g.setGradientFill(gradient);
    g.fillRoundedRectangle(bounds.reduced(0.5f), 6.0f);
    g.setColour(vstengine::ui::colours::border);
    g.drawRoundedRectangle(bounds.reduced(0.5f), 6.0f, 1.0f);

    auto text = getLocalBounds().reduced(18, 10);
    auto right = text.removeFromRight(280);
    g.setColour(vstengine::ui::colours::text);
    g.setFont(juce::Font(20.0f, juce::Font::bold));
    g.drawText(name.toUpperCase(), text.removeFromTop(28), juce::Justification::centredLeft);

    const juce::String tagline = layout == Layout::acid
        ? "303 motion with accent and slide — pattern-first performance"
        : layout == Layout::psyBass
            ? "Tight low-end architecture — transient-shaped rolling bass"
            : "Descriptor-driven electronic instrument workspace";
    g.setColour(vstengine::ui::colours::mutedText);
    g.setFont(12.0f);
    g.drawText(tagline, text.removeFromTop(24), juce::Justification::centredLeft);

    const juce::String keywords = layout == Layout::acid
        ? "SQUELCH  /  SLIDE  /  DRIVE"
        : layout == Layout::psyBass
            ? "MONO  /  PUNCH  /  CONTROL"
            : "SOUND  /  MOTION  /  PERFORMANCE";
    g.setColour(accent.withAlpha(0.92f));
    g.setFont(juce::Font(11.0f, juce::Font::bold));
    g.drawText(keywords, right.removeFromTop(20), juce::Justification::centredRight);
    g.setColour(vstengine::ui::colours::mutedText);
    g.setFont(10.0f);
    g.drawText(layout == Layout::acid ? "ACID ENGINE" : layout == Layout::psyBass ? "PSY BASS ENGINE" : "VOX ENGINE",
               right.removeFromTop(18), juce::Justification::centredRight);

    juce::Path wave;
    const auto waveArea = bounds.withTrimmedLeft(bounds.getWidth() * 0.48f)
                                .withTrimmedRight(300.0f)
                                .reduced(4.0f, 16.0f);
    const float mid = waveArea.getCentreY();
    for (int i = 0; i <= 48; ++i) {
        const float t = static_cast<float>(i) / 48.0f;
        const float x = waveArea.getX() + t * waveArea.getWidth();
        const float y = mid + std::sin(t * juce::MathConstants<float>::twoPi * (layout == Layout::acid ? 2.5f : 1.7f))
                                * waveArea.getHeight() * (0.20f + 0.12f * std::sin(t * 5.0f));
        if (i == 0) wave.startNewSubPath(x, y); else wave.lineTo(x, y);
    }
    g.setColour(accent.withAlpha(0.24f));
    g.strokePath(wave, juce::PathStrokeType(2.0f));
}

VstEngineAudioProcessorEditor::SoundPage::VisualPanel::VisualPanel(
    juce::String titleText, VisualRole visualRole)
    : title(std::move(titleText)), role(visualRole), accent(vstengine::ui::colours::primary)
{
}

void VstEngineAudioProcessorEditor::SoundPage::VisualPanel::setAccent(juce::Colour newAccent)
{
    accent = newAccent;
    repaint();
}

void VstEngineAudioProcessorEditor::SoundPage::VisualPanel::setSubtitle(juce::String text)
{
    subtitle = std::move(text);
    repaint();
}

void VstEngineAudioProcessorEditor::SoundPage::VisualPanel::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(vstengine::ui::colours::panelRaised);
    g.fillRoundedRectangle(bounds, 6.0f);
    g.setColour(vstengine::ui::colours::border);
    g.drawRoundedRectangle(bounds, 6.0f, 1.0f);

    auto area = getLocalBounds().reduced(11, 9);
    auto header = area.removeFromTop(18);
    g.setColour(vstengine::ui::colours::text);
    g.setFont(juce::Font(11.0f, juce::Font::bold));
    g.drawText(title, header, juce::Justification::centredLeft);
    if (subtitle.isNotEmpty()) {
        g.setColour(vstengine::ui::colours::mutedText);
        g.setFont(9.0f);
        g.drawText(subtitle, header, juce::Justification::centredRight);
    }

    auto graph = area.reduced(2, 5).toFloat();
    if (graph.getHeight() < 18.0f || graph.getWidth() < 30.0f)
        return;

    g.setColour(vstengine::ui::colours::background.withAlpha(0.72f));
    g.fillRoundedRectangle(graph, 4.0f);
    g.setColour(vstengine::ui::colours::border.withAlpha(0.55f));
    g.drawRoundedRectangle(graph, 4.0f, 1.0f);

    auto plot = graph.reduced(8.0f, 7.0f);
    juce::Path p;
    switch (role) {
        case VisualRole::oscillator: {
            for (int i = 0; i <= 40; ++i) {
                const float t = static_cast<float>(i) / 40.0f;
                const float x = plot.getX() + t * plot.getWidth();
                const float y = plot.getCentreY() + std::sin(t * juce::MathConstants<float>::twoPi * 1.6f)
                                                  * plot.getHeight() * 0.28f;
                if (i == 0) p.startNewSubPath(x, y); else p.lineTo(x, y);
            }
            break;
        }
        case VisualRole::filter:
            p.startNewSubPath(plot.getX(), plot.getBottom() - plot.getHeight() * 0.12f);
            p.cubicTo(plot.getX() + plot.getWidth() * 0.45f, plot.getBottom() - plot.getHeight() * 0.14f,
                      plot.getX() + plot.getWidth() * 0.58f, plot.getY() + plot.getHeight() * 0.08f,
                      plot.getRight(), plot.getY() + plot.getHeight() * 0.34f);
            break;
        case VisualRole::envelope:
            p.startNewSubPath(plot.getX(), plot.getBottom());
            p.lineTo(plot.getX() + plot.getWidth() * 0.12f, plot.getY() + plot.getHeight() * 0.10f);
            p.lineTo(plot.getX() + plot.getWidth() * 0.36f, plot.getY() + plot.getHeight() * 0.42f);
            p.lineTo(plot.getX() + plot.getWidth() * 0.76f, plot.getY() + plot.getHeight() * 0.42f);
            p.lineTo(plot.getRight(), plot.getBottom());
            break;
        case VisualRole::modulation:
            for (int i = 0; i <= 32; ++i) {
                const float t = static_cast<float>(i) / 32.0f;
                const float x = plot.getX() + t * plot.getWidth();
                const float y = plot.getCentreY() + std::sin(t * juce::MathConstants<float>::twoPi * 2.0f)
                                                  * plot.getHeight() * 0.24f;
                if (i == 0) p.startNewSubPath(x, y); else p.lineTo(x, y);
            }
            break;
        case VisualRole::matrix: {
            const int cols = 5, rows = 3;
            g.setColour(vstengine::ui::colours::border.withAlpha(0.65f));
            for (int c = 0; c <= cols; ++c) {
                const float x = plot.getX() + plot.getWidth() * static_cast<float>(c) / cols;
                g.drawVerticalLine(static_cast<int>(x), plot.getY(), plot.getBottom());
            }
            for (int r = 0; r <= rows; ++r) {
                const float y = plot.getY() + plot.getHeight() * static_cast<float>(r) / rows;
                g.drawHorizontalLine(static_cast<int>(y), plot.getX(), plot.getRight());
            }
            g.setColour(accent.withAlpha(0.9f));
            for (const auto point : { juce::Point<float>{0.18f, 0.25f}, {0.52f, 0.66f}, {0.78f, 0.38f} })
                g.fillEllipse(plot.getX() + point.x * plot.getWidth() - 3.0f,
                              plot.getY() + point.y * plot.getHeight() - 3.0f, 6.0f, 6.0f);
            return;
        }
        case VisualRole::sequencer: {
            const float labelWidth = juce::jmin(54.0f, plot.getWidth() * 0.12f);
            auto lanes = plot.withTrimmedLeft(labelWidth);
            static constexpr std::array<const char*, 5> laneNames { "NOTE", "ACC", "SLIDE", "GATE", "OCT" };
            const int rows = static_cast<int>(laneNames.size());
            g.setFont(8.0f);
            for (int r = 0; r < rows; ++r) {
                const float y0 = plot.getY() + plot.getHeight() * static_cast<float>(r) / rows;
                const float y1 = plot.getY() + plot.getHeight() * static_cast<float>(r + 1) / rows;
                g.setColour(vstengine::ui::colours::mutedText);
                g.drawText(laneNames[static_cast<std::size_t>(r)],
                           juce::Rectangle<float>(plot.getX(), y0, labelWidth - 4.0f, y1 - y0),
                           juce::Justification::centredLeft);
                g.setColour(vstengine::ui::colours::border.withAlpha(0.55f));
                g.drawHorizontalLine(static_cast<int>(y1), lanes.getX(), lanes.getRight());
            }
            for (int s = 0; s <= 16; ++s) {
                const float x = lanes.getX() + lanes.getWidth() * static_cast<float>(s) / 16.0f;
                g.setColour(vstengine::ui::colours::border.withAlpha(s % 4 == 0 ? 0.8f : 0.35f));
                g.drawVerticalLine(static_cast<int>(x), lanes.getY(), lanes.getBottom());
            }
            g.setColour(accent.withAlpha(0.78f));
            for (int s = 0; s < 16; ++s) {
                const float cellW = lanes.getWidth() / 16.0f;
                const float rowH = lanes.getHeight() / rows;
                if ((s * 5 + 1) % 7 < 4)
                    g.fillRoundedRectangle(lanes.getX() + s * cellW + 2.0f,
                                           lanes.getY() + 2.0f + (s % 3) * rowH * 0.10f,
                                           cellW - 4.0f, rowH * 0.52f, 2.0f);
                if (s % 4 == 0)
                    g.fillEllipse(lanes.getX() + s * cellW + cellW * 0.35f,
                                  lanes.getY() + rowH + rowH * 0.35f, 5.0f, 5.0f);
                if (s == 3 || s == 7 || s == 11)
                    g.drawLine(lanes.getX() + s * cellW + 3.0f,
                               lanes.getY() + rowH * 2.5f,
                               lanes.getX() + (s + 1) * cellW - 3.0f,
                               lanes.getY() + rowH * 2.5f, 2.0f);
            }
            return;
        }
        case VisualRole::character:
        case VisualRole::accent:
        case VisualRole::performance:
        case VisualRole::playMode:
        case VisualRole::output: {
            const float y = plot.getCentreY();
            g.setColour(vstengine::ui::colours::border.withAlpha(0.8f));
            g.drawLine(plot.getX(), y, plot.getRight(), y, 2.0f);
            g.setColour(accent.withAlpha(0.86f));
            const float amount = role == VisualRole::output ? 0.72f : role == VisualRole::accent ? 0.58f : 0.42f;
            g.drawLine(plot.getX(), y, plot.getX() + plot.getWidth() * amount, y, 3.0f);
            return;
        }
    }

    g.setColour(accent.withAlpha(0.88f));
    g.strokePath(p, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved,
                                         juce::PathStrokeType::rounded));
}

VstEngineAudioProcessorEditor::SoundPage::SoundPage(VstEngineAudioProcessor& p)
    : processor(p)
{
    addAndMakeVisible(hero);
    for (auto* visual : { &oscillator, &filter, &envelope, &character, &accentPanel,
                          &performance, &modulation, &matrix, &sequencer, &playMode, &output })
        addAndMakeVisible(visual);

    for (std::size_t i = 0; i < knobs.size(); ++i) {
        knobs[i] = std::make_unique<vox::ui::VoxKnob>(
            "M" + juce::String(static_cast<int>(i + 1)), vox::ui::VoxKnob::Size::Normal);
        knobs[i]->setVisible(false);
        addAndMakeVisible(*knobs[i]);
    }
    configureLayout(Layout::generic, "Instrument");
}

void VstEngineAudioProcessorEditor::SoundPage::paint(juce::Graphics& g)
{
    panel(g, getLocalBounds());
}

void VstEngineAudioProcessorEditor::SoundPage::hideAllPanels()
{
    for (auto* visual : { &oscillator, &filter, &envelope, &character, &accentPanel,
                          &performance, &modulation, &matrix, &sequencer, &playMode, &output })
        visual->setVisible(false);
}

void VstEngineAudioProcessorEditor::SoundPage::hideAllKnobs()
{
    for (auto& item : knobs)
        item->setVisible(false);
}

void VstEngineAudioProcessorEditor::SoundPage::configureLayout(Layout newLayout, juce::String instrumentName)
{
    layout = newLayout;
    hero.setLayout(layout, std::move(instrumentName));
    hideAllPanels();
    hideAllKnobs();

    const auto acidGreen = juce::Colour::fromRGB(72, 210, 132);
    const auto accent = layout == Layout::acid ? acidGreen : vstengine::ui::colours::primary;
    for (auto* visual : { &oscillator, &filter, &envelope, &character, &accentPanel,
                          &performance, &modulation, &matrix, &sequencer, &playMode, &output })
        visual->setAccent(accent);

    if (layout == Layout::psyBass) {
        oscillator.setSubtitle("MONO / PHASE");
        filter.setSubtitle("LOW-PASS");
        envelope.setSubtitle("TRANSIENT");
        for (auto* visual : { &oscillator, &filter, &envelope, &character, &accentPanel,
                              &performance, &modulation, &matrix })
            visual->setVisible(true);
        for (std::size_t i = 0; i < 6; ++i) {
            knobs[i]->setKnobSize(vox::ui::VoxKnob::Size::Small);
            knobs[i]->setVisible(true);
        }
    } else if (layout == Layout::acid) {
        oscillator.setSubtitle("303 CORE");
        filter.setSubtitle("RESONANT");
        envelope.setSubtitle("DECAY");
        sequencer.setSubtitle("PATTERN PREVIEW");
        for (auto* visual : { &oscillator, &filter, &envelope, &character, &accentPanel,
                              &performance, &modulation, &sequencer, &playMode, &output })
            visual->setVisible(true);
        for (std::size_t i = 0; i < 8; ++i) {
            knobs[i]->setKnobSize(vox::ui::VoxKnob::Size::Small);
            knobs[i]->setVisible(true);
        }
    } else {
        for (auto* visual : { &oscillator, &filter, &envelope, &performance, &modulation, &matrix })
            visual->setVisible(true);
        for (std::size_t i = 0; i < 6; ++i) {
            knobs[i]->setKnobSize(vox::ui::VoxKnob::Size::Normal);
            knobs[i]->setVisible(true);
        }
    }
    resized();
}

void VstEngineAudioProcessorEditor::SoundPage::placeKnobs(
    juce::Rectangle<int> panelBounds, std::initializer_list<std::size_t> indices)
{
    if (indices.size() == 0)
        return;
    auto content = panelBounds.reduced(8);
    content.removeFromTop(26);
    const int width = content.getWidth() / static_cast<int>(indices.size());
    int x = content.getX();
    for (const auto index : indices) {
        if (index >= knobs.size())
            continue;
        auto cell = juce::Rectangle<int>(x, content.getY(), width, content.getHeight()).reduced(3);
        knobs[index]->setBounds(cell);
        x += width;
    }
}

void VstEngineAudioProcessorEditor::SoundPage::resized()
{
    auto area = getLocalBounds().reduced(10);
    const int heroHeight = juce::jlimit(66, 92, area.getHeight() / 6);
    hero.setBounds(area.removeFromTop(heroHeight));
    area.removeFromTop(8);

    constexpr int gap = 8;
    if (layout == Layout::psyBass) {
        const int row1H = juce::jmax(120, static_cast<int>(area.getHeight() * 0.38f));
        const int row2H = juce::jmax(96, static_cast<int>(area.getHeight() * 0.27f));
        auto row1 = area.removeFromTop(juce::jmin(row1H, area.getHeight()));
        area.removeFromTop(gap);
        auto row2 = area.removeFromTop(juce::jmin(row2H, area.getHeight()));
        area.removeFromTop(gap);
        auto row3 = area;

        const int w1 = (row1.getWidth() - gap * 2) / 3;
        const auto osc = row1.removeFromLeft(w1); row1.removeFromLeft(gap);
        const auto fil = row1.removeFromLeft(w1); row1.removeFromLeft(gap);
        const auto env = row1;
        oscillator.setBounds(osc); filter.setBounds(fil); envelope.setBounds(env);
        placeKnobs(fil, { 0, 1 });
        placeKnobs(env, { 2, 3 });

        const int w2 = (row2.getWidth() - gap * 2) / 3;
        const auto chr = row2.removeFromLeft(w2); row2.removeFromLeft(gap);
        const auto acc = row2.removeFromLeft(w2); row2.removeFromLeft(gap);
        const auto perf = row2;
        character.setBounds(chr); accentPanel.setBounds(acc); performance.setBounds(perf);
        placeKnobs(chr, { 4 });
        placeKnobs(acc, { 5 });

        const int modW = static_cast<int>((row3.getWidth() - gap) * 0.42f);
        const auto mod = row3.removeFromLeft(modW); row3.removeFromLeft(gap);
        modulation.setBounds(mod); matrix.setBounds(row3);
    } else if (layout == Layout::acid) {
        const int topH = juce::jlimit(92, 132, area.getHeight() / 4);
        auto top = area.removeFromTop(topH);
        area.removeFromTop(gap);
        const int panelW = (top.getWidth() - gap * 6) / 7;
        std::array<juce::Rectangle<int>, 7> topPanels;
        for (int i = 0; i < 7; ++i) {
            topPanels[static_cast<std::size_t>(i)] = top.removeFromLeft(i == 6 ? top.getWidth() : panelW);
            if (i != 6) top.removeFromLeft(gap);
        }
        oscillator.setBounds(topPanels[0]); filter.setBounds(topPanels[1]); envelope.setBounds(topPanels[2]);
        character.setBounds(topPanels[3]); accentPanel.setBounds(topPanels[4]); performance.setBounds(topPanels[5]);
        output.setBounds(topPanels[6]);
        placeKnobs(topPanels[0], { 0 });
        placeKnobs(topPanels[1], { 1, 2 });
        placeKnobs(topPanels[2], { 3, 4 });
        placeKnobs(topPanels[3], { 7 });
        placeKnobs(topPanels[4], { 5 });
        placeKnobs(topPanels[5], { 6 });

        const int sequencerH = juce::jmax(126, static_cast<int>(area.getHeight() * 0.58f));
        sequencer.setBounds(area.removeFromTop(juce::jmin(sequencerH, area.getHeight())));
        area.removeFromTop(gap);
        auto bottom = area;
        const int bottomW = (bottom.getWidth() - gap * 2) / 3;
        const auto mod = bottom.removeFromLeft(bottomW); bottom.removeFromLeft(gap);
        const auto perf = bottom.removeFromLeft(bottomW); bottom.removeFromLeft(gap);
        modulation.setBounds(mod); performance.setBounds(perf); playMode.setBounds(bottom);
    } else {
        const int rowH = (area.getHeight() - gap) / 2;
        auto top = area.removeFromTop(rowH); area.removeFromTop(gap); auto bottom = area;
        const int colW = (top.getWidth() - gap * 2) / 3;
        const auto a = top.removeFromLeft(colW); top.removeFromLeft(gap);
        const auto b = top.removeFromLeft(colW); top.removeFromLeft(gap);
        const auto c = top;
        oscillator.setBounds(a); filter.setBounds(b); envelope.setBounds(c);
        placeKnobs(a, { 0 }); placeKnobs(b, { 1, 2 }); placeKnobs(c, { 3 });
        const auto d = bottom.removeFromLeft(colW); bottom.removeFromLeft(gap);
        const auto e = bottom.removeFromLeft(colW); bottom.removeFromLeft(gap);
        performance.setBounds(d); modulation.setBounds(e); matrix.setBounds(bottom);
        placeKnobs(d, { 4 }); placeKnobs(e, { 5 });
    }
}

void VstEngineAudioProcessorEditor::SoundPage::bind(std::size_t slot)
{
    selected = slot;
    attachments.clear();
    const auto* descriptor = processor.instrumentRack().descriptor(slot);
    Layout nextLayout = Layout::generic;
    juce::String instrumentName = "Empty slot";
    if (descriptor != nullptr) {
        instrumentName = descriptor->name;
        if (descriptor->id == "com.ultimavox.psy-bass") nextLayout = Layout::psyBass;
        else if (descriptor->id == "com.ultimavox.acid") nextLayout = Layout::acid;
    }

    for (std::size_t macro = 0; macro < knobs.size(); ++macro) {
        juce::String label(vstengine::instrument::hostparams::macroLabels[macro].data());
        if (descriptor != nullptr)
            for (const auto& parameter : descriptor->parameters)
                if (parameter.preferredMacro == static_cast<std::int8_t>(macro)) {
                    label = parameter.name;
                    break;
                }
        knobs[macro]->setLabel(label);
        knobs[macro]->setTooltip("Macro " + juce::String(static_cast<int>(macro + 1)) + ": " + label);
        attachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            processor.parameters(), vstengine::instrument::hostparams::macroId(slot, macro),
            knobs[macro]->getSlider()));
    }
    configureLayout(nextLayout, instrumentName);
}

VstEngineAudioProcessorEditor::MacroPage::MacroPage(
    VstEngineAudioProcessor& p, juce::String heading, std::size_t visibleCount)
    : processor(p), count(juce::jlimit<std::size_t>(1, knobs.size(), visibleCount))
{
    title.setText(heading, juce::dontSendNotification);
    description.setText("Stable host automation macros and instrument mappings", juce::dontSendNotification);
    vstengine::ui::styleLabel(title, 15.0f, juce::Justification::centredLeft,
                              vstengine::ui::colours::primary);
    vstengine::ui::styleLabel(description, 12.0f, juce::Justification::centredLeft,
                              vstengine::ui::colours::mutedText);
    addAndMakeVisible(title); addAndMakeVisible(description);

    for (std::size_t i = 0; i < knobs.size(); ++i) {
        cards[i] = std::make_unique<vox::ui::VoxPanel>(
            "MACRO " + juce::String(static_cast<int>(i + 1)).paddedLeft('0', 2));
        knobs[i] = std::make_unique<vox::ui::VoxKnob>(
            "Macro " + juce::String(static_cast<int>(i + 1)), vox::ui::VoxKnob::Size::Normal);
        addChildComponent(*cards[i]);
        addChildComponent(*knobs[i]);
        cards[i]->setVisible(i < count);
        knobs[i]->setVisible(i < count);
    }
}

void VstEngineAudioProcessorEditor::MacroPage::paint(juce::Graphics& g) { panel(g, getLocalBounds()); }

void VstEngineAudioProcessorEditor::MacroPage::resized()
{
    auto area = getLocalBounds().reduced(16);
    title.setBounds(area.removeFromTop(24));
    description.setBounds(area.removeFromTop(22));
    area.removeFromTop(10);

    const int columns = 4;
    const int rows = static_cast<int>((count + columns - 1) / columns);
    constexpr int gap = 8;
    const int cellWidth = (area.getWidth() - gap * (columns - 1)) / columns;
    const int cellHeight = rows > 0 ? (area.getHeight() - gap * (rows - 1)) / rows : area.getHeight();
    for (std::size_t i = 0; i < count; ++i) {
        const int col = static_cast<int>(i % columns);
        const int row = static_cast<int>(i / columns);
        auto cell = juce::Rectangle<int>(area.getX() + col * (cellWidth + gap),
                                         area.getY() + row * (cellHeight + gap),
                                         cellWidth, cellHeight);
        cards[i]->setBounds(cell);
        auto knobArea = cell.reduced(10);
        knobArea.removeFromTop(24);
        knobs[i]->setBounds(knobArea);
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
        cards[macro]->setTitle("M" + juce::String(static_cast<int>(macro + 1)).paddedLeft('0', 2)
                               + "  /  " + label.toUpperCase());
        knobs[macro]->setLabel(label);
        knobs[macro]->setTooltip("Macro " + juce::String(static_cast<int>(macro + 1)) + ": " + label);
        attachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            processor.parameters(), vstengine::instrument::hostparams::macroId(slot, macro),
            knobs[macro]->getSlider()));
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
      rackRail(p), instrumentHeader(p), soundPage(p), patternPage(p),
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
