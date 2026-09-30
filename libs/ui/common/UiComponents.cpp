#include "UiComponents.h"

namespace vstengine::ui {

VoxLookAndFeel::VoxLookAndFeel()
{
    setColour(juce::ComboBox::backgroundColourId, colours::panelRaised);
    setColour(juce::ComboBox::outlineColourId, colours::border);
    setColour(juce::ComboBox::textColourId, colours::text);
    setColour(juce::PopupMenu::backgroundColourId, colours::panel);
    setColour(juce::PopupMenu::textColourId, colours::text);
    setColour(juce::PopupMenu::highlightedBackgroundColourId,
              colours::primary.withAlpha(0.22f));
    setColour(juce::Slider::rotarySliderFillColourId, colours::primary);
    setColour(juce::Slider::rotarySliderOutlineColourId, colours::border);
    setColour(juce::Slider::textBoxTextColourId, colours::text);
    setColour(juce::Slider::textBoxBackgroundColourId, colours::background);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
}

void VoxLookAndFeel::drawButtonBackground(
    juce::Graphics& g, juce::Button& button, const juce::Colour& base,
    bool highlighted, bool down)
{
    auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
    auto colour = button.getToggleState() ? colours::primary.darker(0.55f)
        : base;
    if (highlighted) colour = colour.brighter(0.08f);
    if (down) colour = colour.darker(0.12f);
    g.setColour(colour);
    g.fillRoundedRectangle(bounds, metrics::corner);
    g.setColour(button.getToggleState() ? colours::primary : colours::border);
    g.drawRoundedRectangle(bounds, metrics::corner, 1.0f);
}

void VoxLookAndFeel::drawRotarySlider(
    juce::Graphics& g, int x, int y, int width, int height, float position,
    float start, float end, juce::Slider& slider)
{
    const auto diameter = static_cast<float>(juce::jmin(width, height));
    const auto radius = juce::jmax(8.0f, diameter * 0.5f - 7.0f);
    const auto centre = juce::Point<float>(x + width * 0.5f,
                                           y + height * 0.5f);
    const auto stroke = juce::jmax(2.0f, radius * 0.12f);
    const auto arcRadius = juce::jmax(3.0f, radius - stroke * 0.85f);
    const auto angle = start + position * (end - start);
    const auto enabled = slider.isEnabled();

    juce::Path inactiveArc;
    inactiveArc.addCentredArc(centre.x, centre.y, arcRadius, arcRadius,
                              0.0f, start, end, true);
    g.setColour(colours::border);
    g.strokePath(inactiveArc, juce::PathStrokeType(stroke,
        juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path activeArc;
    activeArc.addCentredArc(centre.x, centre.y, arcRadius, arcRadius,
                            0.0f, start, angle, true);
    g.setColour(enabled ? colours::primary : colours::mutedText);
    g.strokePath(activeArc, juce::PathStrokeType(stroke,
        juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const auto bodyRadius = radius * 0.62f;
    const auto body = juce::Rectangle<float>(centre.x - bodyRadius,
                                              centre.y - bodyRadius,
                                              bodyRadius * 2.0f,
                                              bodyRadius * 2.0f);
    g.setColour(colours::background);
    g.fillEllipse(body);
    g.setColour(colours::border.darker(0.18f));
    g.drawEllipse(body, juce::jmax(1.0f, stroke * 0.30f));

    const auto markerStart = centre + juce::Point<float>(0.0f, -bodyRadius * 0.30f)
                                        .rotatedAboutOrigin(angle);
    const auto markerEnd = centre + juce::Point<float>(0.0f, -bodyRadius * 0.88f)
                                      .rotatedAboutOrigin(angle);
    g.setColour(enabled ? colours::text : colours::mutedText);
    g.drawLine({ markerStart, markerEnd }, juce::jmax(1.2f, stroke * 0.42f));
}

void VoxLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height,
                                  bool, int, int, int, int,
                                  juce::ComboBox& box)
{
    auto bounds = juce::Rectangle<float>(0, 0, static_cast<float>(width),
                                          static_cast<float>(height));
    g.setColour(colours::panelRaised);
    g.fillRoundedRectangle(bounds.reduced(0.5f), metrics::corner);
    g.setColour(box.hasKeyboardFocus(true) ? colours::primary
                                           : colours::border);
    g.drawRoundedRectangle(bounds.reduced(0.5f), metrics::corner, 1.0f);
    juce::Path arrow;
    const auto cx = static_cast<float>(width - 16);
    const auto cy = static_cast<float>(height) * 0.5f;
    arrow.addTriangle(cx - 4.0f, cy - 2.0f, cx + 4.0f, cy - 2.0f,
                      cx, cy + 3.0f);
    g.setColour(colours::mutedText);
    g.fillPath(arrow);
}

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
