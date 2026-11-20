#include "WorkspaceModel.h"

#include <array>
#include <cstdio>

using namespace vox::drums::workspace;

int main()
{
    int failures = 0;
    const auto check = [&] (bool ok, const char* message) {
        if (! ok) { std::printf ("FAIL workspace: %s\n", message); ++failures; }
    };
    Selection selection;
    check (selection.selectPad (3) && selection.inspectorPad() == 3, "Inspector follows selection");
    selection.setPinned (true);
    check (selection.selectPad (12) && selection.pad() == 12 && selection.inspectorPad() == 3,
           "pin retains object identity while canonical selection changes");
    check (! selection.selectPad (-1) && ! selection.selectPad (16) && selection.pad() == 12,
           "invalid selection rejected without mutation");
    selection.setPinned (false);
    check (selection.inspectorPad() == 12, "unpin follows current selection immediately");
    for (const auto size : { std::array { 1040, 600 }, std::array { 1280, 620 },
                             std::array { 1440, 800 }, std::array { 1600, 900 } })
        for (int mixer = 0; mixer < 3; ++mixer)
            for (int split = 0; split < 3; ++split)
            {
                Layout settings;
                settings.mixer = static_cast<MixerSize> (mixer);
                settings.split = static_cast<Split> (split);
                const auto boxes = arrange (size[0], size[1], settings);
                const std::array regions { boxes.top, boxes.left, boxes.right, boxes.primary, boxes.secondary, boxes.mixer };
                for (std::size_t a = 0; a < regions.size(); ++a)
                {
                    check (regions[a].inside (size[0], size[1]), "all regions remain in content bounds");
                    for (std::size_t b = a + 1; b < regions.size(); ++b)
                        check (! regions[a].intersects (regions[b]), "regions do not overlap");
                }
                check (boxes.primary.w >= 380 && boxes.primary.h >= 160, "primary editor remains usable");
                check (boxes.left.w >= 160 && boxes.right.w >= 200, "docks keep readable minimum widths");
            }
    Layout extreme;
    extreme.leftWidth = -100;
    extreme.rightWidth = 90000;
    extreme.ratio = -1.0f;
    extreme.split = Split::horizontal;
    const auto narrow = arrange (1040, 600, extreme);
    check (narrow.primary.w >= 380 && narrow.secondary.empty(), "small windows use one editor, not clipped split panes");
    std::printf ("Workspace model: %d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
