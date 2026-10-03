#include "SoundGraphs.h"

#include <cmath>

namespace vstengine::ui {

SoundGraph::SoundGraph (const SoundGraphKind graphKind)
    : kind (graphKind)
{
}

void SoundGraph::setAccent (juce::Colour newAccent)
{
    accent = newAccent;
    repaint();
}

void SoundGraph::setPreviewValue (const float value01)
{
    previewValue01 = juce::jlimit (0.0f, 1.0f, value01);
    repaint();
}

void SoundGraph::setWaveShape (const PreviewWaveShape shape)
{
    waveShape = shape;
    repaint();
}

void SoundGraph::setCaption (juce::String text)
{
    if (caption == text)
        return;
    caption = std::move (text);
    repaint();
}

void SoundGraph::setVerticalWeight (const float weight01)
{
    verticalWeight = juce::jlimit (0.2f, 0.8f, weight01);
    repaint();
}

void SoundGraph::paintSurface (juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setColour (colours::graphSurface);
    g.fillRoundedRectangle (area, 4.0f);
    g.setColour (colours::border.withAlpha (0.55f));
    g.drawRoundedRectangle (area, 4.0f, 1.0f);
}

void SoundGraph::paintGrid (juce::Graphics& g, juce::Rectangle<float> plot,
                            const int columns, const int rows, juce::Colour gridAccent)
{
    g.setColour (gridAccent.withAlpha (0.30f));
    for (int c = 1; c < juce::jmax (1, columns); ++c) {
        const auto x = plot.getX() + plot.getWidth() * static_cast<float> (c)
                                     / static_cast<float> (juce::jmax (1, columns));
        g.drawVerticalLine (static_cast<int> (x), plot.getY(), plot.getBottom());
    }
    for (int r = 1; r < juce::jmax (1, rows); ++r) {
        const auto y = plot.getY() + plot.getHeight() * static_cast<float> (r)
                                     / static_cast<float> (juce::jmax (1, rows));
        g.drawHorizontalLine (static_cast<int> (y), plot.getX(), plot.getRight());
    }
}

juce::Path SoundGraph::buildPath (juce::Rectangle<float> plot) const
{
    juce::Path path;
    const auto value = previewValue01;

    switch (kind) {
        case SoundGraphKind::oscillator: {
            const auto wave = plot.withTrimmedBottom (plot.getHeight() * 0.22f);
            constexpr int samples = 128;
            for (int i = 0; i <= samples; ++i) {
                const auto t = static_cast<float> (i) / static_cast<float> (samples);
                const auto x = wave.getX() + t * wave.getWidth();
                const auto phase = t * juce::MathConstants<float>::twoPi;
                float y = 0.0f;
                switch (waveShape) {
                    case PreviewWaveShape::sine:
                        y = std::sin (phase);
                        break;
                    case PreviewWaveShape::saw:
                        y = 1.0f - 2.0f * std::fmod (t, 1.0f);
                        break;
                    case PreviewWaveShape::square:
                        y = std::fmod (t, 1.0f) < 0.5f ? 1.0f : -1.0f;
                        break;
                    case PreviewWaveShape::triangle:
                        y = 4.0f * std::abs (std::fmod (t, 1.0f) - 0.5f) - 1.0f;
                        break;
                    case PreviewWaveShape::noise:
                        // Deterministic pseudo-noise: a fixed hash of the sample
                        // index keeps the Gate A capture reproducible.
                        y = std::sin (static_cast<float> (i) * 12.9898f) * 0.5f
                            + std::sin (static_cast<float> (i) * 4.1414f) * 0.5f;
                        break;
                }
                // Blend lets the morph position read as a real oscillator control.
                const auto blended = y * (0.55f + 0.45f * value);
                const auto py = wave.getCentreY() - blended * wave.getHeight() * 0.42f;
                if (i == 0)
                    path.startNewSubPath (x, py);
                else
                    path.lineTo (x, py);
            }
            break;
        }

        case SoundGraphKind::filter: {
            // Low-pass response whose corner frequency and resonance follow the
            // preview value, so the curve communicates real filter behaviour.
            const auto band = plot.reduced (2.0f);
            const auto resonance = 0.10f + 0.32f * value;
            const auto corner = 0.14f + 0.62f * value;
            const auto baseY = band.getBottom() - band.getHeight() * 0.16f;
            const auto topY = band.getY() + band.getHeight() * 0.14f;
            path.startNewSubPath (band.getX(), baseY);
            path.cubicTo (band.getX() + band.getWidth() * corner * 0.55f, baseY,
                          band.getX() + band.getWidth() * corner * 0.80f,
                          baseY - band.getHeight() * resonance * 0.55f,
                          band.getX() + band.getWidth() * corner,
                          topY + band.getHeight() * (0.18f - resonance * 0.10f));
            path.cubicTo (band.getX() + band.getWidth() * (corner + 0.09f),
                          topY + band.getHeight() * 0.04f,
                          band.getX() + band.getWidth() * (corner + 0.26f),
                          topY + band.getHeight() * 0.26f,
                          band.getRight(), topY + band.getHeight() * 0.40f);
            break;
        }

        case SoundGraphKind::envelope: {
            const auto band = plot.reduced (2.0f);
            const auto a = juce::jlimit (0.04f, 0.30f, 0.06f + value * 0.18f);
            const auto d = juce::jlimit (0.08f, 0.38f, 0.14f + value * 0.22f);
            const auto sustain = juce::jlimit (0.18f, 0.82f, 0.78f - value * 0.42f);
            const auto r = juce::jlimit (0.10f, 0.42f, 0.20f + value * 0.20f);
            const auto topY = band.getY() + band.getHeight() * 0.10f;
            const auto susY = band.getBottom() - band.getHeight() * sustain;
            const auto bottomY = band.getBottom();

            path.startNewSubPath (band.getX(), bottomY);
            path.lineTo (band.getX() + band.getWidth() * a, topY);
            path.cubicTo (band.getX() + band.getWidth() * (a + d * 0.45f),
                          topY + band.getHeight() * 0.06f,
                          band.getX() + band.getWidth() * (a + d * 0.80f), susY,
                          band.getX() + band.getWidth() * (a + d), susY);
            const auto releaseStart = 1.0f - r;
            path.lineTo (band.getX() + band.getWidth() * releaseStart, susY);
            path.cubicTo (band.getX() + band.getWidth() * (releaseStart + r * 0.35f), susY,
                          band.getX() + band.getWidth() * (releaseStart + r * 0.72f),
                          bottomY - band.getHeight() * 0.05f,
                          band.getRight(), bottomY);
            break;
        }

        case SoundGraphKind::modulation: {
            const auto wave = plot.reduced (2.0f);
            constexpr int samples = 96;
            for (int i = 0; i <= samples; ++i) {
                const auto t = static_cast<float> (i) / static_cast<float> (samples);
                const auto x = wave.getX() + t * wave.getWidth();
                // Bipolar LFO whose depth follows the preview value.
                const auto y = wave.getCentreY()
                    - std::sin (t * juce::MathConstants<float>::twoPi * 2.0f)
                          * wave.getHeight() * (0.12f + 0.30f * value) * 0.5f;
                if (i == 0)
                    path.startNewSubPath (x, y);
                else
                    path.lineTo (x, y);
            }
            break;
        }

        case SoundGraphKind::none:
            break;
    }

    return path;
}

void SoundGraph::paint (juce::Graphics& g)
{
    if (kind == SoundGraphKind::none || getWidth() < 24 || getHeight() < 24)
        return;

    auto area = getLocalBounds().toFloat().reduced (0.0f);
    juce::Graphics::ScopedSaveState saved (g);

    // Graph occupies the requested share of the panel; the remainder is left as
    // panel surface for the control strip beneath it.
    const auto graphHeight = juce::jmax (24.0f,
                                         area.getHeight() * verticalWeight);
    const auto surface = area.withHeight (graphHeight);
    paintSurface (g, surface);

    const auto plot = surface.reduced (8.0f, 7.0f);
    if (plot.getWidth() < 8.0f || plot.getHeight() < 8.0f)
        return;

    paintGrid (g, plot, 5, 4, accent);

    const auto path = buildPath (plot);
    if (path.isEmpty())
        return;

    // Centre line keeps bipolar graphs readable.
    if (kind == SoundGraphKind::oscillator || kind == SoundGraphKind::modulation) {
        g.setColour (colours::borderSubtle.withAlpha (0.85f));
        g.drawHorizontalLine (static_cast<int> (plot.getCentreY()),
                              plot.getX(), plot.getRight());
    }

    auto fill = path;
    const auto end = path.getCurrentPosition();
    fill.lineTo (end.x, plot.getBottom());
    fill.lineTo (plot.getX(), plot.getBottom());
    fill.closeSubPath();
    g.setColour (accent.withAlpha (0.10f));
    g.fillPath (fill);

    g.setColour (accent.withAlpha (0.92f));
    g.strokePath (path, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    if (caption.isNotEmpty()) {
        g.setColour (colours::mutedText);
        g.setFont (8.0f);
        g.drawText (caption, surface.reduced (8, 4).withTrimmedTop (surface.getHeight() - 12.0f),
                    juce::Justification::centredRight, true);
    }
}

} // namespace vstengine::ui