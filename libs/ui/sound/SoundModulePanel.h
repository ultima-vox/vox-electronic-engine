#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

#include "ui/common/UiComponents.h"
#include "vox-ui/components/VoxInlineSelector.h"
#include "vox-ui/components/VoxSectionHeader.h"

namespace vstengine::ui {

// Reusable presentation primitive for one sound module panel (OSCILLATOR,
// FILTER, AMP ENVELOPE, DRIVE, MATRIX, ...).
//
// This owns surface and header chrome plus the content rectangle, nothing else.
// It has no domain knowledge, no parameter state and no instrument-specific
// behaviour, so the Psy Bass, Acid and generic workspaces all compose the same
// primitive instead of sharing one hardcoded panel set.
//
// HEADER
// ------
// The header is exactly one vox::ui::VoxSectionHeader, the shared implementation
// of the canonical `status/power | title | optional selector/action` strip
// (UI_PRODUCTION_IMPLEMENTATION_STANDARD.md section 11). This panel used to
// paint its own header, which is how it ended up with a plain 6 px circle
// instead of a power glyph, a badge with no real affordance, and a Unicode
// black-down-pointing-triangle used as a chevron.
//
// CONTENT OWNERSHIP
// -----------------
// A panel OWNS its content components (graphs, knobs, sequencers, selectors).
// Content is a child of the panel, so every content rectangle is PANEL-LOCAL.
// This is the single coordinate space for module content and it is what makes
// the layout correct: a knob placed in the panel's content rectangle cannot lose
// the panel's own x/y offset.
//
// The workspace supplies composition knowledge through setContentLayout(): it is
// invoked with the content rectangle in panel-local coordinates whenever the
// panel resizes. The workspace decides what goes where; the panel owns where it
// lives.
//
// Do NOT pass getContentBounds() to a component that is a child of the
// workspace. That mixes panel-local and workspace coordinate spaces and
// collapses content towards the workspace origin.
//
// Visual composition only. Nothing here reads or writes engine state.
class SoundModulePanel final : public juce::Component {
public:
    explicit SoundModulePanel (juce::String titleText = {});

    void setSubtitle (juce::String text);

    // Instrument/domain identity accent.
    //
    // The module header itself deliberately stays on the shared interaction
    // accent, because the accepted render draws the power glyph in cyan for the
    // instrument whose identity accent is green. Product content (graphs,
    // sequencers, option lists) applies the identity accent through its own
    // setAccent(); this accessor is how a workspace reads the same value back.
    void setAccent (juce::Colour newAccent);
    [[nodiscard]] juce::Colour getAccent() const noexcept { return accent; }

    // Compact header value readout (filter type, drive type, ...). It is a real
    // VoxComboBox carrying a vector chevron; it stays a readout until a
    // workspace supplies options through getHeaderSelector().setItems(...).
    void setHeaderSelector (juce::String text);
    [[nodiscard]] vox::ui::VoxInlineSelector& getHeaderSelector() noexcept
    {
        return headerSelector;
    }

    // Leading status affordance. These two calls map onto the shared header's
    // power glyph, which is interactive and reports through onPowerToggled.
    void setStatusDotVisible (bool shouldBeVisible);
    void setStatusDotActive (bool shouldBeActive);
    [[nodiscard]] bool isStatusDotActive() const noexcept { return statusDotActive; }

    // Replaces the leading power glyph with a vector status icon. The accepted
    // render uses the gear for AMP ENVELOPE and the matrix glyph for MATRIX.
    void setLeadingIcon (vox::ui::icons::Icon icon);

    // Trailing affordances. `action` is flush right (the bordered "+" on AMP
    // ENVELOPE and MATRIX); `control` is the centred slot the OSCILLATOR and
    // MODULATION segmented rows use.
    void setActionComponent (juce::Component* component);
    void setControlComponent (juce::Component* component);
    void setTrailingPowerVisible (bool shouldBeVisible);

    std::function<void (bool)> onPowerToggled;

    // Below this height the header chrome stops being laid out so that compact
    // breakpoints degrade gracefully instead of clipping text.
    static constexpr int minimumUsefulHeight = 46;

    [[nodiscard]] juce::Rectangle<int> getHeaderBounds() const;

    // PANEL-LOCAL. Only valid for components parented to this panel.
    [[nodiscard]] juce::Rectangle<int> getContentBounds() const;

    // Reparents a set of content components into this panel and lays them out
    // here.
    //
    // `layout` is called with the panel-local content rectangle on every resize,
    // and once immediately so content is positioned even before the first layout
    // pass. Pass an empty/null layout to parent components without arranging
    // them.
    void addContent (const std::vector<juce::Component*>& components,
                     std::function<void (juce::Rectangle<int>)> layout);

    // Drops every content component, so a workspace can rebuild its controls
    // without leaving stale children behind.
    void clearContent();

    [[nodiscard]] juce::Component* getNthContentComponent (int index) const
    {
        return contentComponents.empty() ? nullptr
                                        : contentComponents[static_cast<std::size_t> (index)];
    }

    [[nodiscard]] int getNumContentComponents() const noexcept
    {
        return static_cast<int> (contentComponents.size());
    }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::String headerTitle;
    juce::String subtitle;
    juce::Colour accent { colours::primary };
    bool statusDotVisible { true };
    bool statusDotActive { true };

    vox::ui::VoxSectionHeader header;
    vox::ui::VoxInlineSelector headerSelector { vox::ui::VoxInlineSelector::Form::Header };

    std::vector<juce::Component*> contentComponents;
    std::vector<std::function<void (juce::Rectangle<int>)>> contentLayouts;
};

} // namespace vstengine::ui
