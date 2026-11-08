#pragma once

// FOUND-01 editor: the shared VOX look and feel, product title and an honest
// status line. It exposes no controls because the shell has no engine yet;
// controls arrive with real parameters (UI-02 and the product *-01/*-07 PRs).

#include <juce_audio_processors/juce_audio_processors.h>

#include "Tokens.h"
#include "Typography.h"
#include "VoxLookAndFeel.h"

namespace vox::suite {

class SuiteProcessor;

class SuiteEditor final : public juce::AudioProcessorEditor {
public:
    explicit SuiteEditor (SuiteProcessor& owner);
    ~SuiteEditor() override { setLookAndFeel (nullptr); }

    void paint (juce::Graphics& g) override
    {
        namespace c = vox::ui::tokens::colour;
        namespace s = vox::ui::tokens::spacing;
        auto area = getLocalBounds();
        g.fillAll (c::background);

        auto header = area.removeFromTop (72);
        g.setColour (c::panel);
        g.fillRect (header);
        g.setColour (accent_);
        g.fillRect (header.removeFromBottom (2));
        g.setColour (c::border);
        g.drawHorizontalLine (area.getY(), 0.0f, static_cast<float> (getWidth()));

        auto title = header.reduced (s::xxl, s::md);
        g.setColour (c::textSecondary);
        g.setFont (vox::ui::typography::sectionTitle());
        g.drawText ("ULTIMA VOX MUSIC", title.removeFromTop (18), juce::Justification::centredLeft);
        g.setColour (c::text);
        g.setFont (vox::ui::typography::instrumentTitle());
        g.drawText (productName_, title, juce::Justification::centredLeft);

        auto body = area.reduced (s::xxl);
        g.setFont (vox::ui::typography::controlLabel());
        g.setColour (c::textSecondary);
        g.drawText ("Foundation shell: this build has no sound engine and no controls yet.",
                    body.removeFromTop (24), juce::Justification::centredLeft);

        auto footer = getLocalBounds().removeFromBottom (28).reduced (s::xxl, 0);
        g.setColour (c::textMuted);
        g.setFont (vox::ui::typography::valueText());
        g.drawText (footerText_, footer, juce::Justification::centredLeft);
    }

    void resized() override;

private:
    SuiteProcessor& processor_;
    vox::ui::VoxLookAndFeel lookAndFeel_;
    juce::String productName_;
    juce::String footerText_;
    juce::Colour accent_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SuiteEditor)
};

} // namespace vox::suite
