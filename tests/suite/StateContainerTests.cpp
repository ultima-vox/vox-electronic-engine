// vox_preset VOXS container: round trip, limits and every rejection path.
#include "StateContainer.h"

#include <cstdio>
#include <cstdlib>

using namespace vox::preset;

namespace {
int failures = 0;

void check (bool condition, const char* what)
{
    if (! condition)
    {
        std::printf ("FAIL: %s\n", what);
        ++failures;
    }
}

std::vector<std::byte> bytesOf (std::initializer_list<int> values)
{
    std::vector<std::byte> out;
    for (int v : values)
        out.push_back (static_cast<std::byte> (v));
    return out;
}

StateDocument sample()
{
    StateDocument document;
    document.product = makeTag ("VXDR");
    document.schema = 1;
    document.sections.push_back ({ makeTag ("UIST"), bytesOf ({ 1, 2, 3, 4, 5, 6, 7, 8 }) });
    document.sections.push_back ({ makeTag ("ZZZZ"), {} });
    return document;
}

void patchCrc (std::vector<std::byte>& blob)
{
    blob.resize (blob.size() - checksumBytes);
    const auto crc = crc32 (blob);
    for (int shift = 0; shift < 32; shift += 8)
        blob.push_back (static_cast<std::byte> ((crc >> shift) & 0xffu));
}
} // namespace

int main()
{
    // Known CRC-32 vector.
    const char text[] = "123456789";
    check (crc32 (std::as_bytes (std::span (text, 9))) == 0xcbf43926u, "crc32 reference value");

    const auto blob = encode (sample());
    check (blob.has_value(), "encode valid document");

    const auto decoded = decode (*blob, makeTag ("VXDR"), 1);
    check (decoded.ok(), "decode round trip");
    check (decoded.document.schema == 1, "schema survives");
    check (decoded.document.sections.size() == 2, "both sections survive");
    check (decoded.document.find (makeTag ("UIST")) != nullptr
               && decoded.document.find (makeTag ("UIST"))->data == sample().sections[0].data,
           "section payload survives");

    check (decode (*blob, makeTag ("VXFX"), 1).error == StateError::wrongProduct, "other product rejected");
    check (decode ({}, makeTag ("VXDR"), 1).error == StateError::tooShort, "empty rejected");

    auto bad = *blob;
    bad[0] = std::byte { 'X' };
    check (decode (bad, makeTag ("VXDR"), 1).error == StateError::badMagic, "bad magic rejected");

    bad = *blob;
    bad[10] ^= std::byte { 0x01 };   // inside the product tag
    check (decode (bad, makeTag ("VXDR"), 1).error == StateError::badChecksum, "flipped byte rejected");

    bad = *blob;
    bad.resize (bad.size() - 5);
    check (! decode (bad, makeTag ("VXDR"), 1).ok(), "truncated blob rejected");

    bad = *blob;
    bad[4] = std::byte { 2 };   // container version
    check (decode (bad, makeTag ("VXDR"), 1).error == StateError::unsupportedContainer, "newer container rejected");

    bad = *blob;
    bad[6] = std::byte { 1 };   // reserved
    check (decode (bad, makeTag ("VXDR"), 1).error == StateError::reservedNotZero, "reserved bits rejected");

    auto newer = sample();
    newer.schema = 2;
    check (decode (*encode (newer), makeTag ("VXDR"), 1).error == StateError::unsupportedSchema,
           "newer schema rejected");

    // A section length that runs past the end, with a valid checksum.
    bad = *blob;
    bad[headerBytes + 4] = std::byte { 0xff };
    patchCrc (bad);
    check (decode (bad, makeTag ("VXDR"), 1).error == StateError::truncated, "overlong section rejected");

    // Trailing garbage before the checksum.
    bad = *blob;
    bad.insert (bad.end() - static_cast<std::ptrdiff_t> (checksumBytes), std::byte { 0 });
    patchCrc (bad);
    check (decode (bad, makeTag ("VXDR"), 1).error == StateError::trailingBytes, "trailing bytes rejected");

    // Duplicate section tags: refused by encode and by decode.
    auto duplicate = sample();
    duplicate.sections[1].tag = makeTag ("UIST");
    check (! encode (duplicate).has_value(), "encode refuses duplicate tags");
    bad = *blob;
    for (std::size_t i = 0; i < 4; ++i)   // rename ZZZZ to UIST
        bad[headerBytes + 8 + 8 + i] = static_cast<std::byte> ("UIST"[i]);
    patchCrc (bad);
    check (decode (bad, makeTag ("VXDR"), 1).error == StateError::duplicateSection, "duplicate section rejected");

    // Size and count limits.
    auto big = sample();
    big.sections[1].data.resize (maxStateBytes);
    check (! encode (big).has_value(), "encode refuses > 1 MiB");
    std::vector<std::byte> huge (maxStateBytes + 1);
    check (decode (huge, makeTag ("VXDR"), 1).error == StateError::tooLarge, "decode refuses > 1 MiB");
    auto many = sample();
    many.sections.clear();
    for (std::uint32_t i = 0; i <= maxSections; ++i)
        many.sections.push_back ({ makeTag (std::string_view ("S000")), {} }),
        many.sections.back().tag = { 'S', static_cast<char> ('A' + i / 26 % 26), static_cast<char> ('A' + i % 26), 'X' };
    check (! encode (many).has_value(), "encode refuses > 64 sections");

    if (failures == 0)
        std::printf ("state container tests passed\n");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
