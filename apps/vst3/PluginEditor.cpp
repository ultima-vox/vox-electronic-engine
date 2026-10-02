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

namespace {

// Adapts authoritative rack state into the explicit rack view model. The rack
// component reads only RackSlotView; no display string is parsed or rebuilt.
std::vector<vstengine::ui::RackSlotView> buildRackViews(
    VstEngineAudioProcessor& processor)
{
    const auto& states = processor.instrumentRack().state();
    const auto& runtime = processor.instrumentRack().runtimeState();
    const auto selected = processor.selectedSlotIndex();
    constexpr std::size_t slotCount = vstengine::instrument::maxSlots;

    std::vector<vstengine::ui::RackSlotView> views;
    views.reserve(slotCount);

    for (std::size_t i = 0; i < slotCount; ++i) {
        const auto* descriptor = processor.instrumentRack().descriptor(i);
        const auto& state = states[i];

        vstengine::ui::RackSlotView view;
        view.slotIndex = i;
        view.displayNumber = static_cast<int> (i + 1);
        view.occupied = descriptor != nullptr;
        view.instrumentName = view.occupied
            ? juce::String (descriptor->name) : juce::String ("Empty");
        view.vendorName = view.occupied
            ? juce::String (descriptor->vendor) : juce::String();
        view.versionName = view.occupied
            ? "v" + juce::String (descriptor->instrumentVersion)
            : juce::String();
        view.routeEnabled = state.routing.mode != vstengine::rack::RouteMode::off;
        view.midiChannel = view.routeEnabled ? static_cast<int> (state.routing.channel) : 0;
        view.selected = i == selected;
        view.enabled = state.enabled;
        view.muted = state.mute;
        view.soloed = state.solo;
        view.locked = state.locked;
        view.level = state.level;
        view.active = runtime[i].ownedNotes != 0;
        view.slotIdText = juce::String (state.slotId);
        view.accent = vstengine::ui::colours::primary;
        views.push_back (view);
    }

    return views;
}

} // namespace

void VstEngineAudioProcessorEditor::refreshRack()
{
    rackRail.setViews (buildRackViews (processor));
    rackRail.setSelectedSlot (processor.selectedSlotIndex());
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

    juce::ColourGradient bg(vstengine::ui::colours::panelRaised.brighter(0.035f),
                            bounds.getTopLeft(), vstengine::ui::colours::background,
                            bounds.getBottomRight(), false);
    bg.addColour(0.50, vstengine::ui::colours::panel);
    bg.addColour(0.78, accent.withAlpha(0.16f));
    g.setGradientFill(bg);
    g.fillRoundedRectangle(bounds.reduced(0.5f), 6.0f);

    // Reference-style instrument artwork: layered horizon for Psy Bass and a
    // perspective 303 control surface for Acid. Gate A only; no DSP dependency.
    auto art = bounds.withTrimmedLeft(bounds.getWidth() * 0.26f).reduced(4.0f, 4.0f);
    g.saveState();
    g.reduceClipRegion(art.toNearestInt());

    if (layout == Layout::acid) {
        const auto board = art.reduced(art.getWidth() * 0.05f, art.getHeight() * 0.12f);
        juce::Path deck;
        deck.startNewSubPath(board.getX(), board.getBottom());
        deck.lineTo(board.getX() + board.getWidth() * 0.18f, board.getY());
        deck.lineTo(board.getRight(), board.getY() + board.getHeight() * 0.12f);
        deck.lineTo(board.getRight() - board.getWidth() * 0.08f, board.getBottom());
        deck.closeSubPath();
        juce::ColourGradient deckGradient(juce::Colour::fromRGB(18, 38, 39), board.getTopLeft(),
                                          juce::Colour::fromRGB(4, 16, 22), board.getBottomRight(), false);
        deckGradient.addColour(0.65, accent.withAlpha(0.12f));
        g.setGradientFill(deckGradient);
        g.fillPath(deck);
        g.setColour(accent.withAlpha(0.28f));
        g.strokePath(deck, juce::PathStrokeType(1.2f));

        for (int i = 0; i < 5; ++i) {
            const float t = static_cast<float>(i) / 4.0f;
            const auto cx = board.getX() + board.getWidth() * (0.32f + 0.11f * i);
            const auto cy = board.getY() + board.getHeight() * (0.43f + 0.08f * t);
            const auto r = board.getHeight() * 0.11f;
            g.setColour(vstengine::ui::colours::background.withAlpha(0.92f));
            g.fillEllipse(cx - r, cy - r, r * 2.0f, r * 2.0f);
            g.setColour(accent.withAlpha(0.55f));
            g.drawEllipse(cx - r, cy - r, r * 2.0f, r * 2.0f, 1.5f);
            g.drawLine(cx, cy, cx + std::sin(t * 2.6f) * r * 0.70f,
                       cy - std::cos(t * 2.6f) * r * 0.70f, 1.5f);
        }

        g.setColour(accent.withAlpha(0.13f));
        for (int line = 0; line < 8; ++line) {
            const auto y = board.getY() + board.getHeight() * (0.66f + line * 0.035f);
            g.drawLine(board.getX() + board.getWidth() * 0.22f, y,
                       board.getRight() - board.getWidth() * 0.10f, y, 1.0f);
        }
        g.setColour(accent.withAlpha(0.62f));
        g.setFont(juce::Font(24.0f, juce::Font::bold));
        g.drawText("303", board.withTrimmedLeft(board.getWidth() * 0.72f)
                                .withTrimmedBottom(board.getHeight() * 0.54f),
                   juce::Justification::centred, false);
    } else {
        const auto horizon = art.getBottom() - art.getHeight() * 0.20f;
        for (int layer = 0; layer < 3; ++layer) {
            juce::Path mountains;
            const float base = horizon - layer * art.getHeight() * 0.06f;
            mountains.startNewSubPath(art.getX(), art.getBottom());
            mountains.lineTo(art.getX(), base);
            constexpr int peaks = 13;
            for (int i = 0; i <= peaks; ++i) {
                const float t = static_cast<float>(i) / peaks;
                const float x = art.getX() + t * art.getWidth();
                const float amp = art.getHeight() * (0.10f + 0.035f * layer);
                const float y = base - std::abs(std::sin(t * 17.0f + layer * 1.4f)) * amp
                                   - std::abs(std::sin(t * 31.0f + 0.7f)) * amp * 0.32f;
                mountains.lineTo(x, y);
            }
            mountains.lineTo(art.getRight(), art.getBottom());
            mountains.closeSubPath();
            g.setColour(juce::Colour::fromRGB(4 + layer * 3, 18 + layer * 7, 30 + layer * 11)
                            .withAlpha(0.94f - layer * 0.12f));
            g.fillPath(mountains);
            g.setColour(accent.withAlpha(0.08f + layer * 0.05f));
            g.strokePath(mountains, juce::PathStrokeType(1.0f));
        }

        const auto moonR = art.getHeight() * 0.42f;
        const juce::Point<float> moon(art.getX() + art.getWidth() * 0.58f,
                                      art.getY() + art.getHeight() * 0.48f);
        g.setColour(accent.withAlpha(0.05f));
        g.fillEllipse(moon.x - moonR, moon.y - moonR, moonR * 2.0f, moonR * 2.0f);
        g.setColour(accent.withAlpha(0.42f));
        g.drawEllipse(moon.x - moonR, moon.y - moonR, moonR * 2.0f, moonR * 2.0f, 1.6f);

        g.setColour(accent.withAlpha(0.22f));
        for (int i = 0; i < 11; ++i) {
            const float x = art.getX() + art.getWidth() * (0.10f + 0.078f * i);
            const float h = art.getHeight() * (0.05f + 0.14f * std::abs(std::sin(i * 1.7f)));
            g.fillRoundedRectangle(x, art.getBottom() - h - 8.0f, 2.0f, h, 1.0f);
        }
    }

    for (int i = 0; i < 24; ++i) {
        const float t = static_cast<float>(i) / 23.0f;
        const float x = art.getX() + art.getWidth() * std::fmod(t * 1.73f + 0.11f, 1.0f);
        const float y = art.getY() + art.getHeight() * std::fmod(t * 2.37f + 0.07f, 0.72f);
        g.setColour(accent.withAlpha(0.11f + 0.18f * std::fmod(t * 5.0f, 1.0f)));
        g.fillEllipse(x, y, 1.4f, 1.4f);
    }
    g.restoreState();

    auto text = getLocalBounds().reduced(22, 12);
    auto right = text.removeFromRight(210);
    g.setColour(vstengine::ui::colours::text);
    g.setFont(juce::Font(layout == Layout::acid ? 25.0f : 22.0f, juce::Font::bold));
    g.drawText(name.toUpperCase(), text.removeFromTop(30), juce::Justification::centredLeft);

    const juce::String tagline = layout == Layout::acid
        ? "CLASSIC ATTITUDE  /  MODERN POSSIBILITIES"
        : layout == Layout::psyBass
            ? "DEEP FREQUENCIES  /  HIGHER POSSIBILITIES"
            : "SOUND  /  MOTION  /  PERFORMANCE";
    g.setColour(accent.withAlpha(0.92f));
    g.setFont(10.0f);
    g.drawText(tagline, text.removeFromTop(20), juce::Justification::centredLeft);

    const juce::String keywords = layout == Layout::acid
        ? "SQUELCH\nSEQUENCE\nDISTORT\nTRANSCEND"
        : layout == Layout::psyBass
            ? "DEEP\nEVOLVING\nHYPNOTIC"
            : "CREATE\nEVOLVE\nTRANSCEND";
    g.setColour(accent.withAlpha(0.82f));
    g.setFont(9.0f);
    g.drawFittedText(keywords, right.removeFromTop(54), juce::Justification::topRight, 4);
    g.setColour(vstengine::ui::colours::mutedText);
    g.setFont(8.5f);
    g.drawFittedText(layout == Layout::acid ? "AN ICON\nREIMAGINED" : "MODERN ELECTRONIC\nINSTRUMENT",
                     right.removeFromTop(32), juce::Justification::topRight, 2);

    g.setColour(vstengine::ui::colours::border);
    g.drawRoundedRectangle(bounds.reduced(0.5f), 6.0f, 1.0f);
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
    juce::ColourGradient surface(vstengine::ui::colours::panelRaised.brighter(0.02f),
                                 bounds.getTopLeft(), vstengine::ui::colours::panel,
                                 bounds.getBottomLeft(), false);
    g.setGradientFill(surface);
    g.fillRoundedRectangle(bounds, 6.0f);
    g.setColour(vstengine::ui::colours::border);
    g.drawRoundedRectangle(bounds, 6.0f, 1.0f);

    auto area = getLocalBounds().reduced(11, 9);
    auto header = area.removeFromTop(18);
    g.setColour(accent.withAlpha(0.88f));
    g.drawEllipse(static_cast<float>(header.getX()), static_cast<float>(header.getCentreY() - 3), 6.0f, 6.0f, 1.2f);
    auto titleArea = header.withTrimmedLeft(11);
    g.setColour(vstengine::ui::colours::text);
    g.setFont(juce::Font(10.5f, juce::Font::bold));
    g.drawText(title, titleArea, juce::Justification::centredLeft);
    if (subtitle.isNotEmpty()) {
        g.setColour(vstengine::ui::colours::mutedText);
        g.setFont(8.0f);
        g.drawText(subtitle, header, juce::Justification::centredRight);
    }

    auto graph = area.reduced(2, 5).toFloat();
    if (graph.getHeight() < 18.0f || graph.getWidth() < 30.0f)
        return;

    g.setColour(vstengine::ui::colours::background.withAlpha(0.82f));
    g.fillRoundedRectangle(graph, 4.0f);
    g.setColour(vstengine::ui::colours::border.withAlpha(0.55f));
    g.drawRoundedRectangle(graph, 4.0f, 1.0f);

    auto plot = graph.reduced(8.0f, 7.0f);
    g.setColour(vstengine::ui::colours::borderSubtle.withAlpha(0.36f));
    for (int c = 1; c < 5; ++c) {
        const float x = plot.getX() + plot.getWidth() * static_cast<float>(c) / 5.0f;
        g.drawVerticalLine(static_cast<int>(x), plot.getY(), plot.getBottom());
    }
    for (int r = 1; r < 4; ++r) {
        const float y = plot.getY() + plot.getHeight() * static_cast<float>(r) / 4.0f;
        g.drawHorizontalLine(static_cast<int>(y), plot.getX(), plot.getRight());
    }

    juce::Path p;
    switch (role) {
        case VisualRole::oscillator: {
            const auto waveArea = plot.withTrimmedBottom(plot.getHeight() * 0.28f);
            for (int i = 0; i <= 56; ++i) {
                const float t = static_cast<float>(i) / 56.0f;
                const float x = waveArea.getX() + t * waveArea.getWidth();
                const float y = waveArea.getCentreY()
                    + std::sin(t * juce::MathConstants<float>::twoPi * 2.35f)
                          * waveArea.getHeight() * (0.20f + 0.10f * std::sin(t * 9.0f));
                if (i == 0) p.startNewSubPath(x, y); else p.lineTo(x, y);
            }
            break;
        }
        case VisualRole::filter: {
            const auto graphArea = plot.withTrimmedBottom(plot.getHeight() * 0.28f);
            p.startNewSubPath(graphArea.getX(), graphArea.getBottom() - graphArea.getHeight() * 0.14f);
            p.cubicTo(graphArea.getX() + graphArea.getWidth() * 0.42f,
                      graphArea.getBottom() - graphArea.getHeight() * 0.12f,
                      graphArea.getX() + graphArea.getWidth() * 0.54f,
                      graphArea.getY() + graphArea.getHeight() * 0.12f,
                      graphArea.getRight(), graphArea.getY() + graphArea.getHeight() * 0.38f);
            break;
        }
        case VisualRole::envelope: {
            const auto graphArea = plot.withTrimmedBottom(plot.getHeight() * 0.28f);
            p.startNewSubPath(graphArea.getX(), graphArea.getBottom());
            p.lineTo(graphArea.getX() + graphArea.getWidth() * 0.10f,
                     graphArea.getY() + graphArea.getHeight() * 0.08f);
            p.cubicTo(graphArea.getX() + graphArea.getWidth() * 0.25f,
                      graphArea.getY() + graphArea.getHeight() * 0.25f,
                      graphArea.getX() + graphArea.getWidth() * 0.54f,
                      graphArea.getY() + graphArea.getHeight() * 0.42f,
                      graphArea.getX() + graphArea.getWidth() * 0.70f,
                      graphArea.getY() + graphArea.getHeight() * 0.45f);
            p.lineTo(graphArea.getRight(), graphArea.getBottom());
            break;
        }
        case VisualRole::modulation: {
            for (int i = 0; i <= 40; ++i) {
                const float t = static_cast<float>(i) / 40.0f;
                const float x = plot.getX() + t * plot.getWidth();
                const float y = plot.getCentreY() + std::sin(t * juce::MathConstants<float>::twoPi * 2.0f)
                                                  * plot.getHeight() * (0.16f + t * 0.09f);
                if (i == 0) p.startNewSubPath(x, y); else p.lineTo(x, y);
            }
            break;
        }
        case VisualRole::matrix: {
            g.setColour(vstengine::ui::colours::textSecondary);
            g.setFont(8.0f);
            auto table = plot;
            const int rowH = juce::jmax(13, static_cast<int>(table.getHeight() / 5.0f));
            static constexpr std::array<const char*, 4> sources { "MOD WHEEL", "AFTERTOUCH", "LFO 1", "ENV 2" };
            static constexpr std::array<const char*, 4> destinations { "FILTER CUTOFF", "DRIVE AMOUNT", "PITCH", "RESONANCE" };
            for (int row = 0; row < 4; ++row) {
                auto rr = juce::Rectangle<int>(static_cast<int>(table.getX()),
                                                     static_cast<int>(table.getY()) + row * rowH,
                                                     static_cast<int>(table.getWidth()), rowH);
                g.setColour(vstengine::ui::colours::borderSubtle.withAlpha(0.55f));
                g.drawHorizontalLine(rr.getBottom(), table.getX(), table.getRight());
                g.setColour(vstengine::ui::colours::textSecondary);
                g.drawText(juce::String(row + 1), rr.removeFromLeft(22), juce::Justification::centredLeft);
                auto rowArea = juce::Rectangle<int>(static_cast<int>(table.getX()) + 22,
                                                    static_cast<int>(table.getY()) + row * rowH,
                                                    static_cast<int>(table.getWidth()) - 22, rowH);
                auto src = rowArea.removeFromLeft(static_cast<int>(rowArea.getWidth() * 0.35f));
                auto amt = rowArea.removeFromLeft(static_cast<int>(rowArea.getWidth() * 0.22f));
                g.drawText(sources[static_cast<std::size_t>(row)], src, juce::Justification::centredLeft);
                g.setColour(accent.withAlpha(0.9f));
                g.drawText("+0." + juce::String(35 + row * 10), amt, juce::Justification::centredLeft);
                g.setColour(vstengine::ui::colours::textSecondary);
                g.drawText(destinations[static_cast<std::size_t>(row)], rowArea, juce::Justification::centredLeft);
            }
            return;
        }
        case VisualRole::sequencer: {
            const float labelWidth = juce::jmin(58.0f, plot.getWidth() * 0.11f);
            auto lanes = plot.withTrimmedLeft(labelWidth);
            static constexpr std::array<const char*, 5> laneNames { "NOTE", "ACCENT", "SLIDE", "GATE", "OCTAVE" };
            const int rows = static_cast<int>(laneNames.size());
            g.setFont(7.8f);
            const float stepHeader = juce::jmin(18.0f, plot.getHeight() * 0.10f);
            auto headerArea = lanes.removeFromTop(stepHeader);
            for (int s = 0; s < 16; ++s) {
                const float cellW = headerArea.getWidth() / 16.0f;
                g.setColour(s % 4 == 0 ? accent.withAlpha(0.75f) : vstengine::ui::colours::mutedText);
                g.drawText(juce::String(s + 1),
                           juce::Rectangle<float>(headerArea.getX() + s * cellW, headerArea.getY(), cellW, headerArea.getHeight()),
                           juce::Justification::centred);
            }
            for (int r = 0; r < rows; ++r) {
                const float y0 = lanes.getY() + lanes.getHeight() * static_cast<float>(r) / rows;
                const float y1 = lanes.getY() + lanes.getHeight() * static_cast<float>(r + 1) / rows;
                g.setColour(vstengine::ui::colours::mutedText);
                g.drawText(laneNames[static_cast<std::size_t>(r)],
                           juce::Rectangle<float>(plot.getX(), y0, labelWidth - 4.0f, y1 - y0),
                           juce::Justification::centredLeft);
                g.setColour(vstengine::ui::colours::border.withAlpha(0.55f));
                g.drawHorizontalLine(static_cast<int>(y1), lanes.getX(), lanes.getRight());
            }
            for (int s = 0; s <= 16; ++s) {
                const float x = lanes.getX() + lanes.getWidth() * static_cast<float>(s) / 16.0f;
                g.setColour(vstengine::ui::colours::border.withAlpha(s % 4 == 0 ? 0.82f : 0.38f));
                g.drawVerticalLine(static_cast<int>(x), lanes.getY(), lanes.getBottom());
            }
            for (int s = 0; s < 16; ++s) {
                const float cellW = lanes.getWidth() / 16.0f;
                const float rowH = lanes.getHeight() / rows;
                const bool noteOn = ((s * 7 + 3) % 11) < 7;
                if (noteOn) {
                    const int pitch = 1 + ((s * 5 + 2) % 4);
                    const auto cell = juce::Rectangle<float>(lanes.getX() + s * cellW + 2.0f,
                                                             lanes.getY() + 2.0f,
                                                             cellW - 4.0f, rowH - 4.0f);
                    g.setColour(accent.withAlpha(0.78f));
                    g.fillRoundedRectangle(cell, 2.0f);
                    g.setColour(vstengine::ui::colours::background.withAlpha(0.9f));
                    g.setFont(7.5f);
                    g.drawText(juce::String(pitch == 1 ? "C3" : pitch == 2 ? "D3" : pitch == 3 ? "E3" : "G3"),
                               cell, juce::Justification::centred);
                }
                if (s % 4 == 0) {
                    g.setColour(accent.withAlpha(0.92f));
                    g.fillEllipse(lanes.getX() + s * cellW + cellW * 0.38f,
                                  lanes.getY() + rowH + rowH * 0.34f, 5.0f, 5.0f);
                }
                if (s == 2 || s == 6 || s == 10 || s == 14) {
                    g.setColour(accent.withAlpha(0.72f));
                    g.drawRoundedRectangle(lanes.getX() + s * cellW + 3.0f,
                                           lanes.getY() + rowH * 2.0f + rowH * 0.28f,
                                           cellW - 6.0f, rowH * 0.45f, 2.0f, 1.2f);
                }
                g.setColour(accent.withAlpha(0.55f));
                const float gate = rowH * (0.22f + 0.56f * std::fmod(s * 0.37f, 1.0f));
                g.fillRoundedRectangle(lanes.getX() + s * cellW + cellW * 0.42f,
                                       lanes.getY() + rowH * 3.0f + rowH * 0.10f,
                                       juce::jmax(2.0f, cellW * 0.16f), gate, 1.0f);
            }
            return;
        }
        case VisualRole::character:
        case VisualRole::accent:
        case VisualRole::performance:
        case VisualRole::playMode:
        case VisualRole::output: {
            const int count = role == VisualRole::performance ? 4 : role == VisualRole::playMode ? 3 : 2;
            const auto strip = plot.reduced(4.0f, 2.0f);
            for (int i = 0; i < count; ++i) {
                const float x = strip.getX() + strip.getWidth() * (static_cast<float>(i) + 0.5f) / count;
                const float y = strip.getCentreY();
                const float r = juce::jmin(strip.getHeight() * 0.22f, strip.getWidth() / count * 0.15f);
                g.setColour(vstengine::ui::colours::control);
                g.fillEllipse(x - r, y - r, r * 2.0f, r * 2.0f);
                g.setColour(vstengine::ui::colours::border);
                g.drawEllipse(x - r, y - r, r * 2.0f, r * 2.0f, 1.0f);
                juce::Path arc;
                arc.addCentredArc(x, y, r * 0.86f, r * 0.86f, 0.0f,
                                  juce::MathConstants<float>::pi * 0.78f,
                                  juce::MathConstants<float>::pi * (1.48f + 0.16f * i), true);
                g.setColour(accent.withAlpha(0.86f));
                g.strokePath(arc, juce::PathStrokeType(1.6f));
            }
            return;
        }
    }

    if (!p.isEmpty()) {
        juce::Path fillPath = p;
        const auto last = p.getCurrentPosition();
        fillPath.lineTo(last.x, plot.getBottom());
        fillPath.lineTo(plot.getX(), plot.getBottom());
        fillPath.closeSubPath();
        g.setColour(accent.withAlpha(0.07f));
        g.fillPath(fillPath);
        g.setColour(accent.withAlpha(0.92f));
        g.strokePath(p, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));
    }
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
        character.setSubtitle("ANALOG DRIVE");
        performance.setSubtitle("VOICE / WIDTH");
        matrix.setSubtitle("5 / 16");
        for (auto* visual : { &oscillator, &filter, &envelope, &character, &accentPanel,
                              &performance, &modulation, &matrix })
            visual->setVisible(true);
        for (std::size_t i = 0; i < 6; ++i) {
            knobs[i]->setKnobSize(vox::ui::VoxKnob::Size::Small);
            knobs[i]->setVisible(true);
        }
    } else if (layout == Layout::acid) {
        oscillator.setSubtitle("303 CORE");
        filter.setSubtitle("24 DB");
        envelope.setSubtitle("AMP / FILTER");
        character.setSubtitle("ANALOG TYPE");
        accentPanel.setSubtitle("AMOUNT");
        performance.setSubtitle("SLIDE");
        sequencer.setSubtitle("16 STEP / PATTERN PREVIEW");
        playMode.setSubtitle("MONO");
        output.setSubtitle("MASTER");
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
    content.removeFromTop(24);
    const int width = content.getWidth() / static_cast<int>(indices.size());
    int x = content.getX();
    for (const auto index : indices) {
        if (index >= knobs.size())
            continue;
        auto cell = juce::Rectangle<int>(x, content.getY(), width, content.getHeight()).reduced(2);
        knobs[index]->setBounds(cell);
        x += width;
    }
}

void VstEngineAudioProcessorEditor::SoundPage::resized()
{
    auto area = getLocalBounds().reduced(8);
    const int heroHeight = juce::jlimit(82, 122, static_cast<int>(area.getHeight() * 0.19f));
    hero.setBounds(area.removeFromTop(heroHeight));
    area.removeFromTop(7);

    constexpr int gap = 7;
    if (layout == Layout::psyBass) {
        const int row1H = juce::jlimit(150, 224, static_cast<int>(area.getHeight() * 0.42f));
        const int row2H = juce::jlimit(104, 142, static_cast<int>(area.getHeight() * 0.27f));
        auto row1 = area.removeFromTop(juce::jmin(row1H, area.getHeight()));
        area.removeFromTop(gap);
        auto row2 = area.removeFromTop(juce::jmin(row2H, area.getHeight()));
        area.removeFromTop(gap);
        auto row3 = area;

        const int oscW = static_cast<int>((row1.getWidth() - gap * 2) * 0.31f);
        const int filterW = static_cast<int>((row1.getWidth() - gap * 2) * 0.37f);
        const auto osc = row1.removeFromLeft(oscW); row1.removeFromLeft(gap);
        const auto fil = row1.removeFromLeft(filterW); row1.removeFromLeft(gap);
        const auto env = row1;
        oscillator.setBounds(osc); filter.setBounds(fil); envelope.setBounds(env);
        placeKnobs(fil, { 0, 1 });
        placeKnobs(env, { 2, 3 });

        const int charW = static_cast<int>((row2.getWidth() - gap * 2) * 0.31f);
        const int accentW = static_cast<int>((row2.getWidth() - gap * 2) * 0.37f);
        const auto chr = row2.removeFromLeft(charW); row2.removeFromLeft(gap);
        const auto acc = row2.removeFromLeft(accentW); row2.removeFromLeft(gap);
        const auto perf = row2;
        character.setBounds(chr); accentPanel.setBounds(acc); performance.setBounds(perf);
        placeKnobs(chr, { 4 });
        placeKnobs(acc, { 5 });

        const int modW = static_cast<int>((row3.getWidth() - gap) * 0.52f);
        const auto mod = row3.removeFromLeft(modW); row3.removeFromLeft(gap);
        modulation.setBounds(mod); matrix.setBounds(row3);
    } else if (layout == Layout::acid) {
        const int topH = juce::jlimit(118, 168, static_cast<int>(area.getHeight() * 0.30f));
        auto top = area.removeFromTop(topH);
        area.removeFromTop(gap);

        const int usable = top.getWidth() - gap * 5;
        const int oscW = static_cast<int>(usable * 0.19f);
        const int filterW = static_cast<int>(usable * 0.20f);
        const int envW = static_cast<int>(usable * 0.24f);
        const int driveW = static_cast<int>(usable * 0.14f);
        const int accentW = static_cast<int>(usable * 0.105f);

        const auto osc = top.removeFromLeft(oscW); top.removeFromLeft(gap);
        const auto fil = top.removeFromLeft(filterW); top.removeFromLeft(gap);
        const auto env = top.removeFromLeft(envW); top.removeFromLeft(gap);
        const auto drv = top.removeFromLeft(driveW); top.removeFromLeft(gap);
        const auto acc = top.removeFromLeft(accentW); top.removeFromLeft(gap);
        const auto side = top;
        auto slide = side;
        auto out = side;
        slide.setHeight((side.getHeight() - gap) / 2);
        out.setY(slide.getBottom() + gap);
        out.setHeight(side.getBottom() - out.getY());

        oscillator.setBounds(osc); filter.setBounds(fil); envelope.setBounds(env);
        character.setBounds(drv); accentPanel.setBounds(acc); performance.setBounds(slide); output.setBounds(out);
        placeKnobs(osc, { 0 });
        placeKnobs(fil, { 1, 2 });
        placeKnobs(env, { 3, 4 });
        placeKnobs(drv, { 7 });
        placeKnobs(acc, { 5 });
        placeKnobs(slide, { 6 });

        const int bottomH = juce::jlimit(96, 138, static_cast<int>(area.getHeight() * 0.28f));
        auto bottom = area.removeFromBottom(juce::jmin(bottomH, area.getHeight()));
        area.removeFromBottom(gap);
        sequencer.setBounds(area);

        const int modW = static_cast<int>((bottom.getWidth() - gap * 2) * 0.52f);
        const int perfW = static_cast<int>((bottom.getWidth() - gap * 2) * 0.28f);
        const auto mod = bottom.removeFromLeft(modW); bottom.removeFromLeft(gap);
        const auto perf = bottom.removeFromLeft(perfW); bottom.removeFromLeft(gap);
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
      rackRail(), instrumentHeader(p), soundPage(p), patternPage(p),
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
    const int keyboardHeight = juce::jlimit(86, 104, static_cast<int>(getHeight() * 0.105f));
    auto keyboardArea = area.removeFromBottom(keyboardHeight).reduced(8, 7);
    keyboard.setBounds(keyboardArea);
    keyboard.setKeyWidth(static_cast<float>(keyboardArea.getWidth()) / 43.0f);
    const int railWidth = juce::jlimit(220, 300, static_cast<int>(getWidth() * 0.205f));
    auto rail = area.removeFromLeft(railWidth).reduced(8, 7);
    rackRail.setBounds(rail);
    area = area.reduced(5, 7);
    instrumentHeader.setBounds(area.removeFromTop(66));
    navigation.setBounds(area.removeFromTop(44).reduced(0, 4));
    for (auto* page : pages) page->setBounds(area);
}

void VstEngineAudioProcessorEditor::bindSelection(std::size_t slot)
{
    processor.selectSlot(slot);
    instrumentHeader.bind(slot); soundPage.bind(slot); patternPage.bind(slot);
    routingPage.bind(slot); zonesPage.bind(slot); macrosPage.bind(slot); advancedPage.bind(slot);
    refreshRack();
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
    processor.publishSequenceForAudio(); refreshRack(); instrumentHeader.refresh();
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
