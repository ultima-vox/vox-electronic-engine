#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

#include "VoxIcons.h"

namespace vox::ui {

// The single module/panel header for the family.
//
// UI_PRODUCTION_IMPLEMENTATION_STANDARD.md section 11 defines the canonical
// panel header as: status/power | title | optional selector/action. This class
// implements exactly that strip and nothing else, so a product panel composes a
// header instead of painting its own chrome.
//
// Layout, measured from the accepted SOUND renders:
//
//   [leading glyph] TITLE (subtitle)      [control] [selector] [action] [power]
//
// * the leading slot is either an interactive power glyph or a vector status
//   icon;
// * `control` and `selector` are centred in the free space between the title
//   and the trailing slots, which is where the accepted render puts the
//   OSCILLATOR 1/2/3 segments and the FILTER "Low Pass 24 dB" selector;
// * `action` is flush right (the bordered "+" on AMP ENVELOPE and MATRIX);
// * the optional trailing power glyph sits at the far right.
class VoxSectionHeader : public juce::Component
{
public:
    enum class LeadingType { None, Power, Icon };

    explicit VoxSectionHeader (juce::String title = {});

    void setTitle (juce::String newTitle);
    void setSubtitle (juce::String text);

    // Colour used for the leading glyph. Defaults to the shared interaction
    // accent, which is what the accepted render uses for every module header
    // including the instrument with a different identity accent.
    void setAccentColour (juce::Colour colour);
    [[nodiscard]] juce::Colour getAccentColour() const noexcept { return accentColour; }

    void setPowerVisible (bool visible);
    void setPowerState (bool on);
    [[nodiscard]] bool isPowerOn() const noexcept { return powerOn; }

    void setIcon (std::unique_ptr<juce::Drawable> drawable);
    void setIcon (icons::Icon icon);
    void clearIcon();

    [[nodiscard]] LeadingType getLeadingType() const noexcept { return leading; }

    // Trailing slots. Each component keeps the width it had when it was
    // installed, so a layout pass can never feed its own output back in.
    void setControlComponent (juce::Component* component);
    void setSelectorComponent (juce::Component* component);
    void setActionComponent (juce::Component* component);

    // The accepted render also shows the power affordance at the right edge of
    // the FILTER and MODULATION headers. Both glyphs read the same state and
    // fire the same callback, so there is still exactly one power contract.
    void setTrailingPowerVisible (bool visible);

    // True when power state is on and the callback has not disabled it.
    std::function<void (bool)> onPowerToggled;

    [[nodiscard]] juce::Rectangle<int> getLeadingGlyphBounds() const noexcept { return leadingBounds; }
    [[nodiscard]] juce::Rectangle<int> getTrailingPowerGlyphBounds() const noexcept { return trailingPowerBounds; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    struct Slot
    {
        juce::Component* component = nullptr;
        int preferredWidth = 0;
    };

    static bool isSlotVisible (const Slot& slot) noexcept
    {
        return slot.component != nullptr && slot.component->isVisible();
    }

    void install (Slot& slot, juce::Component* component, int fallbackWidth);
    void togglePower();
    [[nodiscard]] juce::Rectangle<int> titleBounds() const;

    juce::String title;
    juce::String subtitle;
    juce::Colour accentColour;

    LeadingType leading = LeadingType::None;
    bool powerOn = true;
    std::unique_ptr<juce::Drawable> icon;
    icons::Icon vectorIcon { icons::Icon::gear };
    bool hasVectorIcon = false;

    bool trailingPowerVisible = false;

    Slot controlSlot;
    Slot selectorSlot;
    Slot actionSlot;

    juce::Rectangle<int> leadingBounds;
    juce::Rectangle<int> trailingPowerBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoxSectionHeader)
};

} // namespace vox::ui
