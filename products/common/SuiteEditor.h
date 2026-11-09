#pragma once

// Shared editor frame: the VOX look and feel, product header, a status
// footer and the product's content component. A product without content
// shows an honest "no engine" line instead (FOUND-01 shell).

#include <juce_audio_processors/juce_audio_processors.h>

#include "Tokens.h"
#include "Typography.h"
#include "VoxLookAndFeel.h"

#include <memory>

namespace vox::suite {

class SuiteProcessor;

class SuiteEditor final : public juce::AudioProcessorEditor {
public:
    explicit SuiteEditor (SuiteProcessor& owner);
    ~SuiteEditor() override
    {
        content_.reset();
        setLookAndFeel (nullptr);
    }

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

        if (content_ == nullptr)
        {
            auto body = area.reduced (s::xxl);
            g.setFont (vox::ui::typography::controlLabel());
            g.setColour (c::textSecondary);
            g.drawText ("Foundation shell: this build has no sound engine and no controls yet.",
                        body.removeFromTop (24), juce::Justification::centredLeft);
        }

        auto footer = getLocalBounds().removeFromBottom (28).reduced (s::xxl, 0);
        g.setColour (c::textMuted);
        g.setFont (vox::ui::typography::valueText());
        g.drawText (footerText_, footer, juce::Justification::centredLeft);
    }

    void resized() override;

    // The product's controls, laid out between header and footer.
    void setContent (std::unique_ptr<juce::Component> content)
    {
        if (content_ != nullptr)
            removeChildComponent (content_.get());
        content_ = std::move (content);
        if (content_ != nullptr)
            addAndMakeVisible (*content_);
        resized();
        repaint();
    }

    juce::Component* content() const noexcept { return content_.get(); }

    static constexpr int headerHeight = 72, footerHeight = 28;

private:
    SuiteProcessor& processor_;
    vox::ui::VoxLookAndFeel lookAndFeel_;
    juce::String productName_;
    juce::String footerText_;
    juce::Colour accent_;
    std::unique_ptr<juce::Component> content_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SuiteEditor)
};

} // namespace vox::suite
