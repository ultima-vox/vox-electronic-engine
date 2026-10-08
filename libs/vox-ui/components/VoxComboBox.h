#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../Tokens.h"

namespace vox::ui {

// VOX combo box.
//
// The box paints itself: surface, border, value text and a vector chevron from
// the shared icon family (UI_PRODUCTION_IMPLEMENTATION_STANDARD.md section 9).
// It therefore never inherits a stock arrow, arrow colour or text inset from
// whichever LookAndFeel the host product happens to install, which is what let
// the previous build drift away from the accepted render.
class VoxComboBox : public juce::ComboBox
{
public:
    VoxComboBox();

    // A readout is a value display, not an affordance: the popup is suppressed
    // instead of silently opening an empty menu. Used by panel header readouts
    // until a real option list is bound.
    void setReadOnly (bool shouldBeReadOnly);
    [[nodiscard]] bool isReadOnly() const noexcept { return readOnly; }

    // Compact header geometry: smaller text, tighter corner, left aligned.
    void setCompact (bool shouldBeCompact);
    [[nodiscard]] bool isCompact() const noexcept { return compact; }

    void setCornerRadius (float radius);
    [[nodiscard]] float getCornerRadius() const noexcept { return cornerRadius; }

    // Drawn when no item is selected. On a read-only box this is the value text.
    void setPlaceholderText (juce::String text);
    [[nodiscard]] const juce::String& getPlaceholderText() const noexcept { return placeholder; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void lookAndFeelChanged() override;
    void showPopup() override;

private:
    [[nodiscard]] juce::Label* textLabel() const noexcept;
    [[nodiscard]] int chevronReserve() const noexcept;
    [[nodiscard]] int textInset() const noexcept;
    void applyTextPresentation();

    bool readOnly = false;
    bool compact = false;
    float cornerRadius = tokens::radius::small;
    juce::String placeholder;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoxComboBox)
};

} // namespace vox::ui
