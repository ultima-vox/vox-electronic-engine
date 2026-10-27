#pragma once

#include <juce_graphics/juce_graphics.h>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <string_view>

// Instrument identity presentation (VOX accent-colour policy): the accent
// colours identity artwork only; interaction state stays VOX cyan. Artwork is
// procedural and deterministic, never a control and never carries state.
namespace vstengine::ui::identity {

enum class Family { bass, acid, lead, atmos, fx, other };

struct Identity {
    Family family{Family::other};
    juce::Colour accent;
};

inline Identity forInstrument(std::string_view instrumentId) {
    if (instrumentId == "com.ultimavox.psy-bass")
        return {Family::bass, juce::Colour(0xff1e9bff)};
    if (instrumentId == "com.ultimavox.acid")
        return {Family::acid, juce::Colour(0xff39e27a)};
    if (instrumentId == "com.ultimavox.lead")
        return {Family::lead, juce::Colour(0xffa066ff)};
    if (instrumentId == "com.ultimavox.atmos-texture")
        return {Family::atmos, juce::Colour(0xffbfe8ff)};
    if (instrumentId == "com.ultimavox.semantic-fx")
        return {Family::fx, juce::Colour(0xff22c7ff)};
    const auto hash = static_cast<std::uint32_t>(
        juce::String(juce::CharPointer_UTF8(instrumentId.data()),
                     instrumentId.size())
            .hashCode());
    return {Family::other,
            juce::Colour::fromHSV(static_cast<float>(hash & 255u) / 255.0f, 0.55f,
                                  0.9f, 1.0f)};
}

// Small identity thumbnail: dark well, accent haze and a family glyph drawn
// as a glowing waveform.
inline void drawThumbnail(juce::Graphics &g, juce::Rectangle<float> area,
                          const Identity &id) {
    const auto radius = 3.0f;
    juce::Graphics::ScopedSaveState save(g);
    juce::Path clip;
    clip.addRoundedRectangle(area, radius);
    g.reduceClipRegion(clip);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff0b1d2b), area.getX(),
                                           area.getY(), juce::Colour(0xff03080d),
                                           area.getX(), area.getBottom(), false));
    g.fillRect(area);
    g.setGradientFill(juce::ColourGradient(
        id.accent.withAlpha(0.32f), area.getCentreX(), area.getBottom(),
        id.accent.withAlpha(0.0f), area.getCentreX(), area.getY(), true));
    g.fillRect(area);

    juce::Path wave;
    const auto x0 = area.getX(), w = area.getWidth(), base = area.getBottom() - 2.0f;
    const auto h = area.getHeight();
    switch (id.family) {
    case Family::bass: { // rolling ridge
        wave.startNewSubPath(x0, base);
        for (int i = 0; i <= 24; ++i) {
            const auto t = static_cast<float>(i) / 24.0f;
            const auto y = base - h * (0.18f + 0.55f * std::pow(std::sin(t * 3.14159f), 1.6f)
                                       * (0.75f + 0.25f * std::sin(t * 17.0f)));
            wave.lineTo(x0 + t * w, y);
        }
        break;
    }
    case Family::acid: { // resonant spectral spikes
        constexpr float peaks[] = {0.1f, 0.25f, 0.15f, 0.5f, 0.3f, 0.95f, 0.4f,
                                   0.7f, 0.2f, 0.55f, 0.18f, 0.32f, 0.1f};
        wave.startNewSubPath(x0, base);
        const auto n = static_cast<float>(std::size(peaks));
        for (std::size_t i = 0; i < std::size(peaks); ++i) {
            const auto x = x0 + (static_cast<float>(i) + 0.5f) / n * w;
            wave.lineTo(x - w / n * 0.35f, base - h * peaks[i] * 0.2f);
            wave.lineTo(x, base - h * peaks[i] * 0.85f);
            wave.lineTo(x + w / n * 0.35f, base - h * peaks[i] * 0.2f);
        }
        wave.lineTo(x0 + w, base);
        break;
    }
    case Family::lead: { // sharp crystal peaks
        constexpr float peaks[] = {0.35f, 0.6f, 0.4f, 0.9f, 0.5f, 0.7f, 0.3f};
        wave.startNewSubPath(x0, base - h * 0.15f);
        const auto n = static_cast<float>(std::size(peaks));
        for (std::size_t i = 0; i < std::size(peaks); ++i) {
            const auto x = x0 + (static_cast<float>(i) + 0.5f) / n * w;
            wave.lineTo(x, base - h * peaks[i] * 0.85f);
            wave.lineTo(x + w / n * 0.5f, base - h * 0.15f);
        }
        break;
    }
    case Family::atmos: { // drifting light streaks
        for (int s = 0; s < 3; ++s) {
            const auto off = static_cast<float>(s) * 0.22f;
            wave.startNewSubPath(x0, base - h * (0.2f + off));
            wave.cubicTo(x0 + w * 0.35f, base - h * (0.75f + off * 0.2f),
                         x0 + w * 0.6f, base - h * (0.05f + off),
                         x0 + w, area.getY() + h * (0.2f + off * 0.5f));
        }
        break;
    }
    case Family::fx:
    case Family::other: { // swept burst
        wave.startNewSubPath(x0, area.getCentreY());
        for (int i = 0; i <= 40; ++i) {
            const auto t = static_cast<float>(i) / 40.0f;
            const auto env = std::sin(t * 3.14159f);
            wave.lineTo(x0 + t * w, area.getCentreY() +
                                        std::sin(t * 38.0f * (0.4f + t)) * env * h * 0.38f);
        }
        break;
    }
    }
    if (id.family == Family::bass || id.family == Family::acid ||
        id.family == Family::lead) {
        auto fill = wave;
        fill.lineTo(x0 + w, area.getBottom());
        fill.lineTo(x0, area.getBottom());
        fill.closeSubPath();
        g.setGradientFill(juce::ColourGradient(id.accent.withAlpha(0.45f),
                                               area.getCentreX(), area.getY(),
                                               id.accent.withAlpha(0.04f),
                                               area.getCentreX(), area.getBottom(),
                                               false));
        g.fillPath(fill);
    }
    g.setColour(id.accent.withAlpha(0.3f));
    g.strokePath(wave, juce::PathStrokeType(2.6f, juce::PathStrokeType::curved,
                                            juce::PathStrokeType::rounded));
    g.setColour(id.accent.brighter(0.35f));
    g.strokePath(wave, juce::PathStrokeType(1.0f, juce::PathStrokeType::curved,
                                            juce::PathStrokeType::rounded));
}

// Rack footer artwork: layered mountain silhouettes under a faint sky.
inline void drawMountains(juce::Graphics &g, juce::Rectangle<float> area,
                          juce::Colour accent) {
    juce::Graphics::ScopedSaveState save(g);
    g.reduceClipRegion(area.toNearestInt());
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff0d2436), area.getCentreX(),
                                           area.getY(), juce::Colour(0xff06121d),
                                           area.getCentreX(), area.getBottom(), false));
    g.fillRect(area);
    juce::Random stars(7);
    for (int i = 0; i < 26; ++i) {
        const auto x = area.getX() + stars.nextFloat() * area.getWidth();
        const auto y = area.getY() + stars.nextFloat() * area.getHeight() * 0.45f;
        g.setColour(juce::Colours::white.withAlpha(0.12f + stars.nextFloat() * 0.3f));
        g.fillEllipse(x, y, 1.2f, 1.2f);
    }
    struct Layer {
        float top, bottom;
        juce::Colour fill;
        int seed;
    };
    const Layer layers[] = {{0.30f, 0.72f, juce::Colour(0xff17344a), 3},
                            {0.42f, 0.86f, juce::Colour(0xff0e2333), 11},
                            {0.58f, 1.0f, juce::Colour(0xff08151f), 19}};
    for (const auto &layer : layers) {
        juce::Random r(layer.seed);
        juce::Path p;
        const auto baseY = area.getBottom();
        p.startNewSubPath(area.getX(), baseY);
        auto x = area.getX();
        juce::Point<float> previous{x, area.getY() + area.getHeight() * layer.bottom};
        p.lineTo(previous);
        juce::Path ridge;
        ridge.startNewSubPath(previous);
        while (x < area.getRight()) {
            x += area.getWidth() * (0.06f + r.nextFloat() * 0.1f);
            const auto peak = layer.top + r.nextFloat() * (layer.bottom - layer.top);
            const juce::Point<float> next{juce::jmin(x, area.getRight()),
                                          area.getY() + area.getHeight() * peak};
            p.lineTo(next);
            ridge.lineTo(next);
        }
        p.lineTo(area.getRight(), baseY);
        p.closeSubPath();
        g.setColour(layer.fill);
        g.fillPath(p);
        g.setColour(accent.withAlpha(0.18f));
        g.strokePath(ridge, juce::PathStrokeType(1.0f));
    }
}

} // namespace vstengine::ui::identity
