#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Tokens.h"

namespace vox::ui {

class VoxComboBox : public juce::ComboBox
{
public:
    VoxComboBox()
    {
        setSize (120, tokens::size::controlHeight);
        setWantsKeyboardFocus (true);
    }
};

} // namespace vox::ui
