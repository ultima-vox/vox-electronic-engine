#include "MainNavigation.h"

namespace vstengine::ui {

MainNavigation::MainNavigation()
{
    tabs.setTabs ({
        { "sound", "Sound", true },
        { "pattern", "Pattern", true },
        { "routing", "Routing", true },
        { "zones", "Zones", true },
        { "macros", "Macros", true },
        { "advanced", "Advanced", true }
    });

    tabs.onTabChanged = [this] (int index)
    {
        if (synchronizingSelection)
            return;
        if (index < 0 || index >= static_cast<int> (Page::count))
            return;

        currentPage = static_cast<Page> (index);
        if (onPageChanged)
            onPageChanged (currentPage);
    };

    addAndMakeVisible (tabs);
    setCurrentPage (Page::sound);
}

void MainNavigation::setCurrentPage (Page page)
{
    if (page < Page::sound || page >= Page::count)
        return;

    currentPage = page;
    const auto index = static_cast<int> (page);
    synchronizingSelection = true;
    tabs.setSelectedIndex (index);
    synchronizingSelection = false;

    if (onPageChanged)
        onPageChanged (page);
}

void MainNavigation::resized()
{
    tabs.setBounds (getLocalBounds());
}

} // namespace vstengine::ui
