#include "VoxComboBox.h"
#include "../Typography.h"
#include "VoxIcons.h"

namespace vox::ui {

VoxComboBox::VoxComboBox()
{
    setSize (120, tokens::size::controlHeight);
    setWantsKeyboardFocus (true);

    // The box draws its own surface and glyph, so every LookAndFeel-owned part
    // of the stock ComboBox is neutralised here rather than overridden
    // downstream.
    setColour (juce::ComboBox::backgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::ComboBox::arrowColourId, juce::Colours::transparentBlack);
    setColour (juce::ComboBox::textColourId, tokens::colour::text);
}

void VoxComboBox::setReadOnly (const bool shouldBeReadOnly)
{
    if (readOnly == shouldBeReadOnly)
        return;

    readOnly = shouldBeReadOnly;
    setWantsKeyboardFocus (! readOnly);
    repaint();
}

void VoxComboBox::setCompact (const bool shouldBeCompact)
{
    if (compact == shouldBeCompact)
        return;

    compact = shouldBeCompact;
    setJustificationType (compact ? juce::Justification::centredLeft
                                  : juce::Justification::centred);
    applyTextPresentation();
    repaint();
}

void VoxComboBox::setCornerRadius (const float radius)
{
    cornerRadius = juce::jmax (0.0f, radius);
    repaint();
}

void VoxComboBox::setPlaceholderText (juce::String text)
{
    placeholder = std::move (text);
    repaint();
}

juce::Label* VoxComboBox::textLabel() const noexcept
{
    // juce::ComboBox does not expose its value label, so it is located by type
    // rather than by child index: the L&F is free to rebuild it.
    for (int i = 0; i < getNumChildComponents(); ++i)
        if (auto* label = dynamic_cast<juce::Label*> (getChildComponent (i)))
            return label;

    return nullptr;
}

int VoxComboBox::chevronReserve() const noexcept
{
    return juce::jmax (18, juce::roundToInt (static_cast<float> (getHeight()) * 0.78f));
}

int VoxComboBox::textInset() const noexcept
{
    return compact ? tokens::spacing::md : tokens::spacing::sm;
}

void VoxComboBox::applyTextPresentation()
{
    if (auto* label = textLabel())
    {
        label->setFont (compact ? typography::valueText() : typography::controlLabel());
        label->setJustificationType (compact ? juce::Justification::centredLeft
                                             : juce::Justification::centred);
        label->setColour (juce::Label::textColourId,
                          findColour (juce::ComboBox::textColourId));
        label->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);

        auto area = getLocalBounds();
        area.removeFromLeft (textInset());
        area.removeFromRight (chevronReserve());
        label->setBounds (area);
    }
}

void VoxComboBox::resized()
{
    juce::ComboBox::resized();
    applyTextPresentation();
}

void VoxComboBox::lookAndFeelChanged()
{
    juce::ComboBox::lookAndFeelChanged();
    applyTextPresentation();
}

void VoxComboBox::showPopup()
{
    if (readOnly)
        return;

    juce::ComboBox::showPopup();
}

void VoxComboBox::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    if (bounds.isEmpty())
        return;

    g.setColour (tokens::colour::control);
    g.fillRoundedRectangle (bounds, cornerRadius);

    const auto hot = ! readOnly && (isMouseOver (true) || isPopupActive());
    g.setColour (hot ? tokens::colour::accentDim : tokens::colour::border);
    g.drawRoundedRectangle (bounds, cornerRadius, 1.0f);

    auto chevronArea = getLocalBounds().toFloat();
    if (chevronArea.getWidth() > static_cast<float> (chevronReserve()))
    {
        chevronArea = chevronArea.removeFromRight (static_cast<float> (chevronReserve()));
        const auto side = juce::jlimit (8.0f, 13.0f,
                                        static_cast<float> (getHeight()) * 0.34f);
        icons::draw (g, icons::Icon::chevronDown,
                     chevronArea.withSizeKeepingCentre (side, side),
                     readOnly ? tokens::colour::textMuted : tokens::colour::textSecondary);
    }

    if (getText().isEmpty() && placeholder.isNotEmpty())
    {
        auto textArea = getLocalBounds();
        textArea.removeFromLeft (textInset());
        textArea.removeFromRight (chevronReserve());

        g.setColour (readOnly ? tokens::colour::text : tokens::colour::textMuted);
        g.setFont (compact ? typography::valueText() : typography::controlLabel());
        g.drawText (placeholder, textArea,
                    compact ? juce::Justification::centredLeft : juce::Justification::centred,
                    false);
    }
}

} // namespace vox::ui
