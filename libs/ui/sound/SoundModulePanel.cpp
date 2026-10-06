#include "SoundModulePanel.h"

namespace vstengine::ui {

namespace {
constexpr int headerHeight = 28;
constexpr int horizontalPadding = 8;
constexpr int verticalPadding = 4;
} // namespace

SoundModulePanel::SoundModulePanel (juce::String titleText)
    : headerTitle (std::move (titleText))
{
    header.setTitle (headerTitle);
    header.setPowerVisible (statusDotVisible);
    header.setPowerState (statusDotActive);
    header.onPowerToggled = [this] (const bool on) {
        statusDotActive = on;
        if (onPowerToggled)
            onPowerToggled (on);
    };

    headerSelector.setSize (196, 26);
    headerSelector.setPlaceholderText ({});
    headerSelector.setReadOnly (true);
    header.setSelectorComponent (&headerSelector);
    headerSelector.setVisible (false);

    addAndMakeVisible (header);
}

void SoundModulePanel::setSubtitle (juce::String text)
{
    if (subtitle == text)
        return;
    subtitle = std::move (text);
    header.setSubtitle (subtitle);
}

void SoundModulePanel::setAccent (juce::Colour newAccent)
{
    accent = newAccent;
    repaint();
}

void SoundModulePanel::setHeaderSelector (juce::String text)
{
    headerSelector.setPlaceholderText (text);
    // A panel with no selector value shows no selector chrome at all, which is
    // what the accepted render does for OSCILLATOR, AMP ENVELOPE and MATRIX.
    headerSelector.setVisible (text.isNotEmpty());
    header.resized();
    header.repaint();
}

void SoundModulePanel::setStatusDotVisible (bool shouldBeVisible)
{
    statusDotVisible = shouldBeVisible;
    header.setPowerVisible (shouldBeVisible);
}

void SoundModulePanel::setStatusDotActive (bool shouldBeActive)
{
    statusDotActive = shouldBeActive;
    header.setPowerState (shouldBeActive);
}

void SoundModulePanel::setLeadingIcon (const vox::ui::icons::Icon icon)
{
    header.setIcon (icon);
}

void SoundModulePanel::setActionComponent (juce::Component* component)
{
    header.setActionComponent (component);
}

void SoundModulePanel::setControlComponent (juce::Component* component)
{
    header.setControlComponent (component);
}

void SoundModulePanel::setTrailingPowerVisible (bool shouldBeVisible)
{
    header.setTrailingPowerVisible (shouldBeVisible);
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
    const auto headerArea = getHeaderBounds();
    header.setVisible (getHeight() >= minimumUsefulHeight);
    if (header.isVisible())
        header.setBounds (headerArea);

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
}

} // namespace vstengine::ui
