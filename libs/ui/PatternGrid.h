#pragma once
// Lane x step editor for a Rack-owned INTERNAL pattern (e.g. the Full-on Drums
// 8 x 16 grid). It shows only what the instrument declares: its lanes, step
// and bar limits and the step fields it reads. Every edit goes to the owner
// (the Rack) through onEdit; the grid never keeps a competing copy.
#include "pattern/Pattern.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <functional>
#include <memory>

namespace vstengine::ui {
class PatternGrid final : public juce::Component {
public:
  PatternGrid();

  // Owner callbacks. Each returns false with a reason when it rejects the
  // change; the grid then shows the reason and the owner's current pattern.
  std::function<bool(const pattern::Pattern &, juce::String &why)> onEdit;
  std::function<bool(std::size_t factoryIndex, juce::String &why)> onFactory;
  std::function<bool(pattern::Pattern &out)> onReload; // the owner's current pattern

  // Null content: nothing to edit (the grid is hidden by its page).
  void setContent(std::shared_ptr<const pattern::Content>);
  void reload();

  void paint(juce::Graphics &) override;
  void resized() override;
  void mouseDown(const juce::MouseEvent &) override;
  void mouseDrag(const juce::MouseEvent &) override;
  void mouseUp(const juce::MouseEvent &) override;

  // Testing: the cell bounds of (lane, step) in the visible bar, and the bar.
  [[nodiscard]] juce::Rectangle<int> cellBoundsForTesting(std::uint32_t lane,
                                                          std::uint32_t step) const;
  void showBarForTesting(std::uint32_t bar) { showBar(bar); }
  [[nodiscard]] const pattern::Pattern &patternForTesting() const noexcept { return *working; }

private:
  std::shared_ptr<const pattern::Content> content;
  std::unique_ptr<pattern::Pattern> working;
  std::uint32_t bar{};
  juce::ComboBox factory, steps, bars, timing;
  std::array<juce::TextButton, VOX_PATTERN_V2_MAX_BARS> barButtons;
  juce::TextButton clear{"Clear bar"};
  juce::Label hint, status;
  // A press on a sounding cell starts a velocity drag; a release without
  // movement toggles it off.
  int dragLane{-1}, dragStep{-1}, dragStartY{}, dragStartVelocity{};
  bool dragged{};

  [[nodiscard]] bool reads(std::uint32_t field) const noexcept;
  [[nodiscard]] juce::Rectangle<int> gridArea() const;
  [[nodiscard]] bool cellAt(juce::Point<int>, int &lane, int &step) const;
  void showBar(std::uint32_t);
  void commit(const pattern::Pattern &);
  void syncControls();
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PatternGrid)
};
} // namespace vstengine::ui
