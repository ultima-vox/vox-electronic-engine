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
    GlobalHeader(juce::AudioProcessorValueTreeState &, Callbacks);
    void paint(juce::Graphics &) override;
    void resized() override;
    void setPresetName(const juce::String &);
    void setTransportActive(bool);
    void setPresetEntries(const juce::StringArray &, int);
    void setMidiActivity(bool);
    void setCpuLoad(float);
    void setOutputPeaks(float left, float right);
    [[nodiscard]] bool controlsFit() const noexcept;

  private:
    void randomizeSeed();
    juce::AudioProcessorValueTreeState& state;
    juce::Label midi, cpu, outputLabel, seedLabel;
    vox::ui::VoxComboBox preset;
    juce::StringArray presetNames;
    vox::ui::VoxButton previousButton { "<", vox::ui::VoxButton::Type::Icon };
    vox::ui::VoxButton nextButton { ">", vox::ui::VoxButton::Type::Icon };
    vox::ui::VoxButton saveButton { "Save", vox::ui::VoxButton::Type::Secondary };
    vox::ui::VoxButton seedButton{"Dice", vox::ui::VoxButton::Type::Secondary};
    vox::ui::VoxButton panicButton { "Panic", vox::ui::VoxButton::Type::Danger };
    vox::ui::VoxButton settingsButton { "Settings", vox::ui::VoxButton::Type::Secondary };
    juce::Slider output, seed;
    juce::Rectangle<int> brandBounds, meterBounds;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        outputAttachment, seedAttachment;
    float cpuValue{}, peaks[2]{};
    bool transportActive{};
};
} // namespace vstengine::ui
