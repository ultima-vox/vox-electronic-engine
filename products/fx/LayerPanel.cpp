#include "LayerPanel.h"
#include "Processor.h"
#include "Tokens.h"
#include "Typography.h"

namespace vox::fx {
LayerPanel::LayerPanel(FxProcessor& processor, int index) : processor_(processor), index_(index), curve_(processor,index) {
    enabled_.setClickingTogglesState(true);
    enabled_.setTitle("Enable layer " + juce::String(index + 1));
    enabled_.onClick = [this] { processor_.editLayer(static_cast<std::size_t>(index_), [this](NoiseLayer& l){ l.enabled=enabled_.getToggleState(); }); };
    addAndMakeVisible(enabled_);
    const char* names[]{"Noise Riser", "Downlifter", "Shepard", "Impact", "Whoosh", "Laser", "Squelch", "Metallic", "Texture", "Formant", "Glitch", "Granular"};
    for(int i=0;i<12;++i) family_.addItem(names[i],i+1);
    family_.setTitle("Layer " + juce::String(index + 1) + " family");
    family_.onChange = [this] { const int id=family_.getSelectedId(); if(id>=1 && id<=12)
        processor_.editLayer(static_cast<std::size_t>(index_), [id](NoiseLayer& l){l.family=static_cast<NoiseFamily>(id-1);}); };
    addAndMakeVisible(family_);
    auto& g=gain_.getSlider();g.setRange(0,1,0.001);g.setTitle("Layer gain");
    g.onValueChange=[this]{ const float v=static_cast<float>(gain_.getSlider().getValue());
        processor_.editLayer(static_cast<std::size_t>(index_), [v](NoiseLayer& l){l.gain=v;});gain_.repaint(); };
    auto& p=pan_.getSlider();p.setRange(-1,1,0.001);p.setTitle("Layer pan");pan_.setStyle(vox::ui::VoxKnob::Style::Bipolar);
    p.onValueChange=[this]{const float v=static_cast<float>(pan_.getSlider().getValue());
        processor_.editLayer(static_cast<std::size_t>(index_), [v](NoiseLayer& l){l.pan=v;});pan_.repaint();};
    auto& o=offset_.getSlider();o.setRange(0,.999,0.001);o.setTitle("Layer start as fraction of event");
    o.onValueChange=[this]{const float v=static_cast<float>(offset_.getSlider().getValue());
        processor_.editLayer(static_cast<std::size_t>(index_), [v](NoiseLayer& l){l.timing.offset=v;l.timing.span=std::min(l.timing.span,1.0f-v);});offset_.repaint();};
    auto& s=span_.getSlider();s.setRange(.001,1,.001);s.setTitle("Layer length as fraction of event");
    s.onValueChange=[this]{const float v=static_cast<float>(span_.getSlider().getValue());
        processor_.editLayer(static_cast<std::size_t>(index_), [v](NoiseLayer& l){l.timing.span=v;l.timing.offset=std::min(l.timing.offset,1.0f-v);});span_.repaint();};
    for(auto* knob:{&gain_,&pan_,&offset_,&span_})addAndMakeVisible(*knob);
    curveType_.addItem("Gain",1);curveType_.addItem("Pan",2);
    curveType_.setTitle("Layer curve");curveType_.setTooltip("Choose the gain or pan envelope");
    curveType_.setSelectedId(1,juce::dontSendNotification);
    curveType_.onChange=[this]{curve_.setKind(curveType_.getSelectedId()==2?CurveEditor::Kind::pan:CurveEditor::Kind::gain);};
    addAndMakeVisible(curveType_);addAndMakeVisible(curve_);
    refresh(processor_.program().layers[static_cast<std::size_t>(index_)]);
}
void LayerPanel::refresh(const NoiseLayer& layer) {
    enabled_.setToggleState(layer.enabled,juce::dontSendNotification);
    family_.setSelectedId(static_cast<int>(layer.family)+1,juce::dontSendNotification);
    gain_.getSlider().setValue(layer.gain,juce::dontSendNotification);
    pan_.getSlider().setValue(layer.pan,juce::dontSendNotification);
    offset_.getSlider().setValue(layer.timing.offset,juce::dontSendNotification);
    span_.getSlider().setValue(layer.timing.span,juce::dontSendNotification);
    gain_.repaint();pan_.repaint();offset_.repaint();span_.repaint();
    curve_.refresh(curve_.kind()==CurveEditor::Kind::gain?layer.gainCurve:layer.panCurve);
}
void LayerPanel::paint(juce::Graphics& g) {
    namespace c=vox::ui::tokens::colour;
    auto bounds=getLocalBounds().toFloat().reduced(1.0f);
    g.setColour(c::panelRaised);g.fillRoundedRectangle(bounds,6.0f);
    g.setColour(c::border);g.drawRoundedRectangle(bounds,6.0f,1.0f);
    g.setColour(c::text);g.setFont(vox::ui::typography::controlLabel());
    g.drawText("LAYER " + juce::String::charToString(static_cast<juce::juce_wchar>('A'+index_)),getLocalBounds().reduced(10).removeFromTop(24),juce::Justification::centredLeft);
}
void LayerPanel::resized() {
    auto area=getLocalBounds().reduced(10);
    auto header=area.removeFromTop(25);enabled_.setBounds(header.removeFromRight(48));
    curveType_.setBounds(header.removeFromRight(75).reduced(1));
    area.removeFromTop(4);family_.setBounds(area.removeFromTop(30));area.removeFromTop(8);
    auto knobs=area.removeFromTop(85);const int width=knobs.getWidth()/4;
    gain_.setBounds(knobs.removeFromLeft(width));pan_.setBounds(knobs.removeFromLeft(width));
    offset_.setBounds(knobs.removeFromLeft(width));span_.setBounds(knobs);
    curve_.setBounds(area);
}
} // namespace vox::fx
