#include "FxEditor.h"
#include "Processor.h"
#include "Tokens.h"
#include "Typography.h"

namespace vox::fx {
FxPanel::FxPanel (FxProcessor& processor)
    : processor_ (processor), attachment_ (processor.outputParameter(), output_.getSlider()),
      durationAttachment_ (processor.durationParameter(), duration_.getSlider())
{
    addAndMakeVisible (output_);
    addAndMakeVisible (duration_);
    duration_.setDoubleClickResetValue (4.0);
    duration_.getSlider().setTitle ("Event duration in seconds");
    duration_.getSlider().setDescription ("Duration applied to newly triggered events");
    output_.setDoubleClickResetValue (1.0);
    output_.getSlider().setTitle ("Output gain");
    output_.getSlider().setDescription ("Master output gain");
    addAndMakeVisible (audition_);
    addAndMakeVisible (panic_);
    audition_.setTooltip ("Trigger the noise riser");
    panic_.setTooltip ("Fade all active events");
    audition_.onClick = [this] { processor_.audition(); };
    panic_.onClick = [this] { processor_.panic(); };
    startTimerHz (25);
}
void FxPanel::paint (juce::Graphics& g)
{
    namespace c = vox::ui::tokens::colour;
    auto area = getLocalBounds().reduced (32);
    g.setColour (c::text);
    g.setFont (vox::ui::typography::sectionTitle());
    g.drawText ("PERFORM  /  NOISE RISER", area.removeFromTop (30), juce::Justification::centredLeft);
    g.setColour (c::textSecondary);
    g.setFont (vox::ui::typography::controlLabel());
    g.drawText ("One-shot noise riser. Trigger with MIDI or audition.", area.removeFromTop (24), juce::Justification::centredLeft);
    auto meter = getLocalBounds().reduced (32).removeFromBottom (55).removeFromLeft (320).toFloat();
    g.setColour (c::panelRaised); g.fillRoundedRectangle (meter, 4.0f);
    g.setColour (c::accent); g.fillRoundedRectangle (meter.withWidth (meter.getWidth() * shownPeak_), 4.0f);
    g.setColour (c::text); g.setFont (vox::ui::typography::controlLabel());
    g.drawText ("OUTPUT PEAK", meter.toNearestInt().reduced (8), juce::Justification::centredLeft);
}
void FxPanel::resized()
{
    auto area = getLocalBounds().reduced (32);
    area.removeFromTop (80);
    output_.setBounds (area.removeFromLeft (128).removeFromTop (140));
    duration_.setBounds (area.removeFromLeft (128).removeFromTop (140));
    area.removeFromLeft (20);
    auto buttons = area.removeFromLeft (160);
    audition_.setBounds (buttons.removeFromTop (40)); buttons.removeFromTop (10);
    panic_.setBounds (buttons.removeFromTop (40));
}
void FxPanel::timerCallback()
{
    if (!isShowing()) return;
    // Block peak comes from rendered samples; visual decay is message-thread only.
    shownPeak_ = std::max (processor_.outputPeak(), shownPeak_ * 0.88f);
    repaint();
}
} // namespace vox::fx
