#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>
#include <memory>
#include <cstdint>

namespace vox::drums {
class DrumsProcessor;

// Native Sound/Sample views use the donor's canonical structural bindings.
// Selection is a UI context, never an audition command.
class PadControlSurface final : public juce::Component, public juce::FileDragAndDropTarget {
public:
    enum class Group { sound, sample };
    PadControlSurface (DrumsProcessor&, Group);
    ~PadControlSurface() override;
    void setContext (int pad, int player);
    void update();
    void paint (juce::Graphics&) override;
    void resized() override;
    int preferredHeight (int width) const;
    // DLAY SOUND_SECTIONS bits0..3: Source, Shaping, Filter/Envelope, Output.
    // Other bits are retained for forward-safe presentation edits.
    std::uint8_t collapsedSections() const noexcept;
    bool setCollapsedSections (std::uint8_t mask, bool notify = true);
    std::function<void()> onSectionsChanged;
    std::function<void()> onLayoutChanged;
    juce::Slider* structuralSlider (const juce::String&);
    juce::ComboBox* structuralCombo (const juce::String&);
    juce::Button* structuralToggle (const juce::String&);
    juce::Slider* hostSlider (const juce::String& suffix);
    juce::Button* sampleButton (const juce::String&);
    juce::String sampleStatus() const;
    juce::Component& waveform();
    void loadSample (const juce::File&);
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;
    std::function<void (int)> onPlayerSelected;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PadControlSurface)
};
} // namespace vox::drums
