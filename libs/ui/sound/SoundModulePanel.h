#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

#include "common/UiComponents.h"

namespace vstengine::ui {

// Reusable presentation primitive for one sound module panel (OSCILLATOR,
// FILTER, AMP ENVELOPE, DRIVE, MATRIX, ...).
//
// This owns surface and header chrome plus the content rectangle, nothing else.
// It has no domain knowledge, no parameter state and no instrument-specific
// behaviour, so the Psy Bass, Acid and generic workspaces all compose the same
// primitive instead of sharing one hardcoded panel set.
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
    void setAccent (juce::Colour newAccent);
    [[nodiscard]] juce::Colour getAccent() const noexcept { return accent; }

    // Compact right-aligned selector text in the header (waveform type, filter
    // type, drive type, ...). Presentation only during Visual Gate A.
    void setHeaderSelector (juce::String text);

    void setStatusDotVisible (bool shouldBeVisible);
    void setStatusDotActive (bool shouldBeActive);

    // Below this height the header chrome stops being drawn so that compact
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
    juce::String title;
    juce::String subtitle;
    juce::String headerSelector;
    juce::Colour accent { colours::primary };
    bool statusDotVisible { true };
    bool statusDotActive { false };

    std::vector<juce::Component*> contentComponents;
    std::vector<std::function<void (juce::Rectangle<int>)>> contentLayouts;
};

} // namespace vstengine::ui
