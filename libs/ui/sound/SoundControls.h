#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/common/UiComponents.h"
#include "instrument/InstrumentContract.h"
#include "vox-ui/components/VoxKnob.h"

#include <memory>
#include <optional>

namespace vstengine::ui {

// ---------------------------------------------------------------------------
// Gate A visual-only control.
//
// The accepted reference renders show this control, but the current stable
// parameter schema has no host parameter behind it. It is therefore drawn as
// presentation only. Specifically this control:
//
//   * has NO APVTS attachment and NO host automation id
//   * writes NO processor, preset or persistent state
//   * is NOT interactive: it cannot be dragged, automated or MIDI-learned
//   * is NOT serialised anywhere
//
// It exists under UI_PRODUCTION_IMPLEMENTATION_STANDARD.md section 24
// ("Visual-prototype exception") for Visual Gate A composition review only, and
// must be replaced by a bound control at Functional Gate B.
//
// The type name is deliberately explicit so the distinction is obvious in code
// review. Every workspace lists these through
// SoundWorkspace::getGateAVisualOnlyLabels() so the Visual Gate capture can
// report which controls were still mock-driven.
// ---------------------------------------------------------------------------
class GateAVisualKnob final : public juce::Component {
public:
    GateAVisualKnob (juce::String labelText, juce::String valueText,
                     float previewValue01,
                     vox::ui::VoxKnob::Size size = vox::ui::VoxKnob::Size::Normal);

    void setAccent (juce::Colour newAccent);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::String label;
    juce::String valueText;
    float previewValue01 { 0.5f };
    juce::Colour accent { colours::primary };
    float diameter { 47.0f };
    juce::Rectangle<float> knobBounds;
};

// Resolves an EXISTING stable descriptor parameter id to the APVTS macro slot it
// is already exposed through.
//
// Returns std::nullopt when the descriptor does not publish that parameter, or
// publishes it without a macro mapping. This helper never creates, renames or
// reorders a parameter id; it only reads the descriptor that the provider
// already declares.
[[nodiscard]] std::optional<std::size_t> resolveExistingMacroIndex (
    const instrument::InstrumentDescriptor* descriptor, const juce::String& parameterId);

// ---------------------------------------------------------------------------
// A knob that binds to a real host parameter when one genuinely exists.
//
// The workspace requests a stable descriptor parameter id (for example
// "cutoff"). If the descriptor actually provides it and maps it to a macro slot,
// the control owns a VoxKnob plus a real SliderAttachment on the existing
// slotNNMacroMM id. If it does not, the control renders a GateAVisualKnob
// instead, which is inert.
//
// Either way the visual component is the same, so Functional Gate B only has to
// introduce the missing descriptors/APVTS parameters - the presentation layer
// does not change.
// ---------------------------------------------------------------------------
class SoundParameterKnob final : public juce::Component {
public:
    SoundParameterKnob (juce::AudioProcessorValueTreeState& state,
                        juce::String requestedParameterId,
                        juce::String labelText,
                        const instrument::InstrumentDescriptor* descriptor,
                        std::size_t slotIndex,
                        vox::ui::VoxKnob::Size size = vox::ui::VoxKnob::Size::Normal,
                        juce::String previewValueText = {});

    // True only when this control is attached to a real host parameter.
    [[nodiscard]] bool isBoundToRealParameter() const noexcept { return knob != nullptr; }

    // The stable descriptor id this control requested, or an empty string when
    // no host parameter was requested at all.
    //
    // An empty request is meaningful: it means the control is preview-only by
    // construction. Workspaces must never pass a look-alike string here. A
    // descriptor only publishes real, provider-owned ids, so inventing one
    // would misrepresent a nonexistent parameter as if it were part of the
    // stable schema.
    [[nodiscard]] const juce::String& getRequestedParameterId() const noexcept
    {
        return requestedParameterId;
    }

    // Display label, used by the Visual Gate manifest to report which controls
    // were still mock driven without implying an automation id.
    [[nodiscard]] const juce::String& getLabel() const noexcept { return label; }

    void setAccent (juce::Colour newAccent);

    void resized() override;

private:
    juce::String requestedParameterId;
    juce::String label;
    vox::ui::VoxKnob::Size size { vox::ui::VoxKnob::Size::Normal };
    std::unique_ptr<vox::ui::VoxKnob> knob;
    std::unique_ptr<GateAVisualKnob> previewKnob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

} // namespace vstengine::ui
