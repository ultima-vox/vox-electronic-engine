#include "InstrumentRack.h"

#include "common/UiComponents.h"

namespace vstengine::ui {

namespace {

constexpr int cardHeight = 44;
constexpr int cardGap = 3;
constexpr int railPadding = 8;

} // namespace

RackSlotCard::RackSlotCard()
    : vox::ui::VoxButton({}, vox::ui::VoxButton::Type::Toggle)
{
    setClickingTogglesState(false);
    setWantsKeyboardFocus(true);
    onClick = [this] {
        if (onSelected != nullptr)
            onSelected(current.slotIndex);
    };
}

void RackSlotCard::setView (const RackSlotView& newView)
{
    current = newView;
    setToggleState(newView.selected, juce::dontSendNotification);
    setAccessible(true);
    setName(newView.accessibilityText());
    setTooltip(newView.accessibilityText());
    repaint();
}

void RackSlotCard::drawThumbnail (juce::Graphics& g,
                                  juce::Rectangle<int> bounds) const
{
    auto area = bounds.toFloat().reduced (2.0f);

    g.setColour (colours::background);
    g.fillRoundedRectangle (area, 4.0f);

    if (! current.occupied) {
        // Empty slots carry no decorative artwork and are visibly quieter.
        g.setColour (colours::mutedText.withAlpha (0.40f));
        g.drawLine (area.getCentreX() - 5.0f, area.getCentreY(),
                    area.getCentreX() + 5.0f, area.getCentreY(), 1.2f);
        g.drawLine (area.getCentreX(), area.getCentreY() - 5.0f,
                    area.getCentreX(), area.getCentreY() + 5.0f, 1.2f);
        g.setColour (colours::borderSubtle);
        g.drawRoundedRectangle (area, 4.0f, 1.0f);
        return;
    }

    // Identity thumbnail: a deterministic waveform derived from the stable
    // InstrumentId, so a given instrument always looks the same in the rack.
    juce::Path waveform;
    const auto plot = area.reduced (4.0f);
    const auto seed = static_cast<float>
        ((current.instrumentName.hashCode() & 0x0f) + 7);

    for (int i = 0; i <= 18; ++i) {
        const auto t = static_cast<float> (i) / 18.0f;
        const auto x = plot.getX() + plot.getWidth() * t;
        const auto y = plot.getCentreY()
            + std::sin (t * juce::MathConstants<float>::twoPi
                        * (1.2f + seed * 0.03f))
                  * plot.getHeight()
                      * (0.17f + 0.18f * std::sin (t * 4.7f + seed));
        if (i == 0)
            waveform.startNewSubPath (x, y);
        else
            waveform.lineTo (x, y);
    }

    g.setColour (current.accent.withAlpha (current.selected ? 0.96f : 0.58f));
    g.strokePath (waveform,
                  juce::PathStrokeType (1.5f, juce::PathStrokeType::curved,
                                        juce::PathStrokeType::rounded));
    g.setColour (current.selected ? current.accent.withAlpha (0.52f)
                                 : colours::borderSubtle);
    g.drawRoundedRectangle (area, 4.0f, 1.0f);
}

void RackSlotCard::drawStatusStrip (juce::Graphics& g,
                                    juce::Rectangle<float> bounds) const
{
    // Power / activity indicator. Reflects the structured `enabled`/`active`
    // fields, never a parsed token.
    const auto dot = bounds.withSizeKeepingCentre (3.0f, 3.0f);

    juce::Colour colour = colours::mutedText.withAlpha (0.35f);
    if (current.occupied) {
        colour = current.enabled
                   ? (current.active ? current.accent
                                     : current.accent.withAlpha (0.62f))
                   : colours::mutedText.withAlpha (0.45f);
    }
    g.setColour (colour);
    g.fillEllipse (dot);
}

void RackSlotCard::paintButton (juce::Graphics& g,
                                const bool isMouseOverButton,
                                const bool isButtonDown)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);

    // Empty slots must have significantly lower visual weight than occupied
    // slots (render map 4.2).
    auto fill = current.selected ? colours::panelRaised.brighter (0.08f)
                                 : colours::panel;
    if (! current.occupied)
        fill = colours::panel.darker (0.18f);
    if (isMouseOverButton)
        fill = fill.brighter (0.045f);
    if (isButtonDown)
        fill = fill.darker (0.08f);

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, 5.0f);
    g.setColour (current.selected ? colours::primary
                                 : colours::border);
    g.drawRoundedRectangle (bounds, 5.0f, current.selected ? 1.6f : 1.0f);

    if (current.selected) {
        g.setColour (colours::primary);
        g.fillRoundedRectangle (bounds.getX(), bounds.getY() + 4.0f, 3.0f,
                                bounds.getHeight() - 8.0f, 1.5f);
    }

    auto content = getLocalBounds().reduced (7, 4);
    auto indexArea = content.removeFromLeft (25);
    auto thumb = content.removeFromLeft (34);
    content.removeFromLeft (5);
    auto routeArea = content.removeFromRight (42);

    g.setColour (current.selected ? colours::text
                                  : colours::textSecondary);
    g.setFont (juce::Font (10.0f, juce::Font::bold));
    g.drawText (juce::String (current.displayNumber).paddedLeft ('0', 2),
                indexArea, juce::Justification::centred, false);

    drawThumbnail (g, thumb);

    // Name + secondary descriptor. Occupied slots show the resolved provider as
    // the descriptor line; empty slots show the call to action.
    auto nameArea = content;
    auto subArea = nameArea.removeFromBottom (13);

    g.setColour (! current.occupied ? colours::mutedText
                                   : colours::text);
    g.setFont (juce::Font (current.occupied ? 11.0f : 10.0f,
                           current.occupied ? juce::Font::bold
                                            : juce::Font::plain));
    g.drawText (current.occupied ? current.instrumentName : juce::String("Empty"),
                nameArea, juce::Justification::centredLeft, true);

    g.setColour (colours::mutedText);
    g.setFont (8.5f);
    juce::String descriptor = "ADD INSTRUMENT";
    if (current.occupied) {
        descriptor = current.vendorName;
        if (! current.versionName.isEmpty())
            descriptor += " " + current.versionName;
    }
    g.drawText (descriptor, subArea, juce::Justification::centredLeft, true);

    // Mute / solo / lock are textual, not colour-only.
    g.setColour (colours::textSecondary);
    g.setFont (9.0f);
    g.drawText (current.routeText(), routeArea, juce::Justification::centred,
                false);

    auto flags = routeArea.withTop (routeArea.getY() + 13)
                     .reduced (0, 0);
    if (current.muted || current.soloed || current.locked) {
        juce::String flagText;
        if (current.soloed) flagText += "S";
        if (current.muted) flagText += "M";
        if (current.locked) flagText += "L";
        g.setColour (current.soloed ? colours::accent
                        : current.muted ? colours::danger
                                        : colours::mutedText);
        g.setFont (8.5f);
        g.drawText (flagText, flags, juce::Justification::centred, false);
    }

    drawStatusStrip (g, juce::Rectangle<float> (bounds.getRight() - 9.0f,
                                                bounds.getY() + 6.0f, 3.0f, 3.0f));
}

InstrumentRack::InstrumentRack (const std::size_t slotCountToUse)
    : slotCount (std::min<std::size_t> (slotCountToUse, cards.size()))
{
    styleLabel (title, 12.0f, juce::Justification::centredLeft, colours::mutedText);
    addAndMakeVisible (title);

    for (std::size_t i = 0; i < cards.size(); ++i) {
        auto& card = cards[i];
        if (i < slotCount) {
            card.onSelected = [this] (const std::size_t slot) {
                if (onSelected != nullptr)
                    onSelected (slot);
            };
            addAndMakeVisible (card);
        } else {
            card.setVisible (false);
        }
    }
}

void InstrumentRack::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (colours::panel);
    g.fillRoundedRectangle (area, metrics::corner);
    g.setColour (colours::border);
    g.drawRoundedRectangle (area, metrics::corner, 1.0f);
}

void InstrumentRack::setViews (const std::vector<RackSlotView>& newViews)
{
    const auto count = std::min<std::size_t> (newViews.size(), slotCount);
    for (std::size_t i = 0; i < count; ++i) {
        auto view = newViews[i];
        view.slotIndex = i;
        cards[i].setView (view);
    }
}

void InstrumentRack::setSelectedSlot (const std::size_t slot)
{
    selectedSlot = std::min (slot, slotCount == 0 ? 0 : slotCount - 1);
}

void InstrumentRack::resized()
{
    auto area = getLocalBounds().reduced (railPadding);
    title.setBounds (area.removeFromTop (25));

    if (slotCount == 0)
        return;

    // Cards share the available height evenly, so the rail never overflows and
    // extra window height turns into breathing room rather than giant rows.
    const auto cardStride = area.getHeight() / static_cast<int> (slotCount);
    const auto height = std::max (cardHeight - cardGap,
                                  std::min (cardHeight + 14, cardStride - cardGap));

    for (std::size_t i = 0; i < slotCount; ++i)
        cards[i].setBounds (area.removeFromTop (height + cardGap));
}

} // namespace vstengine::ui