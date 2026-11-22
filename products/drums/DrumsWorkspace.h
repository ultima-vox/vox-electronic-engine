#pragma once

#include "WorkspaceModel.h"
#include "DrumsLayoutState.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <memory>

namespace vox::drums {
class DrumsProcessor;
class Page;
class DrumsDockZone;
class PatternEditor;
class SongStrip;

// Native #127 production editor. Owns views, not a second processor or DSP.
// Draft qualification and the remaining dock/responsiveness/host gates are
// tracked in NATIVE_WORKSPACE_STATUS.md.
class DrumsWorkspace final : public juce::Component, public juce::DragAndDropContainer,
                             private juce::Timer, private juce::ChangeListener,
                             private juce::FocusChangeListener {
public:
    struct Telemetry {
        std::array<float, 16> pads {};
        std::array<float, 2> main {};
        std::uint32_t hits = 0, outputs = 0;
    };

    explicit DrumsWorkspace (DrumsProcessor&);
    ~DrumsWorkspace() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void selectPad (int);
    void pinInspector (bool);
    void setListView (bool);
    void setLayout (workspace::Layout);
    workspace::Layout layout() const noexcept;
    const workspace::Selection& selection() const noexcept;
    const Telemetry& telemetry() const noexcept;
    juce::Button& padSelectButton (int);
    juce::Button& padHitButton (int);
    juce::Slider* inspectorSlider (const juce::String& suffix);
    juce::Button* inspectorButton (const juce::String& suffix);
    juce::ComboBox* inspectorCombo (const juce::String& name);
    Page* toolPage (workspace::DockTool);
    juce::ComboBox* hostCombo (const juce::String& id);
    juce::Slider* returnSlider (const juce::String& id);
    // Bound controls for production qualification. Pad Sound controls follow
    // selection; Mixer controls exist for every pad.
    juce::Slider* sliderFor (const juce::String& id);
    juce::Button* buttonFor (const juce::String& id);
    void pollForTesting();
    bool applyWorkspace (const layout::Document&, bool confirmedReset = false);
    bool moveDockPanel (layout::Byte, layout::Type, unsigned group = 0, bool newGroup = false);
    bool hideDockPanel (layout::Byte);
    bool setDockRail (layout::Type, bool);
    DrumsDockZone& dockZone (layout::Type);
    PatternEditor& patternEditor();
    SongStrip& songEditor();

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void globalFocusChanged (juce::Component*) override;
    void readLayout();
    void commitLayout();
    void refreshInspector();
    void showWorkspaceMenu();
    void showToolPanel (layout::Byte);
    void showDockMenu (layout::Type, unsigned, layout::Byte, juce::Component&);
    bool commitDockDocument (std::optional<layout::Document>);
    struct Impl;
    std::unique_ptr<Impl> impl_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumsWorkspace)
};
} // namespace vox::drums
