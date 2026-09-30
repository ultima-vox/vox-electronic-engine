#include "VoxSectionHeader.h"
#include "../Tokens.h"
#include "../Typography.h"

namespace vox::ui {

VoxSectionHeader::VoxSectionHeader (juce::String initialTitle)
    : title (std::move (initialTitle))
{
    setInterceptsMouseClicks (true, true);
}

void VoxSectionHeader::setTitle (juce::String newTitle)
{
    title = std::move (newTitle);
    repaint();
}

void VoxSectionHeader::setPowerVisible (bool visible)
{
    if (visible)
    {
        leading = LeadingType::Power;
        icon.reset();
    }
    else if (leading == LeadingType::Power)
    {
        leading = LeadingType::None;
    }

    resized();
    repaint();
}

void VoxSectionHeader::setPowerState (bool on)
{
    if (powerOn == on)
        return;
    powerOn = on;
    repaint();
}

void VoxSectionHeader::setIcon (std::unique_ptr<juce::Drawable> drawable)
{
    icon = std::move (drawable);
    leading = icon != nullptr ? LeadingType::Icon : LeadingType::None;
    resized();
    repaint();
}

void VoxSectionHeader::clearIcon()
{
    icon.reset();
    if (leading == LeadingType::Icon)
        leading = LeadingType::None;
    resized();
    repaint();
}

void VoxSectionHeader::setActionComponent (juce::Component* component)
{
    if (action == component)
        return;

    if (action != nullptr)
        removeChildComponent (action);

    action = component;
    if (action != nullptr)
        addAndMakeVisible (*action);

    resized();
    repaint();
}

juce::Rectangle<int> VoxSectionHeader::getLeadingBounds() const
{
    if (leading == LeadingType::None)
        return {};

    auto area = getLocalBounds().reduced (0, 2);
    const auto side = juce::jmin (area.getHeight(), tokens::size::controlHeight);
    return area.removeFromLeft (side).reduced (2);
}

juce::Rectangle<int> VoxSectionHeader::getActionBounds() const
{
    if (action == nullptr)
        return {};

    auto area = getLocalBounds().reduced (0, 2);
    const auto preferredWidth = action->getWidth() > 0 ? action->getWidth() : 80;
    return area.removeFromRight (preferredWidth);
}

void VoxSectionHeader::resized()
{
    if (action != nullptr)
        action->setBounds (getActionBounds());
}

void VoxSectionHeader::paint (juce::Graphics& g)
{
    auto titleArea = getLocalBounds();

    if (leading != LeadingType::None)
    {
        const auto leadingBounds = getLeadingBounds();
        if (leading == LeadingType::Power)
        {
            auto circle = leadingBounds.toFloat();
            const auto d = juce::jmin (circle.getWidth(), circle.getHeight());
            circle = circle.withSizeKeepingCentre (d, d).reduced (2.0f);

            g.setColour (powerOn ? tokens::colour::accent : tokens::colour::border);
            g.drawEllipse (circle, 1.5f);

            if (powerOn)
            {
                g.drawLine (circle.getCentreX(), circle.getY() + circle.getHeight() * 0.18f,
                            circle.getCentreX(), circle.getCentreY() + 1.0f, 1.8f);
            }
        }
        else if (icon != nullptr)
        {
            icon->drawWithin (g, leadingBounds.toFloat(),
                              juce::RectanglePlacement::centred, 1.0f);
        }

        titleArea.removeFromLeft (leadingBounds.getWidth() + tokens::spacing::sm);
    }

    if (action != nullptr)
        titleArea.removeFromRight (getActionBounds().getWidth() + tokens::spacing::sm);

    g.setColour (tokens::colour::textSecondary);
    g.setFont (typography::sectionTitle());
    g.drawText (title.toUpperCase(), titleArea,
                juce::Justification::centredLeft, false);
}

void VoxSectionHeader::mouseDown (const juce::MouseEvent& e)
{
    if (leading != LeadingType::Power || ! getLeadingBounds().contains (e.getPosition()))
        return;

    powerOn = ! powerOn;
    repaint();
    if (onPowerToggled)
        onPowerToggled (powerOn);
}

} // namespace vox::ui
