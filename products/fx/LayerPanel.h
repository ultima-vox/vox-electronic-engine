#pragma once
#include "components/VoxKnob.h"
#include "components/VoxButton.h"
#include "components/VoxComboBox.h"
#include "engine/NoiseRiser.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace vox::fx {
class FxProcessor;

// Structural controls apply to newly triggered events and are stored as
// Program content, not presented as DAW automation parameters.
class LayerPanel final : public juce::Component {
public:
    LayerPanel(FxProcessor&, int index);
    void paint(juce::Graphics&) override;
    void resized() override;
    void refresh(const NoiseLayer& layer);
    juce::Slider& gainSlider() noexcept { return gain_.getSlider(); }
    juce::Slider& panSlider() noexcept { return pan_.getSlider(); }
    juce::Button& enabledButton() noexcept { return enabled_; }
private:
    FxProcessor& processor_;
    int index_;
    vox::ui::VoxButton enabled_ { "ON", vox::ui::VoxButton::Type::Toggle };
    vox::ui::VoxComboBox family_;
    vox::ui::VoxKnob gain_ { "GAIN", vox::ui::VoxKnob::Size::Small };
    vox::ui::VoxKnob pan_ { "PAN", vox::ui::VoxKnob::Size::Small };
    vox::ui::VoxKnob offset_ { "START", vox::ui::VoxKnob::Size::Small };
    vox::ui::VoxKnob span_ { "LENGTH", vox::ui::VoxKnob::Size::Small };
};
} // namespace vox::fx
