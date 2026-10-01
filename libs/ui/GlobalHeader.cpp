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
} // namespace

GlobalHeader::GlobalHeader (juce::AudioProcessorValueTreeState& s, Callbacks cb)
    : state (s)
{
    brand.setText ("VOX ELECTRONIC ENGINE", juce::dontSendNotification);
    styleHeaderLabel (brand, 17.0f, juce::Justification::centredLeft,
                      vox::ui::tokens::colour::text);

    preset.setTextWhenNothingSelected ("Init Project");

    midi.setText ("MIDI", juce::dontSendNotification);
    styleHeaderLabel (midi, 10.0f, juce::Justification::centredLeft,
                      vox::ui::tokens::colour::textMuted);

    cpu.setText ("CPU 0%", juce::dontSendNotification);
    styleHeaderLabel (cpu, 10.0f, juce::Justification::centredLeft,
                      vox::ui::tokens::colour::textMuted);

    outputLabel.setText ("OUTPUT", juce::dontSendNotification);
    styleHeaderLabel (outputLabel, 9.0f, juce::Justification::centredRight,
                      vox::ui::tokens::colour::textMuted);

    for (auto* label : { &brand, &midi, &cpu, &outputLabel })
        addAndMakeVisible (*label);

    addAndMakeVisible (preset);
    for (auto* button : { &previousButton, &nextButton, &saveButton,
                          &seedButton, &panicButton, &settingsButton })
        addAndMakeVisible (*button);

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
    seedButton.onClick = [this] { randomizeSeed(); };

    output.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    output.setRotaryParameters (juce::MathConstants<float>::pi * 0.75f,
                                juce::MathConstants<float>::pi * 2.25f, true);
    output.setTextBoxStyle (juce::Slider::TextBoxRight, false, 48, 22);
    output.setTooltip ("Global output level");
    output.setColour (juce::Slider::rotarySliderFillColourId, vox::ui::tokens::colour::accent);
    output.setColour (juce::Slider::rotarySliderOutlineColourId, vox::ui::tokens::colour::border);
    output.setColour (juce::Slider::textBoxTextColourId, vox::ui::tokens::colour::text);
    output.setColour (juce::Slider::textBoxBackgroundColourId, vox::ui::tokens::colour::control);
    output.setColour (juce::Slider::textBoxOutlineColourId, vox::ui::tokens::colour::border);
    addAndMakeVisible (output);

    outputAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        state, "outputLevel", output);
}

void GlobalHeader::randomizeSeed()
{
    if (auto* parameter = state.getParameter ("rngSeed"))
    {
        const auto plain = static_cast<float> (
            juce::Random::getSystemRandom().nextInt (0x7fffffff));
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (plain));
    }
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
                    active ? vox::ui::tokens::colour::textSecondary
                           : vox::ui::tokens::colour::textMuted);
    repaint (midi.getBounds().expanded (18, 4));
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

    if (const auto* value = state.getRawParameterValue ("rngSeed"))
        seedButton.setButtonText ("Seed " + juce::String (juce::roundToInt (value->load())));
    repaint (cpu.getBounds().expanded (78, 4));
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

    const auto brandBounds = brand.getBounds();
    auto logo = juce::Rectangle<float> (static_cast<float> (brandBounds.getX() - 42),
                                        static_cast<float> (brandBounds.getY() + 1),
                                        34.0f, 30.0f);
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

    g.setColour (vox::ui::tokens::colour::accent.withAlpha (0.78f));
    g.setFont (vox::ui::typography::makeFont (7.5f));
    g.drawText ("CREATE   EVOLVE   TRANSCEND",
                brandBounds.withY (brandBounds.getY() + 24).withHeight (10),
                juce::Justification::centredLeft, false);

    const auto midiBounds = midi.getBounds().toFloat();
    g.setColour (midiActive ? vox::ui::tokens::colour::accent
                            : vox::ui::tokens::colour::borderSubtle);
    g.fillEllipse (midiBounds.getRight() - 7.0f, midiBounds.getCentreY() - 2.5f, 5.0f, 5.0f);

    const auto cpuBounds = cpu.getBounds().toFloat();
    auto cpuMeter = juce::Rectangle<float> (cpuBounds.getRight() - 34.0f,
                                            cpuBounds.getCentreY() - 3.0f,
                                            32.0f, 6.0f);
    g.setColour (vox::ui::tokens::colour::background.withAlpha (0.9f));
    g.fillRoundedRectangle (cpuMeter, 2.0f);
    const int bars = 8;
    for (int i = 0; i < bars; ++i)
    {
        const auto bx = cpuMeter.getX() + 2.0f + i * 3.6f;
        const auto active = cpuLoad01 * bars > i;
        g.setColour (active
            ? (cpuLoad01 > 0.8f ? vox::ui::tokens::colour::warning : vox::ui::tokens::colour::accent)
            : vox::ui::tokens::colour::borderSubtle.withAlpha (0.55f));
        g.fillRoundedRectangle (bx, cpuMeter.getY() + 1.0f, 2.2f, 4.0f, 0.8f);
    }

    g.setColour (vox::ui::tokens::colour::border);
    g.drawLine (0.0f, static_cast<float> (getHeight() - 1),
                static_cast<float> (getWidth()), static_cast<float> (getHeight() - 1));
}

void GlobalHeader::resized()
{
    auto area = getLocalBounds().reduced (12, 6);
    const auto compact = getWidth() < 1220;
    auto brandArea = area.removeFromLeft (compact ? 244 : 286);
    brand.setBounds (brandArea.withTrimmedLeft (44).withTrimmedBottom (11));

    previousButton.setBounds (area.removeFromLeft (34).reduced (2));
    preset.setBounds (area.removeFromLeft (compact ? 148 : 174).reduced (2));
    nextButton.setBounds (area.removeFromLeft (34).reduced (2));
    saveButton.setBounds (area.removeFromLeft (54).reduced (2));
    seedButton.setBounds (area.removeFromLeft (compact ? 72 : 82).reduced (2));

    settingsButton.setBounds (area.removeFromRight (76).reduced (2));
    panicButton.setBounds (area.removeFromRight (62).reduced (2));
    output.setBounds (area.removeFromRight (108).reduced (2, 1));
    outputLabel.setBounds (area.removeFromRight (42));

    midi.setBounds (area.removeFromLeft (compact ? 46 : 52));
    cpu.setBounds (area.removeFromLeft (compact ? 72 : 84));
}

} // namespace vstengine::ui
