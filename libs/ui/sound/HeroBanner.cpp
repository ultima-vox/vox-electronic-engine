#include "HeroBanner.h"

#include <cmath>

namespace vstengine::ui {

HeroBanner::HeroBanner()
{
}

void HeroBanner::setIdentity (const HeroIdentity newIdentity, juce::String instrumentName)
{
    identity = newIdentity;
    name = std::move (instrumentName);
    repaint();
}

void HeroBanner::setAccent (juce::Colour newAccent)
{
    accent = newAccent;
    repaint();
}

void HeroBanner::paintArtwork (juce::Graphics& g, juce::Rectangle<float> art)
{
    juce::Graphics::ScopedSaveState saved (g);
    g.reduceClipRegion (art.toNearestInt());

    if (identity == HeroIdentity::acid) {
        // Perspective control surface: a 303-style deck with a knob row.
        const auto board = art.reduced (art.getWidth() * 0.05f, art.getHeight() * 0.12f);

        juce::Path deck;
        deck.startNewSubPath (board.getX(), board.getBottom());
        deck.lineTo (board.getX() + board.getWidth() * 0.18f, board.getY());
        deck.lineTo (board.getRight(), board.getY() + board.getHeight() * 0.12f);
        deck.lineTo (board.getRight() - board.getWidth() * 0.08f, board.getBottom());
        deck.closeSubPath();

        juce::ColourGradient deckGradient (colours::panelRaised, board.getTopLeft(),
                                           colours::graphSurface, board.getBottomRight(), false);
        deckGradient.addColour (0.65f, accent.withAlpha (0.14f));
        g.setGradientFill (deckGradient);
        g.fillPath (deck);
        g.setColour (accent.withAlpha (0.30f));
        g.strokePath (deck, juce::PathStrokeType (1.2f));

        for (int i = 0; i < 5; ++i) {
            const auto t = static_cast<float> (i) / 4.0f;
            const auto cx = board.getX() + board.getWidth() * (0.32f + 0.11f * static_cast<float> (i));
            const auto cy = board.getY() + board.getHeight() * (0.43f + 0.08f * t);
            const auto r = board.getHeight() * 0.11f;
            g.setColour (colours::control);
            g.fillEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f);
            g.setColour (accent.withAlpha (0.55f));
            g.drawEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f, 1.5f);
            g.drawLine (cx, cy, cx + std::sin (t * 2.6f) * r * 0.70f,
                        cy - std::cos (t * 2.6f) * r * 0.70f, 1.5f);
        }

        g.setColour (accent.withAlpha (0.12f));
        for (int line = 0; line < 8; ++line) {
            const auto y = board.getY() + board.getHeight() * (0.66f + static_cast<float> (line) * 0.035f);
            g.drawLine (board.getX() + board.getWidth() * 0.22f, y,
                        board.getRight() - board.getWidth() * 0.10f, y, 1.0f);
        }

        g.setColour (accent.withAlpha (0.62f));
        g.setFont (juce::Font (24.0f, juce::Font::bold));
        g.drawText ("303",
                    board.withTrimmedLeft (board.getWidth() * 0.72f)
                         .withTrimmedBottom (board.getHeight() * 0.54f),
                    juce::Justification::centred, false);
    } else {
        // Panoramic deep-frequency landscape: layered horizon plus a moon.
        const auto horizon = art.getBottom() - art.getHeight() * 0.20f;

        for (int layer = 0; layer < 3; ++layer) {
            juce::Path mountains;
            const auto base = horizon - static_cast<float> (layer) * art.getHeight() * 0.06f;
            mountains.startNewSubPath (art.getX(), art.getBottom());
            mountains.lineTo (art.getX(), base);

            constexpr int peaks = 13;
            for (int i = 0; i <= peaks; ++i) {
                const auto t = static_cast<float> (i) / static_cast<float> (peaks);
                const auto x = art.getX() + t * art.getWidth();
                const auto amp = art.getHeight() * (0.10f + 0.035f * static_cast<float> (layer));
                const auto y = base - std::abs (std::sin (t * 17.0f + static_cast<float> (layer) * 1.4f)) * amp
                                   - std::abs (std::sin (t * 31.0f + 0.7f)) * amp * 0.32f;
                mountains.lineTo (x, y);
            }
            mountains.lineTo (art.getRight(), art.getBottom());
            mountains.closeSubPath();

            g.setColour (juce::Colour::fromRGB (4 + layer * 3, 18 + layer * 7, 30 + layer * 11)
                             .withAlpha (0.94f - static_cast<float> (layer) * 0.12f));
            g.fillPath (mountains);
            g.setColour (accent.withAlpha (0.08f + static_cast<float> (layer) * 0.05f));
            g.strokePath (mountains, juce::PathStrokeType (1.0f));
        }

        const auto moonR = art.getHeight() * 0.42f;
        const juce::Point<float> moon (art.getX() + art.getWidth() * 0.58f,
                                       art.getY() + art.getHeight() * 0.48f);
        g.setColour (accent.withAlpha (0.05f));
        g.fillEllipse (moon.x - moonR, moon.y - moonR, moonR * 2.0f, moonR * 2.0f);
        g.setColour (accent.withAlpha (0.42f));
        g.drawEllipse (moon.x - moonR, moon.y - moonR, moonR * 2.0f, moonR * 2.0f, 1.6f);

        g.setColour (accent.withAlpha (0.22f));
        for (int i = 0; i < 11; ++i) {
            const auto x = art.getX() + art.getWidth() * (0.10f + 0.078f * static_cast<float> (i));
            const auto h = art.getHeight() * (0.05f + 0.14f * std::abs (std::sin (static_cast<float> (i) * 1.7f)));
            g.fillRoundedRectangle (x, art.getBottom() - h - 8.0f, 2.0f, h, 1.0f);
        }
    }

    // Restrained particle field keeps the artwork from reading as a flat band.
    for (int i = 0; i < 24; ++i) {
        const auto t = static_cast<float> (i) / 23.0f;
        const auto x = art.getX() + art.getWidth() * std::fmod (t * 1.73f + 0.11f, 1.0f);
        const auto y = art.getY() + art.getHeight() * std::fmod (t * 2.37f + 0.07f, 0.72f);
        g.setColour (accent.withAlpha (0.11f + 0.18f * std::fmod (t * 5.0f, 1.0f)));
        g.fillEllipse (x, y, 1.4f, 1.4f);
    }
}

void HeroBanner::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    juce::ColourGradient bg (colours::panelRaised.brighter (0.035f), bounds.getTopLeft(),
                             colours::background, bounds.getBottomRight(), false);
    bg.addColour (0.50f, colours::panel);
    bg.addColour (0.78f, accent.withAlpha (0.16f));
    g.setGradientFill (bg);
    g.fillRoundedRectangle (bounds.reduced (0.5f), metrics::corner);

    const auto title = identity == HeroIdentity::acid ? "ACID"
                    : identity == HeroIdentity::psyBass ? "PSY BASS"
                                                       : name.toUpperCase();

    const auto tagline = identity == HeroIdentity::acid
        ? "CLASSIC ATTITUDE  /  MODERN POSSIBILITIES"
        : identity == HeroIdentity::psyBass
            ? "DEEP FREQUENCIES  /  HIGHER POSSIBILITIES"
            : "SOUND  /  MOTION  /  PERFORMANCE";

    const auto keywords = identity == HeroIdentity::acid
        ? juce::String ("SQUELCH\nSEQUENCE\nDISTORT\nTRANSCEND")
        : identity == HeroIdentity::psyBass
            ? juce::String ("DEEP\nEVOLVING\nHYPNOTIC")
            : juce::String ("CREATE\nEVOLVE\nTRANSCEND");

    const auto descriptor = identity == HeroIdentity::acid
        ? juce::String ("AN ICON\nREIMAGINED")
        : juce::String ("MODERN ELECTRONIC\nINSTRUMENT");

    // Compact breakpoint: identity strip only, artwork dropped.
    if (getHeight() >= compactHeight) {
        auto art = bounds.withTrimmedLeft (bounds.getWidth() * 0.26f).reduced (4.0f, 4.0f);
        if (art.getWidth() > 24.0f && art.getHeight() > 20.0f)
            paintArtwork (g, art);
    }

    auto text = getLocalBounds().reduced (22, 12);

    // Keep the whole computation in ints: jmin(int, float) fails to deduce, and
    // Rectangle::removeFromRight takes an int anyway.
    const auto rightWidth = juce::jlimit (0, 210,
        juce::roundToInt (static_cast<float> (text.getWidth()) * 0.34f));
    auto right = text.removeFromRight (rightWidth);

    g.setColour (colours::text);
    g.setFont (juce::Font (identity == HeroIdentity::acid ? 25.0f : 22.0f, juce::Font::bold));
    g.drawText (title, text.removeFromTop (30), juce::Justification::centredLeft, true);

    g.setColour (accent.withAlpha (0.92f));
    g.setFont (10.0f);
    g.drawText (tagline, text.removeFromTop (20), juce::Justification::centredLeft, true);

    if (right.getWidth() > 24.0f) {
        g.setColour (accent.withAlpha (0.82f));
        g.setFont (9.0f);
        g.drawFittedText (keywords, right.removeFromTop (54), juce::Justification::topRight, 4);
        g.setColour (colours::mutedText);
        g.setFont (8.5f);
        g.drawFittedText (descriptor, right.removeFromTop (32), juce::Justification::topRight, 2);
    }

    g.setColour (colours::border);
    g.drawRoundedRectangle (bounds.reduced (0.5f), metrics::corner, 1.0f);
}

} // namespace vstengine::ui