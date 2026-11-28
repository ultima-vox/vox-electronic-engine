#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
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
    // Reference panel-header lucide icons, redrawn as small vector glyphs
    // (no font dependency): sliders, layers, waveform, play circle, grid,
    // route arrow, output bars, step grid, star, warning triangle.
    enum class Icon { none, sliders, layers, wave, preview, grid, flow, output, pattern, star, warning };

    VoxModule (juce::String title, juce::String badge = {}, Tone tone = Tone::neutral)
        : title_ (std::move (title)), badge_ (std::move (badge)), tone_ (tone)
    {
        setInterceptsMouseClicks (false, true);
    }

    void setIcon (Icon newIcon) { icon_ = newIcon; repaint(); }


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
        const auto iconSlot = content.removeFromLeft (20).removeFromTop (20);
        g.setColour (c::panelRaised);
        g.fillRoundedRectangle (iconSlot.toFloat(), t::radius::medium);
        paintIcon (g, iconSlot.toFloat());
        g.setColour (c::textSecondary);
        g.drawRoundedRectangle (iconSlot.reduced (0.5f).toFloat(), t::radius::medium, 1.0f);
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
    // Small vector glyph in the 20x20 header slot (secondary colour), ported
    // from the reference lucide icons: 3px strokes on a 20px grid.
    void paintIcon (juce::Graphics& g, juce::Rectangle<float> slot) const
    {
        if (icon_ == Icon::none)
            return;
        namespace c = tokens::colour;
        const auto stroke = juce::PathStrokeType (1.6f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded);
        auto path = [&] (juce::Path& p) { g.setColour (c::textSecondary); g.strokePath (p, stroke); };
        const auto cxx = slot.getCentreX(), cyy = slot.getCentreY();

        switch (icon_)
        {
            case Icon::sliders:
            {
                for (int i = 0; i < 3; ++i)
                {
                    const auto y = slot.getY() + 4.5f + i * 5.0f;
                    juce::Path line; line.startNewSubPath (slot.getX() + 3, y);
                    line.lineTo (slot.getRight() - 3, y);
                    path (line);
                    const auto knobX = slot.getX() + 6 + i * 3.5f;
                    g.fillEllipse (knobX - 2, y - 2, 4, 4);
                }
                break;
            }
            case Icon::layers:
            {
                for (int i = 0; i < 3; ++i)
                {
                    const auto y = slot.getY() + 4 + i * 4.5f;
                    juce::Path line;
                    line.startNewSubPath (slot.getX() + 3 + i * 1.5f, y);
                    line.lineTo (slot.getRight() - 3 - i * 1.5f, y);
                    path (line);
                }
                break;
            }
            case Icon::wave:
            {
                juce::Path wave;
                wave.startNewSubPath (slot.getX() + 2, cyy);
                wave.lineTo (slot.getX() + 5, cyy);
                wave.quadraticTo (slot.getX() + 6.5f, slot.getY() + 2, slot.getX() + 10, cyy);
                wave.quadraticTo (slot.getX() + 13.5f, slot.getBottom() - 2, slot.getX() + 15, cyy);
                wave.lineTo (slot.getRight() - 2, cyy);
                path (wave);
                break;
            }
            case Icon::preview:
            {
                juce::Path ring;
                ring.addEllipse (cxx - 6.5f, cyy - 6.5f, 13, 13);
                path (ring);
                juce::Path play;
                play.addTriangle ({ cxx - 2.2f, cyy - 3.2f }, { cxx - 2.2f, cyy + 3.2f },
                                  { cxx + 3.6f, cyy });
                g.setColour (c::textSecondary);
                g.fillPath (play);
                break;
            }
            case Icon::grid:
            {
                for (int x = 0; x < 2; ++x)
                    for (int y = 0; y < 2; ++y)
                        g.fillRect (slot.getX() + 3.5f + x * 6.5f, slot.getY() + 3.5f + y * 6.5f,
                                    4.5f, 4.5f);
                break;
            }
            case Icon::flow:
            {
                juce::Path arrow;
                arrow.startNewSubPath (slot.getX() + 3, cyy);
                arrow.lineTo (slot.getRight() - 4, cyy);
                arrow.startNewSubPath (slot.getRight() - 8, cyy - 4);
                arrow.lineTo (slot.getRight() - 4, cyy);
                arrow.lineTo (slot.getRight() - 8, cyy + 4);
                path (arrow);
                break;
            }
            case Icon::output:
            {
                for (int i = 0; i < 3; ++i)
                {
                    const auto x = slot.getX() + 4 + i * 4.5f;
                    const auto h = 5.0f + i * 3.5f;
                    g.fillRect (x, cyy + h / 2.0f - h, 3.0f, h);
                }
                break;
            }
            case Icon::pattern:
            {
                for (int i = 0; i < 4; ++i)
                {
                    const auto x = slot.getX() + 3 + i * 3.5f;
                    g.fillRoundedRectangle (x, cyy - 2, 2.5f, 4, 1.0f);
                }
                break;
            }
            case Icon::star:
            {
                juce::Path star;
                const auto outer = 7.0f, inner = 3.0f;
                for (int i = 0; i < 10; ++i)
                {
                    const auto a = -juce::MathConstants<float>::halfPi
                                 + i * juce::MathConstants<float>::pi / 5.0f;
                    const auto r = i % 2 == 0 ? outer : inner;
                    const auto p = juce::Point<float> (cxx + std::cos (a) * r, cyy + std::sin (a) * r);
                    if (i == 0) star.startNewSubPath (p); else star.lineTo (p);
                }
                star.closeSubPath();
                g.setColour (c::textSecondary);
                g.fillPath (star);
                break;
            }
            case Icon::warning:
            {
                juce::Path tri;
                tri.addTriangle ({ cxx, slot.getY() + 3 }, { slot.getX() + 3, slot.getBottom() - 3 },
                                 { slot.getRight() - 3, slot.getBottom() - 3 });
                path (tri);
                juce::Path mark;
                mark.startNewSubPath (cxx, cyy - 2);
                mark.lineTo (cxx, cyy + 2);
                path (mark);
                g.fillEllipse (cxx - 0.8f, cyy + 3.5f, 1.6f, 1.6f);
                break;
            }
            case Icon::none: break;
        }
    }
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
    Icon icon_ = Icon::none;
    std::vector<juce::Rectangle<int>> wells_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoxModule)
};

} // namespace vox::ui
