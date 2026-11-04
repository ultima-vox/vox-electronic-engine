#include "UiComponents.h"

namespace vstengine::ui {

void styleButton (juce::Button& button, bool isPrimary)
{
    button.setColour (juce::TextButton::buttonColourId,
                      isPrimary ? colours::primary.darker (0.45f) : colours::panelRaised);
    button.setColour (juce::TextButton::buttonOnColourId, colours::primary.darker (0.25f));
    button.setColour (juce::TextButton::textColourOffId, colours::text);
    button.setColour (juce::TextButton::textColourOnId, colours::text);
}

void styleLabel (juce::Label& label, float size, juce::Justification justification,
                 juce::Colour colour)
{
    label.setFont (juce::FontOptions (size));
    label.setJustificationType (justification);
    label.setColour (juce::Label::textColourId, colour);
}

ParameterKnob::ParameterKnob (juce::AudioProcessorValueTreeState& state,
                              const char* parameterId, juce::String labelText,
                              juce::String tooltip)
{
    label.setText (std::move (labelText), juce::dontSendNotification);
    styleLabel (label, 12.0f, juce::Justification::centred, colours::mutedText);
    addAndMakeVisible (label);

    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 82, 20);
    slider.setRotaryParameters(juce::MathConstants<float>::pi * 0.75f,
                               juce::MathConstants<float>::pi * 2.25f,
                               true);
    slider.setMouseDragSensitivity(180);
    slider.setColour (juce::Slider::rotarySliderFillColourId, colours::primary);
    slider.setColour (juce::Slider::rotarySliderOutlineColourId, colours::border);
    slider.setColour (juce::Slider::textBoxTextColourId, colours::text);
    slider.setColour (juce::Slider::textBoxBackgroundColourId, colours::background);
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    slider.setTooltip (tooltip);
    addAndMakeVisible (slider);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        state, parameterId, slider);
}

void ParameterKnob::resized()
{
    auto area = getLocalBounds();
    label.setBounds (area.removeFromTop (22));
    slider.setBounds (area.reduced (2));
}

ParameterSection::ParameterSection (juce::String titleText)
{
    title.setText (std::move (titleText), juce::dontSendNotification);
    styleLabel (title, 13.0f, juce::Justification::centredLeft, colours::text);
    addAndMakeVisible (title);
}

ParameterKnob& ParameterSection::addKnob (juce::AudioProcessorValueTreeState& state,
                                          const char* parameterId, juce::String labelText,
                                          juce::String tooltip)
{
    auto knob = std::make_unique<ParameterKnob> (state, parameterId,
                                                  std::move (labelText), std::move (tooltip));
    auto& result = *knob;
    addAndMakeVisible (result);
    knobs.push_back (std::move (knob));
    return result;
}

void ParameterSection::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (colours::panel);
    g.fillRoundedRectangle (bounds, metrics::corner);
    g.setColour (colours::border);
    g.drawRoundedRectangle (bounds, metrics::corner, 1.0f);
}

void ParameterSection::resized()
{
    auto area = getLocalBounds().reduced (12);
    title.setBounds (area.removeFromTop (24));
    area.removeFromTop (4);
    juce::FlexBox row;
    row.flexDirection = juce::FlexBox::Direction::row;
    row.justifyContent = juce::FlexBox::JustifyContent::spaceAround;
    row.alignItems = juce::FlexBox::AlignItems::stretch;
    for (auto& knob : knobs)
        row.items.add (juce::FlexItem (*knob).withFlex (1.0f).withMinWidth (82.0f));
    row.performLayout (area);
}

} // namespace vstengine::ui
