#include "instrument/vins/VinsZip.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <array>
#include <set>

namespace vstengine::instrument::vins {
namespace {
constexpr std::uint32_t localSignature = 0x04034b50u;
constexpr std::uint32_t centralSignature = 0x02014b50u;
constexpr std::uint32_t endSignature = 0x06054b50u;
constexpr std::uint32_t descriptorSignature = 0x08074b50u;
constexpr std::uint16_t flagDescriptor = 1u << 3;
constexpr std::uint16_t flagUtf8 = 1u << 11;
constexpr std::size_t localHeaderBytes = 30, centralHeaderBytes = 46, endRecordBytes = 22;

// Bounds-checked little-endian cursor over the archive bytes.
struct Reader {
  std::span<const std::uint8_t> bytes;
  bool has(std::size_t at, std::size_t count) const noexcept {
    return at <= bytes.size() && count <= bytes.size() - at;
  }
  std::uint16_t u16(std::size_t at) const noexcept {
    return static_cast<std::uint16_t>(bytes[at] | (bytes[at + 1] << 8));
  }
  std::uint32_t u32(std::size_t at) const noexcept {
    return static_cast<std::uint32_t>(bytes[at]) | (static_cast<std::uint32_t>(bytes[at + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[at + 2]) << 16) |
           (static_cast<std::uint32_t>(bytes[at + 3]) << 24);
  }
};

struct Central {
  std::uint16_t flags, method;
  std::uint32_t crc, compressed, uncompressed, localOffset;
  std::string name;
};

bool fail(std::string &why, std::string reason) {
  why = std::move(reason);
  return false;
}

bool hasZip64Extra(const Reader &r, std::size_t at, std::size_t length) {
  std::size_t pos = at;
  const std::size_t end = at + length;
  while (pos + 4 <= end) {
    const auto id = r.u16(pos);
    const auto size = r.u16(pos + 2);
    if (id == 0x0001)
      return true;
    pos += 4u + size;
  }
  return false;
}

std::string lowerAscii(std::string s) {
  for (auto &c : s)
    if (c >= 'A' && c <= 'Z')
      c = static_cast<char>(c - 'A' + 'a');
  return s;
}

bool inflate(std::span<const std::uint8_t> compressed, std::size_t size,
             std::vector<std::uint8_t> &out) {
  auto *source = new juce::MemoryInputStream(compressed.data(), compressed.size(), false);
  juce::GZIPDecompressorInputStream stream(source, true,
                                           juce::GZIPDecompressorInputStream::deflateFormat);
  out.assign(size, 0);
  std::size_t done = 0;
  while (done < size) {
    const auto chunk = static_cast<int>(std::min<std::size_t>(size - done, 1u << 16));
    const auto got = stream.read(out.data() + done, chunk);
    if (got <= 0)
      return false; // shorter than declared
    done += static_cast<std::size_t>(got);
  }
  std::uint8_t extra = 0;
  return stream.read(&extra, 1) <= 0; // longer than declared
}
} // namespace

std::uint32_t crc32(std::span<const std::uint8_t> data) noexcept {
  static const auto table = [] {
    std::array<std::uint32_t, 256> t{};
    for (std::uint32_t i = 0; i < 256; ++i) {
      std::uint32_t c = i;
      for (int k = 0; k < 8; ++k)
        c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      t[i] = c;
    }
    return t;
  }();
  std::uint32_t c = 0xFFFFFFFFu;
  for (const auto b : data)
    c = table[(c ^ b) & 0xFFu] ^ (c >> 8);
  return c ^ 0xFFFFFFFFu;
}

bool validMemberName(const std::string &name, std::size_t maxBytes) noexcept {
  if (name.empty() || name.size() > maxBytes || name.front() == '/' || name.back() == '/')
    return false;
  std::size_t segmentStart = 0;
  for (std::size_t i = 0; i <= name.size(); ++i) {
    if (i == name.size() || name[i] == '/') {
      const auto segment = std::string_view(name).substr(segmentStart, i - segmentStart);
      if (segment.empty() || segment == "." || segment == "..")
        return false;
      segmentStart = i + 1;
      continue;
    }
    const char c = name[i];
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                    c == '.' || c == '_' || c == '-';
    if (!ok)
      return false;
  }
  return true;
}

bool readZip(std::span<const std::uint8_t> archive, const ZipLimits &limits,
             std::vector<ZipMember> &out, std::string &why) {
  out.clear();
  const Reader r{archive};
  if (archive.size() > limits.maxArchiveBytes)
    return fail(why, "archive larger than " + std::to_string(limits.maxArchiveBytes) + " bytes");
  if (archive.size() < endRecordBytes)
    return fail(why, "not a ZIP archive (too short)");

  // End of central directory: the record (plus its comment) must end the file.
  std::size_t endAt = 0;
  bool foundEnd = false;
  const std::size_t lowest = archive.size() > endRecordBytes + 0xFFFFu
                                 ? archive.size() - endRecordBytes - 0xFFFFu
                                 : 0;
  for (std::size_t at = archive.size() - endRecordBytes + 1; at-- > lowest;) {
    if (r.u32(at) == endSignature && at + endRecordBytes + r.u16(at + 20) == archive.size()) {
      endAt = at;
      foundEnd = true;
      break;
    }
  }
  if (!foundEnd)
    return fail(why, "not a ZIP archive (no end-of-central-directory record at the end)");
  const auto diskNumber = r.u16(endAt + 4), cdDisk = r.u16(endAt + 6);
  const auto entriesOnDisk = r.u16(endAt + 8), entries = r.u16(endAt + 10);
  const auto cdSize = r.u32(endAt + 12), cdOffset = r.u32(endAt + 16);
  if (diskNumber != 0 || cdDisk != 0 || entriesOnDisk != entries)
    return fail(why, "multi-disk archives are not supported");
  if (entries == 0xFFFFu || cdSize == 0xFFFFFFFFu || cdOffset == 0xFFFFFFFFu)
    return fail(why, "ZIP64 archives are not supported");
  if (entries == 0)
    return fail(why, "empty archive");
  if (entries > limits.maxEntries)
    return fail(why, "more than " + std::to_string(limits.maxEntries) + " members");
  if (static_cast<std::size_t>(cdOffset) + cdSize != endAt)
    return fail(why, "central directory does not end at the end record");

  // Central directory.
  std::vector<Central> central;
  std::set<std::string> folded;
  std::size_t pos = cdOffset;
  std::size_t total = 0;
  for (std::uint32_t i = 0; i < entries; ++i) {
    if (!r.has(pos, centralHeaderBytes) || r.u32(pos) != centralSignature)
      return fail(why, "malformed central directory record " + std::to_string(i));
    Central c{};
    const auto madeBy = r.u16(pos + 4);
    c.flags = r.u16(pos + 8);
    c.method = r.u16(pos + 10);
    c.crc = r.u32(pos + 16);
    c.compressed = r.u32(pos + 20);
    c.uncompressed = r.u32(pos + 24);
    const auto nameLength = r.u16(pos + 28), extraLength = r.u16(pos + 30),
               commentLength = r.u16(pos + 32), diskStart = r.u16(pos + 34);
    const auto external = r.u32(pos + 38);
    c.localOffset = r.u32(pos + 42);
    const std::size_t recordBytes = centralHeaderBytes + nameLength + extraLength + commentLength;
    if (!r.has(pos, recordBytes) || pos + recordBytes > endAt)
      return fail(why, "central directory record " + std::to_string(i) + " overruns");
    if ((c.flags & ~(flagDescriptor | flagUtf8)) != 0)
      return fail(why, "member " + std::to_string(i) + ": encrypted or unsupported flags");
    if (c.method != 0 && c.method != 8)
      return fail(why, "member " + std::to_string(i) + ": unsupported compression method " +
                           std::to_string(c.method));
    if (diskStart != 0 || c.compressed == 0xFFFFFFFFu || c.uncompressed == 0xFFFFFFFFu ||
        c.localOffset == 0xFFFFFFFFu ||
        hasZip64Extra(r, pos + centralHeaderBytes + nameLength, extraLength))
      return fail(why, "member " + std::to_string(i) + ": ZIP64 is not supported");
    c.name.assign(reinterpret_cast<const char *>(archive.data() + pos + centralHeaderBytes),
                  nameLength);
    if (!validMemberName(c.name, limits.maxNameBytes))
      return fail(why, "invalid member name '" + c.name + "'");
    if ((madeBy >> 8) == 3 && ((external >> 16) & 0xF000u) == 0xA000u)
      return fail(why, "member '" + c.name + "' is a symbolic link");
    if (!folded.insert(lowerAscii(c.name)).second)
      return fail(why, "duplicate member '" + c.name + "'");
    if (c.uncompressed > limits.maxEntryBytes)
      return fail(why, "member '" + c.name + "' larger than " +
                           std::to_string(limits.maxEntryBytes) + " bytes");
    if (c.method == 0 && c.compressed != c.uncompressed)
      return fail(why, "member '" + c.name + "': stored sizes differ");
    if (c.uncompressed > (1u << 20) &&
        c.uncompressed / std::max<std::uint32_t>(c.compressed, 1) > limits.maxCompressionRatio)
      return fail(why, "member '" + c.name + "': compression ratio too high");
    total += c.uncompressed;
    if (total > limits.maxTotalBytes)
      return fail(why, "members larger than " + std::to_string(limits.maxTotalBytes) +
                           " bytes in total");
    central.push_back(std::move(c));
    pos += recordBytes;
  }
  if (pos != endAt)
    return fail(why, "central directory size does not match its records");

  // Local entries: contiguous from 0 to the central directory, in order.
  std::size_t expected = 0;
  std::vector<ZipMember> members;
  members.reserve(central.size());
  for (const auto &c : central) {
    if (c.localOffset != expected)
      return fail(why, "member '" + c.name + "': gap or overlap before its local header");
    if (!r.has(expected, localHeaderBytes) || r.u32(expected) != localSignature)
      return fail(why, "member '" + c.name + "': malformed local header");
    const auto flags = r.u16(expected + 6), method = r.u16(expected + 8);
    const auto nameLength = r.u16(expected + 26), extraLength = r.u16(expected + 28);
    const std::size_t dataAt = expected + localHeaderBytes + nameLength + extraLength;
    if (!r.has(expected, localHeaderBytes + nameLength + extraLength) || dataAt > cdOffset)
      return fail(why, "member '" + c.name + "': local header overruns");
    const std::string localName(
        reinterpret_cast<const char *>(archive.data() + expected + localHeaderBytes), nameLength);
    if (localName != c.name || flags != c.flags || method != c.method ||
        hasZip64Extra(r, expected + localHeaderBytes + nameLength, extraLength))
      return fail(why, "member '" + c.name + "': local header disagrees with the directory");
    if ((flags & flagDescriptor) == 0 &&
        (r.u32(expected + 14) != c.crc || r.u32(expected + 18) != c.compressed ||
         r.u32(expected + 22) != c.uncompressed))
      return fail(why, "member '" + c.name + "': local sizes or CRC disagree");
    if (static_cast<std::size_t>(cdOffset) - dataAt < c.compressed)
      return fail(why, "member '" + c.name + "': data overruns the central directory");
    std::size_t next = dataAt + c.compressed;
    if (flags & flagDescriptor) {
      std::size_t d = next;
      if (r.has(d, 4) && r.u32(d) == descriptorSignature)
        d += 4;
      if (!r.has(d, 12) || d + 12 > cdOffset || r.u32(d) != c.crc ||
          r.u32(d + 4) != c.compressed || r.u32(d + 8) != c.uncompressed)
        return fail(why, "member '" + c.name + "': data descriptor disagrees");
      next = d + 12;
    }
    const auto compressed = archive.subspan(dataAt, c.compressed);
    ZipMember member;
    member.name = c.name;
    if (c.method == 0) {
      member.data.assign(compressed.begin(), compressed.end());
    } else if (!inflate(compressed, c.uncompressed, member.data)) {
      return fail(why, "member '" + c.name + "': deflate data does not match its declared size");
    }
    if (crc32(member.data) != c.crc)
      return fail(why, "member '" + c.name + "': CRC-32 mismatch");
    members.push_back(std::move(member));
    expected = next;
  }
  if (expected != cdOffset)
    return fail(why, "unexpected bytes between the last member and the central directory");
  out = std::move(members);
  return true;
}

std::vector<std::uint8_t> writeZip(const std::vector<ZipMember> &members) {
  std::vector<std::uint8_t> z;
  auto u16 = [&](std::uint32_t v) {
    z.push_back(static_cast<std::uint8_t>(v));
    z.push_back(static_cast<std::uint8_t>(v >> 8));
  };
  auto u32 = [&](std::uint32_t v) {
    u16(v & 0xFFFFu);
    u16(v >> 16);
  };
  constexpr std::uint32_t dosDate = (0u << 9) | (1u << 5) | 1u; // 1980-01-01
  std::vector<std::uint32_t> offsets, crcs;
  for (const auto &m : members) {
    offsets.push_back(static_cast<std::uint32_t>(z.size()));
    crcs.push_back(crc32(m.data));
    u32(localSignature);
    u16(10);
    u16(flagUtf8);
    u16(0);
    u16(0);
    u16(dosDate);
    u32(crcs.back());
    u32(static_cast<std::uint32_t>(m.data.size()));
    u32(static_cast<std::uint32_t>(m.data.size()));
    u16(static_cast<std::uint32_t>(m.name.size()));
    u16(0);
    z.insert(z.end(), m.name.begin(), m.name.end());
    z.insert(z.end(), m.data.begin(), m.data.end());
  }
  const auto cdOffset = static_cast<std::uint32_t>(z.size());
  for (std::size_t i = 0; i < members.size(); ++i) {
    const auto &m = members[i];
    u32(centralSignature);
    u16(20);
    u16(10);
    u16(flagUtf8);
    u16(0);
    u16(0);
    u16(dosDate);
    u32(crcs[i]);
    u32(static_cast<std::uint32_t>(m.data.size()));
    u32(static_cast<std::uint32_t>(m.data.size()));
    u16(static_cast<std::uint32_t>(m.name.size()));
    u16(0);
    u16(0);
    u16(0);
    u16(0);
    u32(0);
    u32(offsets[i]);
    z.insert(z.end(), m.name.begin(), m.name.end());
  }
  const auto cdSize = static_cast<std::uint32_t>(z.size()) - cdOffset;
  u32(endSignature);
  u16(0);
  u16(0);
  u16(static_cast<std::uint32_t>(members.size()));
  u16(static_cast<std::uint32_t>(members.size()));
  u32(cdSize);
  u32(cdOffset);
  u16(0);
  return z;
}

} // namespace vstengine::instrument::vins
