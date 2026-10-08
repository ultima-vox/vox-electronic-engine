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
    juce::String vendorName;          // descriptor vendor, fallback descriptor line
    juce::String versionName;         // "<major>.<minor>"
    // The descriptor line the card actually draws, e.g. `Analog Drive 1.0`.
    //
    // It is resolved by the shell (see InstrumentIdentity.h) rather than derived
    // here, because the accepted render's per-instrument engine name has no home
    // in InstrumentContract yet. An empty value falls back to
    // `vendorName versionName`, which is what an unmapped instrument renders.
    juce::String descriptorLine;
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

    // The two-line-capable secondary descriptor. Never empty for an occupied slot.
    [[nodiscard]] juce::String secondaryText() const
    {
        if (! occupied)
            return {};

        if (descriptorLine.isNotEmpty())
            return descriptorLine;

        return versionName.isEmpty() ? vendorName : vendorName + " " + versionName;
    }

    // State must never be conveyed by colour alone, so the card keeps a text
    // representation for assistive/visual redundancy.
    [[nodiscard]] juce::String accessibilityText() const
    {
        juce::String text = "Slot " + juce::String(displayNumber) + ", ";
        text += occupied ? instrumentName : juce::String("Empty");
        text += ", " + routeText();
        if (occupied && secondaryText().isNotEmpty())
            text += ", " + secondaryText();
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
// The power affordance is a real child button, so enable/bypass is a real
// interaction rather than a painted 3 px dot.
class RackSlotCard final : public vox::ui::VoxButton {
public:
    RackSlotCard();

    void setView (const RackSlotView& newView);
    [[nodiscard]] const RackSlotView& view() const noexcept { return current; }

    void paintButton (juce::Graphics&, bool isMouseOverButton, bool isButtonDown) override;

    std::function<void(std::size_t)> onSelected;
    // Fired with the requested new enabled state. The shell applies it to the
    // authoritative slot state; the card never mutates the model itself.
    std::function<void(std::size_t, bool)> onEnableToggled;

private:
    void drawThumbnail (juce::Graphics&, juce::Rectangle<int> bounds) const;

    vox::ui::VoxIconButton powerGlyph { vox::ui::icons::Icon::power, true };
    RackSlotView current;
};

// The persistent left rack rail.
//
// Composition (accepted render):
//
//   INSTRUMENT RACK                      16 slots   [+]
//   [16 cards, 3 px apart]
//   [artwork panel]  SOUND / FOR A DIFFERENT / TOMORROW
class InstrumentRack final : public juce::Component {
public:
    explicit InstrumentRack (std::size_t slotCount = vstengine::instrument::maxSlots);

    void paint (juce::Graphics&) override;
    void resized() override;

    void setViews (const std::vector<RackSlotView>& newViews);
    void setSelectedSlot (std::size_t);

    [[nodiscard]] std::size_t getSelectedSlot() const noexcept { return selectedSlot; }

    std::function<void(std::size_t)> onSelected;
    // Forwarded from the cards. The rail does not own slot state.
    std::function<void(std::size_t, bool)> onEnableToggled;
    // The `+` affordance. Visual Gate A: the rack has no "create instrument"
    // operation yet, so the shell decides what, if anything, it does.
    std::function<void()> onAddRequested;

    // Reference geometry, measured from the accepted render.
    static constexpr int cardHeight = 44;
    static constexpr int cardGap = 3;
    static constexpr int railPadding = 8;
    static constexpr int headerHeight = 33;
    static constexpr int artworkPanelHeight = 155;

private:
    std::array<RackSlotCard, vstengine::instrument::maxSlots> cards;
    juce::Label title { "INSTRUMENT RACK" };
    juce::Label slotCountLabel { "16 slots" };
    vox::ui::VoxIconButton addButton { vox::ui::icons::Icon::plus };
    juce::Rectangle<int> artworkBounds;
    std::size_t slotCount {};
    std::size_t selectedSlot {};
};

} // namespace vstengine::ui
