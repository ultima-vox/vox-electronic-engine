#pragma once

// Message-thread UI policy only. No musical state, processor or JUCE ownership.
#include <algorithm>
#include <utility>

namespace vox::drums::workspace {

inline std::pair<int, int> fitToDisplay (int width, int height, int availableWidth, int availableHeight) noexcept
{
    return { std::max (1040, std::min (width, availableWidth)), std::max (700, std::min (height, availableHeight)) };
}

class Selection {
public:
    int pad() const noexcept { return pad_; }
    int inspectorPad() const noexcept { return pinned_ ? pinnedPad_ : pad_; }
    bool pinned() const noexcept { return pinned_; }
    bool selectPad (int pad) noexcept
    {
        if (pad < 0 || pad >= 16) return false;
        pad_ = pad;
        return true;
    }
    void setPinned (bool pinned) noexcept
    {
        if (pinned && ! pinned_) pinnedPad_ = pad_;
        pinned_ = pinned;
    }
private:
    int pad_ = 0, pinnedPad_ = 0;
    bool pinned_ = false;
};

enum class Split { single, horizontal, vertical };
enum class MixerSize { collapsed, compact, expanded };
enum class Editor { step, song, sound, sample };
enum class DockTool { inspector, generator, library, routing, diagnostics };

struct Layout {
    int leftWidth = 208, rightWidth = 220;
    bool leftVisible = true, rightVisible = true, leftRail = false, rightRail = false, centralVisible = true;
    int compactHeight = 170, expandedHeight = 280, maximized = 255;
    float ratio = 0.5f;
    Split split = Split::single;
    MixerSize mixer = MixerSize::compact;
    Editor primary = Editor::step, secondary = Editor::song;
    DockTool rightTool = DockTool::inspector;
    bool operator== (const Layout&) const = default;
};

struct Box {
    int x = 0, y = 0, w = 0, h = 0;
    bool empty() const noexcept { return w <= 0 || h <= 0; }
    bool inside (int width, int height) const noexcept
    {
        return empty() || (x >= 0 && y >= 0 && x + w <= width && y + h <= height);
    }
    bool intersects (Box b) const noexcept
    {
        return ! empty() && ! b.empty() && x < b.x + b.w && b.x < x + w && y < b.y + b.h && b.y < y + h;
    }
};

struct Regions { Box top, left, right, primary, secondary, mixer; };

inline Regions arrange (int width, int height, Layout layout) noexcept
{
    // The frame guarantees >=1040x600 content. Remain bounded during construction.
    if (width < 760 || height < 400) return { { 0, 0, width, height }, {}, {}, {}, {}, {} };
    int left = ! layout.leftVisible ? 0 : layout.leftRail ? 26 : std::clamp (layout.leftWidth, 196, 640);
    int right = ! layout.rightVisible ? 0 : layout.rightRail ? 26 : std::clamp (layout.rightWidth, 196, 640);
    const int leftMinimum = std::min (left, 196), rightMinimum = std::min (right, 196);
    const int extra = left + right - leftMinimum - rightMinimum;
    const int budget = width - 392 - leftMinimum - rightMinimum;
    if (extra > budget && extra > 0)
    {
        const int first = std::max (0, budget) * (left - leftMinimum) / extra;
        left = leftMinimum + first; right = rightMinimum + std::max (0, budget) - first;
    }
    const int requestedMixer = layout.mixer == MixerSize::collapsed ? 28
        : layout.mixer == MixerSize::compact ? std::clamp (layout.compactHeight, 120, 220) : std::clamp (layout.expandedHeight, 280, 560);
    const int mixer = std::min (requestedMixer, height - 212);
    const int bodyHeight = height - 40 - mixer - 12;
    Regions r { { 0, 0, width, 36 }, { 0, 40, left, bodyHeight },
                { width - right, 40, right, bodyHeight }, { left + 6, 40, width - left - right - 12, bodyHeight },
                {}, { 0, height - mixer, width, mixer } };
    if (! layout.centralVisible) { r.primary = {}; return r; }
    if (layout.maximized == 1 && layout.split != Split::single) { r.secondary = r.primary; r.primary = {}; return r; }
    if (layout.maximized == 0) return r;
    const float ratio = std::clamp (layout.ratio, 0.25f, 0.75f);
    if (layout.split == Split::horizontal && r.primary.w >= 766)
    {
        const int first = std::clamp (static_cast<int> (static_cast<float> (r.primary.w - 6) * ratio), 380, r.primary.w - 386);
        r.secondary = { r.primary.x + first + 6, r.primary.y, r.primary.w - first - 6, r.primary.h };
        r.primary.w = first;
    }
    else if (layout.split == Split::vertical && bodyHeight >= 326)
    {
        const int first = std::clamp (static_cast<int> (static_cast<float> (bodyHeight - 6) * ratio), 160, bodyHeight - 166);
        r.secondary = { r.primary.x, r.primary.y + first + 6, r.primary.w, bodyHeight - first - 6 };
        r.primary.h = first;
    }
    return r;
}

} // namespace vox::drums::workspace
