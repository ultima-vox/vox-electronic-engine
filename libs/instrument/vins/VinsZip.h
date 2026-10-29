#pragma once
// Strict, bounded reader and deterministic writer for the ZIP container of a
// .vins instrument package (#73). Controller thread only.
//
// Accepted subset (anything else is rejected, never guessed):
// - one disk; no ZIP64, encryption, patching or strong encryption;
// - methods 0 (stored) and 8 (deflate); general flags limited to bit 3
//   (data descriptor) and bit 11 (UTF-8 names);
// - the end-of-central-directory record ends the file (an archive comment is
//   allowed but bounded); the central directory immediately precedes it;
// - local entries are contiguous from offset 0 up to the central directory,
//   in central-directory order, with no gaps or overlaps, and each local
//   header agrees with its central record (name, method, flags, sizes, CRC);
// - every entry is decompressed into exactly its declared size (a bounded
//   read; a short or long stream is an error) and its CRC-32 is verified;
// - member names are portable relative paths: [A-Za-z0-9._-] segments
//   separated by '/', no '.', '..' or empty segments, no leading '/', no
//   backslash or drive letter, and unique ignoring ASCII case; directory
//   entries (trailing '/') and symbolic links are rejected.
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace vstengine::instrument::vins {

struct ZipLimits {
  std::size_t maxArchiveBytes = 64u << 20;    // whole .vins file
  std::size_t maxEntries = 256;
  std::size_t maxEntryBytes = 48u << 20;      // one uncompressed member
  std::size_t maxTotalBytes = 128u << 20;     // all uncompressed members
  std::size_t maxCompressionRatio = 200;      // uncompressed / compressed
  std::size_t maxNameBytes = 200;
};

struct ZipMember {
  std::string name;
  std::vector<std::uint8_t> data;
};

// True when `name` is an acceptable member name (see the header comment).
[[nodiscard]] bool validMemberName(const std::string &name, std::size_t maxBytes) noexcept;

// Parses and fully decompresses `archive`. On failure returns false with a
// reason and leaves `out` empty.
[[nodiscard]] bool readZip(std::span<const std::uint8_t> archive, const ZipLimits &limits,
                           std::vector<ZipMember> &out, std::string &why);

// Writes a deterministic archive of stored members (fixed timestamps, given
// order). Used by the package tool and tests; members must have valid names.
[[nodiscard]] std::vector<std::uint8_t> writeZip(const std::vector<ZipMember> &members);

[[nodiscard]] std::uint32_t crc32(std::span<const std::uint8_t> data) noexcept;

} // namespace vstengine::instrument::vins
