#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

#include "HeroBanner.h"
#include "SoundWorkspace.h"
#include "ui/common/UiComponents.h"
#include "instrument/InstrumentContract.h"

namespace vstengine::ui {

// SOUND page orchestration.
//
// Owns the hero band and exactly one instrument-specific workspace, chosen from
// the selected slot's InstrumentId. This replaces the previous arrangement
// where one class held a fixed set of eleven relabelled panels and switched
// between them with a Layout enum.
//
// The page deliberately does not know about the plugin processor. It receives
// only the APVTS and the selected descriptor, which keeps the UI layer free of
// concrete instrument checks and lets Functional Gate B swap the preview data
// source without touching this orchestration.
class SoundPage final : public juce::Component {
public:
    SoundPage();

    // Rebinds to a newly selected slot. Any APVTS attachments held by the
    // previous workspace are released before the new workspace is created.
    void bind (juce::AudioProcessorValueTreeState& state, std::size_t slotIndex,
               const instrument::InstrumentDescriptor* descriptor);

    // Labels of every control still rendered as Gate A preview-only.
    //
    // The Visual Gate capture uses this to report which controls were mock
    // driven, as required by UI_PRODUCTION_IMPLEMENTATION_STANDARD.md section 24.
    [[nodiscard]] juce::StringArray getGateAVisualOnlyLabels() const;

    // Hero band, owned by the shell.
    //
    // The shell places the hero band explicitly (it is a full-width band in the
    // accepted composition, at the same x range as the module rows) rather than
    // deriving it from a fraction of the page height. When the shell has set it,
    // this page keeps that rectangle and only lays out the workspace below it.
    // With no shell rectangle the page falls back to its own proportional band so
    // the page still composes correctly when used on its own.
    void setHeroBounds (juce::Rectangle<int> boundsInEditorSpace);

    [[nodiscard]] juce::Rectangle<int> getHeroBounds() const noexcept { return heroBounds; }

    [[nodiscard]] juce::String getSelectedInstrumentName() const { return instrumentName; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void selectWorkspaceFor (const instrument::InstrumentDescriptor* descriptor);

    HeroBanner hero;
    std::unique_ptr<SoundWorkspace> workspace;
    juce::String instrumentName { "Empty slot" };
    juce::Rectangle<int> heroBounds;
    bool heroBoundsFromShell {};
};

} // namespace vstengine::ui
