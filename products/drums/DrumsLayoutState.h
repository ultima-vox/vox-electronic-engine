#pragma once

// DLAY v1, #130 §4 with the pre-release simplification in §14.
// Message/host thread data only; no Component, file or audio ownership.
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace vox::drums::layout {

using Byte = std::uint8_t;
using Bytes = std::vector<Byte>;
inline constexpr std::size_t maxBytes = 2048, maxRecords = 32, maxRecordBytes = 256;
// Newer versions are not parsed using v1's size/layout rules. The owning
// processor additionally budgets them beside future musical-state growth.
inline constexpr std::size_t maxOpaqueBytes = 1u << 20; // VOXS hard input cap
enum class Preset : Byte { beatmaking, composition, soundDesign, mixing };
enum class Type : Byte {
    workspace = 1,
    // 2 is retired by D1-a. UIST is the only editor-size owner.
    left = 3, right, central, mixer, padRack, paramLanes, inspectorPin, soundSections,
    selection // §14 adds SELECTION; appended as type 11, never reuses a type.
};
struct Record {
    Byte type = 0;
    Bytes payload;
    bool operator== (const Record&) const = default;
};
struct Document {
    std::vector<Record> records;
    bool operator== (const Document&) const = default;
};
enum class Status { valid, invalid, future };
struct Result {
    Status status = Status::invalid;
    Document document;
};

// Never allocates from an unchecked count or length. Invalid/future bytes
// remain owned by the caller for whole-section preservation on project save.
Result decode (std::span<const Byte>);
std::optional<Bytes> encode (const Document&);
Document factory (Preset);
const Record* find (const Document&, Type) noexcept;

struct DockGroup {
    unsigned heightPermille = 1000;
    Byte active = 0;
    Bytes tabs;
    bool operator== (const DockGroup&) const = default;
};
struct Dock {
    unsigned width = 208;
    bool rail = false;
    std::vector<DockGroup> groups;
    bool operator== (const Dock&) const = default;
};
std::optional<Dock> dock (const Document&, Type side);
std::optional<Document> setDock (const Document&, Type side, const std::optional<Dock>&);
// Transactional native docking actions. Closed panel IDs 1..6 only; the
// Mixer/central editors cannot accidentally be reparented into a side dock.
std::optional<Document> movePanel (const Document&, Byte panel, Type destination, unsigned group = 0, bool newGroup = false);
std::optional<Document> hidePanel (const Document&, Byte panel);

// Project ownership policy, independent of the native views and user files.
// The caller holds its state lock. Restores are already bounded by VOXS (1 MiB);
// oversized/invalid DLAY is preserved as-is until the first committed edit.
class ProjectState {
public:
    ProjectState();
    void restore (std::span<const Byte>);
    const Document& document() const noexcept { return document_; }
    Status status() const noexcept { return status_; }
    Bytes save() const;
    // Future data: edits affect this session only unless Reset was confirmed.
    // Returns false only for an invalid proposed document.
    bool commit (Document, bool confirmedReset = false);
    bool operator== (const ProjectState&) const = default;
private:
    Document document_;
    std::optional<Bytes> original_;
    Status status_ = Status::valid;
};

} // namespace vox::drums::layout
