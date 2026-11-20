#pragma once

#include "WorkspaceModel.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <memory>

namespace vox::drums {
class DrumsProcessor;

// Candidate native #127 workspace. Instantiated by qualification tests while
// Sound/Sample and DLAY are ported; the production editor is switched only when
// its existing functional inventory is preserved. No second processor or DSP.
class DrumsWorkspace final : public juce::Component, private juce::Timer {
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
    void pollForTesting();

private:
    void timerCallback() override;
    struct Impl;
    std::unique_ptr<Impl> impl_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumsWorkspace)
};
} // namespace vox::drums
