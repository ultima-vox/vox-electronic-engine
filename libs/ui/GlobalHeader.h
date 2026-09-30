#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "vox-ui/VoxComponents.h"
#include <functional>
#include <memory>

namespace vstengine::ui {

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

private:
    void randomizeSeed();

    juce::AudioProcessorValueTreeState& state;
    juce::Label brand, midi, cpu, outputLabel;
    vox::ui::VoxComboBox preset;
    juce::StringArray presetNames;
    vox::ui::VoxButton previousButton { "<", vox::ui::VoxButton::Type::Icon };
    vox::ui::VoxButton nextButton { ">", vox::ui::VoxButton::Type::Icon };
    vox::ui::VoxButton saveButton { "Save", vox::ui::VoxButton::Type::Secondary };
    vox::ui::VoxButton seedButton { "Seed", vox::ui::VoxButton::Type::Secondary };
    vox::ui::VoxButton panicButton { "Panic", vox::ui::VoxButton::Type::Danger };
    vox::ui::VoxButton settingsButton { "Settings", vox::ui::VoxButton::Type::Secondary };
    juce::Slider output;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputAttachment;
};

} // namespace vstengine::ui
