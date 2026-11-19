#pragma once
#include "engine/CurveTarget.h"
#include "engine/CurveEdit.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace vox::fx {
class FxProcessor;

// Layer or Program trajectory editor. Nodes are normalized; endpoints stay at 0 and 1 in time.
// Double-click inserts, Delete removes an interior node, drag snaps time to
// 1/16, L/S/E/H select the outgoing segment shape, Ctrl-Z/Y undo/redo.
class CurveEditor final : public juce::Component {
public:
    using Kind = CurveKind;
    CurveEditor(FxProcessor&, std::optional<std::size_t> layer);
    void setKind(Kind);
    Kind kind() const noexcept { return target_.kind; }
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;
    void refresh();
    std::optional<float> visiblePlayhead() const noexcept { return playhead_; }
    bool resetCurve();
    bool setCurve(const timeline::Curve&); // one undoable structural edit
    bool undo();
    bool redo();
private:
    juce::Rectangle<float> graph() const noexcept;
    juce::Point<float> pointFor(const timeline::Node&) const noexcept;
    int hit(juce::Point<float>) const noexcept;
    bool publish(const timeline::Curve&);
    bool applyEdit(const timeline::Curve&);
    void updateTitle();
    void enterNodeValues();
    bool copyCurve() const;
    bool pasteCurve();
    void syncRestore();
    FxProcessor& processor_;
    CurveTarget target_;
    timeline::Curve shown_;
    std::optional<float> playhead_;
    timeline::Curve beforeDrag_;
    juce::UndoManager undo_;
    int selected_ = -1;
    curveedit::Selection selection_ = 0;
    bool dragging_ = false;
    std::uint64_t restoreEpoch_ = 0;
};
} // namespace vox::fx
