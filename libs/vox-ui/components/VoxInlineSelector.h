#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "VoxButton.h"
#include "VoxComboBox.h"
#include "VoxIconButton.h"
#include "VoxIcons.h"

namespace vox::ui {

// Inline selector, in the two forms the accepted render uses.
//
//   Stepper  [<] [ value dropdown ] [>]   OSCILLATOR "Wavetable 01" row
//   Header   [ value dropdown ]           FILTER "Low Pass 24 dB" readout
//
// Both forms own a real VoxComboBox rather than a painted badge, so the
// affordance, the value and the hit area cannot disagree. The stepper arrows use
// the shared vector chevrons: the previous build drew the "<" and ">" header
// buttons as literal text, which UI_PRODUCTION_IMPLEMENTATION_STANDARD.md
// section 9 forbids.
class VoxInlineSelector : public juce::Component
{
public:
    enum class Form { Stepper, Header };

    explicit VoxInlineSelector (Form form = Form::Stepper);

    [[nodiscard]] Form getForm() const noexcept { return form; }

    void setItems (juce::StringArray items, int selectedIndex = 0);
    void setSelectedIndex (int index);
    [[nodiscard]] int getSelectedIndex() const noexcept;
    [[nodiscard]] juce::String getSelectedText() const;

    // The underlying dropdown, for callers that need item ids or attachments.
    [[nodiscard]] VoxComboBox& getComboBox() noexcept { return combo; }
    [[nodiscard]] const VoxComboBox& getComboBox() const noexcept { return combo; }

    // Drawn until an item is selected.
    void setPlaceholderText (juce::String text);

    // Header form only: a readout has no options to choose from yet, so the
    // popup is suppressed instead of opening an empty menu.
    void setReadOnly (bool shouldBeReadOnly);
    [[nodiscard]] bool isReadOnly() const noexcept { return combo.isReadOnly(); }

    void setSteppingEnabled (bool previousEnabled, bool nextEnabled);

    std::function<void (int)> onChanged;
    std::function<void()> onPrevious;
    std::function<void()> onNext;

    void resized() override;

private:
    Form form = Form::Stepper;
    VoxComboBox combo;
    VoxIconButton previous { icons::Icon::chevronLeft };
    VoxIconButton next { icons::Icon::chevronRight };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoxInlineSelector)
};

} // namespace vox::ui
