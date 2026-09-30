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
    auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (tokens::colour::panel);
    g.fillRoundedRectangle (bounds, tokens::radius::medium);
    g.setColour (tokens::colour::border);
    g.drawRoundedRectangle (bounds, tokens::radius::medium, 1.0f);

    if (title.isNotEmpty())
    {
        g.setColour (tokens::colour::textSecondary);
        g.setFont (typography::sectionTitle());
        g.drawText (title.toUpperCase(),
                    getLocalBounds().reduced (tokens::spacing::md).removeFromTop (20),
                    juce::Justification::centredLeft, false);
    }
}

juce::Rectangle<int> VoxPanel::getContentBounds() const
{
    auto area = getLocalBounds().reduced (tokens::spacing::md);
    if (title.isNotEmpty())
        area.removeFromTop (24);
    return area;
}

} // namespace vox::ui
