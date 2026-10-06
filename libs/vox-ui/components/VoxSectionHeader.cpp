#include "VoxSectionHeader.h"
#include "../Tokens.h"
#include "../Typography.h"

namespace vox::ui {

namespace {

// Measured from the accepted SOUND render: the leading glyph occupies a ~20 px
// square that is 17 px of drawn glyph, and the title starts one small spacing
// step after it.
constexpr int leadingBoxMax = 20;
constexpr float glyphFraction = 0.86f;

} // namespace

VoxSectionHeader::VoxSectionHeader (juce::String initialTitle)
    : title (std::move (initialTitle)),
      accentColour (tokens::colour::accent)
{
    setInterceptsMouseClicks (true, true);
}

void VoxSectionHeader::setTitle (juce::String newTitle)
{
    title = std::move (newTitle);
    repaint();
}

void VoxSectionHeader::setSubtitle (juce::String text)
{
    if (subtitle == text)
        return;
    subtitle = std::move (text);
    resized();
    repaint();
}

void VoxSectionHeader::setAccentColour (juce::Colour colour)
{
    accentColour = colour;
    repaint();
}

void VoxSectionHeader::setPowerVisible (bool visible)
{
    if (visible)
    {
        leading = LeadingType::Power;
        icon.reset();
        hasVectorIcon = false;
    }
    else
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
    hasVectorIcon = false;
    leading = icon != nullptr ? LeadingType::Icon : LeadingType::None;
    resized();
    repaint();
}

void VoxSectionHeader::setIcon (const icons::Icon newIcon)
{
    vectorIcon = newIcon;
    hasVectorIcon = true;
    icon.reset();
    leading = LeadingType::Icon;
    resized();
    repaint();
}

void VoxSectionHeader::clearIcon()
{
    icon.reset();
    hasVectorIcon = false;
    if (leading == LeadingType::Icon)
        leading = LeadingType::None;
    resized();
    repaint();
}

void VoxSectionHeader::install (Slot& slot, juce::Component* component, const int fallbackWidth)
{
    if (slot.component == component)
        return;

    if (slot.component != nullptr)
        removeChildComponent (slot.component);

    slot.component = component;
    slot.preferredWidth = 0;

    if (slot.component != nullptr)
    {
        slot.preferredWidth = slot.component->getWidth() > 0 ? slot.component->getWidth()
                                                             : fallbackWidth;
        addAndMakeVisible (*slot.component);
    }

    resized();
    repaint();
}

void VoxSectionHeader::setControlComponent (juce::Component* component)
{
    install (controlSlot, component, 145);
}

void VoxSectionHeader::setSelectorComponent (juce::Component* component)
{
    install (selectorSlot, component, 196);
}

void VoxSectionHeader::setActionComponent (juce::Component* component)
{
    install (actionSlot, component, tokens::size::controlHeight);
}

void VoxSectionHeader::setTrailingPowerVisible (const bool visible)
{
    if (trailingPowerVisible == visible)
        return;
    trailingPowerVisible = visible;
    resized();
    repaint();
}

void VoxSectionHeader::resized()
{
    leadingBounds = {};
    trailingPowerBounds = {};

    auto area = getLocalBounds().reduced (0, 2);
    if (area.getWidth() <= 0 || area.getHeight() <= 0)
        return;

    const auto leadingSide = juce::jmin (area.getHeight(), leadingBoxMax);

    if (leading != LeadingType::None)
    {
        leadingBounds = area.removeFromLeft (leadingSide);
        area.removeFromLeft (tokens::spacing::sm);
    }

    // Trailing slots are claimed right to left so the right edge is stable
    // regardless of how wide the title happens to be.
    if (trailingPowerVisible)
    {
        trailingPowerBounds = area.removeFromRight (leadingSide);
        area.removeFromRight (tokens::spacing::md);
    }

    if (isSlotVisible (actionSlot))
    {
        actionSlot.component->setBounds (
            area.removeFromRight (juce::jmin (actionSlot.preferredWidth, area.getWidth())));
        area.removeFromRight (tokens::spacing::md);
    }

    const auto titleFont = typography::sectionTitle();
    auto titleWidth = juce::roundToInt (juce::GlyphArrangement::getStringWidth (
        titleFont, title.toUpperCase()));

    if (subtitle.isNotEmpty())
        titleWidth += tokens::spacing::xs
                      + juce::roundToInt (juce::GlyphArrangement::getStringWidth (
                            typography::valueText(), subtitle));

    // The centre slots live in the free space after the title, which is where
    // the accepted render places the OSCILLATOR segments and the FILTER
    // selector: equidistant between the title and the trailing controls.
    auto centreArea = area.withTrimmedLeft (
        juce::jmin (area.getWidth(), titleWidth + tokens::spacing::md));

    const auto hasControl = isSlotVisible (controlSlot);
    const auto hasSelector = isSlotVisible (selectorSlot);

    const auto centreWidth = (hasControl ? controlSlot.preferredWidth : 0)
        + (hasSelector ? selectorSlot.preferredWidth
                             + (hasControl ? tokens::spacing::sm : 0)
                       : 0);

    if (centreWidth > 0)
    {
        auto block = centreArea.withSizeKeepingCentre (
            juce::jmin (centreWidth, centreArea.getWidth()), centreArea.getHeight());

        if (hasControl)
        {
            controlSlot.component->setBounds (
                block.removeFromLeft (juce::jmin (controlSlot.preferredWidth, block.getWidth())));
            block.removeFromLeft (tokens::spacing::sm);
        }

        if (hasSelector)
            selectorSlot.component->setBounds (
                block.removeFromLeft (juce::jmin (selectorSlot.preferredWidth, block.getWidth())));
    }
}

juce::Rectangle<int> VoxSectionHeader::titleBounds() const
{
    auto area = getLocalBounds().reduced (0, 2);

    if (leading != LeadingType::None)
        area.setLeft (leadingBounds.getRight() + tokens::spacing::sm);

    // The title must never be drawn underneath a trailing or centred control,
    // so it is clipped against whichever of them starts furthest left.
    auto right = area.getRight();

    if (trailingPowerVisible)
        right = juce::jmin (right, trailingPowerBounds.getX() - tokens::spacing::md);

    for (const auto* slot : { &actionSlot, &selectorSlot, &controlSlot })
        if (isSlotVisible (*slot))
            right = juce::jmin (right, slot->component->getX() - tokens::spacing::md);

    area.setRight (juce::jmax (area.getX(), right));
    return area;
}

void VoxSectionHeader::paint (juce::Graphics& g)
{
    const auto glyphColour = leading == LeadingType::Power && ! powerOn
        ? tokens::colour::border
        : accentColour;

    if (leading != LeadingType::None)
    {
        const auto side = static_cast<float> (juce::jmin (leadingBounds.getWidth(),
                                                           leadingBounds.getHeight()))
                          * glyphFraction;
        const auto glyphArea = leadingBounds.toFloat().withSizeKeepingCentre (side, side);

        if (leading == LeadingType::Power)
            icons::draw (g, icons::Icon::power, glyphArea, glyphColour);
        else if (hasVectorIcon)
            icons::draw (g, vectorIcon, glyphArea, glyphColour);
        else if (icon != nullptr)
            icon->drawWithin (g, glyphArea,
                              juce::RectanglePlacement::centred, 1.0f);
    }

    if (trailingPowerVisible)
    {
        const auto side = static_cast<float> (juce::jmin (trailingPowerBounds.getWidth(),
                                                          trailingPowerBounds.getHeight()))
                          * glyphFraction;
        icons::draw (g, icons::Icon::power,
                     trailingPowerBounds.toFloat().withSizeKeepingCentre (side, side),
                     powerOn ? accentColour : tokens::colour::border);
    }

    auto titleArea = titleBounds();
    if (titleArea.isEmpty())
        return;

    g.setColour (tokens::colour::text);
    g.setFont (typography::sectionTitle());

    const auto titleText = title.toUpperCase();
    // One pixel of slack: the measured advance width is a float, and drawing
    // into a truncated integer width clips the last glyph of an otherwise
    // fitting title.
    const auto titleWidth = juce::jmin (
        juce::roundToInt (juce::GlyphArrangement::getStringWidth (typography::sectionTitle(),
                                                                  titleText)) + 2,
        titleArea.getWidth());

    g.drawText (titleText, titleArea.withWidth (titleWidth),
                juce::Justification::centredLeft, false);

    if (subtitle.isNotEmpty())
    {
        auto subtitleArea = titleArea.withTrimmedLeft (titleWidth + tokens::spacing::xs);
        g.setColour (tokens::colour::textMuted);
        g.setFont (typography::valueText());
        g.drawText (subtitle, subtitleArea, juce::Justification::centredLeft, false);
    }
}

void VoxSectionHeader::togglePower()
{
    powerOn = ! powerOn;
    repaint();
    if (onPowerToggled)
        onPowerToggled (powerOn);
}

void VoxSectionHeader::mouseDown (const juce::MouseEvent& e)
{
    if (leading == LeadingType::Power && leadingBounds.contains (e.getPosition()))
    {
        togglePower();
        return;
    }

    if (trailingPowerVisible && trailingPowerBounds.contains (e.getPosition()))
        togglePower();
}

} // namespace vox::ui
