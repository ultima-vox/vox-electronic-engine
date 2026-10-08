#pragma once

#include "VoxButton.h"
#include "VoxIcons.h"

namespace vox::ui {

// Square, icon-only button drawn from the shared vector icon family.
//
// This is deliberately a thin configuration of VoxButton rather than a second
// button implementation. VoxButton already owns the normal/hover/down/disabled/
// toggled state painting; VoxIconButton only pins the type to Icon, defaults to
// a square control-sized footprint and gives the glyph a name. Anything that
// needs extra state painting must be added to VoxButton so a text button and an
// icon button can never drift apart.
class VoxIconButton final : public VoxButton
{
public:
    explicit VoxIconButton (icons::Icon icon, bool isToggle = false);

    void setIcon (icons::Icon icon);
    icons::Icon getIcon() const noexcept { return glyph; }

    void setToggled (bool shouldBeToggled);
    bool isToggled() const noexcept { return getToggleState(); }

private:
    icons::Icon glyph;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoxIconButton)
};

} // namespace vox::ui
