#pragma once

// Message-thread UI policy only. No musical state, processor or JUCE ownership.
#include <algorithm>

namespace vox::drums::workspace {

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

struct Layout {
    int leftWidth = 208, rightWidth = 220;
    float ratio = 0.5f;
    Split split = Split::single;
    MixerSize mixer = MixerSize::compact;
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
    const int left = std::clamp (layout.leftWidth, 160, 280);
    const int right = std::clamp (layout.rightWidth, 200, 300);
    const int mixer = layout.mixer == MixerSize::collapsed ? 28 : (layout.mixer == MixerSize::compact ? 170 : 240);
    const int bodyHeight = height - 40 - mixer - 12;
    Regions r { { 0, 0, width, 36 }, { 0, 40, left, bodyHeight },
                { width - right, 40, right, bodyHeight }, { left + 6, 40, width - left - right - 12, bodyHeight },
                {}, { 0, height - mixer, width, mixer } };
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
