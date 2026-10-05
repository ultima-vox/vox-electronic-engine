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

void SoundWorkspace::clearPanelContent()
{
    for (auto& panel : panels)
        if (panel != nullptr)
            panel->clearContent();
}

void SoundWorkspace::addPanelContent (SoundModulePanel& panel,
                                      const std::vector<juce::Component*>& components,
                                      std::function<void (juce::Rectangle<int>)> layout)
{
    panel.addContent (components, std::move (layout));
}

void SoundWorkspace::addControlStrip (SoundModulePanel& panel,
                                      const std::vector<SoundParameterKnob*>& knobs,
                                      const int columns, const int gap)
{
    if (knobs.empty())
        return;

    std::vector<juce::Component*> items;
    items.reserve (knobs.size());
    for (auto* knob : knobs)
        if (knob != nullptr)
            items.push_back (knob);
    if (items.empty())
        return;

    // The knobs become children of the panel, so this hook receives panel-local
    // coordinates and every setBounds() below is in the panel's own space.
    //
    // The item list is captured by value, so the hook never dereferences the
    // workspace and stays valid independently of it.
    addPanelContent (panel, items, [items, columns, gap] (juce::Rectangle<int> content)
    {
        if (columns > 1)
            layoutControlGrid (items.data(), static_cast<int> (items.size()),
                               content, columns, gap);
        else
            layoutControlRow (items.data(), static_cast<int> (items.size()),
                              content, gap);
    });
}

void SoundWorkspace::addCentreControl (SoundModulePanel& panel, SoundParameterKnob& knob)
{
    addPanelContent (panel, { &knob }, [&knob] (juce::Rectangle<int> content)
    {
        // One dominant control, centred, bounded so it cannot grow into the
        // panel header.
        const auto side = juce::jlimit (0, content.getWidth(),
            juce::jlimit (0, content.getHeight(), content.getHeight()));
        knob.setBounds (content.reduced (juce::jmax (0, (content.getWidth() - side) / 2), 0));
    });
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