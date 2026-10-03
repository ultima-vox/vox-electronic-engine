#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <vox-ui/VoxComponents.h>
#include "instrument/InstrumentContract.h"
#include <array>
#include <functional>
#include <vector>

namespace vstengine::ui {

// Explicit view model for one rack slot card.
//
// This is the ONLY contract the rack rendering is allowed to read. Previously
// the card reconstructed slot number, instrument name, channel and empty/occupied
// state by tokenising a display string built elsewhere, which made the UI parse
// its own model and silently lost fields that had no separator-safe token.
// Every field the card draws is now a named, typed member here.
struct RackSlotView {
    std::size_t slotIndex {};
    int displayNumber { 1 };
    juce::String instrumentName;
    juce::String vendorName;          // secondary descriptor line
    juce::String versionName;
    int midiChannel {};               // 0 == OFF
    bool routeEnabled {};
    bool selected {};
    bool enabled { true };
    bool muted {};
    bool soloed {};
    bool locked {};
    bool active {};                   // currently sounding
    float level { 1.0f };
    bool occupied {};                 // an instrument is loaded in this slot
    juce::String slotIdText;          // stable SlotId, shown for identity
    juce::Colour accent { juce::Colours::cyan };

    [[nodiscard]] juce::String routeText() const
    {
        return routeEnabled ? "CH" + juce::String(midiChannel) : juce::String("OFF");
    }

    // State must never be conveyed by colour alone, so the card keeps a text
    // representation for assistive/visual redundancy.
    [[nodiscard]] juce::String accessibilityText() const
    {
        juce::String text = "Slot " + juce::String(displayNumber) + ", ";
        text += occupied ? instrumentName : juce::String("Empty");
        text += ", " + routeText();
        if (selected) text += ", selected";
        if (! enabled) text += ", bypassed";
        if (muted) text += ", muted";
        if (soloed) text += ", soloed";
        if (locked) text += ", locked";
        if (active) text += ", active";
        return text;
    }
};

// One rack slot card.
//
// Selection is delivered through onSelected; the card never reads global state.
class RackSlotCard final : public vox::ui::VoxButton {
public:
    RackSlotCard();

    void setView (const RackSlotView& newView);
    [[nodiscard]] const RackSlotView& view() const noexcept { return current; }

    void paintButton (juce::Graphics&, bool isMouseOverButton, bool isButtonDown) override;

    std::function<void(std::size_t)> onSelected;

private:
    void drawThumbnail (juce::Graphics&, juce::Rectangle<int> bounds) const;
    void drawStatusStrip (juce::Graphics&, juce::Rectangle<float> bounds) const;

    RackSlotView current;
};

// The persistent left rack rail.
class InstrumentRack final : public juce::Component {
public:
    explicit InstrumentRack (std::size_t slotCount = vstengine::instrument::maxSlots);

    void paint (juce::Graphics&) override;
    void resized() override;

    void setViews (const std::vector<RackSlotView>& newViews);
    void setSelectedSlot (std::size_t);

    [[nodiscard]] std::size_t getSelectedSlot() const noexcept { return selectedSlot; }

    std::function<void(std::size_t)> onSelected;

private:
    std::array<RackSlotCard, vstengine::instrument::maxSlots> cards;
    juce::Label title { "INSTRUMENT RACK" };
    std::size_t slotCount {};
    std::size_t selectedSlot {};
};

} // namespace vstengine::ui