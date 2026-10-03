#include "SoundWorkspace.h"

namespace vstengine::ui {

void SoundWorkspace::setIdentity (juce::Colour accent, const HeroIdentity hero)
{
    identityAccent = accent;
    heroIdentity = hero;
    applyAccentToPanels();
}

SoundModulePanel& SoundWorkspace::addPanel (juce::String title)
{
    panels.push_back (std::make_unique<SoundModulePanel> (std::move (title)));
    auto& panel = *panels.back();
    panel.setAccent (identityAccent);
    addAndMakeVisible (panel);
    return panel;
}

void SoundWorkspace::applyAccentToPanels()
{
    for (auto& panel : panels)
        panel->setAccent (identityAccent);
}

void SoundWorkspace::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (colours::panel);
    g.fillRoundedRectangle (bounds, metrics::corner);
    g.setColour (colours::border);
    g.drawRoundedRectangle (bounds, metrics::corner, 1.0f);
}

void layoutControlRow (juce::Component* const* items, const int count,
                       juce::Rectangle<int> area, const int gap)
{
    if (count <= 0 || items == nullptr)
        return;

    const int usable = area.getWidth() - gap * (count - 1);
    if (usable <= 0)
        return;

    const int cellWidth = usable / count;
    int x = area.getX();
    for (int i = 0; i < count; ++i) {
        if (items[i] == nullptr)
            continue;
        items[i]->setBounds (juce::Rectangle<int> (x, area.getY(),
                                                   cellWidth, area.getHeight()));
        x += cellWidth + gap;
    }
}

void layoutControlGrid (juce::Component* const* items, const int count,
                        juce::Rectangle<int> area, const int columns, const int gap)
{
    if (count <= 0 || items == nullptr || columns <= 0)
        return;

    const int rows = (count + columns - 1) / columns;
    const auto cellWidth = juce::jmax (1,
        (area.getWidth() - gap * (columns - 1)) / columns);
    const auto cellHeight = juce::jmax (1,
        (area.getHeight() - gap * (rows - 1)) / rows);

    for (int i = 0; i < count; ++i) {
        if (items[i] == nullptr)
            continue;
        const int column = i % columns;
        const int row = i / columns;
        items[i]->setBounds (juce::Rectangle<int> (
            area.getX() + (cellWidth + gap) * column,
            area.getY() + (cellHeight + gap) * row,
            cellWidth, cellHeight));
    }
}

} // namespace vstengine::ui