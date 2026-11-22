#include "DrumsLayoutState.h"
#include "engine/BarTiming.h"

#include <array>
#include <algorithm>

namespace vox::drums::layout {
namespace {
unsigned u16 (std::span<const Byte> b, std::size_t at) noexcept
{
    return static_cast<unsigned> (b[at]) | (static_cast<unsigned> (b[at + 1]) << 8);
}
bool between (unsigned value, unsigned low, unsigned high) noexcept { return value >= low && value <= high; }

bool validRecord (const Record& r, std::array<bool, 20>& panels)
{
    const auto p = std::span (r.payload);
    const auto size = p.size();
    const auto panel = [&] (Byte id, bool central) {
        if (! (central ? between (id, 16, 19) : between (id, 1, 6)) || panels[id]) return false;
        panels[id] = true;
        return true;
    };
    switch (static_cast<Type> (r.type))
    {
        case Type::workspace: return size == 2 && p[0] <= 3 && p[1] <= 1;
        case Type::left:
        case Type::right:
        {
            if (size < 4 || ! between (u16 (p, 0), 196, 640) || p[2] > 1 || ! between (p[3], 1, 3)) return false;
            unsigned sum = 0;
            std::size_t at = 4;
            for (unsigned group = 0; group < p[3]; ++group)
            {
                if (at + 4 > size) return false;
                sum += u16 (p, at);
                const auto active = p[at + 2], tabs = p[at + 3];
                at += 4;
                if (! between (tabs, 1, 6) || active >= tabs || at + tabs > size) return false;
                for (unsigned tab = 0; tab < tabs; ++tab)
                    if (! panel (p[at++], false)) return false;
            }
            return at == size && between (sum, 997, 1003);
        }
        case Type::central:
            if (size < 6 || ! between (p[0], 1, 2) || size != 5u + p[0]
                || p[1] > 1 || ! between (u16 (p, 2), 150, 850)
                || (p.back() != 255 && p.back() >= p[0])) return false;
            for (unsigned i = 0; i < p[0]; ++i)
                if (! panel (p[4 + i], true)) return false;
            return true;
        case Type::mixer:
            return size == 6 && p[0] <= 2 && between (u16 (p, 1), 120, 220)
                && between (u16 (p, 3), 280, 560) && p[5] <= 1;
        case Type::padRack: return size == 1 && p[0] <= 1;
        case Type::paramLanes:
            if (size < 1 || p[0] > 2 || size != 1u + p[0]) return false;
            for (std::size_t i = 1; i < size; ++i) if (p[i] > 3) return false;
            return true;
        case Type::inspectorPin:
            if (size == 0) return false;
            switch (p[0])
            {
                case 0: return size == 1;
                case 1: return size == 2 && p[1] < 16;
                case 2: return size == 7 && p[1] < 5 && p[2] < 2 && p[3] < 4 && p[4] < 16
                    && p[5] <= p[6] && p[6] < timing::maxSlotsPerBar;
                case 3: return size == 2 && p[1] < 64;
                default: return false;
            }
        case Type::soundSections: return size == 1;
        case Type::selection: return size == 3 && p[0] < 16 && p[1] < 64 && p[2] < 4;
        default: return true; // Opaque, including retired type 2.
    }
}

bool valid (const Document& d)
{
    if (d.records.size() > maxRecords) return false;
    int previous = -1;
    std::size_t size = 6;
    std::array<bool, 20> panels {};
    for (const auto& r : d.records)
    {
        if (r.type <= previous || r.payload.size() > maxRecordBytes) return false;
        size += 3 + r.payload.size();
        if (size > maxBytes || ! validRecord (r, panels)) return false;
        previous = r.type;
    }
    return true;
}
void append16 (Bytes& b, std::size_t value)
{
    b.push_back (static_cast<Byte> (value));
    b.push_back (static_cast<Byte> (value >> 8));
}
} // namespace

Result decode (std::span<const Byte> bytes)
{
    if (bytes.size() < 4 || bytes.size() > maxOpaqueBytes) return {};
    const auto version = static_cast<std::uint32_t> (bytes[0]) | (static_cast<std::uint32_t> (bytes[1]) << 8)
        | (static_cast<std::uint32_t> (bytes[2]) << 16) | (static_cast<std::uint32_t> (bytes[3]) << 24);
    if (version > 1) return { Status::future, {} };
    if (version != 1 || bytes.size() < 6 || bytes.size() > maxBytes) return {};
    const auto count = u16 (bytes, 4);
    if (count > maxRecords) return {};
    Document doc;
    doc.records.reserve (count);
    std::size_t at = 6;
    for (unsigned i = 0; i < count; ++i)
    {
        if (at + 3 > bytes.size()) return {};
        const auto type = bytes[at];
        const auto length = u16 (bytes, at + 1);
        at += 3;
        if (length > maxRecordBytes || at + length > bytes.size()) return {};
        doc.records.push_back ({ type, Bytes (bytes.begin() + static_cast<std::ptrdiff_t> (at),
                                             bytes.begin() + static_cast<std::ptrdiff_t> (at + length)) });
        at += length;
    }
    if (at != bytes.size() || ! valid (doc)) return {};
    return { Status::valid, std::move (doc) };
}

std::optional<Bytes> encode (const Document& doc)
{
    if (! valid (doc)) return std::nullopt;
    Bytes bytes { 1, 0, 0, 0 };
    append16 (bytes, doc.records.size());
    for (const auto& r : doc.records)
    {
        bytes.push_back (r.type);
        append16 (bytes, r.payload.size());
        bytes.insert (bytes.end(), r.payload.begin(), r.payload.end());
    }
    return bytes;
}

Document factory (Preset preset)
{
    if (static_cast<Byte> (preset) > 3) preset = Preset::beatmaking;
    Byte primary = 16, secondary = 17, count = 1, mixer = 1;
    if (preset == Preset::composition) { primary = 17; secondary = 16; count = 2; }
    if (preset == Preset::soundDesign) { primary = 18; secondary = 19; count = 2; }
    if (preset == Preset::mixing) mixer = 2;
    Bytes central { count, 0, 244, 1, primary };
    if (count == 2) central.push_back (secondary);
    central.push_back (255);
    return { { { 1, { static_cast<Byte> (preset), 0 } },
               { 3, { 208, 0, 0, 1, 232, 3, 0, 1, 1 } },
               { 4, { 220, 0, 0, 1, 232, 3, 0, 1, 3 } },
               { 5, std::move (central) }, { 6, { mixer, 170, 0, 24, 1, 0 } },
               { 7, { 0 } }, { 8, { 1, 0 } }, { 9, { 0 } }, { 10, { 0 } }, { 11, { 0, 0, 0 } } } };
}

const Record* find (const Document& doc, Type type) noexcept
{
    for (const auto& r : doc.records) if (r.type == static_cast<Byte> (type)) return &r;
    return nullptr;
}

std::optional<Dock> dock (const Document& document, Type side)
{
    if ((side != Type::left && side != Type::right) || ! valid (document)) return {};
    const auto* record = find (document, side);
    if (! record) return {};
    const auto& p = record->payload;
    Dock result { u16 (p, 0), p[2] != 0, {} };
    std::size_t at = 4;
    for (unsigned n = 0; n < p[3]; ++n)
    {
        DockGroup group { u16 (p, at), p[at + 2], {} };
        const auto tabs = p[at + 3]; at += 4;
        group.tabs.assign (p.begin() + static_cast<std::ptrdiff_t> (at), p.begin() + static_cast<std::ptrdiff_t> (at + tabs));
        at += tabs;
        result.groups.push_back (std::move (group));
    }
    return result;
}

std::optional<Document> setDock (const Document& document, Type side, const std::optional<Dock>& zone)
{
    if ((side != Type::left && side != Type::right) || ! valid (document)) return {};
    auto result = document;
    const auto type = static_cast<Byte> (side);
    std::erase_if (result.records, [=] (const auto& record) { return record.type == type; });
    if (zone)
    {
        if (! between (zone->width, 196, 640) || zone->groups.empty() || zone->groups.size() > 3) return {};
        Record record { type, {} };
        append16 (record.payload, zone->width);
        record.payload.push_back (static_cast<Byte> (zone->rail));
        record.payload.push_back (static_cast<Byte> (zone->groups.size()));
        for (const auto& group : zone->groups)
        {
            if (group.heightPermille > 1003 || group.tabs.empty() || group.tabs.size() > 6 || group.active >= group.tabs.size()) return {};
            append16 (record.payload, group.heightPermille);
            record.payload.push_back (group.active);
            record.payload.push_back (static_cast<Byte> (group.tabs.size()));
            record.payload.insert (record.payload.end(), group.tabs.begin(), group.tabs.end());
        }
        const auto at = std::lower_bound (result.records.begin(), result.records.end(), type,
                                         [] (const auto& r, Byte id) { return r.type < id; });
        result.records.insert (at, std::move (record));
    }
    return valid (result) ? std::optional { std::move (result) } : std::nullopt;
}

namespace {
void normaliseGroups (Dock& zone)
{
    unsigned sum = 0, remaining = 1000;
    for (const auto& group : zone.groups) sum += group.heightPermille;
    for (std::size_t n = 0; n < zone.groups.size(); ++n)
    {
        auto& height = zone.groups[n].heightPermille;
        const auto weight = sum ? height * 1000 / sum : 1000 / static_cast<unsigned> (zone.groups.size());
        height = n + 1 == zone.groups.size() ? remaining : std::min (remaining, weight);
        remaining -= height;
    }
}
}

std::optional<Document> hidePanel (const Document& document, Byte panel)
{
    if (! between (panel, 1, 6) || ! valid (document)) return {};
    auto result = document;
    for (const auto side : { Type::left, Type::right })
    {
        auto zone = dock (result, side);
        if (! zone) continue;
        for (auto& group : zone->groups)
        {
            const auto at = std::find (group.tabs.begin(), group.tabs.end(), panel);
            if (at == group.tabs.end()) continue;
            const auto index = static_cast<std::size_t> (at - group.tabs.begin());
            group.tabs.erase (at);
            if (index < group.active) --group.active;
            if (! group.tabs.empty()) group.active = static_cast<Byte> (std::min<std::size_t> (group.active, group.tabs.size() - 1));
        }
        std::erase_if (zone->groups, [] (const auto& group) { return group.tabs.empty(); });
        normaliseGroups (*zone);
        if (zone->groups.empty()) zone.reset();
        const auto changed = setDock (result, side, zone);
        if (! changed) return {};
        result = *changed;
    }
    return result;
}

std::optional<Document> movePanel (const Document& document, Byte panel, Type destination, unsigned group, bool newGroup)
{
    if (destination != Type::left && destination != Type::right) return {};
    const auto original = dock (document, destination);
    if (! newGroup && original)
    {
        if (group >= original->groups.size()) return {};
        auto zone = *original;
        auto& target = zone.groups[group];
        const auto present = std::find (target.tabs.begin(), target.tabs.end(), panel);
        if (present != target.tabs.end())
        {
            target.active = static_cast<Byte> (present - target.tabs.begin()); zone.rail = false;
            return setDock (document, destination, zone);
        }
        for (unsigned n = 0; n < group; ++n)
            if (zone.groups[n].tabs == Bytes { panel }) { --group; break; }
    }
    const auto hidden = hidePanel (document, panel);
    if (! hidden) return {};
    auto zone = dock (*hidden, destination);
    if (! zone)
    {
        const auto previous = dock (document, destination);
        zone = Dock { previous ? previous->width : (destination == Type::left ? 208u : 220u), false, {} };
    }
    if (newGroup || zone->groups.empty())
    {
        if (zone->groups.size() >= 3) return {};
        group = static_cast<unsigned> (zone->groups.size());
        zone->groups.push_back ({ zone->groups.empty() ? 1000u : 1000u / static_cast<unsigned> (zone->groups.size()), 0, {} });
        normaliseGroups (*zone);
    }
    if (group >= zone->groups.size() || zone->groups[group].tabs.size() >= 6) return {};
    auto& target = zone->groups[group];
    target.active = static_cast<Byte> (target.tabs.size()); target.tabs.push_back (panel);
    zone->rail = false; // Opening/moving a panel exposes its destination.
    return setDock (*hidden, destination, zone);
}

ProjectState::ProjectState() : document_ (factory (Preset::beatmaking)) {}

void ProjectState::restore (std::span<const Byte> bytes)
{
    auto result = decode (bytes);
    document_ = result.status == Status::valid ? std::move (result.document) : factory (Preset::beatmaking);
    if (bytes.size() <= maxOpaqueBytes) original_ = Bytes (bytes.begin(), bytes.end());
    else original_.reset(); // Cannot originate from an accepted VOXS container.
    status_ = result.status;
}

Bytes ProjectState::save() const
{
    return original_ ? *original_ : *encode (document_);
}

bool ProjectState::recoverCorruptV1()
{
    if (status_ != Status::invalid || ! original_ || original_->size() < 4
        || (*original_)[0] != 1 || (*original_)[1] != 0 || (*original_)[2] != 0 || (*original_)[3] != 0) return false;
    document_ = factory (Preset::beatmaking);
    document_.records.push_back ({ static_cast<Byte> (Type::recovery), { 1 } });
    original_.reset();
    status_ = Status::valid;
    return true;
}

bool ProjectState::wasRecovered() const noexcept
{
    const auto* record = find (document_, Type::recovery);
    return record && record->payload.size() == 1 && record->payload[0] == 1;
}

bool ProjectState::commit (Document doc, bool confirmedReset)
{
    if (! valid (doc)) return false;
    // An ordinary layout action cannot silently drop records this reader
    // does not understand. Known records may change; opaque records survive.
    for (const auto& record : document_.records)
    {
        if (record.type == 1 || between (record.type, 3, 11)) continue;
        if (record.type == static_cast<Byte> (Type::recovery) && confirmedReset && wasRecovered()) continue;
        const auto at = std::lower_bound (doc.records.begin(), doc.records.end(), record.type,
                                         [] (const Record& r, Byte type) { return r.type < type; });
        if (at != doc.records.end() && at->type == record.type) *at = record;
        else doc.records.insert (at, record);
    }
    if (! valid (doc)) return false; // Unknown payloads must still fit 2 KiB.
    document_ = std::move (doc);
    if (status_ != Status::future || confirmedReset)
    {
        original_.reset();
        status_ = Status::valid;
    }
    return true;
}

} // namespace vox::drums::layout
