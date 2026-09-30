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
    styleHeaderLabel (brand, 19.0f, juce::Justification::centredLeft,
                      vox::ui::tokens::colour::text);

    preset.setTextWhenNothingSelected ("Init");

    midi.setText ("MIDI", juce::dontSendNotification);
    styleHeaderLabel (midi, 11.0f, juce::Justification::centred,
                      vox::ui::tokens::colour::textMuted);

    cpu.setText ("CPU 0%", juce::dontSendNotification);
    styleHeaderLabel (cpu, 11.0f, juce::Justification::centred,
                      vox::ui::tokens::colour::textMuted);

    outputLabel.setText ("OUTPUT", juce::dontSendNotification);
    styleHeaderLabel (outputLabel, 10.0f, juce::Justification::centredRight,
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

    output.setSliderStyle (juce::Slider::LinearHorizontal);
    output.setTextBoxStyle (juce::Slider::TextBoxRight, false, 58, 22);
    output.setTooltip ("Global output level");
    output.setColour (juce::Slider::trackColourId, vox::ui::tokens::colour::accent);
    output.setColour (juce::Slider::backgroundColourId, vox::ui::tokens::colour::control);
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
    preset.setText (name.isEmpty() ? "Unsaved" : name, juce::dontSendNotification);
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
    midi.setText (active ? "MIDI +" : "MIDI", juce::dontSendNotification);
    midi.setColour (juce::Label::textColourId,
                    active ? vox::ui::tokens::colour::success
                           : vox::ui::tokens::colour::textMuted);
}

void GlobalHeader::setCpuLoad (float load)
{
    load = juce::jlimit (0.0f, 4.0f, load);
    cpu.setText ("CPU " + juce::String (juce::roundToInt (load * 100.0f)) + "%",
                 juce::dontSendNotification);
    cpu.setColour (juce::Label::textColourId,
                   load > 0.8f ? vox::ui::tokens::colour::warning
                               : vox::ui::tokens::colour::textMuted);

    if (const auto* value = state.getRawParameterValue ("rngSeed"))
        seedButton.setButtonText ("Seed " + juce::String (juce::roundToInt (value->load())));
}

void GlobalHeader::paint (juce::Graphics& g)
{
    g.fillAll (vox::ui::tokens::colour::panel);
    g.setColour (vox::ui::tokens::colour::border);
    g.drawLine (0.0f, static_cast<float> (getHeight() - 1),
                static_cast<float> (getWidth()), static_cast<float> (getHeight() - 1));
}

void GlobalHeader::resized()
{
    auto area = getLocalBounds().reduced (14, 8);
    brand.setBounds (area.removeFromLeft (220));
    previousButton.setBounds (area.removeFromLeft (38).reduced (2));
    preset.setBounds (area.removeFromLeft (160).reduced (2));
    nextButton.setBounds (area.removeFromLeft (38).reduced (2));
    saveButton.setBounds (area.removeFromLeft (58).reduced (2));
    seedButton.setBounds (area.removeFromLeft (82).reduced (2));
    midi.setBounds (area.removeFromLeft (52));
    cpu.setBounds (area.removeFromLeft (58));
    settingsButton.setBounds (area.removeFromRight (78).reduced (2));
    panicButton.setBounds (area.removeFromRight (64).reduced (2));
    output.setBounds (area.removeFromRight (112).reduced (4, 2));
    outputLabel.setBounds (area.removeFromRight (54));
}

} // namespace vstengine::ui
