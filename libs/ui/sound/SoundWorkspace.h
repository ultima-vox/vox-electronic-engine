#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

#include "HeroBanner.h"
#include "SoundModulePanel.h"
#include "ui/common/UiComponents.h"
#include "instrument/InstrumentContract.h"

namespace vstengine::ui {

enum class SoundWorkspaceKind { generic, psyBass, acid };

// Everything a workspace needs in order to resolve real host parameters.
//
// This is a read-only view of authoritative state. Workspaces never construct
// or mutate parameter ids here.
struct SoundWorkspaceBinding {
    juce::AudioProcessorValueTreeState& state;
    std::size_t slotIndex {};
    const instrument::InstrumentDescriptor* descriptor {};
};

// Base for the per-instrument SOUND compositions.
//
// UI_PRODUCTION_IMPLEMENTATION_STANDARD.md section 20 requires instrument
// specific composition to live behind a provider/descriptor boundary rather
// than as concrete instrument checks inside the plugin editor. SoundPage owns
// exactly one workspace at a time and swaps it by InstrumentId.
//
// Subclasses compose their own panel set and control set. They deliberately do
// NOT share one fixed set of relabelled panels: Psy Bass and Acid have
// genuinely different compositions because their identities differ.
class SoundWorkspace : public juce::Component {
public:
    ~SoundWorkspace() override = default;

    // Resolves controls against the newly selected slot. Implementations must
    // dispose any previous APVTS attachments before creating new ones.
    virtual void bind (const SoundWorkspaceBinding&) = 0;

    // Labels of every control that is still Gate A preview-only, so the Visual
    // Gate capture can report which controls were mock driven.
    [[nodiscard]] virtual juce::StringList getGateAVisualOnlyLabels() const { return {}; }

    [[nodiscard]] virtual SoundWorkspaceKind getKind() const = 0;

    [[nodiscard]] juce::Colour getIdentityAccent() const noexcept { return identityAccent; }
    [[nodiscard]] HeroIdentity getHeroIdentity() const noexcept { return heroIdentity; }

    void paint (juce::Graphics&) override;

protected:
    void setIdentity (juce::Colour accent, HeroIdentity hero);

    [[nodiscard]] SoundModulePanel& addPanel (juce::String title);
    void applyAccentToPanels();

    juce::Colour identityAccent { colours::primary };
    HeroIdentity heroIdentity { HeroIdentity::generic };

    std::vector<std::unique_ptr<SoundModulePanel>> panels;
};

// Distributes controls evenly across a horizontal strip.
//
// The strip is divided into `count` equal cells, so a control row never
// overflows its panel and always keeps a stable musical order.
void layoutControlRow (juce::Component* const* items, int count,
                       juce::Rectangle<int> area, int gap = 4);

// Same, but wrapping into `columns` x `rows`. Used where a panel needs a
// second control line under the first.
void layoutControlGrid (juce::Component* const* items, int count,
                        juce::Rectangle<int> area, int columns, int gap = 4);

} // namespace vstengine::ui
