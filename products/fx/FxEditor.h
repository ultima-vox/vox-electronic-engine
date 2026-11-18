#pragma once
#include "components/VoxKnob.h"
#include "components/VoxButton.h"
#include "components/VoxComboBox.h"
#include "LayerPanel.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include "engine/FileExport.h"

namespace vox::fx {
class FxProcessor;

class GeneratorPanel final : public juce::Component, private juce::Timer {
public:
    explicit GeneratorPanel(FxProcessor&);
    void paint(juce::Graphics&) override;
    void resized() override;
    juce::ComboBox& styleSelector() noexcept { return style_; }
    juce::ComboBox& complexitySelector() noexcept { return complexity_; }
    juce::Slider& intensitySlider() noexcept { return intensity_; }
    juce::Slider& energySlider() noexcept { return energy_; }
    juce::Slider& stereoSlider() noexcept { return stereo_; }
    juce::ComboBox& lockLayerSelector() noexcept { return lockLayer_; }
    juce::Button& fieldLock(std::size_t index) noexcept { return fieldLocks_[index]; }
private:
    void timerCallback() override;
    FxProcessor& processor_;
    juce::ComboBox style_,complexity_,lockLayer_;
    std::array<juce::ToggleButton,7> fieldLocks_;
    juce::Slider intensity_,energy_,stereo_;
    std::array<juce::Label,5> labels_;
    juce::TextButton close_ { "Close" };
};

// An honest FX-01 panel: only controls with working processor bindings are
// exposed. Additional workspaces are added as their DSP/controller exists.
class FxPanel final : public juce::Component, private juce::Timer {
public:
    explicit FxPanel (FxProcessor&);
    ~FxPanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    juce::Slider& outputSlider() noexcept { return output_.getSlider(); }
    juce::Slider& durationSlider() noexcept { return duration_.getSlider(); }
    juce::ComboBox& comparisonSelector() noexcept { return comparison_; }
    juce::ComboBox& factorySelector() noexcept { return factory_; }
    juce::ComboBox& familySelector() noexcept { return family_; }
    juce::ComboBox& eventBarsSelector() noexcept { return eventBars_; }
    juce::Slider& macroDepthSlider() noexcept { return macroDepth_; }
    juce::Slider& macroValueSlider() noexcept { return macroValue_; }
    juce::ComboBox& macroDestinationSelector() noexcept { return macroDestination_; }
    juce::ComboBox& macroSelector() noexcept { return macroSelector_; }
    juce::Slider& seedSlider() noexcept { return seed_; }
    GeneratorPanel& generatorPanel() noexcept { return *generatorPanel_; }
    juce::Button& generateButton() noexcept { return generate_; }
    juce::Button& mutateButton() noexcept { return mutate_; }
    juce::ComboBox& patternGridSelector() noexcept { return patternGrid_; }
    juce::ComboBox& patternPageSelector() noexcept { return patternPage_; }
    juce::ComboBox& patternStepSelector() noexcept { return patternStepSelector_; }
    juce::Slider& patternSwingSlider() noexcept { return patternSwing_; }
    juce::Slider& patternNoteSlider() noexcept { return patternDetails_[0]; }
    juce::Slider& patternVelocitySlider() noexcept { return patternDetails_[1]; }
    juce::Slider& patternProbabilitySlider() noexcept { return patternDetails_[2]; }
    juce::Slider& patternRatchetSlider() noexcept { return patternDetails_[3]; }
    juce::ComboBox& patternLengthSelector() noexcept { return patternLength_; }
    juce::ComboBox& patternBarSelector() noexcept { return patternBar_; }
    juce::Button& patternStep(std::size_t index) noexcept { return patternSteps_[index]; }
    juce::ComboBox& patternSelector() noexcept { return patternSelector_; }
    juce::Button& midiExportButton() noexcept { return midiExport_; }
    juce::Button& sampleImportButton() noexcept { return sampleImport_; }
    juce::Button& sampleBuiltinButton() noexcept { return sampleBuiltin_; }
    juce::Button& patternButton() noexcept { return patternEnabled_; }
    juce::Button& layerLock(std::size_t index) noexcept { return layerLocks_[index]; }
    juce::ComboBox& effectSelector(std::size_t index) noexcept { return effectsType_[index]; }
    juce::Slider& effectWet(std::size_t index) noexcept { return effectsWet_[index]; }
    LayerPanel& layerPanel(std::size_t index) noexcept { return *layers_[index]; }
private:
    void timerCallback() override;
    void importSample();
    void exportPattern();
    void exportWav();
    void refreshPattern();
    void refreshMacro();
    void refreshGenerator();
    FxProcessor& processor_;
    vox::ui::VoxKnob output_ { "OUTPUT", vox::ui::VoxKnob::Size::Large };
    juce::SliderParameterAttachment attachment_;
    vox::ui::VoxKnob duration_ { "DURATION (S)", vox::ui::VoxKnob::Size::Large };
    juce::SliderParameterAttachment durationAttachment_;
    vox::ui::VoxComboBox family_,eventBars_,playMode_,triggerPolicy_,macroSelector_,macroDestination_,factory_,comparison_;
    juce::Label clockStatus_;
    juce::Slider macroValue_,macroDepth_;
    std::unique_ptr<juce::SliderParameterAttachment> macroAttachment_;
    vox::ui::VoxButton audition_ { "AUDITION" };
    vox::ui::VoxButton panic_ { "PANIC" };
    juce::Slider seed_;
    vox::ui::VoxButton generate_ { "GENERATE" };
    vox::ui::VoxButton mutate_ { "MUTATE" };
    juce::TextButton generatorSettings_ { "Settings" };
    std::unique_ptr<GeneratorPanel> generatorPanel_;
    vox::ui::VoxButton midiExport_ { "MIDI" };
    vox::ui::VoxButton wavExport_ { "WAV" };
    juce::TextButton sampleImport_ { "Import" },sampleBuiltin_ { "Built-in" };
    juce::Label sampleStatus_;
    std::unique_ptr<juce::FileChooser> sampleChooser_;
    std::unique_ptr<juce::FileChooser> exportChooser_;
    std::unique_ptr<fileexport::WavJob> wavJob_;
    std::array<juce::ToggleButton,4> layerLocks_;
    vox::ui::VoxComboBox patternSelector_,patternLength_,patternBar_,patternStepSelector_,patternGrid_,patternPage_;
    std::array<juce::Slider,4> patternDetails_;
    juce::Slider patternSwing_;
    std::array<juce::TextButton,16> patternSteps_;
    vox::ui::VoxButton patternEnabled_ { "HOST PATTERN", vox::ui::VoxButton::Type::Toggle };
    std::unique_ptr<juce::ButtonParameterAttachment> patternAttachment_;
    std::array<std::unique_ptr<LayerPanel>, 4> layers_;
    std::array<vox::ui::VoxComboBox,4> effectsType_;
    std::array<juce::Slider,4> effectsWet_, effectsAmount_;
    std::array<std::unique_ptr<juce::SliderParameterAttachment>,4> wetAttachments_,amountAttachments_;
    float shownPeak_ = 0.0f;
    unsigned shownEvents_=0,shownGrains_=0,clipHold_=0;
    std::uint32_t shownDropped_=0;
};
} // namespace vox::fx
