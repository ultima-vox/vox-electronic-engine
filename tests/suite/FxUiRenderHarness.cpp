// VOX FX UI render harness (TZ §13 evidence).
//
// Offscreen mode (default): renders the native FxPanel for every workspace at
// the acceptance resolutions and DPI scales and writes PNG evidence plus a
// manifest. No display server is required (component snapshot, no peer).
//
// Windowed mode ("--windowed"): opens a real native window and steps through
// the same matrix with pauses so a human can capture actual DAW-style
// screenshots on a machine with a display (Windows acceptance path; the
// offscreen matrix is explicitly preliminary evidence only).
#include "Processor.h"
#include "FxEditor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <cstdio>
#include <string>

namespace {
struct Size { int width, height; };
constexpr Size sizes[] { {1040,700}, {1280,720}, {1440,900}, {1440,1080}, {1600,1000} };
constexpr float dpiScales[] { 1.00f, 1.25f, 1.50f, 2.00f };
constexpr int workspaceCount = vox::fx::FxPanel::workspaceCount;

juce::Image render (vox::fx::FxPanel& panel, float scale)
{
    return panel.createComponentSnapshot (panel.getLocalBounds(), true, scale);
}

bool writePng (const juce::Image& image, const juce::File& file)
{
    juce::TemporaryFile temp (file);
    {
        juce::FileOutputStream stream (temp.getFile());
        if (!stream.openedOk()) return false;
        if (!juce::PNGImageFormat().writeImageToStream (image, stream)) return false;
        stream.flush();
    }
    return temp.overwriteTargetFileWithTemporary();
}
} // namespace

int main (int argc, char** argv)
{
    const bool windowed = argc > 1 && std::string (argv[1]) == "--windowed";
    // --program: capture an equivalent LOADED program state (reference
    // default "Cascade Rise": riser family, seed 2026, 4 s duration) instead
    // of the flat default curves. The seed program is loaded through the
    // editor's real "Load seed program" control.
    const bool program = argc > 1 && std::string (argv[1]) == "--program";
    const juce::File outDir = argc > 2
        ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[2])
        : juce::File::getCurrentWorkingDirectory().getChildFile ("ui-render");
    outDir.createDirectory();

    vox::fx::FxProcessor processor;
    auto panel = std::make_unique<vox::fx::FxPanel> (processor);
    if (program)
    {
        (void) processor.editGeneratorState ([] (auto& state) { state.seed = 2026; });
        panel->factorySelector().setSelectedId (1, juce::sendNotificationSync);
        // The harness does not pump the 25 Hz editor timer; sync the seed
        // chip directly so the capture shows the loaded program state.
        panel->seedSlider().setValue (processor.generatorState().seed, juce::dontSendNotification);
    }
    if (!windowed && !program)
    {
        panel->setSize (1440, 900);
        panel->setWorkspace (vox::fx::FxPanel::Workspace::perform);
        for (auto* child : panel->getChildren())
            if (auto* l = dynamic_cast<juce::Label*> (child))
                if (l->getY() < 60)
                    std::printf ("LBL y=%d x=%d w=%d vis=%d text='%s'\n",
                        l->getY(), l->getX(), l->getWidth(), (int) l->isVisible(),
                        l->getText().toRawUTF8());
        int n=0;
        for (auto* child : panel->getChildren()) { (void) child; ++n; }
        std::printf ("children=%d\n", n);
    }
    std::FILE* manifest = std::fopen (
        outDir.getChildFile ("manifest.tsv").getFullPathName().toRawUTF8(), "w");
    std::fprintf (manifest, "workspace\twidth\theight\tdpi_scale\tfile\tbytes\n");

    if (windowed)
    {
        // Real display path: human-operated capture. Requires a window
        // server; used for the owner's native screenshot pass.
        juce::DocumentWindow window ("VOX FX render harness", juce::Colours::black,
                                     juce::DocumentWindow::closeButton);
        window.setUsingNativeTitleBar (true);
        window.setContentNonOwned (panel.get(), false);
        window.setVisible (true);
        for (const auto& size : sizes)
            for (const float scale : dpiScales)
            {
                window.setSize (size.width + 16, size.height + 48);
                panel->setSize (size.width, size.height);
                for (int w = 0; w < workspaceCount; ++w)
                {
                    panel->setWorkspace (static_cast<vox::fx::FxPanel::Workspace> (w));
                    std::printf ("size %dx%d scale %.2f workspace %d — capture now, Enter for next\n",
                                 size.width, size.height, scale, w);
                    std::fflush (stdout);
                    std::getchar();
                }
            }
        std::fclose (manifest);
        return 0;
    }

    int failures = 0, written = 0;
    for (const auto& size : sizes)
    {
        panel->setSize (size.width, size.height);
        for (const float scale : dpiScales)
        {
            for (int w = 0; w < workspaceCount; ++w)
            {
                panel->setWorkspace (static_cast<vox::fx::FxPanel::Workspace> (w));
                const auto name = juce::String ("ws") + juce::String (w) + "_"
                    + juce::String (size.width) + "x" + juce::String (size.height)
                    + "_dpi" + juce::String (std::lround (scale * 100));
                const auto target = outDir.getChildFile (name + ".png");
                const auto image = render (*panel, scale);
                if (image.isNull() || image.getWidth() == 0 ||
                    !writePng (image, target))
                { ++failures; std::printf ("FAIL %s\n", name.toRawUTF8()); continue; }
                ++written;
                std::fprintf (manifest, "%d\t%d\t%d\t%.2f\t%s.png\t%lld\n", w,
                              size.width, size.height, scale, name.toRawUTF8(),
                              static_cast<long long> (target.getSize()));
            }
        }
    }
    std::fclose (manifest);
    std::printf ("rendered %d images, %d failure(s) -> %s\n", written, failures,
                 outDir.getFullPathName().toRawUTF8());
    return failures == 0 ? 0 : 1;
}
