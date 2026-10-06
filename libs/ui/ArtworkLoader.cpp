#include "ArtworkLoader.h"

#include <array>
#include <map>
#include <mutex>

namespace vstengine::ui::artwork {

namespace {

// File stems. A Role is a semantic slot; the stem is its file name, so renaming
// one is a one-line change here and nowhere else.
constexpr const char* stemFor (const Role role) noexcept
{
    switch (role)
    {
        case Role::heroPsyBass: return "hero-psy-bass";
        case Role::heroAcid:    return "hero-acid";
        case Role::heroGeneric: return "hero-generic";
        case Role::railFooter:  return "rail-footer";
        case Role::slotPsyBass: return "slot-psy-bass";
        case Role::slotAcid:    return "slot-acid";
        case Role::slotLead:    return "slot-lead";
        case Role::slotAtmos:   return "slot-atmos";
        case Role::slotEmpty:   return "slot-empty";
        case Role::logo:        return "logo";
    }

    return "";
}

// Extensions tried in order. PNG first because the reference renders are PNG and
// lossless; JPEG second because photographic artwork is often delivered that way.
constexpr std::array<const char*, 3> extensions { ".png", ".jpg", ".jpeg" };

// A cache entry is either a decoded image or a confirmed miss. `probed` is what
// makes a missing asset free: without it every repaint would stat the disk.
struct Entry {
    bool probed {};
    juce::Image image;
};

std::mutex& cacheMutex()
{
    static std::mutex mutex;
    return mutex;
}

std::map<int, Entry>& cache()
{
    static std::map<int, Entry> entries;
    return entries;
}

juce::File& overrideDirectory()
{
    static juce::File directory;
    return directory;
}

// Walks up from `start` looking for `_pr43-visual-gate/reference/artwork`.
// Bounded so a pathological path cannot spin.
juce::File walkUpForArtwork (juce::File start)
{
    for (int depth = 0; depth < 12 && start.isDirectory(); ++depth)
    {
        auto candidate = start.getChildFile ("_pr43-visual-gate")
                              .getChildFile ("reference")
                              .getChildFile ("artwork");

        if (candidate.isDirectory())
            return candidate;

        auto parent = start.getParentDirectory();

        if (parent == start)
            break;

        start = parent;
    }

    return {};
}

} // namespace

std::vector<juce::File> candidateDirectories()
{
    std::vector<juce::File> directories;

    const auto fromEnvironment = juce::SystemStats::getEnvironmentVariable (
        "VOX_UI_ARTWORK_DIR", {});

    if (fromEnvironment.isNotEmpty())
        directories.push_back (juce::File (fromEnvironment));

    if (auto fromWorkingDirectory = walkUpForArtwork (juce::File::getCurrentWorkingDirectory());
        fromWorkingDirectory != juce::File())
        directories.push_back (fromWorkingDirectory);

    if (auto fromSourceLocation = walkUpForArtwork (
            juce::File::getSpecialLocation (juce::File::currentExecutableFile)
                .getParentDirectory());
        fromSourceLocation != juce::File())
        directories.push_back (fromSourceLocation);

    return directories;
}

juce::File directory()
{
    if (const auto& forced = overrideDirectory(); forced != juce::File())
        return forced;

    for (const auto& candidate : candidateDirectories())
        if (candidate.isDirectory())
            return candidate;

    return {};
}

bool has (const Role role)
{
    return imageFor (role).isValid();
}

const juce::Image& imageFor (const Role role)
{
    const std::lock_guard<std::mutex> lock (cacheMutex());

    auto& entry = cache()[static_cast<int> (role)];

    if (entry.probed)
        return entry.image;

    entry.probed = true;

    const auto root = directory();

    if (! root.isDirectory())
        return entry.image;

    const juce::String stem (stemFor (role));

    for (const auto* extension : extensions)
    {
        const auto file = root.getChildFile (stem + extension);

        if (! file.existsAsFile())
            continue;

        if (auto decoded = juce::ImageFileFormat::loadFrom (file); decoded.isValid())
        {
            entry.image = std::move (decoded);
            break;
        }
    }

    return entry.image;
}

void drawFilled (juce::Graphics& g, const juce::Image& source,
                 const juce::Rectangle<float> target, const float cornerRadius)
{
    if (! source.isValid() || target.isEmpty())
        return;

    juce::Graphics::ScopedSaveState saved (g);

    // The rounded clip is what keeps a full-bleed band on the panel's corner
    // radius instead of squaring off the composition.
    if (cornerRadius > 0.0f)
    {
        juce::Path clip;
        clip.addRoundedRectangle (target, cornerRadius);
        g.reduceClipRegion (clip, {});
    }
    else
    {
        g.reduceClipRegion (target.toNearestInt());
    }

    const auto sourceAspect = static_cast<float> (source.getWidth())
                            / static_cast<float> (source.getHeight());
    const auto targetAspect = target.getWidth() / target.getHeight();

    auto scaled = target;

    if (sourceAspect > targetAspect)
    {
        // Source is wider: fill the height and let the width overflow.
        scaled.setWidth (target.getHeight() * sourceAspect);
        scaled.setX (target.getCentreX() - scaled.getWidth() * 0.5f);
    }
    else
    {
        scaled.setHeight (target.getWidth() / sourceAspect);
        scaled.setY (target.getCentreY() - scaled.getHeight() * 0.5f);
    }

    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (source, scaled);
}

void drawContrastOverlay (juce::Graphics& g, const juce::Rectangle<float> area,
                          const juce::Colour tint)
{
    if (area.isEmpty())
        return;

    juce::Graphics::ScopedSaveState saved (g);

    if (area.getWidth() > 0.0f)
    {
        juce::ColourGradient leftToRight (tint.withAlpha (0.86f), area.getX(), area.getCentreY(),
                                          tint.withAlpha (0.22f),
                                          area.getX() + area.getWidth() * 0.62f,
                                          area.getCentreY(), false);
        g.setGradientFill (leftToRight);
        g.fillRect (area);
    }

    juce::ColourGradient bottomUp (tint.withAlpha (0.30f), area.getCentreX(), area.getBottom(),
                                   tint.withAlpha (0.0f), area.getCentreX(),
                                   area.getY() + area.getHeight() * 0.35f, false);
    g.setGradientFill (bottomUp);
    g.fillRect (area);
}

void clearCache()
{
    const std::lock_guard<std::mutex> lock (cacheMutex());
    cache().clear();
}

void setDirectoryOverride (const juce::File& newDirectory)
{
    {
        const std::lock_guard<std::mutex> lock (cacheMutex());
        overrideDirectory() = newDirectory;
        cache().clear();
    }
}

} // namespace vstengine::ui::artwork
