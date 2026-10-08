#include "SoundControls.h"

#include "instrument/HostParameterSchema.h"

namespace vstengine::ui {

// --- GateAVisualKnob -------------------------------------------------------

GateAVisualKnob::GateAVisualKnob (juce::String labelText, juce::String value,
                                  const float preview, const vox::ui::VoxKnob::Size size)
    : label (std::move (labelText)), valueText (std::move (value)),
      accent (colours::primary)
{
    // Presentation only: neither this control nor the VoxKnob it borrows its
    // anatomy from may take a click, a drag or keyboard focus.
    setInterceptsMouseClicks (false, false);

    knob = std::make_unique<vox::ui::VoxKnob> (label, size);
    knob->setInterceptsMouseClicks (false, false);
    knob->setWantsKeyboardFocus (false);

    auto& slider = knob->getSlider();
    slider.setRange (0.0, 1.0, 0.001);
    slider.setValue (juce::jlimit (0.0f, 1.0f, preview), juce::dontSendNotification);

    // The reference prints the previewed value exactly, so the preview string is
    // the formatter rather than the slider's own rounding of the preview ratio.
    slider.textFromValueFunction = [text = valueText] (double) { return text; };
    slider.valueFromTextFunction = [] (const juce::String&) { return 0.0; };

    knob->setColour (juce::Slider::rotarySliderFillColourId,
                     accent.withAlpha (previewAccentAlpha));
    addAndMakeVisible (*knob);
}

void GateAVisualKnob::setAccent (juce::Colour newAccent)
{
    accent = newAccent;
    if (knob != nullptr)
        knob->setColour (juce::Slider::rotarySliderFillColourId,
                         accent.withAlpha (previewAccentAlpha));
    repaint();
}

void GateAVisualKnob::resized()
{
    if (knob != nullptr)
        knob->setBounds (getLocalBounds());
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