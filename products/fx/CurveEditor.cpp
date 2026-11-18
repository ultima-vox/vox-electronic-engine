#include "CurveEditor.h"
#include "Processor.h"
#include "Tokens.h"
#include "Typography.h"
#include <algorithm>
#include <cmath>

namespace vox::fx {
namespace {
timeline::Curve& curveFor(NoiseLayer& layer,CurveEditor::Kind kind) noexcept {
    return kind==CurveEditor::Kind::gain?layer.gainCurve:layer.panCurve;
}
const timeline::Curve& curveFor(const NoiseLayer& layer,CurveEditor::Kind kind) noexcept {
    return kind==CurveEditor::Kind::gain?layer.gainCurve:layer.panCurve;
}
class CurveChange final : public juce::UndoableAction {
public:
    CurveChange(FxProcessor& p,int layer,CurveEditor::Kind which,timeline::Curve before,timeline::Curve after)
        : processor(p), index(layer), kind(which), oldCurve(before), newCurve(after) {}
    bool perform() override { return apply(newCurve); }
    bool undo() override { return apply(oldCurve); }
private:
    bool apply(const timeline::Curve& curve) {
        return processor.editLayer(static_cast<std::size_t>(index),[&](NoiseLayer& l){curveFor(l,kind)=curve;});
    }
    FxProcessor& processor;
    int index;
    CurveEditor::Kind kind;
    timeline::Curve oldCurve,newCurve;
};
}
CurveEditor::CurveEditor(FxProcessor& processor,int layer) : processor_(processor),layer_(layer) {
    setWantsKeyboardFocus(true);
    setTitle("Layer gain curve");
    setDescription("Double click to add nodes; drag to edit. Delete removes a node. L, S, E or H select segment shape.");
    shown_=curveFor(processor_.program().layers[static_cast<std::size_t>(layer_)],kind_);
    restoreEpoch_=processor_.programRestoreEpoch();
}
void CurveEditor::setKind(Kind kind) {
    if(kind_==kind)return;
    kind_=kind;dragging_=false;selected_=-1;undo_.clearUndoHistory();
    setTitle(kind_==Kind::gain?"Layer gain curve":"Layer pan curve");
    shown_=curveFor(processor_.program().layers[static_cast<std::size_t>(layer_)],kind_);
    repaint();
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
    const auto epoch=processor_.programRestoreEpoch();
    if(epoch!=restoreEpoch_) {
        undo_.clearUndoHistory();dragging_=false;selected_=-1;restoreEpoch_=epoch;
        shown_=curveFor(processor_.program().layers[static_cast<std::size_t>(layer_)],kind_);
        repaint();
    }
}
void CurveEditor::refresh(const timeline::Curve& curve) {
    syncRestore();
    if(!dragging_ && curve.valid()){shown_=curve;if(selected_>=shown_.count)selected_=-1;repaint();}
}
bool CurveEditor::publish(const timeline::Curve& curve) {
    return curve.valid() && processor_.editLayer(static_cast<std::size_t>(layer_),[&](NoiseLayer& l){curveFor(l,kind_)=curve;});
}
bool CurveEditor::setCurve(const timeline::Curve& curve) {
    syncRestore();
    if(!curve.valid())return false;
    const auto old=curveFor(processor_.program().layers[static_cast<std::size_t>(layer_)],kind_);
    undo_.beginNewTransaction();
    if(!undo_.perform(new CurveChange(processor_,layer_,kind_,old,curve)))return false;
    shown_=curve;repaint();return true;
}
bool CurveEditor::undo(){syncRestore();const bool ok=undo_.undo();refresh(curveFor(processor_.program().layers[static_cast<std::size_t>(layer_)],kind_));return ok;}
bool CurveEditor::redo(){syncRestore();const bool ok=undo_.redo();refresh(curveFor(processor_.program().layers[static_cast<std::size_t>(layer_)],kind_));return ok;}
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
    (void)publish(shown_);repaint();
}
void CurveEditor::mouseUp(const juce::MouseEvent&) {
    syncRestore();
    if(!dragging_) return;
    dragging_=false;
    const auto after=shown_;undo_.beginNewTransaction();
    (void)undo_.perform(new CurveChange(processor_,layer_,kind_,beforeDrag_,after));repaint();
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
    ++next.count;if(setCurve(next))selected_=at;
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
        --next.count;selected_=-1;return setCurve(next);
    }
    switch(juce::CharacterFunctions::toLowerCase(static_cast<juce::juce_wchar>(key.getTextCharacter()))){
        case 'l':next.nodes[static_cast<std::size_t>(selected_)].shape=timeline::Shape::linear;break;
        case 's':next.nodes[static_cast<std::size_t>(selected_)].shape=timeline::Shape::smooth;break;
        case 'e':next.nodes[static_cast<std::size_t>(selected_)].shape=timeline::Shape::exponential;break;
        case 'h':next.nodes[static_cast<std::size_t>(selected_)].shape=timeline::Shape::hold;break;
        default:return false;
    }
    return setCurve(next);
}
} // namespace vox::fx
