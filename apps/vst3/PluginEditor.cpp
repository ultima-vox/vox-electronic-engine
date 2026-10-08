#include "PluginEditor.h"
#include "instrument/HostParameterSchema.h"
#include "modules/BuiltInProvider.h"
#include "ui/common/UiComponents.h"
#include "vox-ui/Typography.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

using Page = vstengine::ui::MainNavigation::Page;

// ===========================================================================
// Shell band geometry
// ===========================================================================
//
// Reference values are the brief's accepted measurements at 1448x1086. Vertical
// positions are derived from the reference value scaled by (height / 1086) with
// a floor, so every band starts where the render says it starts at the reference
// size and no band can vanish or invert at a smaller one.
namespace {

constexpr int refHeaderBottom = 74;    // global header, full width
constexpr int refNavTop = 150;         // nav tab strip
constexpr int refHeroTop = 190;        // hero band
constexpr int refRow1Top = 340;        // module row 1
constexpr int refRow2Top = 600;        // module row 2
constexpr int refRow3Top = 745;        // module row 3
constexpr int refFooterTop = 906;      // keyboard/footer band
constexpr int refBottomMargin = 8;

constexpr int refRailLeft = 8;
constexpr int refRailRight = 318;
constexpr int refContentRight = 1435;

constexpr int refHeaderMinHeight = 52;
constexpr int refNavMinHeight = 26;
constexpr int refHeroMinHeight = 74;
constexpr int refInstrumentHeaderMinHeight = 46;
constexpr int refRowMinHeight = 40;
constexpr int refFooterMinHeight = 70;

int bandFloor (const int referenceValue)
{
    return juce::jmax (4, juce::roundToInt (static_cast<float> (referenceValue) * 0.55f));
}

// Linear map from reference vertical position to editor position, with a floor so
// compact heights compress the shell instead of squeezing bands to nothing.
juce::Rectangle<int> mapBand (const int top, const int bottom, const int width,
                              const int editorHeight, const int minHeight)
{
    const auto scale = static_cast<float> (editorHeight)
                     / static_cast<float> (vstengine::ui::shellReferenceHeight);
    const auto y0 = juce::roundToInt (static_cast<float> (top) * scale);
    auto y1 = juce::roundToInt (static_cast<float> (bottom) * scale);

    if (y1 - y0 < minHeight)
        y1 = y0 + minHeight;

    if (y1 > editorHeight)
        y1 = editorHeight;

    return { 0, y0, width, juce::jmax (0, y1 - y0) };
}

} // namespace

vstengine::ui::ShellLayout vstengine::ui::makeShellLayout (const int width, const int height)
{
    ShellLayout layout;

    const auto w = juce::jmax (320, width);
    const auto h = juce::jmax (320, height);
    const auto scaleY = static_cast<float> (h) / static_cast<float> (shellReferenceHeight);

    // Vertical bands, contiguous by construction.
    layout.header = mapBand (0, refHeaderBottom, w, h, refHeaderMinHeight);
    layout.instrumentHeader = mapBand (refHeaderBottom, refNavTop, w, h,
                                       refInstrumentHeaderMinHeight);
    layout.navigation = mapBand (refNavTop, refHeroTop, w, h, refNavMinHeight);
    layout.hero = mapBand (refHeroTop, refRow1Top, w, h, refHeroMinHeight);

    // Horizontal scale. The rail keeps its reference fraction of the width and
    // the content column is everything to its right; contentRight is clamped
    // inside the editor so the content column is never wider than the window.
    const auto scaleX = static_cast<float> (w) / static_cast<float> (shellReferenceWidth);
    const auto railRight = juce::jlimit (refRailLeft + 80, juce::jmax (refRailLeft + 80, w - 200),
                                         juce::roundToInt (static_cast<float> (refRailRight) * scaleX));
    const auto contentX = railRight;
    const auto contentRight = juce::jmax (contentX + 120,
                                          juce::jmin (w - juce::roundToInt (13.0f * scaleX),
                                                      juce::roundToInt (static_cast<float> (refContentRight)
                                                                        * scaleX)));

    // The footer is anchored to the bottom edge and the reference bottom gutter
    // is subtracted from it, so the rail's bottom margin and the footer's are the
    // same object.
    const auto bottomGutter = bandFloor (refBottomMargin);
    layout.footer = { contentX, juce::jmin (juce::roundToInt (static_cast<float> (refFooterTop) * scaleY),
                                            h - refFooterMinHeight),
                      juce::jmax (1, contentRight - contentX), 0 };
    layout.footer.setBottom (juce::jmax (layout.footer.getY() + refFooterMinHeight,
                                         h - bottomGutter));

    // Three module rows distributed across the space between the hero and the
    // footer, preserving the reference proportions (250 / 135 / 145).
    const auto rowsTop = layout.hero.getBottom();
    const auto rowsBottom = juce::jmax (rowsTop + 3 * refRowMinHeight, layout.footer.getY());
    const auto rowSpan = rowsBottom - rowsTop;
    const auto totalRefSpan = static_cast<float> (refFooterTop - refRow1Top);

    auto rowOffset = [&] (const int referencePosition)
    {
        return juce::roundToInt (static_cast<float> (rowSpan)
                                 * static_cast<float> (referencePosition - refRow1Top) / totalRefSpan);
    };

    layout.row1 = { contentX, rowsTop, juce::jmax (1, contentRight - contentX),
                    juce::jmax (1, rowOffset (refRow2Top)) };
    layout.row2 = { contentX, layout.row1.getBottom(), juce::jmax (1, contentRight - contentX),
                    juce::jmax (1, rowOffset (refRow3Top) - rowOffset (refRow2Top)) };
    layout.row3 = { contentX, layout.row2.getBottom(), juce::jmax (1, contentRight - contentX),
                    juce::jmax (1, rowsBottom - layout.row2.getBottom()) };

    // Full-height rail. Its top edge overlaps the global header by the reference
    // 12 px, which is what makes the rail read as the tallest object in the
    // composition.
    const auto railTop = juce::jmax (0, layout.header.getBottom() - juce::roundToInt (12.0f * scaleY));
    layout.rail = { refRailLeft, railTop, juce::jmax (60, railRight - refRailLeft),
                    juce::jmax (100, h - bottomGutter - railTop) };

    // Content-column bands: inset for the chrome rows, flush for the module rows
    // and the footer.
    const auto contentInset = juce::roundToInt (4.0f * scaleX);
    layout.instrumentHeader = layout.instrumentHeader.withLeft (contentX + contentInset)
                                                     .withRight (contentRight - contentInset);
    layout.navigation = layout.navigation.withLeft (contentX + contentInset)
                                         .withRight (contentRight - contentInset);
    layout.hero = layout.hero.withLeft (contentX + contentInset)
                             .withRight (contentRight - contentInset);

    // The workspace is the content column between the hero and the footer. It is
    // intentionally disjoint from the rail (different x) and from the footer
    // (different y), which is the invariant the shell smoke test asserts.
    layout.workspace = { contentX, layout.hero.getBottom(), juce::jmax (1, contentRight - contentX),
                         juce::jmax (1, layout.footer.getY() - layout.hero.getBottom()) };

    return layout;
}

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

// ===========================================================================
// Footer: Pitch/Mod faders, KEYBOARD/CHORDS/SCALE strip, velocity curve,
// MIDI Learn, keyboard-layout affordance and the piano.
// ===========================================================================
VstEngineAudioProcessorEditor::WheelFader::WheelFader (juce::String labelText)
    : label (std::move (labelText))
{
    setSliderStyle (juce::Slider::LinearVertical);
    setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    setRange (0.0, 1.0, 0.0);
    setValue (0.5, juce::dontSendNotification);
    setTooltip (label);
    setWantsKeyboardFocus (true);
}

void VstEngineAudioProcessorEditor::WheelFader::paint (juce::Graphics& g)
{
    // Self-painting on purpose. The installed product LookAndFeel has no
    // drawLinearSlider override, so a stock juce::Slider here would render with
    // JUCE's default chrome, which is not production acceptance. Drawing the
    // component ourselves also keeps the wheel inside the canonical palette.
    auto bounds = getLocalBounds().toFloat();

    auto labelArea = bounds.removeFromBottom (14.0f);
    auto track = bounds.reduced (bounds.getWidth() * 0.18f, 4.0f);

    if (track.getWidth() < 4.0f || track.getHeight() < 8.0f)
        return;

    g.setColour (vstengine::ui::colours::control);
    g.fillRoundedRectangle (track, 3.0f);
    g.setColour (vstengine::ui::colours::border);
    g.drawRoundedRectangle (track, 3.0f, 1.0f);

    // Centre detent: a pitch wheel reads as "centred at rest", so the reference
    // line is part of the affordance rather than decoration.
    g.setColour (vstengine::ui::colours::borderSubtle);
    g.drawLine (track.getX() + 1.0f, track.getCentreY(),
                track.getRight() - 1.0f, track.getCentreY(), 1.0f);

    const auto value01 = static_cast<float> ((getValue() - getMinimum())
                                             / juce::jmax (1.0e-6, getMaximum() - getMinimum()));
    const auto gripHeight = juce::jmax (7.0f, track.getHeight() * 0.16f);
    const auto travel = track.getHeight() - gripHeight - 4.0f;
    const auto gripY = track.getBottom() - 2.0f - gripHeight - value01 * travel;

    auto grip = juce::Rectangle<float> (track.getX() + 2.0f, gripY,
                                        juce::jmax (3.0f, track.getWidth() - 4.0f), gripHeight);
    g.setColour (vstengine::ui::colours::panelRaised.brighter (0.12f));
    g.fillRoundedRectangle (grip, 2.0f);
    g.setColour (vstengine::ui::colours::primary.withAlpha (0.85f));
    g.drawRoundedRectangle (grip, 2.0f, 1.2f);

    for (int i = 1; i <= 4; ++i)
    {
        const auto y = track.getY() + track.getHeight() * static_cast<float> (i) / 5.0f;
        g.setColour (vstengine::ui::colours::borderSubtle.withAlpha (0.7f));
        g.drawLine (track.getX() + 3.0f, y, track.getRight() - 3.0f, y, 1.0f);
    }

    g.setColour (vstengine::ui::colours::textSecondary);
    g.setFont (vox::ui::typography::valueText());
    g.drawText (label, labelArea, juce::Justification::centred, false);
}

VstEngineAudioProcessorEditor::FooterBar::FooterBar (VstEngineAudioProcessor& p)
    : keyboard (p.keyboardState(), juce::MidiKeyboardComponent::horizontalKeyboard)
{
    // KEYBOARD / CHORDS / SCALE is the footer's own mode strip. It reuses the
    // shared VoxTabBar so its geometry (including the 5 px gutter) cannot drift
    // from the main navigation tabs.
    modeTabs.setTabs ({
        { "keyboard", "KEYBOARD", true },
        { "chords", "CHORDS", true },
        { "scale", "SCALE", true }
    });
    modeTabs.setSelectedIndex (0);
    addAndMakeVisible (modeTabs);

    velocityCurveLabel.setText ("Velocity Curve", juce::dontSendNotification);
    velocityCurveLabel.setFont (vox::ui::typography::valueText());
    velocityCurveLabel.setJustificationType (juce::Justification::centredRight);
    velocityCurveLabel.setColour (juce::Label::textColourId,
                                  vstengine::ui::colours::textSecondary);
    velocityCurveLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (velocityCurveLabel);

    // Gate A only: the velocity curve is display chrome in the accepted render.
    // The selector is a real VoxComboBox, so it cannot open an empty menu or
    // render a stock arrow; it carries a declared value and reports its state
    // through getGateAVisualOnlyLabels() on the page that owns it.
    velocityCurve.addItemList ({ "Normal", "Soft", "Hard", "Fixed" }, 1);
    velocityCurve.setSelectedItemIndex (0, juce::dontSendNotification);
    velocityCurve.setTooltip ("Velocity curve (visual gate only)");
    addAndMakeVisible (velocityCurve);

    // Gate A only: MIDI Learn is an affordance in the accepted render. It is
    // present, declared and inert; it never fakes a bound parameter.
    midiLearn.setTooltip ("MIDI Learn (visual gate only)");
    addAndMakeVisible (midiLearn);

    keyboardLayout.setTooltip ("Keyboard layout (visual gate only)");
    addAndMakeVisible (keyboardLayout);

    addAndMakeVisible (pitch);
    addAndMakeVisible (mod);

    keyboard.setAvailableRange (keyboardLowestNote, keyboardHighestNote);
    // The accepted render labels the range C0..C6 on a standard MIDI keyboard.
    // This build's range is C1..C5 (five octaves, which is the span the footer
    // can hold), and JUCE derives the printed octave from its own middle-C
    // convention. 3 == the MIDI default, so the labels read C1..C5 with no
    // product-specific octave offset.
    keyboard.setOctaveForMiddleC (3);
    keyboard.setScrollButtonsVisible (false);
    addAndMakeVisible (keyboard);
}

void VstEngineAudioProcessorEditor::FooterBar::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (0.5f);

    g.setColour (vstengine::ui::colours::panel);
    g.fillRoundedRectangle (bounds, vstengine::ui::metrics::corner);
    g.setColour (vstengine::ui::colours::border);
    g.drawRoundedRectangle (bounds, vstengine::ui::metrics::corner, 1.0f);

    // The keyboard gets its own recessed surface, which is what separates the
    // piano from the footer chrome in the accepted render.
    const auto piano = keyboard.getBounds().toFloat().expanded (3.0f, 3.0f);
    if (! piano.isEmpty())
    {
        g.setColour (vstengine::ui::colours::graphSurface);
        g.fillRoundedRectangle (piano, 4.0f);
        g.setColour (vstengine::ui::colours::borderSubtle);
        g.drawRoundedRectangle (piano, 4.0f, 1.0f);
    }
}

void VstEngineAudioProcessorEditor::FooterBar::resized()
{
    auto area = getLocalBounds().reduced (6, 5);

    // --- Toolbar row -------------------------------------------------------
    auto toolbar = area.removeFromTop (juce::jlimit (24, 32, area.getHeight() / 5));

    auto tabs = toolbar.removeFromLeft (juce::jlimit (170, 260,
                                                      juce::roundToInt (static_cast<float> (toolbar.getWidth()) * 0.26f)));
    modeTabs.setBounds (tabs.reduced (0, 1));

    // Right-hand controls, claimed right to left so their right edge is stable.
    auto layoutButton = toolbar.removeFromRight (juce::jlimit (26, 34, toolbar.getHeight()));
    keyboardLayout.setBounds (layoutButton.reduced (2, 1));

    toolbar.removeFromRight (6);
    auto learnButton = toolbar.removeFromRight (juce::jlimit (74, 96,
                                                             juce::roundToInt (static_cast<float> (layoutButton.getWidth()) * 3.0f)));
    midiLearn.setBounds (learnButton.reduced (2, 1));

    toolbar.removeFromRight (6);
    auto curveBox = toolbar.removeFromRight (juce::jlimit (86, 120,
                                                           juce::roundToInt (static_cast<float> (learnButton.getWidth()) * 1.25f)));
    velocityCurve.setBounds (curveBox.reduced (2, 1));

    auto curveLabel = toolbar.removeFromRight (juce::jlimit (70, 104,
                                                             juce::roundToInt (static_cast<float> (curveBox.getWidth()) * 1.1f)));
    velocityCurveLabel.setBounds (curveLabel.reduced (4, 0));

    // --- Piano row ---------------------------------------------------------
    area.removeFromTop (5);

    // Pitch/Mod column. The wheels are deliberately narrow: they are a column of
    // two vertical faders, not a pair of knobs.
    auto wheels = area.removeFromLeft (juce::jlimit (34, 46,
                                                     juce::roundToInt (static_cast<float> (area.getHeight()) * 0.30f)));
    wheels.removeFromRight (2);
    pitch.setBounds (wheels.removeFromLeft (wheels.getWidth() / 2));
    mod.setBounds (wheels);

    area.removeFromLeft (8);

    // The piano is scaled to fill exactly the space the footer leaves for it, so
    // the rendered key geometry always fits the component. `fitsWidth` is the
    // width the requested range needs at the chosen key width; the component is
    // given exactly that, capped by what is actually available.
    const auto available = juce::jmax (1, area.getWidth());
    const auto rangeSpan = static_cast<float> (keyboardHighestNote - keyboardLowestNote) + 1.0f;
    const auto keyWidth = juce::jmax (minKeyWidth, static_cast<float> (available) / rangeSpan);
    const auto fitsWidth = juce::roundToInt (keyWidth * rangeSpan);

    // Set the bounds first and the key width second: KeyboardComponentBase
    // recomputes its scroll state in resized(), and setKeyWidth is the call that
    // owns the key geometry, so it must be the last word.
    keyboard.setBounds (area.removeFromLeft (juce::jmin (available, fitsWidth)));
    keyboard.setKeyWidth (keyWidth);
}

// ===========================================================================
// Shell callbacks
// ===========================================================================
void VstEngineAudioProcessorEditor::SequenceCallbacks::onCopy() { processor.sequence().copyTo(clipboard); copied = true; }void VstEngineAudioProcessorEditor::SequenceCallbacks::onPaste() { if (copied) processor.sequence().paste(clipboard); }
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
            ? juce::String (descriptor->instrumentVersion)
            : juce::String();
        view.descriptorLine = view.occupied
            ? vstengine::ui::rackDescriptorLine (
                  descriptor->id, view.vendorName,
                  descriptor->instrumentVersion)
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
        view.accent = view.occupied
            ? vstengine::ui::identityAccentFor (descriptor->id)
            : vstengine::ui::colours::primary;
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
    // Accepted render: ~26 px instrument title (typography::instrumentTitlePx).
    vstengine::ui::styleLabel (title, vox::ui::typography::instrumentTitlePx,
                               juce::Justification::centredLeft,
                               vstengine::ui::colours::text);
    // Meta line: `Slot N | CHn | <engine> <major.minor>`.
    vstengine::ui::styleLabel (meta, 12.0f, juce::Justification::centredLeft,
                               vstengine::ui::colours::mutedText);
    vstengine::ui::styleLabel (channelLabel, 12.0f, juce::Justification::centredRight,
                               vstengine::ui::colours::textSecondary);

    // Instrument selector. It owns the prev/next chevrons (Stepper form), so the
    // arrows step the INSTRUMENT, which is what the accepted render shows; the
    // sound preset is a separate dropdown below them.
    instrument.setPlaceholderText ("Choose instrument");
    int item = 1;
    for (const auto* descriptor : processor.availableInstruments())
        instrument.getComboBox().addItem (descriptor->name, item++);

    instrument.onChanged = [this] (int index)
    {
        const auto descriptors = processor.availableInstruments();
        if (index < 0 || index >= static_cast<int> (descriptors.size()))
            return;

        juce::String diagnostic;
        processor.loadSlotInstrument (selected, descriptors[static_cast<std::size_t> (index)]->id,
                                      diagnostic);
        bind (selected);
        if (onModelChanged) onModelChanged();
    };
    auto stepInstrument = [this] (int delta)
    {
        auto& box = instrument.getComboBox();
        if (box.getNumItems() == 0)
            return;

        const auto next = (box.getSelectedItemIndex() + delta + box.getNumItems())
                        % box.getNumItems();
        box.setSelectedItemIndex (next, juce::sendNotificationAsync);
    };
    instrument.onPrevious = [stepInstrument] { stepInstrument (-1); };
    instrument.onNext = [stepInstrument] { stepInstrument (1); };

    // Sound preset dropdown. The empty-combo regression came from the reworked
    // VoxComboBox no longer surfacing juce::ComboBox's
    // `setTextWhenNothingSelected` text, so the placeholder is now supplied
    // through the box's own placeholder API.
    preset.setPlaceholderText ("Sound preset");
    preset.onChange = [this]
    {
        const auto index = preset.getSelectedItemIndex();
        if (index < 0 || index >= static_cast<int> (presetIds.size()))
            return;

        juce::String diagnostic;
        processor.applySelectedSoundPreset (presetIds[static_cast<std::size_t> (index)], diagnostic);
    };

    midiIn.addItem ("OFF", 1);
    for (int channel = 1; channel <= 16; ++channel)
        midiIn.addItem ("CH" + juce::String (channel), channel + 1);
    midiIn.setTooltip ("MIDI channel");

    const std::array<juce::Component*, 9> components {
        &title, &meta, &rename, &instrument, &preset, &favourite,
        &channelLabel, &midiIn, nullptr
    };
    for (auto* component : components)
        if (component != nullptr)
            addAndMakeVisible (component);

    rename.setTooltip ("Rename instrument");
    favourite.setTooltip ("Favourite");

    midiIn.onChange = [this]
    {
        juce::String diagnostic;
        const int requested = midiIn.getSelectedId() - 1;
        if (! processor.assignSlotChannel (selected, requested,
                VstEngineAudioProcessor::ChannelConflictAction::reject, diagnostic))
            refresh();
        if (onModelChanged) onModelChanged();
    };
}

void VstEngineAudioProcessorEditor::InstrumentHeader::paint(juce::Graphics& g) { panel(g, getLocalBounds()); }

void VstEngineAudioProcessorEditor::InstrumentHeader::resized()
{
    auto area = getLocalBounds().reduced (14, 6);

    // Two internal rows: the identity/control row on top, the meta line below it.
    // The meta line is what the accepted render puts under the title
    // (`Slot 1 | CH1 | Ultima Vox 1.0`).
    auto metaRow = area.removeFromBottom (juce::jlimit (14, 20, area.getHeight() / 3));
    auto topRow = area;

    meta.setBounds (metaRow);

    // Identity block: ~26 px title plus the pencil rename affordance.
    auto identity = topRow.removeFromLeft (juce::jlimit (140, 260,
                                                         juce::roundToInt (static_cast<float> (getWidth()) * 0.20f)));
    auto titleRow = identity.reduced (0, juce::jmax (0, identity.getHeight() / 8));
    title.setBounds (titleRow.removeFromLeft (juce::jmax (70, titleRow.getWidth() - 30)));
    rename.setBounds (titleRow.removeFromLeft (juce::jmin (26, titleRow.getWidth())).reduced (1));

    // Instrument selector with its two chevrons (the VoxInlineSelector Stepper
    // form) sits in the remaining space, centred, exactly where the accepted
    // render puts it.
    const auto rightColumnWidth = juce::jlimit (150, 250,
                                                juce::roundToInt (static_cast<float> (getWidth()) * 0.19f));
    const auto selectorSlotWidth = juce::jmax (150, topRow.getWidth() - rightColumnWidth - 150);
    auto selectorSlot = topRow.removeFromLeft (selectorSlotWidth);
    instrument.setBounds (selectorSlot.removeFromLeft (juce::jmax (140, selectorSlot.getWidth() - 8))
                              .reduced (0, juce::jmax (2, selectorSlot.getHeight() / 8)));

    // The sound preset dropdown lives to the right of the instrument selector and
    // before the right-hand cluster.
    preset.setBounds (topRow.removeFromLeft (juce::jmax (110, topRow.getWidth() - rightColumnWidth))
                            .reduced (0, juce::jmax (2, topRow.getHeight() / 8)));

    // Right-hand cluster, claimed right to left.
    auto right = topRow;
    channelLabel.setBounds (right.removeFromRight (62));
    midiIn.setBounds (right.removeFromRight (juce::jlimit (70, 100, right.getWidth() / 5))
                           .reduced (0, juce::jmax (2, right.getHeight() / 8)));
    right.removeFromRight (6);
    favourite.setBounds (right.removeFromRight (32).reduced (1, juce::jmax (2, right.getHeight() / 8)));
}

void VstEngineAudioProcessorEditor::InstrumentHeader::bind(std::size_t slot)
{
    selected = slot;
    const auto& state = processor.instrumentRack().state()[selected];
    const auto descriptors = processor.availableInstruments();
    int descriptorIndex = -1;
    for (std::size_t i = 0; i < descriptors.size(); ++i)
        if (descriptors[i]->id == state.instrumentId) descriptorIndex = static_cast<int>(i);
    instrument.setSelectedIndex (descriptorIndex);
    preset.clear(juce::dontSendNotification); presetIds.clear();
    for (const auto& content : processor.selectedContent())
        if (content.kind == vstengine::instrument::ContentKind::soundPreset) {
            presetIds.push_back(content.id);
            preset.addItem(content.name, static_cast<int>(presetIds.size()));
        }
    if (! presetIds.empty())
        preset.setSelectedItemIndex (0, juce::dontSendNotification);
    refresh();
}

void VstEngineAudioProcessorEditor::InstrumentHeader::refresh()
{
    const auto& state = processor.instrumentRack().state()[selected];
    const auto* descriptor = processor.instrumentRack().descriptor(selected);

    const int channel = state.routing.mode == vstengine::rack::RouteMode::off
                            ? 0 : static_cast<int> (state.routing.channel);

    title.setText (descriptor != nullptr ? juce::String (descriptor->name)
                                         : juce::String ("Empty slot"),
                   juce::dontSendNotification);

    // The accepted render tints the instrument title with the instrument's own
    // identity accent rather than leaving it white.
    title.setColour (juce::Label::textColourId,
                     descriptor != nullptr
                         ? vstengine::ui::identityAccentFor (descriptor->id)
                         : vstengine::ui::colours::mutedText);

    // Meta line, character-exact shape: `Slot N | CHn | <engine> <major.minor>`.
    if (descriptor != nullptr)
    {
        const auto engineVersion = vstengine::ui::rackDescriptorLine (
            descriptor->id, juce::String (descriptor->vendor), descriptor->instrumentVersion);

        meta.setText ("Slot " + juce::String (static_cast<int> (selected + 1))
                          + "  |  " + (channel == 0 ? juce::String ("OFF")
                                                    : "CH" + juce::String (channel))
                          + "  |  " + engineVersion,
                      juce::dontSendNotification);
    }
    else
    {
        meta.setText ("Slot " + juce::String (static_cast<int> (selected + 1))
                          + "  |  OFF  |  Choose instrument to activate slot",
                      juce::dontSendNotification);
    }

    channelLabel.setText ("Channel", juce::dontSendNotification);
    midiIn.setSelectedId (channel + 1, juce::dontSendNotification);
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
      rackRail(), instrumentHeader(p), soundPage(), patternPage(p),
      routingPage(p), zonesPage(p), macrosPage(p, "MACROS", 8), advancedPage(p),
      pages { &soundPage, &patternPage, &routingPage, &zonesPage, &macrosPage, &advancedPage },
      footer(p)
{
    setLookAndFeel(&lookAndFeel);
    addAndMakeVisible(header); addAndMakeVisible(rackRail); addAndMakeVisible(instrumentHeader);
    addAndMakeVisible(navigation);
    for (auto* page : pages) addChildComponent(page);
    addAndMakeVisible(footer);
    rackRail.onSelected = [this](std::size_t slot) { bindSelection(slot); };
    instrumentHeader.onModelChanged = [this] { bindSelection(processor.selectedSlotIndex()); };
    navigation.onPageChanged = [this](Page page) { showPage(page); };
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
    // One band table drives the whole shell. See makeShellLayout() above.
    const auto shell = vstengine::ui::makeShellLayout (getWidth(), getHeight());

    header.setBounds (shell.header);
    rackRail.setBounds (shell.rail);
    instrumentHeader.setBounds (shell.instrumentHeader);
    navigation.setBounds (shell.navigation);
    soundPage.setHeroBounds (shell.hero);

    // The workspace is the content column between the hero and the footer. Module
    // panels therefore end exactly where the footer begins, which is what the
    // accepted render shows.
    for (auto* page : pages)
        page->setBounds (shell.workspace);

    footer.setBounds (shell.footer);
}

void VstEngineAudioProcessorEditor::bindSelection(std::size_t slot)
{
    processor.selectSlot(slot);
    instrumentHeader.bind(slot);
    soundPage.bind(processor.parameters(), slot, processor.instrumentRack().descriptor(slot));
    patternPage.bind(slot);
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
juce::Rectangle<int> VstEngineAudioProcessorEditor::rackBoundsForTesting() const noexcept { return rackRail.getBounds(); }
juce::Rectangle<int> VstEngineAudioProcessorEditor::workspaceBoundsForTesting() const noexcept { return pages[static_cast<std::size_t>(navigation.getCurrentPage())]->getBounds(); }
juce::Rectangle<int> VstEngineAudioProcessorEditor::keyboardBoundsForTesting() const noexcept
{
    // EDITOR SPACE. The piano is a child of the footer, so its own bounds are
    // footer-local; callers (and the shell smoke test) reason in editor space, so
    // the offset is applied here once rather than at every call site.
    return footer.getKeyboard().getBounds() + footer.getPosition();
}

juce::Rectangle<int> VstEngineAudioProcessorEditor::keyboardLocalBoundsForTesting() const noexcept
{
    return footer.getKeyboard().getBounds();
}

vstengine::ui::ShellLayout VstEngineAudioProcessorEditor::shellLayoutForTesting() const noexcept
{
    return vstengine::ui::makeShellLayout (getWidth(), getHeight());
}

float VstEngineAudioProcessorEditor::keyboardKeySpanForTesting() const noexcept
{
    // Read the span back out of the component's own key geometry rather than
    // recomputing keyWidth * noteCount, which would make the smoke test a
    // tautology: this is the width the keys actually occupy, including the
    // partial key at the top of the range.
    const auto& keys = footer.getKeyboard();
    const auto lowest = keys.getRectangleForKey (keys.getRangeStart());
    const auto highest = keys.getRectangleForKey (keys.getRangeEnd());

    return highest.getRight() - lowest.getX();
}

int VstEngineAudioProcessorEditor::keyboardLowestNoteForTesting() const noexcept
{
    return footer.getKeyboard().getRangeStart();
}

int VstEngineAudioProcessorEditor::keyboardHighestNoteForTesting() const noexcept
{
    return footer.getKeyboard().getRangeEnd();
}

float VstEngineAudioProcessorEditor::keyboardKeyWidthForTesting() const noexcept
{
    return footer.getKeyboard().getKeyWidth();
}

juce::Rectangle<float> VstEngineAudioProcessorEditor::keyboardKeyBoundsForTesting (const int note) const noexcept
{
    const auto& keys = footer.getKeyboard();

    if (note < keys.getRangeStart() || note > keys.getRangeEnd())
        return {};

    return keys.getRectangleForKey (note);
}

juce::Rectangle<int> VstEngineAudioProcessorEditor::footerBoundsForTesting() const noexcept
{
    return footer.getBounds();
}

bool VstEngineAudioProcessorEditor::keyboardKeysFitComponentForTesting() const noexcept
{
    const auto& keys = footer.getKeyboard();

    if (keys.getKeyWidth() <= 0.0f)
        return false;

    for (int note = keys.getRangeStart(); note <= keys.getRangeEnd(); ++note)
    {
        const auto bounds = keys.getRectangleForKey (note);

        if (bounds.getX() < -0.5f || bounds.getRight() > static_cast<float> (keys.getWidth()) + 0.5f)
            return false;
    }

    return true;
}

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
