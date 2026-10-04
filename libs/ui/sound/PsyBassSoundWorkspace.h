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

// Psy Bass SOUND composition, matching the accepted reference layout.
//
//   Row 1  ~30 / 34 / 32   OSCILLATOR      FILTER        AMP ENVELOPE
//   Row 2  ~30 / 34 / 32   DRIVE/CHARACTER ACCENT        PERFORMANCE
//   Row 3  ~52 / 46        MODULATION      MOD MATRIX
//
// This is a real composition, not the generic panel set relabelled. Controls
// whose semantics genuinely match an existing descriptor parameter bind to it
// (cutoff, resonance, decay, drive, accent). Everything else the reference
// render shows is rendered by GateAVisualKnob, because the current Psy Bass
// descriptor does not publish it. `space` and `motion` are deliberately never
// requested, since mapping them onto unrelated reference controls would be
// misleading.
class PsyBassSoundWorkspace final : public SoundWorkspace {
public:
    PsyBassSoundWorkspace();

    void bind (const SoundWorkspaceBinding&) override;

    [[nodiscard]] SoundWorkspaceKind getKind() const override
    {
        return SoundWorkspaceKind::psyBass;
    }

    [[nodiscard]] juce::StringArray getGateAVisualOnlyLabels() const override;

    void resized() override;

private:
    void buildComposition();
    void buildControls();

    // Creates a control bound to `requestedId` when the selected descriptor
    // actually publishes that parameter, and an explicit Gate A preview-only
    // control otherwise.
    SoundParameterKnob& addControl (juce::String requestedId, juce::String label,
                                    juce::String previewValue,
                                    vox::ui::VoxKnob::Size size);

    // Row 1
    SoundModulePanel* oscillatorPanel { nullptr };
    SoundModulePanel* filterPanel { nullptr };
    SoundModulePanel* envelopePanel { nullptr };
    std::unique_ptr<SoundGraph> oscillatorGraph;
    std::unique_ptr<SoundGraph> filterGraph;
    std::unique_ptr<SoundGraph> envelopeGraph;
    std::array<SoundParameterKnob*, 4> oscillatorKnobs {};
    std::array<SoundParameterKnob*, 4> filterKnobs {};
    std::array<SoundParameterKnob*, 4> envelopeKnobs {};

    // Row 2
    SoundModulePanel* characterPanel { nullptr };
    SoundModulePanel* accentPanel { nullptr };
    SoundModulePanel* performancePanel { nullptr };
    std::array<SoundParameterKnob*, 4> characterKnobs {};
    std::array<SoundParameterKnob*, 4> accentKnobs {};
    std::array<SoundParameterKnob*, 4> performanceKnobs {};

    // Row 3
    SoundModulePanel* modulationPanel { nullptr };
    SoundModulePanel* matrixPanel { nullptr };
    std::unique_ptr<SoundTabStrip> modulationTabs;
    std::unique_ptr<SoundGraph> modulationGraph;
    std::array<SoundParameterKnob*, 4> modulationKnobs {};
    std::unique_ptr<GateAVisualMatrix> matrix;

    // Owns the controls; the raw-pointer arrays above index into it.
    std::vector<std::unique_ptr<SoundParameterKnob>> controls;

    std::size_t slotIndex {};
    const instrument::InstrumentDescriptor* descriptor { nullptr };
    juce::AudioProcessorValueTreeState* state { nullptr };
};

} // namespace vstengine::ui