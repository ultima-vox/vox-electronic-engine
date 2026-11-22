#pragma once

#include "DrumsLayoutState.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace vox::drums {

// A side zone owns up to three groups, never their active panel components.
class DrumsDockZone final : public juce::Component {
public:
    using Panel = std::function<juce::Component* (layout::Byte)>;
    using Height = std::function<int (layout::Byte)>;
    using Commit = std::function<void (const layout::Dock&)>;
    using Menu = std::function<void (unsigned, layout::Byte, juce::Component&)>;
    explicit DrumsDockZone (layout::Type side);
    ~DrumsDockZone() override;
    void configure (std::optional<layout::Dock>, Panel, Height, Commit, Menu);
    void clear();
    void resized() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    static const char* panelName (layout::Byte) noexcept;
    std::size_t groupCount() const noexcept;
    juce::TabbedButtonBar* tabs (std::size_t) noexcept;
private:
    struct Group;
    std::optional<layout::Dock> model_;
    std::vector<std::unique_ptr<Group>> groups_;
    juce::TextButton rail_;
    Commit commit_;
    std::uint64_t generation_ = 0;
    int dragging_ = -1;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumsDockZone)
};
} // namespace vox::drums
