#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>
#include <vector>

#include "GateAVisualData.h"
#include "SoundControls.h"
#include "SoundGraphs.h"
#include "SoundModulePanel.h"
#include "SoundWorkspace.h"

namespace vstengine::ui {

// Acid SOUND composition, matching the accepted reference layout.
//
//   Top strip (non-uniform)  OSCILLATOR ~22 / FILTER ~21 / ENVELOPE ~22
//                            DISTORTION-DRIVE ~13 / ACCENT + SLIDE + OUTPUT
//   Centre                   16-step, 5-lane STEP SEQUENCER (dominant)
//   Bottom row               MODULATION ~52 / PERFORMANCE ~30 / PLAY MODE ~16
//
// Acid is deliberately NOT the Psy Bass panel set relabelled. The sequencer is
// the centre of the composition, and the top strip uses the reference's uneven
// widths rather than an equal card grid.
class AcidSoundWorkspace final : public SoundWorkspace {
public:
    AcidSoundWorkspace();

    void bind (const SoundWorkspaceBinding&) override;

    [[nodiscard]] SoundWorkspaceKind getKind() const override
    {
        return SoundWorkspaceKind::acid;
    }

    [[nodiscard]] juce::StringArray getGateAVisualOnlyLabels() const override;

    void resized() override;

private:
    void buildComposition();
    void buildControls();

    SoundParameterKnob& addControl (juce::String requestedId, juce::String label,
                                    juce::String previewValue,
                                    vox::ui::VoxKnob::Size size);

    // Non-uniform top strip: 22 / 21 / 22 / 13, with the remainder forming the
    // right block that holds ACCENT + SLIDE above OUTPUT.
    void layoutTopStrip(juce::Rectangle<int> area);
    void layoutBottomRow(juce::Rectangle<int> area);

    // Top strip
    SoundModulePanel* oscillatorPanel { nullptr };
    SoundModulePanel* filterPanel { nullptr };
    SoundModulePanel* envelopePanel { nullptr };
    SoundModulePanel* drivePanel { nullptr };
    SoundModulePanel* accentPanel { nullptr };
    SoundModulePanel* slidePanel { nullptr };
    SoundModulePanel* outputPanel { nullptr };

    std::array<SoundParameterKnob*, 4> oscillatorKnobs {};
    std::array<SoundParameterKnob*, 4> filterKnobs {};
    std::array<SoundParameterKnob*, 4> envelopeKnobs {};
    std::array<SoundParameterKnob*, 3> driveKnobs {};
    SoundParameterKnob* accentKnob { nullptr };
    SoundParameterKnob* slideKnob { nullptr };
    std::array<SoundParameterKnob*, 2> outputKnobs {};

    // Centre
    SoundModulePanel* sequencerPanel { nullptr };
    std::unique_ptr<GateAVisualSequencer> sequencer;
    std::unique_ptr<SoundGraph> envelopeGraph;
    std::unique_ptr<SoundTabStrip> envelopeTabs;

    // Bottom row
    SoundModulePanel* modulationPanel { nullptr };
    SoundModulePanel* performancePanel { nullptr };
    SoundModulePanel* playModePanel { nullptr };
    std::unique_ptr<SoundTabStrip> modulationTabs;
    std::unique_ptr<SoundGraph> modulationGraph;
    std::array<SoundParameterKnob*, 5> modulationKnobs {};
    std::array<SoundParameterKnob*, 4> performanceKnobs {};
    std::unique_ptr<GateAVisualOptionList> playMode;

    std::vector<std::unique_ptr<SoundParameterKnob>> controls;

    std::size_t slotIndex {};
    const instrument::InstrumentDescriptor* descriptor { nullptr };
    juce::AudioProcessorValueTreeState* state { nullptr };
};

} // namespace vstengine::ui