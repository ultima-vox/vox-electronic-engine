#include "SoundControls.h"

#include "instrument/HostParameterSchema.h"

#include <cmath>

namespace vstengine::ui {

namespace {

constexpr float smallDiameter = 36.0f;
constexpr float normalDiameter = 47.0f;
constexpr float largeDiameter = 62.0f;
constexpr int labelHeight = 12;
constexpr int valueHeight = 12;

float diameterFor (const vox::ui::VoxKnob::Size size) noexcept
{
    switch (size) {
        case vox::ui::VoxKnob::Size::Small:  return smallDiameter;
        case vox::ui::VoxKnob::Size::Large:  return largeDiameter;
        case vox::ui::VoxKnob::Size::Normal: break;
    }
    return normalDiameter;
}

// Shared knob anatomy, matching VoxKnob's layer order closely enough that a
// preview knob and a bound knob read as the same component family.
void paintKnobBody (juce::Graphics& g, juce::Rectangle<float> area, float value01,
                    juce::Colour accent, bool dimmed)
{
    const auto d = juce::jmin (area.getWidth(), area.getHeight());
    const auto centre = area.getCentre();
    const auto r = d * 0.5f;
    auto body = juce::Rectangle<float> (d, d).withCentre (centre);

    g.setColour (colours::control);
    g.fillEllipse (body);

    juce::ColourGradient face (colours::panelRaised.brighter (0.06f), body.getTopLeft(),
                               colours::control.darker (0.25f), body.getBottomLeft(), false);
    g.setGradientFill (face);
    g.fillEllipse (body.reduced (d * 0.10f));

    const auto ringR = r * 0.80f;
    const auto start = juce::MathConstants<float>::pi * 0.78f;
    const auto sweep = juce::MathConstants<float>::pi * 1.44f;

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, ringR, ringR, 0.0f, start, start + sweep, true);
    g.setColour (colours::borderSubtle);
    g.strokePath (track, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    const auto clamped = juce::jlimit (0.0f, 1.0f, value01);
    if (clamped > 0.001f) {
        juce::Path active;
        active.addCentredArc (centre.x, centre.y, ringR, ringR, 0.0f, start,
                              start + sweep * clamped, true);
        // Preview-only controls use a visibly quieter arc so that a bound
        // control and an unbound one are distinguishable on a Gate A capture.
        g.setColour (accent.withAlpha (dimmed ? 0.42f : 0.95f));
        g.strokePath (active, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));
    }

    const auto angle = start + sweep * clamped;
    const auto pointer = juce::Point<float> (
        centre.x + std::cos (angle) * r * 0.52f,
        centre.y + std::sin (angle) * r * 0.52f);
    g.setColour (colours::text.withAlpha (dimmed ? 0.55f : 1.0f));
    g.drawLine (centre.x, centre.y, pointer.x, pointer.y, juce::jmax (1.2f, d * 0.035f));

    g.setColour (colours::border);
    g.drawEllipse (body, 1.0f);
}

} // namespace

// --- GateAVisualKnob -------------------------------------------------------

GateAVisualKnob::GateAVisualKnob (juce::String labelText, juce::String value,
                                  const float preview, const vox::ui::VoxKnob::Size size)
    : label (std::move (labelText)), valueText (std::move (value)),
      previewValue01 (preview), diameter (diameterFor (size))
{
    setInterceptsMouseClicks (false, false);
}

void GateAVisualKnob::setAccent (juce::Colour newAccent)
{
    accent = newAccent;
    repaint();
}

void GateAVisualKnob::resized()
{
    const auto textHeight = labelHeight + valueHeight;
    auto area = getLocalBounds().toFloat().reduced (1.0f);
    if (area.getHeight() > textHeight + 8.0f)
        area.removeFromBottom (static_cast<float> (textHeight));

    const auto d = juce::jmin (diameter, juce::jmin (area.getWidth(), area.getHeight()));
    knobBounds = juce::Rectangle<float> (d, d).withCentre (area.getCentre());
}

void GateAVisualKnob::paint (juce::Graphics& g)
{
    paintKnobBody (g, knobBounds, previewValue01, accent, true);

    auto text = getLocalBounds().reduced (1, 0);
    auto valueArea = text.removeFromBottom (valueHeight);
    auto labelArea = text.removeFromBottom (labelHeight);

    g.setFont (juce::Font (9.0f));
    g.setColour (colours::textSecondary);
    g.drawText (label, labelArea, juce::Justification::centred, true);

    g.setFont (juce::Font (9.5f));
    g.setColour (accent.withAlpha (0.72f));
    g.drawText (valueText, valueArea, juce::Justification::centred, true);
}

// --- parameter resolution --------------------------------------------------

std::optional<std::size_t> resolveExistingMacroIndex (
    const instrument::InstrumentDescriptor* descriptor, const juce::String& parameterId)
{
    if (descriptor == nullptr || parameterId.isEmpty())
        return std::nullopt;

    for (const auto& parameter : descriptor->parameters) {
        if (juce::String (parameter.id) != parameterId)
            continue;
        if (parameter.preferredMacro < 0)
            return std::nullopt;
        return static_cast<std::size_t> (parameter.preferredMacro);
    }
    return std::nullopt;
}

// --- SoundParameterKnob ----------------------------------------------------

SoundParameterKnob::SoundParameterKnob (juce::AudioProcessorValueTreeState& state,
                                        juce::String requestedId,
                                        juce::String labelText,
                                        const instrument::InstrumentDescriptor* descriptor,
                                        const std::size_t slotIndex,
                                        const vox::ui::VoxKnob::Size knobSize,
                                        juce::String previewText)
    : requestedParameterId (std::move (requestedId)), label (std::move (labelText)),
      size (knobSize)
{
    const auto macroIndex = resolveExistingMacroIndex (descriptor, requestedParameterId);

    if (macroIndex.has_value()) {
        knob = std::make_unique<vox::ui::VoxKnob> (label, knobSize);
        knob->setLabel (label);
        knob->setTooltip (label + "  (" + requestedParameterId + ")");
        addAndMakeVisible (*knob);

        // Attaches to the macro slot the provider already declared. No new
        // automation id is introduced here.
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            state, instrument::hostparams::macroId (slotIndex, *macroIndex),
            knob->getSlider());
    } else {
        // No real parameter exists for this control. Render it as an explicit
        // Gate A visual-only knob: inert, unbound and unserialised.
        const auto preview = knobSize == vox::ui::VoxKnob::Size::Small ? 0.5f : 0.42f;
        previewKnob = std::make_unique<GateAVisualKnob> (label,
                                                        std::move (previewText), preview,
                                                        knobSize);
        addAndMakeVisible (*previewKnob);
    }
}

void SoundParameterKnob::setAccent (juce::Colour newAccent)
{
    if (knob != nullptr)
        knob->setColour (juce::Slider::rotarySliderFillColourId, newAccent);
    if (previewKnob != nullptr)
        previewKnob->setAccent (newAccent);
}

void SoundParameterKnob::resized()
{
    if (knob != nullptr)
        knob->setBounds (getLocalBounds());
    if (previewKnob != nullptr)
        previewKnob->setBounds (getLocalBounds());
}

} // namespace vstengine::ui