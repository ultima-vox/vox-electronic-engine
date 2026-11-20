#include "DrumsLayoutState.h"

#include <cstdio>

using namespace vox::drums::layout;

int main()
{
    int failures = 0;
    const auto check = [&] (bool ok, const char* message) {
        if (! ok) { std::printf ("FAIL DLAY: %s\n", message); ++failures; }
    };
    const auto roundTrip = [&] (const Document& doc) {
        const auto bytes = encode (doc);
        if (! bytes) return false;
        const auto decoded = decode (*bytes);
        return decoded.status == Status::valid && decoded.document == doc && encode (decoded.document) == bytes;
    };
    const auto invalid = [&] (Document doc) { return ! encode (doc).has_value(); };
    for (int preset = 0; preset < 4; ++preset)
        check (roundTrip (factory (static_cast<Preset> (preset))), "factory layouts canonical, unique and bounded");
    auto doc = factory (Preset::beatmaking);
    check (roundTrip ({}), "empty v1 document is canonical; absent records use defaults");
    auto bytes = *encode (doc);
    for (std::size_t cut = 0; cut < bytes.size(); ++cut)
        check (decode (std::span (bytes).first (cut)).status == Status::invalid, "every truncation rejected");
    auto trailing = bytes;
    trailing.push_back (0);
    check (decode (trailing).status == Status::invalid, "trailing bytes rejected");
    auto unknown = doc;
    unknown.records.push_back ({ 250, { 0, 255, 42, 0 } });
    check (roundTrip (unknown), "unknown payload preserved exactly in its numeric position");
    // D1-a: record 2 is retired; UIST is the sole size record. A future
    // writer's record 2 stays opaque, never becomes a second size owner.
    unknown.records.insert (unknown.records.begin() + 1, { 2, { 1, 2, 3 } });
    check (roundTrip (unknown), "retired size type is opaque and preserved");
    auto duplicate = doc;
    duplicate.records.insert (duplicate.records.begin(), doc.records.front());
    check (invalid (duplicate), "duplicate types invalid, not normalised");
    auto descending = doc;
    std::swap (descending.records[0], descending.records[1]);
    check (invalid (descending), "out of order types invalid");
    auto tooMany = Document {};
    for (int type = 100; type < 133; ++type) tooMany.records.push_back ({ static_cast<Byte> (type), {} });
    check (invalid (tooMany), "33 records rejected");
    tooMany.records.pop_back();
    check (roundTrip (tooMany), "32 records accepted");
    Document maximum;
    for (int type = 100; type < 107; ++type) maximum.records.push_back ({ static_cast<Byte> (type), Bytes (256, 0xa5) });
    maximum.records.push_back ({ 107, Bytes (226, 0x5a) });
    check (encode (maximum)->size() == 2048 && roundTrip (maximum), "exact 2 KiB payload accepted");
    maximum.records.back().payload.push_back (0);
    check (invalid (maximum), "2 KiB plus one rejected");
    check (invalid ({ { { 100, Bytes (257, 1) } } }), "individual payload limit enforced");
    auto oversized = Bytes (2049, 0);
    oversized[0] = 2;
    check (decode (oversized).status == Status::future, "future version is opaque even beyond the current v1 payload bound");
    ProjectState newer;
    newer.restore (oversized);
    newer.commit (factory (Preset::composition));
    check (newer.status() == Status::future && newer.save() == oversized, "ordinary layout edits cannot overwrite larger future data");
    oversized.resize ((1u << 20) + 1);
    check (decode (oversized).status == Status::invalid, "opaque payload still obeys the VOXS hard input cap");
    for (const auto& future : { Bytes { 2, 0, 0, 0 }, Bytes { 255, 255, 255, 255, 31, 97, 0 } })
        check (decode (future).status == Status::future, "future payload not interpreted as v1");
    check (decode (Bytes { 0, 0, 0, 0, 0, 0 }).status == Status::invalid, "version zero rejected");

    const auto rejects = [&] (Byte type, Bytes payload, const char* reason) {
        Document one { { { type, std::move (payload) } } };
        check (invalid (one), reason);
    };
    rejects (1, { 4, 0 }, "workspace preset range");
    rejects (1, { 0, 2 }, "workspace modified boolean");
    rejects (1, { 0, 0, 0 }, "known record extra bytes");
    const Bytes zone { 208, 0, 0, 1, 232, 3, 0, 1, 1 };
    check (roundTrip ({ { { 3, zone } } }), "one zone valid");
    for (const auto index : { 0u, 2u, 3u, 4u, 6u, 7u, 8u })
    {
        auto bad = zone;
        bad[index] = index == 0 ? 1 : (index == 8 ? 16 : 255);
        rejects (3, bad, "zone width/boolean/count/sum/active/panel strictly checked");
    }
    auto samePanel = Document { { { 3, zone }, { 4, zone } } };
    check (invalid (samePanel), "panel unique across docks");
    rejects (5, { 2, 0, 244, 1, 16, 16, 255 }, "central editor unique");
    rejects (5, { 1, 0, 244, 1, 1, 255 }, "side panel cannot occupy centre");
    rejects (5, { 1, 2, 244, 1, 16, 255 }, "split enum range");
    rejects (5, { 1, 0, 149, 0, 16, 255 }, "ratio minimum");
    rejects (5, { 1, 0, 244, 1, 16, 1 }, "maximized refers to existing editor");
    rejects (6, { 3, 170, 0, 24, 1, 0 }, "Mixer state range");
    rejects (6, { 1, 119, 0, 24, 1, 0 }, "compact height minimum");
    rejects (6, { 1, 170, 0, 255, 0, 0 }, "expanded height minimum");
    rejects (6, { 1, 170, 0, 24, 1, 2 }, "Mixer bank range");
    rejects (7, { 2 }, "Pad Rack view range");
    rejects (8, { 3, 0, 1, 2 }, "parameter lane count bounded");
    rejects (8, { 1, 4 }, "parameter lane kind range");
    rejects (8, { 2, 0 }, "parameter lane length exact");
    for (const auto& pin : { Bytes { 0 }, Bytes { 1, 15 }, Bytes { 2, 4, 1, 3, 15, 0, 63 }, Bytes { 3, 63 } })
        check (roundTrip ({ { { 9, pin } } }), "pin boundaries accepted");
    for (const auto& pin : { Bytes { 4 }, Bytes { 0, 0 }, Bytes { 1, 16 }, Bytes { 2, 5, 1, 3, 15, 0, 63 },
                            Bytes { 2, 4, 2, 3, 15, 0, 63 }, Bytes { 2, 4, 1, 4, 15, 0, 63 },
                            Bytes { 2, 4, 1, 3, 16, 0, 63 }, Bytes { 2, 4, 1, 3, 15, 0, 64 },
                            Bytes { 2, 4, 1, 3, 15, 9, 8 }, Bytes { 3, 64 } })
        rejects (9, pin, "pin type, exact length and object ranges checked");
    check (roundTrip ({ { { 10, { 255 } }, { 11, { 15, 63, 3 } } } }), "all section bits and selection boundaries accepted");
    rejects (11, { 16, 63, 3 }, "selected pad range");
    rejects (11, { 15, 64, 3 }, "selected song bar range");
    rejects (11, { 15, 63, 4 }, "selected pattern bar range");
    // Decoder validates independently of the writer (malicious raw bytes).
    bytes[6] = 255;
    check (decode (bytes).status == Status::invalid, "wire order checked before exposing a document");
    ProjectState project;
    const auto original = project.save();
    check (decode (original).status == Status::valid, "no DLAY defaults to canonical Beatmaking");
    project.restore (bytes);
    check (project.status() == Status::invalid && project.save() == bytes
           && project.document() == factory (Preset::beatmaking), "invalid bytes preserved while displaying default");
    check (project.commit (factory (Preset::composition)) && project.status() == Status::valid
           && project.save() != bytes, "first committed edit replaces invalid bytes");
    const Bytes future { 2, 0, 0, 0, 91, 92, 93 };
    project.restore (future);
    check (project.commit (factory (Preset::soundDesign)) && project.status() == Status::future
           && project.document() == factory (Preset::soundDesign) && project.save() == future,
           "future layout edits are session-only");
    check (project.commit (factory (Preset::beatmaking), true) && project.status() == Status::valid
           && project.save() == original, "only explicitly confirmed Reset replaces future bytes");
    project.restore (*encode (unknown));
    auto edited = factory (Preset::mixing);
    check (project.commit (edited), "known layout edited with unknown records");
    const auto restoredUnknown = decode (project.save());
    check (restoredUnknown.status == Status::valid && restoredUnknown.document.records[1] == unknown.records[1]
           && restoredUnknown.document.records.back() == unknown.records.back(), "opaque records survive committed edits too");
    const auto before = project;
    check (! project.commit (duplicate) && project == before, "rejected commit leaves project state unchanged");
    auto docks = factory (Preset::beatmaking);
    const auto moved = movePanel (docks, 1, Type::right);
    check (moved && ! dock (*moved, Type::left) && dock (*moved, Type::right)->groups[0].tabs == Bytes { 3, 1 },
           "move the single Pad Rack to the right; empty left zone disappears");
    check (moved && roundTrip (*moved), "native move produces canonical DLAY");
    auto stacked = movePanel (*moved, 4, Type::right, 0, true);
    check (stacked && dock (*stacked, Type::right)->groups.size() == 2 && roundTrip (*stacked), "new dock group retains canonical height sum");
    stacked = movePanel (*stacked, 5, Type::right, 0, true);
    check (stacked && dock (*stacked, Type::right)->groups.size() == 3, "third dock group accepted");
    check (! movePanel (*stacked, 6, Type::right, 0, true), "fourth dock group refused transactionally");
    const auto shifted = movePanel (*stacked, 4, Type::right, 2);
    check (shifted && dock (*shifted, Type::right)->groups.size() == 2
           && dock (*shifted, Type::right)->groups[1].tabs == Bytes { 5, 4 }, "moving a sole tab keeps the original destination group identity after removal");
    const auto hidden = hidePanel (*stacked, 4);
    check (hidden && dock (*hidden, Type::right)->groups.size() == 2 && roundTrip (*hidden), "hide removes empty group and rebalances heights");
    auto one = dock (*hidden, Type::right);
    one->rail = true; one->width = 640;
    const auto rail = setDock (*hidden, Type::right, one);
    check (rail && dock (*rail, Type::right)->rail && dock (*rail, Type::right)->width == 640 && roundTrip (*rail), "rail preserves expanded width");
    check (! movePanel (docks, 16, Type::left) && ! movePanel (docks, 0, Type::right)
           && ! movePanel (docks, 1, Type::central) && ! hidePanel (docks, 16), "closed zone/panel contract enforced by UI actions");
    std::printf ("DLAY codec: %d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
