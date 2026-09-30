#include "VoxPanel.h"
#include "../Tokens.h"
#include "../Typography.h"

namespace vox::ui {

VoxPanel::VoxPanel (juce::String titleText) : title (std::move (titleText)) {}

void VoxPanel::setTitle (juce::String newTitle)
{
    title = std::move (newTitle);
    repaint();
}

void VoxPanel::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);

    juce::ColourGradient surface (tokens::colour::panelRaised.withAlpha (0.96f),
                                  bounds.getTopLeft(),
                                  tokens::colour::panel,
                                  bounds.getBottomLeft(), false);
    surface.addColour (0.56, tokens::colour::panel);
    g.setGradientFill (surface);
    g.fillRoundedRectangle (bounds, tokens::radius::medium);

    g.setColour (tokens::colour::borderSubtle.withAlpha (0.92f));
    g.drawRoundedRectangle (bounds.reduced (1.0f), tokens::radius::medium - 1.0f, 1.0f);
    g.setColour (tokens::colour::border.withAlpha (0.78f));
    g.drawRoundedRectangle (bounds, tokens::radius::medium, 1.0f);

    if (title.isNotEmpty())
    {
        const auto headerHeight = 30.0f;
        const auto header = bounds.withHeight (headerHeight);

        g.setColour (tokens::colour::background.withAlpha (0.28f));
        g.fillRoundedRectangle (header, tokens::radius::medium);
        g.fillRect (juce::Rectangle<float> (header.getX(), header.getBottom() - tokens::radius::medium,
                                            header.getWidth(), tokens::radius::medium));

        g.setColour (tokens::colour::accent.withAlpha (0.72f));
        g.fillRoundedRectangle (header.getX() + 10.0f, header.getCentreY() - 1.5f,
                                14.0f, 3.0f, 1.5f);

        g.setColour (tokens::colour::textSecondary);
        g.setFont (typography::sectionTitle());
        g.drawText (title.toUpperCase(),
                    juce::Rectangle<int> (30, 0, getWidth() - 42, static_cast<int> (headerHeight)),
                    juce::Justification::centredLeft, false);

        g.setColour (tokens::colour::borderSubtle.withAlpha (0.9f));
        g.drawHorizontalLine (static_cast<int> (headerHeight),
                              bounds.getX() + 10.0f, bounds.getRight() - 10.0f);
    }
}

juce::Rectangle<int> VoxPanel::getContentBounds() const
{
    auto area = getLocalBounds().reduced (tokens::spacing::md);
    if (title.isNotEmpty())
        area.removeFromTop (28);
    return area;
}

} // namespace vox::ui
