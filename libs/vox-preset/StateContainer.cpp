#include "StateContainer.h"

#include <algorithm>

namespace vox::preset {
namespace {

constexpr Tag magic = makeTag ("VOXS");

void putU16 (std::vector<std::byte>& out, std::uint16_t value)
{
    out.push_back (static_cast<std::byte> (value & 0xffu));
    out.push_back (static_cast<std::byte> ((value >> 8) & 0xffu));
}

void putU32 (std::vector<std::byte>& out, std::uint32_t value)
{
    for (int shift = 0; shift < 32; shift += 8)
        out.push_back (static_cast<std::byte> ((value >> shift) & 0xffu));
}

void putTag (std::vector<std::byte>& out, Tag tag)
{
    for (char c : tag)
        out.push_back (static_cast<std::byte> (c));
}

class Reader {
public:
    explicit Reader (std::span<const std::byte> bytes) noexcept : bytes_ (bytes) {}

    std::size_t remaining() const noexcept { return bytes_.size() - position_; }

    bool u16 (std::uint16_t& value) noexcept
    {
        if (remaining() < 2)
            return false;
        value = static_cast<std::uint16_t> (std::to_integer<unsigned> (bytes_[position_])
                                            | (std::to_integer<unsigned> (bytes_[position_ + 1]) << 8));
        position_ += 2;
        return true;
    }

    bool u32 (std::uint32_t& value) noexcept
    {
        if (remaining() < 4)
            return false;
        value = 0;
        for (int i = 0; i < 4; ++i)
            value |= std::to_integer<std::uint32_t> (bytes_[position_ + static_cast<std::size_t> (i)]) << (8 * i);
        position_ += 4;
        return true;
    }

    bool tag (Tag& value) noexcept
    {
        if (remaining() < 4)
            return false;
        for (std::size_t i = 0; i < 4; ++i)
            value[i] = static_cast<char> (bytes_[position_ + i]);
        position_ += 4;
        return true;
    }

    bool take (std::size_t count, std::span<const std::byte>& out) noexcept
    {
        if (remaining() < count)
            return false;
        out = bytes_.subspan (position_, count);
        position_ += count;
        return true;
    }

private:
    std::span<const std::byte> bytes_;
    std::size_t position_ = 0;
};

} // namespace

const Section* StateDocument::find (Tag tag) const noexcept
{
    for (const auto& section : sections)
        if (section.tag == tag)
            return &section;
    return nullptr;
}

std::string_view describe (StateError error) noexcept
{
    switch (error)
    {
        case StateError::none:                 return "ok";
        case StateError::tooShort:             return "state is too short";
        case StateError::tooLarge:             return "state exceeds the 1 MiB limit";
        case StateError::badMagic:             return "not a VOX state (bad magic)";
        case StateError::unsupportedContainer: return "unsupported VOX state container version";
        case StateError::reservedNotZero:      return "reserved header field is not zero";
        case StateError::wrongProduct:         return "state belongs to a different product";
        case StateError::unsupportedSchema:    return "state schema is newer than this build or invalid";
        case StateError::tooManySections:      return "too many state sections";
        case StateError::truncated:            return "state is truncated";
        case StateError::duplicateSection:     return "duplicate state section";
        case StateError::trailingBytes:        return "unexpected bytes after the last section";
        case StateError::badChecksum:          return "state checksum mismatch";
    }
    return "unknown state error";
}

std::uint32_t crc32 (std::span<const std::byte> bytes) noexcept
{
    std::uint32_t crc = 0xffffffffu;
    for (auto b : bytes)
    {
        crc ^= std::to_integer<std::uint32_t> (b);
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

std::optional<std::vector<std::byte>> encode (const StateDocument& document)
{
    if (document.sections.size() > maxSections || document.schema == 0)
        return std::nullopt;

    std::size_t total = headerBytes + checksumBytes;
    for (std::size_t i = 0; i < document.sections.size(); ++i)
    {
        const auto& section = document.sections[i];
        for (std::size_t j = 0; j < i; ++j)
            if (document.sections[j].tag == section.tag)
                return std::nullopt;
        total += 8 + section.data.size();
        if (total > maxStateBytes)
            return std::nullopt;
    }

    std::vector<std::byte> out;
    out.reserve (total);
    putTag (out, magic);
    putU16 (out, containerVersion);
    putU16 (out, 0);
    putTag (out, document.product);
    putU32 (out, document.schema);
    putU32 (out, static_cast<std::uint32_t> (document.sections.size()));
    for (const auto& section : document.sections)
    {
        putTag (out, section.tag);
        putU32 (out, static_cast<std::uint32_t> (section.data.size()));
        out.insert (out.end(), section.data.begin(), section.data.end());
    }
    putU32 (out, crc32 (out));
    return out;
}

DecodeResult decode (std::span<const std::byte> bytes, Tag expectedProduct,
                     std::uint32_t maxSchema)
{
    DecodeResult result;
    const auto fail = [&result] (StateError error) {
        result.error = error;
        result.document = {};
        return result;
    };

    if (bytes.size() < headerBytes + checksumBytes)
        return fail (StateError::tooShort);
    if (bytes.size() > maxStateBytes)
        return fail (StateError::tooLarge);

    Reader header (bytes);
    Tag tag {};
    std::uint16_t container = 0, reserved = 0;
    std::uint32_t schema = 0, count = 0;
    header.tag (tag);
    if (tag != magic)
        return fail (StateError::badMagic);
    header.u16 (container);
    if (container != containerVersion)
        return fail (StateError::unsupportedContainer);
    header.u16 (reserved);
    if (reserved != 0)
        return fail (StateError::reservedNotZero);

    const auto body = bytes.first (bytes.size() - checksumBytes);
    Reader trailer (bytes.last (checksumBytes));
    std::uint32_t storedCrc = 0;
    trailer.u32 (storedCrc);
    if (storedCrc != crc32 (body))
        return fail (StateError::badChecksum);

    Reader reader (body);
    reader.tag (tag);
    reader.u16 (container);
    reader.u16 (reserved);
    reader.tag (result.document.product);
    if (result.document.product != expectedProduct)
        return fail (StateError::wrongProduct);
    reader.u32 (schema);
    if (schema == 0 || schema > maxSchema)
        return fail (StateError::unsupportedSchema);
    result.document.schema = schema;
    reader.u32 (count);
    if (count > maxSections)
        return fail (StateError::tooManySections);

    result.document.sections.reserve (count);
    for (std::uint32_t i = 0; i < count; ++i)
    {
        Section section;
        std::uint32_t length = 0;
        std::span<const std::byte> data;
        if (! reader.tag (section.tag) || ! reader.u32 (length) || ! reader.take (length, data))
            return fail (StateError::truncated);
        if (result.document.find (section.tag) != nullptr)
            return fail (StateError::duplicateSection);
        section.data.assign (data.begin(), data.end());
        result.document.sections.push_back (std::move (section));
    }
    if (reader.remaining() != 0)
        return fail (StateError::trailingBytes);
    return result;
}

} // namespace vox::preset
