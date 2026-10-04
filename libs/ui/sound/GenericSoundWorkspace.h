#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>
#include <vector>

#include "SoundControls.h"
#include "SoundGraphs.h"
#include "SoundModulePanel.h"
#include "SoundWorkspace.h"

namespace vstengine::ui {

// Fallback SOUND composition for instruments that have no dedicated workspace.
//
// This exists so that Lead, Atmos, Semantic FX and any future rack instrument
// still get a deliberate, dense page. It is NOT a shared fallback that Psy Bass
// or Acid are meant to use - those have their own workspaces, because the
// accepted renders treat them as different instruments.
class GenericSoundWorkspace final : public SoundWorkspace {
public:
    GenericSoundWorkspace();

    void bind (const SoundWorkspaceBinding&) override;

    [[nodiscard]] SoundWorkspaceKind getKind() const override
    {
        return SoundWorkspaceKind::generic;
    }

    [[nodiscard]] juce::StringArray getGateAVisualOnlyLabels() const override;

    void resized() override;

private:
    void buildComposition();
    void buildControls();

    SoundParameterKnob& addControl (juce::String requestedId, juce::String label,
                                    juce::String previewValue);

    SoundModulePanel* oscillatorPanel { nullptr };
    SoundModulePanel* filterPanel { nullptr };
    SoundModulePanel* envelopePanel { nullptr };
    SoundModulePanel* characterPanel { nullptr };
    SoundModulePanel* performancePanel { nullptr };
    SoundModulePanel* modulationPanel { nullptr };

    std::unique_ptr<SoundGraph> oscillatorGraph;
    std::unique_ptr<SoundGraph> filterGraph;
    std::unique_ptr<SoundGraph> envelopeGraph;
    std::unique_ptr<SoundGraph> modulationGraph;

    std::array<SoundParameterKnob*, 4> oscillatorKnobs {};
    std::array<SoundParameterKnob*, 4> filterKnobs {};
    std::array<SoundParameterKnob*, 4> envelopeKnobs {};
    std::array<SoundParameterKnob*, 3> characterKnobs {};
    std::array<SoundParameterKnob*, 4> performanceKnobs {};
    std::array<SoundParameterKnob*, 4> modulationKnobs {};

    std::vector<std::unique_ptr<SoundParameterKnob>> controls;

    std::size_t slotIndex {};
    const instrument::InstrumentDescriptor* descriptor { nullptr };
    juce::AudioProcessorValueTreeState* state { nullptr };
};

} // namespace vstengine::ui