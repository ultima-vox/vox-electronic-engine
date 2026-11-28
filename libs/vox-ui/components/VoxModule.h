#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include "../Tokens.h"
#include "../Typography.h"

namespace vox::ui {

// Reference module ("ds-panel") from the Gate-1 accepted design language
// (system.css @ PR #160 e420d2c0): gradient panel with a 42 px header row
// (icon slot, display title, spacer, status badge chip). Children are laid
// out by the parent inside bodyArea() — the module only draws the chrome.
class VoxModule : public juce::Component
{
public:
    enum class Tone { neutral, accent, warning };

    VoxModule (juce::String title, juce::String badge = {}, Tone tone = Tone::neutral)
        : title_ (std::move (title)), badge_ (std::move (badge)), tone_ (tone)
    {
        setInterceptsMouseClicks (false, true);
    }

    // Grouped dial wells (fx.css .well) painted by the module backdrop; the
    // editor declares them at layout time in module-local coordinates.
    void clearWells() { wells_.clear(); }
    void addWell (juce::Rectangle<int> localBounds) { wells_.push_back (localBounds); }

    void setBadge (juce::String text, Tone tone)
    {
        badge_ = std::move (text);
        tone_ = tone;
        repaint();
    }
    void setModuleTitle (const juce::String& newTitle)
    {
        title_ = newTitle;
        repaint();
    }

    // Client area below the header, inset like .panel-body.
    juce::Rectangle<int> bodyArea() const noexcept
    {
        return getLocalBounds()
            .withTrimmedTop (tokens::size::moduleHeaderHeight)
            .reduced (11, 9);
    }
    juce::Rectangle<int> headerArea() const noexcept
    {
        return getLocalBounds().removeFromTop (tokens::size::moduleHeaderHeight)
            .reduced (11, 0);
    }

    void paint (juce::Graphics& g) override
    {
        namespace c = tokens::colour;
        namespace t = tokens;
        namespace ty = typography;
        const auto bounds = getLocalBounds().toFloat();
        // Panel gradient: 160deg #1b1e26 -> #181b22 45% (reference ds-panel).
        g.setColour (juce::Colour (0xff1b1e26));
        g.fillRoundedRectangle (bounds, t::radius::panel);
        g.setColour (c::panel);
        g.fillRoundedRectangle (bounds.withTop (bounds.getY() + bounds.getHeight() * 0.45f),
                                t::radius::panel);
        g.setColour (c::border);
        g.drawRoundedRectangle (bounds.reduced (0.5f), t::radius::panel, 1.0f);

        // Grouped wells: inset #151820 fields with soft borders (fx.css).
        for (const auto& well : wells_)
        {
            g.setColour (c::wellGroup);
            g.fillRoundedRectangle (well.toFloat(), 6.0f);
            g.setColour (c::borderSubtle);
            g.drawRoundedRectangle (well.toFloat().reduced (0.5f), 6.0f, 1.0f);
        }

        // Header row: gradient #222630 -> #1e222a with bottom border.
        auto header = getLocalBounds().removeFromTop (t::size::moduleHeaderHeight).toFloat();
        juce::ColourGradient headerGradient (juce::Colour (0xff222630), header.getX(), header.getY(),
                                             juce::Colour (0xff1e222a), header.getX(), header.getBottom(), false);
        g.setGradientFill (headerGradient);
        g.fillRect (header);
        g.setColour (c::borderSubtle);
        g.drawHorizontalLine (t::size::moduleHeaderHeight - 1, header.getX(), header.getRight());

        // Icon slot + title + badge (reference panel-header anatomy).
        auto content = headerArea();
        g.setColour (c::panelRaised);
        g.fillRoundedRectangle (content.removeFromLeft (20).removeFromTop (20).toFloat(),
                                t::radius::medium);
        g.setColour (c::textSecondary);
        g.drawRoundedRectangle (headerArea().removeFromLeft (20).removeFromTop (20).toFloat()
                                    .reduced (0.5f), t::radius::medium, 1.0f);
        content.removeFromLeft (27);
        const auto titleArea = content.removeFromLeft (content.getWidth() -
                                  (badge_.isEmpty() ? 0 : badgeWidth()));
        g.setColour (c::text);
        g.setFont (ty::moduleTitle());
        g.drawText (title_, titleArea, juce::Justification::centredLeft);
        if (!badge_.isEmpty())
            drawBadge (g, content.removeFromRight (badgeWidth()).removeFromTop (22).translated (0, 8));
    }

private:
    [[nodiscard]] int badgeWidth() const noexcept
    {
        if (badge_.isEmpty()) return 0;
        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText (typography::badge(), badge_, 0.0f, 0.0f);
        return static_cast<int> (glyphs.getBoundingBox (0, -1, true).getWidth()) + 20;
    }
    void drawBadge (juce::Graphics& g, juce::Rectangle<int> area)
    {
        namespace c = tokens::colour;
        const auto toneColour = tone_ == Tone::accent ? c::fx::accent
                              : tone_ == Tone::warning ? c::warning
                                                       : c::textSecondary;
        g.setColour (c::control);
        g.fillRoundedRectangle (area.toFloat(), 5.0f);
        g.setColour (toneColour.withAlpha (0.55f));
        g.drawRoundedRectangle (area.toFloat().reduced (0.5f), 5.0f, 1.0f);
        g.setColour (tone_ == Tone::neutral ? c::textSecondary : toneColour);
        g.setFont (typography::badge());
        g.drawText (badge_, area, juce::Justification::centred);
    }

    juce::String title_, badge_;
    Tone tone_ = Tone::neutral;
    std::vector<juce::Rectangle<int>> wells_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoxModule)
};

} // namespace vox::ui
