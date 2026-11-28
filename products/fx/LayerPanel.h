#pragma once
#include "CurveEditor.h"
#include "components/VoxKnob.h"
#include "components/VoxButton.h"
#include "components/VoxComboBox.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <functional>
#include <memory>

namespace vox::fx {
class FxProcessor;

// Reference DesignZone layer card (PR #160 e420d2c0): family select, enable
// switch + solo toggle, a 3-column dial grid of real layer parameters and a
// 2x2 curve-thumbnail directory. The thumbnails open the matching timeline
// editor; the full interactive curve editor stays reachable as a contextual
// overlay (CURVES toggle) so per-card advanced editing is preserved.
class LayerPanel final : public juce::Component {
public:
    LayerPanel(FxProcessor&, int index);
    void paint(juce::Graphics&) override;
    void resized() override;
    void refresh(const NoiseLayer& layer);
    juce::Slider& gainSlider() noexcept { return gain_.getSlider(); }
    juce::Slider& panSlider() noexcept { return pan_.getSlider(); }
    juce::Button& enabledButton() noexcept { return enabled_; }
    juce::Button& soloButton() noexcept { return solo_; }
    CurveEditor& curveEditor() noexcept { return curve_; }
    juce::ComboBox& curveSelector() noexcept { return curveType_; }
    // Contextual source editing: each card opens the source editor on its own layer.
    juce::Button& editSourceButton() noexcept { return editSource_; }
    std::function<void()> onEditSource;
    // Curve thumbnails: kind 0..3 (gain/pitch/cutoff/pan) opens the TIMELINE
    // workspace on that layer's trajectory (reference openTimeline behavior).
    std::function<void(int)> onOpenTimeline;
private:
    template<typename Edit> void editKnob(Edit);
    std::optional<std::uint64_t> dragRevision_;
    FxProcessor& processor_;
    int index_;
    CurveEditor curve_;
    vox::ui::VoxButton enabled_ { "ON", vox::ui::VoxButton::Type::Toggle };
    vox::ui::VoxButton solo_ { "SOLO", vox::ui::VoxButton::Type::Toggle };
    vox::ui::VoxButton editSource_ { "SOURCE" };
    vox::ui::VoxButton curveEdit_ { "CURVES", vox::ui::VoxButton::Type::Toggle };
    vox::ui::VoxComboBox family_,curveType_;
    vox::ui::VoxKnob gain_ { "GAIN", vox::ui::VoxKnob::Size::Small };
    vox::ui::VoxKnob pan_ { "PAN", vox::ui::VoxKnob::Size::Small };
    vox::ui::VoxKnob transpose_ { "TRANSPOSE", vox::ui::VoxKnob::Size::Small };
    vox::ui::VoxKnob offset_ { "START", vox::ui::VoxKnob::Size::Small };
    vox::ui::VoxKnob span_ { "LENGTH", vox::ui::VoxKnob::Size::Small };
    vox::ui::VoxKnob character_ { "CHARACTER", vox::ui::VoxKnob::Size::Small };
    // 2x2 curve directory buttons (reference .fx-layer-curves).
    struct CurveThumb : juce::Component, juce::SettableTooltipClient {
        LayerPanel& owner;
        int kind;
        CurveThumb (LayerPanel& o, int k) : owner (o), kind (k) {}
        void paint (juce::Graphics&) override;
        void mouseUp (const juce::MouseEvent&) override;
    };
    std::array<std::unique_ptr<CurveThumb>,4> curveThumbs_;
    bool curveOverlayOpen_ = false;
};
} // namespace vox::fx
