#include "SoundModulePanel.h"

namespace vstengine::ui {

namespace {
constexpr int headerHeight = 20;
constexpr int horizontalPadding = 10;
constexpr int verticalPadding = 8;
} // namespace

SoundModulePanel::SoundModulePanel (juce::String titleText)
    : title (std::move (titleText))
{
}

void SoundModulePanel::setSubtitle (juce::String text)
{
    if (subtitle == text)
        return;
    subtitle = std::move (text);
    repaint();
}

void SoundModulePanel::setAccent (juce::Colour newAccent)
{
    accent = newAccent;
    repaint();
}

void SoundModulePanel::setHeaderSelector (juce::String text)
{
    if (headerSelector == text)
        return;
    headerSelector = std::move (text);
    repaint();
}

void SoundModulePanel::setStatusDotVisible (bool shouldBeVisible)
{
    statusDotVisible = shouldBeVisible;
    repaint();
}

void SoundModulePanel::setStatusDotActive (bool shouldBeActive)
{
    statusDotActive = shouldBeActive;
    repaint();
}

juce::Rectangle<int> SoundModulePanel::getHeaderBounds() const
{
    return getLocalBounds().reduced (horizontalPadding, verticalPadding)
        .removeFromTop (headerHeight);
}

juce::Rectangle<int> SoundModulePanel::getContentBounds() const
{
    return getLocalBounds().reduced (horizontalPadding, verticalPadding)
        .withTrimmedTop (headerHeight + 4);
}

void SoundModulePanel::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (0.5f);

    juce::ColourGradient surface (colours::panelRaised.brighter (0.02f),
                                  bounds.getTopLeft(), colours::panel,
                                  bounds.getBottomLeft(), false);
    g.setGradientFill (surface);
    g.fillRoundedRectangle (bounds, metrics::corner);
    g.setColour (colours::border);
    g.drawRoundedRectangle (bounds, metrics::corner, 1.0f);

    if (getHeight() < minimumUsefulHeight)
        return;

    auto header = getHeaderBounds();

    if (statusDotVisible) {
        g.setColour (accent.withAlpha (0.88f));
        g.drawEllipse (static_cast<float> (header.getX()),
                       static_cast<float> (header.getCentreY() - 3.0f),
                       6.0f, 6.0f, 1.2f);
        if (statusDotActive) {
            g.setColour (accent.withAlpha (0.28f));
            g.fillEllipse (static_cast<float> (header.getX()),
                           static_cast<float> (header.getCentreY() - 3.0f),
                           6.0f, 6.0f);
        }
    }

    auto titleArea = header.withTrimmedLeft (statusDotVisible ? 11 : 0);

    // Reserve room for the right-aligned selector/subtitle so a long instrument
    // selector can never push the module title out of the panel.
    if (headerSelector.isNotEmpty())
        titleArea.setWidth (juce::jmax (0, titleArea.getWidth() - 74));

    g.setColour (colours::text);
    g.setFont (juce::Font (10.5f, juce::Font::bold));
    g.drawText (title, titleArea, juce::Justification::centredLeft, true);

    if (headerSelector.isNotEmpty()) {
        auto selector = juce::Rectangle<int> (header.getRight() - 74, header.getY(),
                                              74, header.getHeight())
                            .reduced (0, 2);
        g.setColour (colours::control);
        g.fillRoundedRectangle (selector.toFloat(), 3.0f);
        g.setColour (colours::border);
        g.drawRoundedRectangle (selector.toFloat(), 3.0f, 1.0f);
        g.setColour (accent.withAlpha (0.85f));
        g.setFont (8.5f);
        g.drawText (headerSelector, selector.reduced (4, 0),
                    juce::Justification::centredRight, true);
        g.setColour (colours::mutedText.withAlpha (0.9f));
        g.drawText ("\xE2\x96\xBC", selector.withTrimmedLeft (selector.getWidth() - 10),
                    juce::Justification::centred, false);
    } else if (subtitle.isNotEmpty()) {
        g.setColour (colours::mutedText);
        g.setFont (8.0f);
        g.drawText (subtitle, header, juce::Justification::centredRight, true);
    }
}

} // namespace vstengine::ui