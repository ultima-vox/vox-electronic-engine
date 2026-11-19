#include "CurveEditor.h"
#include "Processor.h"
#include "Tokens.h"
#include "Typography.h"
#include <algorithm>
#include <cmath>

namespace vox::fx {
namespace {
class CurveChange final : public juce::UndoableAction {
public:
    CurveChange(FxProcessor& p,CurveTarget target,std::uint64_t revision,timeline::Curve before,timeline::Curve after)
        : processor(p), target_(target), expectedRevision(revision), oldCurve(before), newCurve(after) {}
    bool perform() override { return apply(newCurve); }
    bool undo() override { return apply(oldCurve); }
private:
    bool apply(const timeline::Curve& curve) {
        return processor.editCurveIfRevision(target_, expectedRevision, curve);
    }
    FxProcessor& processor;
    CurveTarget target_;
    std::uint64_t expectedRevision;
    timeline::Curve oldCurve,newCurve;
};
}
CurveEditor::CurveEditor(FxProcessor& processor,std::optional<std::size_t> layer)
    : processor_(processor), target_{layer, Kind::gain} {
    jassert(!layer || *layer < 4);
    setWantsKeyboardFocus(true);
    updateTitle();
    setDescription("Double click to add nodes; drag to edit. Delete removes a node. L, S, E or H select segment shape.");
    refresh();
}
void CurveEditor::updateTitle() {
    const char* name = kind()==Kind::gain ? "amplitude" : kind()==Kind::pitch ? "pitch" :
                       kind()==Kind::cutoff ? "cutoff" : "panorama";
    setTitle(juce::String(target_.layer ? "Layer " : "Program ") + name + " curve");
}
void CurveEditor::setKind(Kind kind) {
    if (target_.kind == kind || static_cast<unsigned>(kind) > static_cast<unsigned>(Kind::pan)) return;
    target_.kind=kind;dragging_=false;selected_=-1;undo_.clearUndoHistory();
    updateTitle();
    refresh();
}
juce::Rectangle<float> CurveEditor::graph() const noexcept { return getLocalBounds().toFloat().reduced(8.0f); }
juce::Point<float> CurveEditor::pointFor(const timeline::Node& n) const noexcept {
    auto r=graph(); return {r.getX()+n.time*r.getWidth(),r.getBottom()-n.value*r.getHeight()};
}
int CurveEditor::hit(juce::Point<float> point) const noexcept {
    int result=-1;float best=9.0f*9.0f;
    for(int i=0;i<shown_.count;++i){const auto d=pointFor(shown_.nodes[static_cast<std::size_t>(i)])-point;
        const float dist=d.x*d.x+d.y*d.y;if(dist<=best){best=dist;result=i;}}
    return result;
}
void CurveEditor::paint(juce::Graphics& g) {
    namespace c=vox::ui::tokens::colour;
    auto r=graph(); g.setColour(c::graph);g.fillRoundedRectangle(r,3.0f);
    g.setColour(c::border);g.drawRoundedRectangle(r,3.0f,1.0f);
    juce::Path path;
    constexpr int samples=80;
    for(int i=0;i<=samples;++i){const float t=static_cast<float>(i)/samples;
        const float x=r.getX()+t*r.getWidth(), y=r.getBottom()-shown_.evaluate(t)*r.getHeight();
        if(i==0)path.startNewSubPath(x,y);else path.lineTo(x,y);}
    g.setColour(c::accent);g.strokePath(path,juce::PathStrokeType(1.5f));
    for(int i=0;i<shown_.count;++i){const auto point=pointFor(shown_.nodes[static_cast<std::size_t>(i)]);
        g.setColour(i==selected_?c::text:c::accent);g.fillEllipse(point.x-3.0f,point.y-3.0f,6.0f,6.0f);}
    if(hasKeyboardFocus(true)){g.setColour(c::text);g.drawRoundedRectangle(r,3.0f,1.5f);}
}
void CurveEditor::syncRestore() {
    const auto snapshot=processor_.versionedProgram();
    if(snapshot.revision!=restoreEpoch_) {
        undo_.clearUndoHistory();dragging_=false;selected_=-1;restoreEpoch_=snapshot.revision;
        if (const auto* curve = target_.get(snapshot.program)) shown_ = *curve;
        repaint();
    }
}
void CurveEditor::refresh() {
    // Take curve and revision together; an earlier panel snapshot may already
    // have been superseded by a host restore on another thread.
    const auto snapshot=processor_.versionedProgram();
    if(snapshot.revision!=restoreEpoch_) {
        undo_.clearUndoHistory();dragging_=false;selected_=-1;restoreEpoch_=snapshot.revision;
    }
    if(!dragging_){if (const auto* curve = target_.get(snapshot.program)) shown_ = *curve;
        if(selected_>=shown_.count)selected_=-1;
        repaint();}
}
bool CurveEditor::publish(const timeline::Curve& curve) {
    return processor_.editCurveIfRevision(target_, restoreEpoch_, curve);
}
bool CurveEditor::setCurve(const timeline::Curve& curve) {
    syncRestore();
    return applyEdit(curve);
}
bool CurveEditor::resetCurve() { return setCurve(timeline::Curve::constant(target_.defaultValue())); }
bool CurveEditor::applyEdit(const timeline::Curve& curve) {
    if (!curve.valid()) return false;
    const auto snapshot = processor_.versionedProgram();
    const auto* old = target_.get(snapshot.program);
    // The caller may have constructed the edit from shown_ before a host
    // restore. Never carry that edit into the newly restored Program.
    if (snapshot.revision != restoreEpoch_ || !old) { refresh(); return false; }
    undo_.beginNewTransaction();
    if (!undo_.perform(new CurveChange(processor_, target_, restoreEpoch_, *old, curve))) { refresh(); return false; }
    shown_=curve;repaint();return true;
}
bool CurveEditor::undo(){syncRestore();const bool ok=undo_.undo();refresh();return ok;}
bool CurveEditor::redo(){syncRestore();const bool ok=undo_.redo();refresh();return ok;}
void CurveEditor::mouseDown(const juce::MouseEvent& event) {
    syncRestore();
    grabKeyboardFocus(); selected_=hit(event.position);dragging_=selected_>=0;
    if(dragging_) beforeDrag_=shown_;
    repaint();
}
void CurveEditor::mouseDrag(const juce::MouseEvent& event) {
    syncRestore();
    if(!dragging_ || selected_<0)return;
    auto r=graph();if(r.getWidth()<=0 || r.getHeight()<=0)return;
    auto& node=shown_.nodes[static_cast<std::size_t>(selected_)];
    node.value=juce::jlimit(0.0f,1.0f,(r.getBottom()-event.position.y)/r.getHeight());
    if(selected_>0 && selected_+1<shown_.count){
        const float time=std::round(juce::jlimit(0.0f,1.0f,(event.position.x-r.getX())/r.getWidth())*16.0f)/16.0f;
        const float lo=shown_.nodes[static_cast<std::size_t>(selected_-1)].time+0.0001f;
        const float hi=shown_.nodes[static_cast<std::size_t>(selected_+1)].time-0.0001f;
        if(lo<=hi)node.time=juce::jlimit(lo,hi,time);
    }
    if (!publish(shown_)) { dragging_=false; refresh(); }
    repaint();
}
void CurveEditor::mouseUp(const juce::MouseEvent&) {
    syncRestore();
    if(!dragging_) return;
    dragging_=false;
    const auto after=shown_;undo_.beginNewTransaction();
    (void)undo_.perform(new CurveChange(processor_,target_,restoreEpoch_,beforeDrag_,after));repaint();
}
void CurveEditor::mouseDoubleClick(const juce::MouseEvent& event) {
    syncRestore();
    dragging_=false;auto r=graph();if(r.getWidth()<=0||r.getHeight()<=0||shown_.count>=16)return;
    const float time=std::round(juce::jlimit(0.0f,1.0f,(event.position.x-r.getX())/r.getWidth())*16.0f)/16.0f;
    if(time<=0||time>=1)return;
    auto next=shown_;int at=1;while(at<next.count && next.nodes[static_cast<std::size_t>(at)].time<time)++at;
    if(at>=next.count || next.nodes[static_cast<std::size_t>(at)].time==time)return;
    for(int i=next.count;i>at;--i)next.nodes[static_cast<std::size_t>(i)]=next.nodes[static_cast<std::size_t>(i-1)];
    next.nodes[static_cast<std::size_t>(at)]={time,juce::jlimit(0.0f,1.0f,(r.getBottom()-event.position.y)/r.getHeight()),timeline::Shape::linear};
    ++next.count;if(applyEdit(next))selected_=at;
}
bool CurveEditor::keyPressed(const juce::KeyPress& key) {
    syncRestore();
    if(key.getModifiers().isCommandDown() && key.getKeyCode()=='z')return undo();
    if(key.getModifiers().isCommandDown() && key.getKeyCode()=='y')return redo();
    if(selected_<0 || selected_>=shown_.count)return false;
    auto next=shown_;
    if(key==juce::KeyPress::deleteKey || key==juce::KeyPress::backspaceKey){
        if(selected_==0 || selected_+1==next.count)return false;
        for(int i=selected_;i+1<next.count;++i)next.nodes[static_cast<std::size_t>(i)]=next.nodes[static_cast<std::size_t>(i+1)];
        --next.count;selected_=-1;return applyEdit(next);
    }
    switch(juce::CharacterFunctions::toLowerCase(static_cast<juce::juce_wchar>(key.getTextCharacter()))){
        case 'l':next.nodes[static_cast<std::size_t>(selected_)].shape=timeline::Shape::linear;break;
        case 's':next.nodes[static_cast<std::size_t>(selected_)].shape=timeline::Shape::smooth;break;
        case 'e':next.nodes[static_cast<std::size_t>(selected_)].shape=timeline::Shape::exponential;break;
        case 'h':next.nodes[static_cast<std::size_t>(selected_)].shape=timeline::Shape::hold;break;
        default:return false;
    }
    return applyEdit(next);
}
} // namespace vox::fx
