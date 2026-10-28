#pragma once

#include <algorithm>
#include <cstdint>

namespace vstengine::acid {

// Sequence offsets can span the entire pattern domain; use wide arithmetic
// before clamping to the actual MIDI note range for display.
[[nodiscard]] inline int acidDisplayMidiNote(int root, int offset) noexcept {
    const auto note = static_cast<std::int64_t>(root) + static_cast<std::int64_t>(offset);
    return static_cast<int>(std::clamp(note, std::int64_t {0}, std::int64_t {127}));
}

} // namespace vstengine::acid
