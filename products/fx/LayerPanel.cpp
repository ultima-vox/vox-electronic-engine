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
    auto& o=offset_.getSlider();o.setRange(0,.999,0.001);o.setTitle("Layer start as fraction of event");
    o.onValueChange=[this]{const float v=static_cast<float>(offset_.getSlider().getValue());
        editKnob([v](NoiseLayer& l){l.timing.offset=v;l.timing.span=std::min(l.timing.span,1.0f-v);});offset_.repaint();};
    auto& s=span_.getSlider();s.setRange(.001,1,.001);s.setTitle("Layer length as fraction of event");
    s.onValueChange=[this]{const float v=static_cast<float>(span_.getSlider().getValue());
        editKnob([v](NoiseLayer& l){l.timing.span=v;l.timing.offset=std::min(l.timing.offset,1.0f-v);});span_.repaint();};
    for(auto* knob:{&gain_,&pan_,&offset_,&span_}){
        auto& slider=knob->getSlider();
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
    addAndMakeVisible(curveType_);addAndMakeVisible(curve_);
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
        offset_.getSlider().setValue(layer.timing.offset,juce::dontSendNotification);
        span_.getSlider().setValue(layer.timing.span,juce::dontSendNotification);
    }
    gain_.repaint();pan_.repaint();offset_.repaint();span_.repaint();
    curve_.refresh();
}
void LayerPanel::paint(juce::Graphics& g) {
    namespace c = vox::ui::tokens::colour;
    namespace r = vox::ui::tokens::radius;
    const auto bounds = getLocalBounds().toFloat();
    g.setColour(c::panel);
    g.fillRoundedRectangle(bounds, r::panel);
    g.setColour(c::border);g.drawRoundedRectangle(bounds.reduced(0.5f), r::panel, 1.0f);
    // Module header: layer title + IN PROGRAM/INACTIVE badge, controls right.
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
    auto header=area.removeFromTop(34);enabled_.setBounds(header.removeFromRight(48));
    solo_.setBounds(header.removeFromRight(48).reduced(1,0));
    editSource_.setBounds(header.removeFromRight(62).reduced(1,0));
    area.removeFromTop(4);family_.setBounds(area.removeFromTop(30));area.removeFromTop(8);
    auto knobs=area.removeFromTop(85);const int width=knobs.getWidth()/4;
    gain_.setBounds(knobs.removeFromLeft(width));pan_.setBounds(knobs.removeFromLeft(width));
    offset_.setBounds(knobs.removeFromLeft(width));span_.setBounds(knobs);
    curveType_.setBounds(area.removeFromTop(25).reduced(1));
    curve_.setBounds(area);
}
} // namespace vox::fx
