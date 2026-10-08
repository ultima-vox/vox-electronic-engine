#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "vox-ui/VoxComponents.h"
#include <functional>
#include <memory>

namespace vstengine::ui {

// Global header bar (the top band).
//
// Canonical order, from the accepted render and left to right:
//
//   brand | prev | next | preset | Save | Seed | Randomise | cube | divider |
//   MIDI | CPU | Output | Panic | Settings
//
// Every affordance is a vox::ui primitive: the chevrons, dice, cube, gear are
// the shared vector icon family (never a text "<" or ">"), the preset and the
// seed field are real controls, and the output knob is a real juce::Slider bound
// to `outputLevel`.
//
// TWO DECLARED GATE-A-ONLY ITEMS
// ------------------------------
// 1. The `cube` button. The accepted render shows a wireframe cube glyph in the
//    header and no state contract exists for it. It is an INERT CONTROL: it is
//    present, declared through getGateAVisualOnlyLabels() and does nothing.
//    Making it "work" would mean inventing a feature.
// 2. The Seed readout/randomise pair. `Seed` shows the live `rngSeed` value and
//    the dice button fires the SAME randomisation the previous build's Seed
//    button fired (`randomizeSeed()`); no new state is introduced.
class GlobalHeader final : public juce::Component {
public:
    struct Callbacks {
        std::function<void()> previous, next, save, panic, settings;
        std::function<void(int)> choosePreset;
    };

    GlobalHeader (juce::AudioProcessorValueTreeState&, Callbacks);
    void paint (juce::Graphics&) override;
    void resized() override;
    void setPresetName (const juce::String&);
    void setTransportActive (bool);
    void setPresetEntries (const juce::StringArray&, int selectedIndex);
    void setMidiActivity (bool);
    void setCpuLoad (float);

    // Labels of every affordance in this header that is Visual Gate A chrome
    // only, so the visual-gate capture can declare them (standard section 24).
    [[nodiscard]] static juce::StringArray getGateAVisualOnlyLabels();

private:
    void randomizeSeed();
    void refreshSeedReadout();
    void paintOutputScale (juce::Graphics&, juce::Rectangle<int> knobArea);
    void paintCpuMeter (juce::Graphics&, juce::Rectangle<int> area);
    void paintMidiIndicator (juce::Graphics&, juce::Rectangle<int> area);

    juce::AudioProcessorValueTreeState& state;

    juce::Label brand, tagline, seedLabel, seedValue, midi, cpu, outputLabel;
    vox::ui::VoxComboBox preset;
    juce::StringArray presetNames;

    vox::ui::VoxIconButton previousButton { vox::ui::icons::Icon::chevronLeft };
    vox::ui::VoxIconButton nextButton { vox::ui::icons::Icon::chevronRight };
    vox::ui::VoxButton saveButton { "Save", vox::ui::VoxButton::Type::Secondary };
    vox::ui::VoxIconButton diceButton { vox::ui::icons::Icon::dice };
    vox::ui::VoxIconButton cubeButton { vox::ui::icons::Icon::cube };
    vox::ui::VoxButton panicButton { "Panic", vox::ui::VoxButton::Type::Danger };
    vox::ui::VoxButton settingsButton { "Settings", vox::ui::VoxButton::Type::Secondary };

    juce::Slider output;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputAttachment;

    float cpuLoad01 {};
    bool midiActive {};

    juce::Rectangle<int> dividerBounds;
    juce::Rectangle<int> midiDotBounds;
    juce::Rectangle<int> cpuMeterBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GlobalHeader)
};

} // namespace vstengine::ui
