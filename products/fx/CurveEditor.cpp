#include "CurveEditor.h"
#include "Processor.h"
#include "Tokens.h"
#include "Typography.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>

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
    setDescription("Double click to add nodes. Shift click to select multiple; drag or arrow keys move them. Enter edits one node numerically. Command-C/V copy or paste the entire curve. L, S, E or H set segment shape.");
    refresh();
}
void CurveEditor::updateTitle() {
    const char* name = kind()==Kind::gain ? "amplitude" : kind()==Kind::pitch ? "pitch" :
                       kind()==Kind::cutoff ? "cutoff" : "panorama";
    setTitle(juce::String(target_.layer ? "Layer " : "Program ") + name + " curve");
}
void CurveEditor::setKind(Kind kind) {
    if (target_.kind == kind || static_cast<unsigned>(kind) > static_cast<unsigned>(Kind::pan)) return;
    target_.kind=kind;dragging_=false;selected_=-1;selection_=0;undo_.clearUndoHistory();
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
    if(playhead_){
        const float x=r.getX()+*playhead_*r.getWidth();
        g.setColour(c::text.withAlpha(.65f));g.drawVerticalLine(juce::roundToInt(x),r.getY(),r.getBottom());
    }
    juce::Path path;
    constexpr int samples=80;
    for(int i=0;i<=samples;++i){const float t=static_cast<float>(i)/samples;
        const float x=r.getX()+t*r.getWidth(), y=r.getBottom()-shown_.evaluate(t)*r.getHeight();
        if(i==0)path.startNewSubPath(x,y);else path.lineTo(x,y);}
    g.setColour(c::accent);g.strokePath(path,juce::PathStrokeType(1.5f));
    for(int i=0;i<shown_.count;++i){const auto point=pointFor(shown_.nodes[static_cast<std::size_t>(i)]);
        g.setColour(selection_&curveedit::bit(static_cast<unsigned>(i))?c::text:c::accent);g.fillEllipse(point.x-3.0f,point.y-3.0f,6.0f,6.0f);}
    if(hasKeyboardFocus(true)){g.setColour(c::text);g.drawRoundedRectangle(r,3.0f,1.5f);}
}
void CurveEditor::syncRestore() {
    const auto snapshot=processor_.versionedProgram();
    if(snapshot.revision!=restoreEpoch_) {
        undo_.clearUndoHistory();dragging_=false;selected_=-1;selection_=0;restoreEpoch_=snapshot.revision;playhead_.reset();
        if (const auto* curve = target_.get(snapshot.program)) shown_ = *curve;
        repaint();
    }
}
void CurveEditor::refresh() {
    // Take curve and revision together; an earlier panel snapshot may already
    // have been superseded by a host restore on another thread.
    const auto snapshot=processor_.versionedProgram();
    if(snapshot.revision!=restoreEpoch_) {
        undo_.clearUndoHistory();dragging_=false;selected_=-1;selection_=0;restoreEpoch_=snapshot.revision;playhead_.reset();
    }
    playhead_=dragging_?std::nullopt:processor_.curvePlayhead(snapshot.publication,target_.layer);
    if(!dragging_){if (const auto* curve = target_.get(snapshot.program)) shown_ = *curve;
        if(selected_>=shown_.count){selected_=-1;selection_=0;}
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
    shown_=curve;playhead_.reset();repaint();return true;
}
bool CurveEditor::undo(){syncRestore();const bool ok=undo_.undo();refresh();return ok;}
bool CurveEditor::redo(){syncRestore();const bool ok=undo_.redo();refresh();return ok;}
void CurveEditor::mouseDown(const juce::MouseEvent& event) {
    syncRestore();
    grabKeyboardFocus();
    const int touched=hit(event.position);
    if(touched<0){selected_=-1;selection_=0;dragging_=false;repaint();return;}
    const auto bit=curveedit::bit(static_cast<unsigned>(touched));
    if(event.mods.isShiftDown()) {
        selection_^=bit;
        selected_=selection_&bit?touched:-1;
        dragging_=false;
    } else {
        if(!(selection_&bit))selection_=bit;
        selected_=touched;dragging_=true;beforeDrag_=shown_;playhead_.reset();
    }
    repaint();
}
void CurveEditor::mouseDrag(const juce::MouseEvent& event) {
    syncRestore();
    if(!dragging_ || selected_<0)return;
    auto r=graph();if(r.getWidth()<=0 || r.getHeight()<=0)return;
    const auto& anchor=beforeDrag_.nodes[static_cast<std::size_t>(selected_)];
    float time=std::clamp((event.position.x-r.getX())/r.getWidth(),0.0f,1.0f);
    if(!event.mods.isAltDown())time=std::round(time*16.0f)/16.0f;
    const float value=std::clamp((r.getBottom()-event.position.y)/r.getHeight(),0.0f,1.0f);
    if(!curveedit::move(beforeDrag_,selection_,time-anchor.time,value-anchor.value,shown_))return;
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
    ++next.count;if(applyEdit(next)){selected_=at;selection_=curveedit::bit(static_cast<unsigned>(at));}
}
bool CurveEditor::copyCurve() const {
    std::ostringstream text;
    text.imbue(std::locale::classic());
    text << "VOX-FX-CURVE-1 " << static_cast<unsigned>(shown_.count) << std::setprecision(9);
    for(unsigned i=0;i<shown_.count;++i){const auto& n=shown_.nodes[i];text << '\n' << n.time << ' ' << n.value << ' ' << static_cast<unsigned>(n.shape);}
    juce::SystemClipboard::copyTextToClipboard(juce::String(text.str()));
    return true;
}
bool CurveEditor::pasteCurve() {
    const auto clipboard=juce::SystemClipboard::getTextFromClipboard();
    if(clipboard.length()>1024)return false;
    std::istringstream input(clipboard.toStdString());input.imbue(std::locale::classic());
    std::string magic;unsigned count=0;
    if(!(input>>magic>>count)||magic!="VOX-FX-CURVE-1"||count<2||count>16)return false;
    timeline::Curve next;next.count=static_cast<std::uint8_t>(count);
    for(unsigned i=0;i<count;++i){auto& n=next.nodes[i];unsigned shape=0;
        if(!(input>>n.time>>n.value>>shape)||shape>3)return false;
        n.shape=static_cast<timeline::Shape>(shape);
    }
    if(!(input>>std::ws).eof()||!applyEdit(next))return false;
    selection_=0;selected_=-1;repaint();return true;
}
void CurveEditor::enterNodeValues() {
    const int index=selected_;
    if(index<0 || selection_!=curveedit::bit(static_cast<unsigned>(index)))return;
    const auto original=shown_;const auto revision=restoreEpoch_;const auto target=target_;
    const auto& node=original.nodes[static_cast<std::size_t>(index)];
    const bool interior=index>0 && index+1<original.count;
    if(!juce::MessageManager::getInstance()->isThisTheMessageThread())return;
    auto* dialog=new juce::AlertWindow("Edit trajectory node","Values are percentages, from 0 to 100.",juce::MessageBoxIconType::NoIcon,this);
    const auto percentage=[](float value){return juce::String(100.0*value,6);};
    if(interior)dialog->addTextEditor("time",percentage(node.time),"Time (%)");
    dialog->addTextEditor("value",percentage(node.value),"Value (%)");
    dialog->addButton("Apply",1,juce::KeyPress(juce::KeyPress::returnKey));
    dialog->addButton("Cancel",0,juce::KeyPress(juce::KeyPress::escapeKey));
    dialog->enterModalState(true,juce::ModalCallbackFunction::create(
        [safe=juce::Component::SafePointer<CurveEditor>(this), window=juce::Component::SafePointer<juce::AlertWindow>(dialog),original,revision,target,index,interior](int result){
            if(result!=1||!safe||!window||safe->restoreEpoch_!=revision||safe->target_.kind!=target.kind)return;
            const auto parse=[](const juce::String& text,float& value){
                std::istringstream in(text.toStdString());in.imbue(std::locale::classic());float percent=0;
                if(!(in>>percent)||!(in>>std::ws).eof()||!std::isfinite(percent)||percent<0||percent>100)return false;
                value=percent/100.0f;return true;
            };
            auto next=original;auto& n=next.nodes[static_cast<std::size_t>(index)];
            if(!parse(window->getTextEditorContents("value"),n.value) ||
               (interior&&!parse(window->getTextEditorContents("time"),n.time)) || !next.valid()) {
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,"Invalid node", "Use 0–100%; time must stay between neighbouring nodes.");return;
            }
            (void)safe->applyEdit(next);
        }),true);
}
bool CurveEditor::keyPressed(const juce::KeyPress& key) {
    syncRestore();
    const int code=key.getKeyCode();
    if(key.getModifiers().isCommandDown()) {
        if(code=='z'||code=='Z')return key.getModifiers().isShiftDown()?redo():undo();
        if(code=='y'||code=='Y')return redo();
        if(code=='c'||code=='C')return copyCurve();
        if(code=='v'||code=='V')return pasteCurve();
        if(code=='a'||code=='A'){selection_=(curveedit::Selection{1}<<shown_.count)-1;repaint();return true;}
    }
    if(code==juce::KeyPress::tabKey && key.getModifiers().isAltDown()){
        selected_=(selected_+1)%shown_.count;selection_=curveedit::bit(static_cast<unsigned>(selected_));repaint();return true;
    }
    if(!selection_)return false;
    if(code==juce::KeyPress::returnKey){enterNodeValues();return true;}
    auto next=shown_;
    if(code==juce::KeyPress::deleteKey || code==juce::KeyPress::backspaceKey){
        if(!curveedit::erase(shown_,selection_,next) || !applyEdit(next))return false;
        selection_=0;selected_=-1;repaint();return true;
    }
    float dt=0,dv=0;
    if(code==juce::KeyPress::leftKey)dt=-1.0f/16;
    if(code==juce::KeyPress::rightKey)dt=1.0f/16;
    if(code==juce::KeyPress::upKey)dv=.01f;
    if(code==juce::KeyPress::downKey)dv=-.01f;
    if(dt!=0||dv!=0)return curveedit::move(shown_,selection_,dt,dv,next)&&applyEdit(next);
    timeline::Shape shape;
    switch(juce::CharacterFunctions::toLowerCase(static_cast<juce::juce_wchar>(key.getTextCharacter()))){
        case 'l':shape=timeline::Shape::linear;break;
        case 's':shape=timeline::Shape::smooth;break;
        case 'e':shape=timeline::Shape::exponential;break;
        case 'h':shape=timeline::Shape::hold;break;
        default:return false;
    }
    for(unsigned i=0;i+1<next.count;++i)if(selection_&curveedit::bit(i))next.nodes[i].shape=shape;
    return applyEdit(next);
}
} // namespace vox::fx
