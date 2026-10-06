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

void SoundModulePanel::addContent (const std::vector<juce::Component*>& components,
                                   std::function<void (juce::Rectangle<int>)> layout)
{
    if (components.empty())
        return;

    // Reparent into this panel, so the components' coordinates become
    // panel-local from here on. addAndMakeVisible detaches a component from any
    // previous parent, which is what keeps one consistent coordinate model.
    for (auto* component : components)
        if (component != nullptr) {
            addAndMakeVisible (*component);
            contentComponents.push_back (component);
        }

    contentLayouts.push_back (std::move (layout));

    // Lay out immediately so content is correctly placed even before the first
    // resize pass reaches this panel.
    if (contentLayouts.back() != nullptr)
        contentLayouts.back() (getContentBounds());
}

void SoundModulePanel::clearContent()
{
    for (auto* component : contentComponents)
        if (component != nullptr)
            removeChildComponent (component);
    contentComponents.clear();
    contentLayouts.clear();
}

void SoundModulePanel::resized()
{
    const auto content = getContentBounds();
    for (auto& layout : contentLayouts)
        if (layout != nullptr)
            layout (content);
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

    // The right-aligned selector badge is sized to its own text rather than a flat
    // reservation. A fixed 74px reservation squeezed narrow module titles down to
    // an ellipsis (the DISTORTION panel rendered as "DISTO...") and let the badge
    // label draw underneath its own chevron.
    const auto chevronWidth = 10;
    const auto selectorWidth = headerSelector.isNotEmpty()
        ? juce::jlimit (36, 96, juce::roundToInt (juce::GlyphArrangement::getStringWidth (
                                      juce::Font (8.5f), headerSelector))
                                 + chevronWidth + 12)
        : 0;
    if (selectorWidth > 0)
        titleArea.setWidth (juce::jmax (0, titleArea.getWidth() - selectorWidth));

    // A module title identifies its panel, so shrink it to fit rather than
    // ellipsising it away.
    auto titleFont = juce::Font (10.5f, juce::Font::bold);
    while (titleFont.getHeight() > 7.0f
           && juce::GlyphArrangement::getStringWidth (titleFont, title)
                  > static_cast<float> (titleArea.getWidth()))
        titleFont = titleFont.withHeight (titleFont.getHeight() - 0.5f);

    g.setColour (colours::text);
    g.setFont (titleFont);
    g.drawText (title, titleArea, juce::Justification::centredLeft, true);

    if (selectorWidth > 0) {
        auto selector = juce::Rectangle<int> (header.getRight() - selectorWidth, header.getY(),
                                              selectorWidth, header.getHeight())
                            .reduced (0, 2);
        g.setColour (colours::control);
        g.fillRoundedRectangle (selector.toFloat(), 3.0f);
        g.setColour (colours::border);
        g.drawRoundedRectangle (selector.toFloat(), 3.0f, 1.0f);
        g.setColour (accent.withAlpha (0.85f));

        // The badge carries a real control value ("LOW-PASS 24", "SAWTOOTH"), so
        // shrink it to fit the box rather than ellipsising the value away.
        const auto badgeText = selector.reduced (4, 0).withTrimmedRight (chevronWidth);
        auto badgeFont = juce::Font (8.5f);
        while (badgeFont.getHeight() > 6.5f
               && juce::GlyphArrangement::getStringWidth (badgeFont, headerSelector)
                      > static_cast<float> (badgeText.getWidth()))
            badgeFont = badgeFont.withHeight (badgeFont.getHeight() - 0.5f);
        g.setFont (badgeFont);
        g.drawText (headerSelector, badgeText, juce::Justification::centredRight, false);

        g.setColour (colours::mutedText.withAlpha (0.9f));
        g.drawText ("\xE2\x96\xBC", selector.withTrimmedLeft (selector.getWidth() - chevronWidth),
                    juce::Justification::centred, false);
    } else if (subtitle.isNotEmpty()) {
        g.setColour (colours::mutedText);
        g.setFont (8.0f);
        g.drawText (subtitle, header, juce::Justification::centredRight, true);
    }
}

} // namespace vstengine::ui