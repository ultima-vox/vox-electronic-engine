#pragma once

#include "vox-ui/components/VoxTabBar.h"
#include <functional>

namespace vstengine::ui {

class MainNavigation final : public juce::Component {
public:
    enum class Page {
        sound = 0, rack = sound, presets = sound,
        pattern = 1, sequence = pattern,
        routing = 2, zones = 3, macros = 4,
        advanced = 5, settings = advanced,
        count = 6
    };

    MainNavigation();
    void resized() override;
    void setCurrentPage (Page);
    [[nodiscard]] Page getCurrentPage() const noexcept { return currentPage; }
    std::function<void(Page)> onPageChanged;

private:
    vox::ui::VoxTabBar tabs;
    Page currentPage { Page::sound };
    bool synchronizingSelection {};
};

} // namespace vstengine::ui
