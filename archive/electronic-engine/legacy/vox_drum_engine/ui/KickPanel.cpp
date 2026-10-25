#include "KickPanel.h"

namespace vstengine::ui {

KickPanel::KickPanel (juce::AudioProcessorValueTreeState& state)
{
    pitch.addKnob (state, "kickPitchStart", "Pitch Start");
    pitch.addKnob (state, "kickPitchEnd", "Pitch End");
    pitch.addKnob (state, "kickPitchDecay", "Pitch Decay");
    pitch.addKnob (state, "kickPitchCurve", "Pitch Curve");
    pitch.addKnob (state, "kickTune", "Tune");
    pitch.addKnob (state, "kickPhase", "Phase");
    body.addKnob (state, "kickBodyDecay", "Body Decay");
    body.addKnob (state, "kickTail", "Tail");
    body.addKnob (state, "kickSub", "Sub");
    transient.addKnob (state, "kickClick", "Click");
    transient.addKnob (state, "kickClickTone", "Click Tone");
    transient.addKnob (state, "kickTransient", "Transient");
    output.addKnob (state, "kickDrive", "Drive");
    output.addKnob (state, "kickClip", "Clip");
    output.addKnob (state, "kickOutputLevel", "Output");
    for (auto* section : { &pitch, &body, &transient, &output })
        addAndMakeVisible (section);
}

void KickPanel::resized()
{
    auto area = getLocalBounds().reduced (8);
    const auto rowHeight = (area.getHeight() - 8) / 2;
    auto top = area.removeFromTop (rowHeight);
    area.removeFromTop (8);
    juce::Grid grid;
    grid.templateColumns = { juce::Grid::TrackInfo (juce::Grid::Fr (3)),
                             juce::Grid::TrackInfo (juce::Grid::Fr (2)) };
    grid.templateRows = { juce::Grid::TrackInfo (juce::Grid::Fr (1)) };
    grid.columnGap = juce::Grid::Px (8);
    grid.items = { juce::GridItem (pitch), juce::GridItem (body) };
    grid.performLayout (top);
    grid.items = { juce::GridItem (transient), juce::GridItem (output) };
    grid.templateColumns = { juce::Grid::TrackInfo (juce::Grid::Fr (1)),
                             juce::Grid::TrackInfo (juce::Grid::Fr (1)) };
    grid.performLayout (area);
}

} // namespace vstengine::ui
