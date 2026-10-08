#include "VoxInlineSelector.h"
#include "../Tokens.h"

namespace vox::ui {

namespace {
constexpr int stepperGap = 6;
} // namespace

VoxInlineSelector::VoxInlineSelector (Form selectorForm)
    : form (selectorForm)
{
    combo.setCompact (form == Form::Header);
    combo.setReadOnly (form == Form::Header);

    // A stepper arrow is a chevron in a bordered square, matching the accepted
    // OSCILLATOR "Wavetable 01" row.
    previous.setBorderVisible (true);
    next.setBorderVisible (true);

    previous.onClick = [this]
    {
        if (onPrevious)
            onPrevious();
    };
    next.onClick = [this]
    {
        if (onNext)
            onNext();
    };

    combo.onChange = [this]
    {
        if (onChanged)
            onChanged (getSelectedIndex());
    };

    addAndMakeVisible (combo);
    if (form == Form::Stepper)
    {
        addAndMakeVisible (previous);
        addAndMakeVisible (next);
        setSize (280, 30);
    }
    else
    {
        setSize (196, 30);
    }
}

void VoxInlineSelector::setItems (juce::StringArray items, const int selectedIndex)
{
    combo.clear (juce::dontSendNotification);
    for (int i = 0; i < items.size(); ++i)
        combo.addItem (items[i], i + 1);

    if (! items.isEmpty())
    {
        combo.setSelectedId (juce::jlimit (0, items.size() - 1, selectedIndex) + 1,
                             juce::dontSendNotification);
        combo.setReadOnly (false);
    }

    setSteppingEnabled (! items.isEmpty(), ! items.isEmpty());
}

void VoxInlineSelector::setSelectedIndex (const int index)
{
    if (index < 0 || index >= combo.getNumItems())
        return;

    combo.setSelectedId (index + 1, juce::sendNotificationSync);
}

int VoxInlineSelector::getSelectedIndex() const noexcept
{
    const auto id = combo.getSelectedId();
    return id > 0 ? id - 1 : -1;
}

juce::String VoxInlineSelector::getSelectedText() const
{
    return combo.getText();
}

void VoxInlineSelector::setPlaceholderText (juce::String text)
{
    combo.setPlaceholderText (std::move (text));
}

void VoxInlineSelector::setReadOnly (const bool shouldBeReadOnly)
{
    combo.setReadOnly (shouldBeReadOnly);
}

void VoxInlineSelector::setSteppingEnabled (const bool previousEnabled, const bool nextEnabled)
{
    previous.setEnabled (previousEnabled);
    next.setEnabled (nextEnabled);
}

void VoxInlineSelector::resized()
{
    auto area = getLocalBounds();

    if (form == Form::Header)
    {
        combo.setBounds (area);
        return;
    }

    const auto side = juce::jmin (area.getHeight(), juce::jmax (24, area.getWidth() / 8));
    previous.setBounds (area.removeFromLeft (side));
    area.removeFromLeft (stepperGap);
    next.setBounds (area.removeFromRight (side));
    area.removeFromRight (stepperGap);
    combo.setBounds (area);
}

} // namespace vox::ui
