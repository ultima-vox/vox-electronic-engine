#include "MatchPanel.h"

namespace vstengine::ui {
namespace {
void setupValueLabel (juce::Label& l) { styleLabel (l, 13.0f, juce::Justification::centredLeft); }
juce::String db (double linear) { return juce::String (juce::Decibels::gainToDecibels (linear), 1) + " dB"; }
}
MatchPanel::MatchPanel (juce::AudioProcessorValueTreeState& s, Callbacks cb)
    : state (s), callbacks (std::move (cb))
{
    actionTitle.setText ("ACTIONS", juce::dontSendNotification); measuredTitle.setText ("MEASURED", juce::dontSendNotification);
    recommendedTitle.setText ("RECOMMENDED", juce::dontSendNotification); currentTitle.setText ("CURRENT / APPLIED", juce::dontSendNotification);
    for (auto* l : { &actionTitle, &measuredTitle, &recommendedTitle, &currentTitle }) { styleLabel (*l, 13, juce::Justification::centredLeft, colours::primary); addAndMakeVisible (*l); }
    for (auto& l : measured) { setupValueLabel (l); addAndMakeVisible (l); }
    for (auto& l : recommended) { setupValueLabel (l); addAndMakeVisible (l); }
    for (auto& l : current) { setupValueLabel (l); addAndMakeVisible (l); }
    styleLabel (status, 12, juce::Justification::centredLeft, colours::mutedText); status.setText ("Analyze current Bass and Kick to create bounded recommendations.", juce::dontSendNotification); addAndMakeVisible (status);
    styleButton (analyzeButton, true); styleButton (applyButton); addAndMakeVisible (analyzeButton); addAndMakeVisible (applyButton);
    analyzeButton.onClick = [this] { analyze(); }; applyButton.onClick = [this] { apply(); }; applyButton.setEnabled (false); updateLabels();
}
void MatchPanel::analyze() { if (!callbacks.analyze) return; report=callbacks.analyze(); hasReport=true; applyButton.setEnabled(true); status.setText(report.overlapImproves?"Recommendation reduces measured overlap.":"Overlap already low; recommendation remains bounded.",juce::dontSendNotification); updateLabels(); }
void MatchPanel::apply() { if(!hasReport||!callbacks.apply)return; callbacks.apply(report.adjustments); status.setText("Recommendation applied to canonical parameters.",juce::dontSendNotification); updateLabels(); }
void MatchPanel::updateLabels()
{
    const auto unavailable=juce::String("—"); const auto value=[this,&unavailable](juce::String v){return hasReport?v:unavailable;};
    measured[0].setText("Kick fundamental   "+value(juce::String(report.kickDominantHz,1)+" Hz"),juce::dontSendNotification);
    measured[1].setText("Bass fundamental   "+value(juce::String(report.bassDominantHz,1)+" Hz"),juce::dontSendNotification);
    measured[2].setText("Kick tail   "+value(juce::String(report.kickTailSeconds*1000.0,1)+" ms"),juce::dontSendNotification);
    measured[3].setText("Bass onset   "+value(juce::String(report.bassOnsetSeconds*1000.0,1)+" ms"),juce::dontSendNotification);
    measured[4].setText("Overlap   "+value(juce::String(report.spectralOverlap*100.0,1)+" %"),juce::dontSendNotification);
    measured[5].setText("Phase correlation   "+value(juce::String(report.phaseCorrelation*100.0,1)+" %"),juce::dontSendNotification);
    measured[6].setText("Peak relationship   "+value(juce::String(report.peakRatio*100.0,1)+" %"),juce::dontSendNotification);
    recommended[0].setText("Bass Timing Offset   "+value(juce::String(report.adjustments.bassTimingOffsetMs)+" ms"),juce::dontSendNotification);
    recommended[1].setText("Kick Tail   "+value(juce::String(report.adjustments.kickTailMultiplier*100.0,0)+" %"),juce::dontSendNotification);
    recommended[2].setText("Kick Phase   "+value(juce::String(report.adjustments.kickPhaseDeg,0)+" deg"),juce::dontSendNotification);
    recommended[3].setText("Bass Level   "+value(juce::String(report.adjustments.bassLevelDb,1)+" dB"),juce::dontSendNotification);
    auto raw=[this](const char* id){return state.getRawParameterValue(id)->load();};
    current[0].setText("Bass Timing Offset   "+juce::String(raw("matchBassTimingOffsetMs"),0)+" ms",juce::dontSendNotification);
    current[1].setText("Kick Tail   "+juce::String(raw("kickTail")*1000.0f,1)+" ms",juce::dontSendNotification);
    current[2].setText("Kick Phase   "+juce::String(raw("kickPhase"),0)+" deg",juce::dontSendNotification);
    current[3].setText("Bass Output   "+db(raw("outputLevel")),juce::dontSendNotification);
}
void MatchPanel::paint(juce::Graphics& g){g.fillAll(colours::background);g.setColour(colours::panel);for(auto r:{juce::Rectangle<int>(8,8,getWidth()-16,82),juce::Rectangle<int>(8,100,(getWidth()-26)/2,getHeight()-148),juce::Rectangle<int>(18+(getWidth()-26)/2,100,(getWidth()-26)/2,getHeight()-148)})g.fillRoundedRectangle(r.toFloat(),8);}
void MatchPanel::resized(){auto a=getLocalBounds().reduced(20);actionTitle.setBounds(a.removeFromTop(20));auto actions=a.removeFromTop(42);analyzeButton.setBounds(actions.removeFromLeft(150).reduced(2));applyButton.setBounds(actions.removeFromLeft(120).reduced(2));a.removeFromTop(18);auto left=a.removeFromLeft((a.getWidth()-16)/2);a.removeFromLeft(16);measuredTitle.setBounds(left.removeFromTop(26));for(auto&l:measured)l.setBounds(left.removeFromTop(34));recommendedTitle.setBounds(a.removeFromTop(26));for(auto&l:recommended)l.setBounds(a.removeFromTop(36));a.removeFromTop(12);currentTitle.setBounds(a.removeFromTop(26));for(auto&l:current)l.setBounds(a.removeFromTop(36));status.setBounds(20,getHeight()-38,getWidth()-40,24);}
} // namespace vstengine::ui
