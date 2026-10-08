#include "GlobalHeader.h"
#include "vox-ui/Tokens.h"
#include "vox-ui/Typography.h"

namespace vstengine::ui {

namespace {

void styleHeaderLabel (juce::Label& label,
                       float height,
                       juce::Justification justification,
                       juce::Colour colour)
{
    label.setFont (vox::ui::typography::makeFont (height));
    label.setJustificationType (justification);
    label.setColour (juce::Label::textColourId, colour);
    label.setInterceptsMouseClicks (false, false);
}

// The accepted render draws the output knob's scale as two 9 px strings under
// the knob: `-inf` at the bottom of the scale and `+16` at the top.
constexpr const char* outputScaleLow = "-inf";
constexpr const char* outputScaleHigh = "+16";

} // namespace

juce::StringArray GlobalHeader::getGateAVisualOnlyLabels()
{
    // Declared Gate-A-only chrome. Neither is bound to host state and neither
    // pretends to be: see the header comment.
    return { "Global header cube button", "Global header output scale marks" };
}

GlobalHeader::GlobalHeader (juce::AudioProcessorValueTreeState& s, Callbacks cb)
    : state (s)
{
    brand.setText ("VOX ELECTRONIC ENGINE", juce::dontSendNotification);
    styleHeaderLabel (brand, 17.0f, juce::Justification::centredLeft,
                      vox::ui::tokens::colour::text);

    tagline.setText ("CREATE   EVOLVE   TRANSCEND", juce::dontSendNotification);
    styleHeaderLabel (tagline, 8.0f, juce::Justification::centredLeft,
                      vox::ui::tokens::colour::accent.withAlpha (0.78f));

    preset.setPlaceholderText ("Init Project");
    preset.setCompact (true);

    seedLabel.setText ("Seed", juce::dontSendNotification);
    styleHeaderLabel (seedLabel, 11.0f, juce::Justification::centredRight,
                      vox::ui::tokens::colour::textSecondary);

    seedValue.setText ("0", juce::dontSendNotification);
    styleHeaderLabel (seedValue, 12.0f, juce::Justification::centred,
                      vox::ui::tokens::colour::text);

    midi.setText ("MIDI", juce::dontSendNotification);
    styleHeaderLabel (midi, 10.0f, juce::Justification::centredLeft,
                      vox::ui::tokens::colour::textMuted);

    cpu.setText ("CPU 0%", juce::dontSendNotification);
    styleHeaderLabel (cpu, 10.0f, juce::Justification::centredLeft,
                      vox::ui::tokens::colour::textMuted);

    outputLabel.setText ("Output", juce::dontSendNotification);
    styleHeaderLabel (outputLabel, 10.0f, juce::Justification::centredRight,
                      vox::ui::tokens::colour::textMuted);

    for (auto* label : { &brand, &tagline, &seedLabel, &seedValue, &midi, &cpu, &outputLabel })
        addAndMakeVisible (*label);

    addAndMakeVisible (preset);
    const std::array<juce::Button*, 7> buttons {
        &previousButton, &nextButton, &saveButton, &diceButton,
        &cubeButton, &panicButton, &settingsButton
    };
    for (auto* button : buttons)
        addAndMakeVisible (*button);

    // Vector chevrons, not the text "<" / ">" the previous build used.
    for (auto* button : { &previousButton, &nextButton })
    {
        button->setBorderVisible (false);
        button->setTooltip (button == &previousButton ? "Previous preset" : "Next preset");
    }
    nextButton.setTooltip ("Next preset");

    // Dice = randomise the seed through the SAME path the previous Seed button
    // used. No new state, no new automation id.
    diceButton.setBorderVisible (false);
    diceButton.setTooltip ("Randomise seed");
    diceButton.onClick = [this] { randomizeSeed(); };

    // Gate A only. Present, declared, inert.
    cubeButton.setBorderVisible (false);
    cubeButton.setTooltip ("Browser (visual gate only)");

    // Gear glyph on Settings.
    settingsButton.setIcon (vox::ui::icons::Icon::gear, 14.0f);

    previousButton.onClick = std::move (cb.previous);
    nextButton.onClick = std::move (cb.next);
    saveButton.onClick = std::move (cb.save);
    panicButton.onClick = std::move (cb.panic);
    settingsButton.onClick = std::move (cb.settings);
    preset.onChange = [this, choose = std::move (cb.choosePreset)]
    {
        if (choose)
            choose (preset.getSelectedItemIndex());
    };

    output.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    output.setRotaryParameters (juce::MathConstants<float>::pi * 0.75f,
                                juce::MathConstants<float>::pi * 2.25f, true);
    // No text box: the dB readout under the knob is painted by this component so
    // the scale marks and the value render as one instrument.
    output.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    output.setTooltip ("Global output level");
    output.setColour (juce::Slider::rotarySliderFillColourId, vox::ui::tokens::colour::accent);
    output.setColour (juce::Slider::rotarySliderOutlineColourId, vox::ui::tokens::colour::border);
    addAndMakeVisible (output);

    outputAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        state, "outputLevel", output);

    refreshSeedReadout();
}

void GlobalHeader::randomizeSeed()
{
    if (auto* parameter = state.getParameter ("rngSeed"))
    {
        const auto plain = static_cast<float> (
            juce::Random::getSystemRandom().nextInt (0x7fffffff));
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (plain));
    }

    refreshSeedReadout();
}

void GlobalHeader::refreshSeedReadout()
{
    if (const auto* value = state.getRawParameterValue ("rngSeed"))
        seedValue.setText (juce::String (juce::roundToInt (value->load())),
                           juce::dontSendNotification);
}

void GlobalHeader::setPresetName (const juce::String& name)
{
    preset.setText (name.isEmpty() ? "Init Project" : name, juce::dontSendNotification);
}

void GlobalHeader::setTransportActive (bool)
{
}

void GlobalHeader::setPresetEntries (const juce::StringArray& names, int selectedIndex)
{
    if (presetNames != names)
    {
        presetNames = names;
        preset.clear (juce::dontSendNotification);
        preset.addItemList (names, 1);
    }
    preset.setSelectedItemIndex (selectedIndex, juce::dontSendNotification);
}

void GlobalHeader::setMidiActivity (bool active)
{
    midiActive = active;
    midi.setColour (juce::Label::textColourId,
                    active ? vox::ui::tokens::colour::accent
                           : vox::ui::tokens::colour::textMuted);
    repaint (midiDotBounds.expanded (4));
}

void GlobalHeader::setCpuLoad (float load)
{
    load = juce::jlimit (0.0f, 4.0f, load);
    cpuLoad01 = juce::jlimit (0.0f, 1.0f, load);
    cpu.setText ("CPU " + juce::String (juce::roundToInt (load * 100.0f)) + "%",
                 juce::dontSendNotification);
    cpu.setColour (juce::Label::textColourId,
                   load > 0.8f ? vox::ui::tokens::colour::warning
                               : vox::ui::tokens::colour::textMuted);
    repaint (cpuMeterBounds.expanded (4));
}

void GlobalHeader::paintMidiIndicator (juce::Graphics& g, const juce::Rectangle<int> area)
{
    if (area.isEmpty())
        return;

    // Dim state is still visible: the indicator must read as "a MIDI input lamp
    // that is currently idle", not as a missing control. The previous build drew
    // it in `borderSubtle`, which is indistinguishable from the background.
    g.setColour (midiActive ? vox::ui::tokens::colour::accent
                            : vox::ui::tokens::colour::accent.withAlpha (0.28f));
    g.fillEllipse (area.toFloat());

    g.setColour (vox::ui::tokens::colour::accent.withAlpha (midiActive ? 0.55f : 0.18f));
    g.drawEllipse (area.toFloat().expanded (1.5f), 1.0f);
}

void GlobalHeader::paintCpuMeter (juce::Graphics& g, const juce::Rectangle<int> area)
{
    if (area.isEmpty())
        return;

    constexpr int segments = 8;
    const auto gap = 1.0f;
    const auto segmentWidth = (static_cast<float> (area.getWidth())
                               - gap * static_cast<float> (segments - 1))
                              / static_cast<float> (segments);

    if (segmentWidth < 1.0f)
        return;

    const auto lit = cpuLoad01 * static_cast<float> (segments);
    const auto hot = cpuLoad01 > 0.8f;

    for (int i = 0; i < segments; ++i)
    {
        const auto x = static_cast<float> (area.getX())
                     + static_cast<float> (i) * (segmentWidth + gap);
        const auto segment = juce::Rectangle<float> (x, static_cast<float> (area.getY()),
                                                     segmentWidth,
                                                     static_cast<float> (area.getHeight()));
        const auto isLit = lit > static_cast<float> (i);

        // Unlit segments keep a visible outline, so the meter reads as an
        // 8-segment scale at 0 % instead of disappearing.
        g.setColour (isLit
                         ? (hot ? vox::ui::tokens::colour::warning
                                : vox::ui::tokens::colour::accent)
                         : vox::ui::tokens::colour::background);
        g.fillRoundedRectangle (segment, 1.0f);

        g.setColour (isLit ? juce::Colours::transparentBlack
                           : vox::ui::tokens::colour::borderSubtle);
        if (! isLit)
            g.drawRoundedRectangle (segment.reduced (0.5f), 1.0f, 1.0f);
    }
}

void GlobalHeader::paintOutputScale (juce::Graphics& g, const juce::Rectangle<int> knobArea)
{
    if (knobArea.isEmpty())
        return;

    // Scale marks, from the accepted render: `-inf` under the low end and `+16`
    // under the high end, plus the live readout.
    const auto scaleHeight = juce::jlimit (8, 12, knobArea.getHeight() / 5);
    auto scaleRow = juce::Rectangle<int> (knobArea.getX() - 4,
                                          knobArea.getBottom() - scaleHeight,
                                          knobArea.getWidth() + 8,
                                          scaleHeight);

    g.setColour (vox::ui::tokens::colour::textMuted);
    g.setFont (vox::ui::typography::makeFont (8.5f));
    g.drawText (outputScaleLow, scaleRow.removeFromLeft (scaleRow.getWidth() / 2),
                juce::Justification::centred, false);
    g.drawText (outputScaleHigh, scaleRow, juce::Justification::centred, false);

    // Live readout: the parameter's own value at the parameter's own resolution,
    // drawn above the scale marks. It is not a fabricated dB conversion.
    auto readout = juce::Rectangle<int> (knobArea.getX() + knobArea.getWidth() + 4,
                                         knobArea.getY() + 6,
                                         46, knobArea.getHeight() - 18);
    g.setColour (vox::ui::tokens::colour::text);
    g.setFont (vox::ui::typography::makeFont (13.0f));
    g.drawText (juce::String (output.getValue(), 2), readout,
                juce::Justification::centredLeft, false);
}

void GlobalHeader::paint (juce::Graphics& g)
{
    juce::ColourGradient surface (vox::ui::tokens::colour::panelRaised,
                                  0.0f, 0.0f,
                                  vox::ui::tokens::colour::panel,
                                  0.0f, static_cast<float> (getHeight()), false);
    surface.addColour (0.62, vox::ui::tokens::colour::panel);
    g.setGradientFill (surface);
    g.fillAll();

    // Brand mark: the waveform glyph plus the tracking-wide tagline.
    const auto brandBounds = brand.getBounds();
    auto logo = juce::Rectangle<float> (static_cast<float> (brandBounds.getX() - 42),
                                        static_cast<float> (brandBounds.getY() + 1),
                                        34.0f, static_cast<float> (brandBounds.getHeight()));
    juce::Path wave;
    for (int i = 0; i <= 18; ++i)
    {
        const auto t = static_cast<float> (i) / 18.0f;
        const auto x = logo.getX() + t * logo.getWidth();
        const auto amp = logo.getHeight() * (0.10f + 0.30f * std::abs (std::sin (t * 9.0f)));
        const auto y = logo.getCentreY() + std::sin (t * juce::MathConstants<float>::twoPi * 2.4f) * amp;
        if (i == 0) wave.startNewSubPath (x, y); else wave.lineTo (x, y);
    }
    g.setColour (vox::ui::tokens::colour::accent.withAlpha (0.95f));
    g.strokePath (wave, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));

    // Hairline divider between the browser cluster (cube) and the status cluster.
    if (! dividerBounds.isEmpty())
    {
        g.setColour (vox::ui::tokens::colour::border);
        g.fillRect (dividerBounds);
    }

    paintMidiIndicator (g, midiDotBounds);
    paintCpuMeter (g, cpuMeterBounds);
    paintOutputScale (g, output.getBounds());

    g.setColour (vox::ui::tokens::colour::border);
    g.drawLine (0.0f, static_cast<float> (getHeight() - 1),
                static_cast<float> (getWidth()), static_cast<float> (getHeight() - 1));
}

void GlobalHeader::resized()
{
    auto area = getLocalBounds().reduced (12, 6);
    const auto compact = getWidth() < 1220;

    // --- Brand -------------------------------------------------------------
    auto brandArea = area.removeFromLeft (compact ? 228 : 272);
    auto brandText = brandArea.withTrimmedLeft (44);
    brand.setBounds (brandText.removeFromTop (juce::jmax (16, brandText.getHeight() - 16)));
    tagline.setBounds (brandText.removeFromTop (16));

    // --- Right cluster, claimed right to left ------------------------------
    // Widths are the accepted render's proportions. The cluster is fully
    // specified so the flexible middle (the preset dropdown) cannot be starved.
    settingsButton.setBounds (area.removeFromRight (compact ? 82 : 92).reduced (2));
    panicButton.setBounds (area.removeFromRight (compact ? 58 : 64).reduced (2));
    output.setBounds (area.removeFromRight (compact ? 48 : 54).reduced (2, 1));
    outputLabel.setBounds (area.removeFromRight (compact ? 40 : 46));

    cpuMeterBounds = area.removeFromRight (compact ? 38 : 46)
                         .withSizeKeepingCentre (compact ? 38 : 46, 8);
    cpu.setBounds (area.removeFromRight (compact ? 40 : 46));
    midiDotBounds = juce::Rectangle<int> (area.removeFromRight (compact ? 38 : 44).getRight() - 11,
                                          area.getCentreY() - 4, 8, 8);
    midi.setBounds (juce::Rectangle<int> (midiDotBounds.getX() - 38, area.getY(), 34, area.getHeight()));

    dividerBounds = area.removeFromRight (1).reduced (0, 8);
    area.removeFromRight (8);

    cubeButton.setBounds (area.removeFromRight (compact ? 28 : 32).reduced (2));
    area.removeFromRight (2);
    diceButton.setBounds (area.removeFromRight (compact ? 28 : 32).reduced (2));
    area.removeFromRight (4);

    auto seedArea = area.removeFromRight (compact ? 108 : 126);
    seedLabel.setBounds (seedArea.removeFromLeft (compact ? 34 : 40).reduced (2, 0));
    seedValue.setBounds (seedArea.reduced (2, 1));

    area.removeFromRight (6);
    saveButton.setBounds (area.removeFromRight (compact ? 52 : 58).reduced (2));
    area.removeFromRight (4);

    // Prev / next chevrons flank the preset dropdown: prev on its left, next on
    // its right. The middle band absorbs the remaining width, which is what makes
    // the preset the widest control in the header.
    const auto chevronWidth = compact ? 28 : 32;
    auto prevSlot = area.removeFromLeft (chevronWidth + 4);

    nextButton.setBounds (area.removeFromRight (chevronWidth + 4).reduced (2));
    prevSlot.removeFromLeft (2);
    previousButton.setBounds (prevSlot.withWidth (chevronWidth).reduced (2));

    preset.setBounds (area.reduced (2, 1));
}

} // namespace vstengine::ui
