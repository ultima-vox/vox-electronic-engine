#pragma once

// VOXS: the versioned, product-tagged state container shared by the three
// ULTIMA VOX MUSIC plugins (FOUND-00 §5.4). JUCE-free; never called from the
// audio thread (it allocates).
//
// Layout, all integers little-endian:
//   "VOXS" | u16 container (=1) | u16 reserved (=0) | char[4] product
//   | u32 schema | u32 sectionCount | sections... | u32 crc32(all before)
//   section: char[4] tag | u32 length | length bytes
//
// decode() validates everything before returning a document; a caller commits
// the result only when decode() succeeded, so a rejected blob never changes
// the live plugin.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace vox::preset {

using Tag = std::array<char, 4>;

constexpr Tag makeTag (std::string_view text) noexcept
{
    Tag tag {};
    for (std::size_t i = 0; i < tag.size() && i < text.size(); ++i)
        tag[i] = text[i];
    return tag;
}

inline constexpr std::uint16_t containerVersion = 1;
inline constexpr std::size_t maxStateBytes = 1u << 20;   // 1 MiB
inline constexpr std::uint32_t maxSections = 64;
inline constexpr std::size_t headerBytes = 4 + 2 + 2 + 4 + 4 + 4;
inline constexpr std::size_t checksumBytes = 4;

struct Section {
    Tag tag {};
    std::vector<std::byte> data;
};

struct StateDocument {
    Tag product {};
    std::uint32_t schema = 0;
    std::vector<Section> sections;

    const Section* find (Tag tag) const noexcept;
};

enum class StateError {
    none,
    tooShort,
    tooLarge,
    badMagic,
    unsupportedContainer,
    reservedNotZero,
    wrongProduct,
    unsupportedSchema,
    tooManySections,
    truncated,
    duplicateSection,
    trailingBytes,
    badChecksum,
};

std::string_view describe (StateError) noexcept;

struct DecodeResult {
    StateError error = StateError::none;
    StateDocument document;

    bool ok() const noexcept { return error == StateError::none; }
};

// Returns std::nullopt when the document breaks a limit (size, section count,
// duplicate tag); nothing is written in that case.
std::optional<std::vector<std::byte>> encode (const StateDocument&);

// Accepts only `expectedProduct` with 1 <= schema <= maxSchema.
DecodeResult decode (std::span<const std::byte> bytes, Tag expectedProduct,
                     std::uint32_t maxSchema);

std::uint32_t crc32 (std::span<const std::byte> bytes) noexcept;

} // namespace vox::preset
