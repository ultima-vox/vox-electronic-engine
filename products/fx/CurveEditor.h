#pragma once
#include "engine/Timeline.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace vox::fx {
class FxProcessor;

// Layer gain/pan curve editor. Nodes are normalized; endpoints stay at 0 and 1 in time.
// Double-click inserts, Delete removes an interior node, drag snaps time to
// 1/16, L/S/E/H select the outgoing segment shape, Ctrl-Z/Y undo/redo.
class CurveEditor final : public juce::Component {
public:
    enum class Kind { gain, pan };
    CurveEditor(FxProcessor&, int layer);
    void setKind(Kind);
    Kind kind() const noexcept { return kind_; }
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;
    void refresh(const timeline::Curve&);
    bool setCurve(const timeline::Curve&); // one undoable structural edit
    bool undo();
    bool redo();
private:
    juce::Rectangle<float> graph() const noexcept;
    juce::Point<float> pointFor(const timeline::Node&) const noexcept;
    int hit(juce::Point<float>) const noexcept;
    bool publish(const timeline::Curve&);
    void syncRestore();
    FxProcessor& processor_;
    int layer_;
    Kind kind_=Kind::gain;
    timeline::Curve shown_;
    timeline::Curve beforeDrag_;
    juce::UndoManager undo_;
    int selected_ = -1;
    bool dragging_ = false;
    std::uint64_t restoreEpoch_ = 0;
};
} // namespace vox::fx
