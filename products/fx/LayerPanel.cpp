#include "LayerPanel.h"
#include "Processor.h"
#include "Tokens.h"
#include "Typography.h"

namespace vox::fx {
LayerPanel::LayerPanel(FxProcessor& processor, int index) : processor_(processor), index_(index), curve_(processor,static_cast<std::size_t>(index)) {
    setInterceptsMouseClicks(false, true);
    enabled_.setClickingTogglesState(true);
    enabled_.setTitle("Enable layer " + juce::String(index + 1));
    enabled_.onClick = [this] { processor_.editLayer(static_cast<std::size_t>(index_), [this](NoiseLayer& l){ l.enabled=enabled_.getToggleState(); }); };
    addAndMakeVisible(enabled_);
    solo_.setClickingTogglesState(true);
    solo_.setTitle("Monitor solo layer " + juce::String(index+1));
    solo_.setTooltip("Monitor selected layers together. Does not enable muted layers, change the Program or affect offline export.");
    solo_.onClick=[this]{processor_.toggleLayerSolo(static_cast<std::size_t>(index_));};
    addAndMakeVisible(solo_);
    editSource_.setTitle("Edit source of layer " + juce::String(index + 1));
    editSource_.setTooltip("Open the source editor for this layer: scale, pitch, filters and family controls");
    editSource_.onClick=[this]{ if(onEditSource) onEditSource(); };
    addAndMakeVisible(editSource_);
    // Contextual per-card curve editor (overlay): full interactive editing
    // without leaving the layer card. Closed by default; the reference card
    // composition shows the dial grid + 2x2 thumbnails.
    curveEdit_.setClickingTogglesState(true);
    curveEdit_.setTitle("Toggle curve editor for layer " + juce::String(index + 1));
    curveEdit_.setTooltip("Open this layer's full trajectory editor here. Thumbnails open the same curves in the timeline.");
    curveEdit_.onClick=[this]{
        curveOverlayOpen_=curveEdit_.getToggleState();
        resized();
    };
    addAndMakeVisible(curveEdit_);
    const char* names[]{"Noise Riser", "Downlifter", "Shepard", "Impact", "Whoosh", "Laser", "Squelch", "Metallic", "Texture", "Formant", "Glitch", "Granular"};
    for(int i=0;i<12;++i) family_.addItem(names[i],i+1);
    family_.setTitle("Layer " + juce::String(index + 1) + " family");
    family_.onChange = [this] { const int id=family_.getSelectedId(); if(id>=1 && id<=12)
        processor_.editLayer(static_cast<std::size_t>(index_), [id](NoiseLayer& l){l.family=static_cast<NoiseFamily>(id-1);}); };
    addAndMakeVisible(family_);
    auto& g=gain_.getSlider();g.setRange(0,1,0.001);g.setTitle("Layer gain");
    g.onValueChange=[this]{ const float v=static_cast<float>(gain_.getSlider().getValue());
        editKnob([v](NoiseLayer& l){l.gain=v;});gain_.repaint(); };
    auto& p=pan_.getSlider();p.setRange(-1,1,0.001);p.setTitle("Layer pan");pan_.setStyle(vox::ui::VoxKnob::Style::Bipolar);
    p.onValueChange=[this]{const float v=static_cast<float>(pan_.getSlider().getValue());
        editKnob([v](NoiseLayer& l){l.pan=v;});pan_.repaint();};
    auto& tr=transpose_.getSlider();tr.setRange(-24,24,1);tr.setTitle("Layer transpose in semitones");
    transpose_.setStyle(vox::ui::VoxKnob::Style::Bipolar);
    tr.onValueChange=[this]{const float v=static_cast<float>(transpose_.getSlider().getValue());
        editKnob([v](NoiseLayer& l){l.transpose=v;});transpose_.repaint();};
    auto& o=offset_.getSlider();o.setRange(0,.999,0.001);o.setTitle("Layer start as fraction of event");
    o.onValueChange=[this]{const float v=static_cast<float>(offset_.getSlider().getValue());
        editKnob([v](NoiseLayer& l){l.timing.offset=v;l.timing.span=std::min(l.timing.span,1.0f-v);});offset_.repaint();};
    auto& s=span_.getSlider();s.setRange(.001,1,.001);s.setTitle("Layer length as fraction of event");
    s.onValueChange=[this]{const float v=static_cast<float>(span_.getSlider().getValue());
        editKnob([v](NoiseLayer& l){l.timing.span=v;l.timing.offset=std::min(l.timing.offset,1.0f-v);});span_.repaint();};
    auto& ch=character_.getSlider();ch.setRange(-100,100,.1);ch.setTitle("Layer source character (percent, legacy is zero)");
    character_.setStyle(vox::ui::VoxKnob::Style::Bipolar);
    ch.onValueChange=[this]{const float v=static_cast<float>(character_.getSlider().getValue())*.01f;
        editKnob([v](NoiseLayer& l){l.character=v;});character_.repaint();};
    // Reference .fx-layer-dials: 3-column grid, 43px dials, 9px labels.
    for(auto* knob:{&gain_,&pan_,&transpose_,&offset_,&span_,&character_}){
        auto& slider=knob->getSlider();
        knob->setCustomDiameter(43);
        knob->setLabelSmall(true);
        slider.onDragStart=[this]{dragRevision_=processor_.versionedProgram().revision;};
        slider.onDragEnd=[this]{
            dragRevision_.reset();
            refresh(processor_.program().layers[static_cast<std::size_t>(index_)]);
        };
        addAndMakeVisible(*knob);
    }
    curveType_.addItem("Amplitude",1);curveType_.addItem("Pitch",2);curveType_.addItem("Cutoff",3);curveType_.addItem("Panorama",4);
    curveType_.setTitle("Layer trajectory");curveType_.setTooltip("Edit layer amplitude, pitch (plus or minus 24 semitones), logarithmic cutoff or panorama trajectory");
    curveType_.setSelectedId(1,juce::dontSendNotification);
    curveType_.onChange=[this]{curve_.setKind(static_cast<CurveEditor::Kind>(curveType_.getSelectedId()-1));};
    addChildComponent(curveType_);addChildComponent(curve_);
    // 2x2 curve directory (reference .fx-layer-curves): label + live curve
    // preview; click opens the TIMELINE workspace on that exact trajectory.
    for(int i=0;i<4;++i){
        auto thumb=std::make_unique<CurveThumb>(*this,i);
        static const char* kinds[]{"amplitude","pitch","cutoff","panorama"};
        thumb->setTitle("Edit layer "+juce::String::charToString(static_cast<juce::juce_wchar>('A'+index_))+
            juce::String(" ")+kinds[i]+juce::String(" curve"));
        thumb->setDescription(thumb->getTitle());
        thumb->setTooltip("Open this trajectory in the Event Timeline");
        addAndMakeVisible(*thumb);
        curveThumbs_[static_cast<std::size_t>(i)]=std::move(thumb);
    }
    refresh(processor_.program().layers[static_cast<std::size_t>(index_)]);
}
template<typename Edit> void LayerPanel::editKnob(Edit edit) {
    const auto index=static_cast<std::size_t>(index_);
    if(dragRevision_)(void)processor_.editLayerIfRevision(index,*dragRevision_,edit);
    else (void)processor_.editLayer(index,edit);
}
void LayerPanel::refresh(const NoiseLayer& layer) {
    enabled_.setToggleState(layer.enabled,juce::dontSendNotification);
    solo_.setToggleState((processor_.layerSoloMask()&(1u<<index_))!=0,juce::dontSendNotification);
    family_.setSelectedId(static_cast<int>(layer.family)+1,juce::dontSendNotification);
    // Keep the drag's original revision and thumb until it ends, including
    // across timer refreshes after a restore. Re-read the restored values on release.
    if(!dragRevision_){
        gain_.getSlider().setValue(layer.gain,juce::dontSendNotification);
        pan_.getSlider().setValue(layer.pan,juce::dontSendNotification);
        transpose_.getSlider().setValue(layer.transpose,juce::dontSendNotification);
        offset_.getSlider().setValue(layer.timing.offset,juce::dontSendNotification);
        span_.getSlider().setValue(layer.timing.span,juce::dontSendNotification);
        character_.getSlider().setValue(layer.character*100.0f,juce::dontSendNotification);
    }
    gain_.repaint();pan_.repaint();transpose_.repaint();offset_.repaint();span_.repaint();character_.repaint();
    if(curveOverlayOpen_) curve_.refresh();
    for(auto& thumb:curveThumbs_) thumb->repaint();
}
void LayerPanel::paint(juce::Graphics& g) {
    namespace c = vox::ui::tokens::colour;
    namespace r = vox::ui::tokens::radius;
    const auto bounds = getLocalBounds().toFloat();
    g.setColour(c::panel);
    g.fillRoundedRectangle(bounds, r::panel);
    g.setColour(c::border);g.drawRoundedRectangle(bounds.reduced(0.5f), r::panel, 1.0f);
    // Module header: layer title + IN PROGRAM/INACTIVE badge.
    auto header = getLocalBounds().removeFromTop(30).reduced(10, 0);
    g.setColour(c::text);g.setFont(vox::ui::typography::moduleTitle());
    g.drawText("LAYER " + juce::String::charToString(static_cast<juce::juce_wchar>('A'+index_)),header.removeFromLeft(86),juce::Justification::centredLeft);
    const bool active=enabled_.getToggleState();
    const auto badge=header.removeFromLeft(84).removeFromTop(18).translated(0,4);
    g.setColour(c::control);g.fillRoundedRectangle(badge.toFloat(),5.0f);
    g.setColour(active?c::fx::accent.withAlpha(0.55f):c::border);g.drawRoundedRectangle(badge.toFloat().reduced(0.5f),5.0f,1.0f);
    g.setColour(active?c::fx::accent:c::textSecondary);g.setFont(vox::ui::typography::badge());
    g.drawText(active?"IN PROGRAM":"INACTIVE",badge,juce::Justification::centred);
}
void LayerPanel::resized() {
    auto area=getLocalBounds().reduced(10);
    area.removeFromTop(30); // painted header (title + badge)
    family_.setBounds(area.removeFromTop(30));area.removeFromTop(6);
    // Inline enable/solo + contextual entries (reference Switch + Solo row;
    // SOURCE/CURVES are native contextual advanced-editing entries).
    auto controls=area.removeFromTop(26);
    enabled_.setBounds(controls.removeFromLeft(52).reduced(1));
    controls.removeFromLeft(6);
    solo_.setBounds(controls.removeFromLeft(56).reduced(1));
    controls.removeFromLeft(6);
    editSource_.setBounds(controls.removeFromLeft(70).reduced(1));
    controls.removeFromLeft(6);
    curveEdit_.setBounds(controls.removeFromLeft(72).reduced(1));
    area.removeFromTop(6);
    if(curveOverlayOpen_){
        // Contextual overlay: full curve editor covers the card body.
        curveType_.setVisible(true);
        curve_.setVisible(true);
        curveType_.setBounds(area.removeFromTop(26).reduced(1));
        area.removeFromTop(4);
        curve_.setBounds(area);
        for(auto& thumb:curveThumbs_) thumb->setVisible(false);
        return;
    }
    for(auto& thumb:curveThumbs_) thumb->setVisible(true);
    curveType_.setVisible(false);
    curve_.setVisible(false);
    // 3-column dial grid (reference .fx-layer-dials) of real layer dials:
    // gain, pan, transpose / start, length, character.
    const int thumbBlock=58*2+5;
    auto dials=area.removeFromTop(std::max(140,area.getHeight()-thumbBlock-10));
    area.removeFromTop(10);
    auto thumbs=area;
    const int column=dials.getWidth()/3;
    const int row=dials.getHeight()/2;
    vox::ui::VoxKnob* order[]{&gain_,&pan_,&transpose_,&offset_,&span_,&character_};
    for(int i=0;i<6;++i){
        const auto cell=juce::Rectangle<int>(dials.getX()+(i%3)*column,dials.getY()+(i/3)*row,column,row);
        order[i]->setBounds(cell);
    }
    const int thumbWidth=thumbs.getWidth()/2;
    const int thumbHeight=thumbs.getHeight()/2;
    for(int i=0;i<4;++i){
        const auto cell=juce::Rectangle<int>(thumbs.getX()+(i%2)*thumbWidth,thumbs.getY()+(i/2)*thumbHeight,
            i%2==0?thumbWidth-5:thumbWidth,i/2==0?thumbHeight-5:thumbHeight);
        curveThumbs_[static_cast<std::size_t>(i)]->setBounds(cell);
    }
}
void LayerPanel::CurveThumb::paint(juce::Graphics& g) {
    namespace c = vox::ui::tokens::colour;
    namespace ty = vox::ui::typography;
    auto bounds=getLocalBounds().toFloat();
    const bool hover=isMouseOver()||isMouseButtonDown();
    g.setColour(vox::ui::tokens::colour::wellGroup); // reference .fx-layer-curves button well
    g.fillRoundedRectangle(bounds,5.0f);
    g.setColour(hover?c::fx::accent.withAlpha(0.42f):c::borderSubtle);
    g.drawRoundedRectangle(bounds.reduced(0.5f),5.0f,1.0f);
    auto label=bounds.removeFromTop(13.0f).reduced(4,0);
    g.setColour(hover?c::fx::accent:c::textSecondary);
    g.setFont(ty::badge());
    static const char* names[]{"AMP","PITCH","CUTOFF","PAN"};
    g.drawText(names[kind],label,juce::Justification::centredLeft);
    const auto& layer=owner.processor_.program().layers[static_cast<std::size_t>(owner.index_)];
    const timeline::Curve& curve=kind==0?layer.gainCurve:kind==1?layer.pitchCurve
        :kind==2?layer.cutoffCurve:layer.panCurve;
    auto graph=bounds.reduced(4,3);
    juce::Path path;
    constexpr int steps=32;
    for(int i=0;i<=steps;++i){
        const float phase=i/static_cast<float>(steps);
        const float value=juce::jlimit(0.0f,1.0f,curve.evaluate(phase));
        const float x=graph.getX()+phase*graph.getWidth();
        const float y=graph.getBottom()-value*graph.getHeight();
        if(i==0)path.startNewSubPath(x,y);else path.lineTo(x,y);
    }
    g.setColour(c::themeAccent);
    g.strokePath(path,juce::PathStrokeType(1.4f));
}
void LayerPanel::CurveThumb::mouseUp(const juce::MouseEvent&) {
    if(owner.onOpenTimeline) owner.onOpenTimeline(kind);
}
} // namespace vox::fx
