#include "PatternStore.h"

#include <algorithm>
#include <cstring>
#include <memory>

namespace vox::drums {

//==============================================================================
namespace gridcodec {
void putU8 (std::vector<std::byte>& out, std::uint8_t v)
{
    out.push_back (static_cast<std::byte> (v));
}

void putU16 (std::vector<std::byte>& out, std::uint16_t v)
{
    putU8 (out, static_cast<std::uint8_t> (v & 0xffu));
    putU8 (out, static_cast<std::uint8_t> (v >> 8));
}

void putU32 (std::vector<std::byte>& out, std::uint32_t v)
{
    for (int s = 0; s < 32; s += 8)
        out.push_back (static_cast<std::byte> ((v >> s) & 0xffu));
}

std::uint8_t Reader::u8()
{
    if (at >= data.size())
    {
        ok = false;
        return 0;
    }
    return std::to_integer<std::uint8_t> (data[at++]);
}

std::uint16_t Reader::u16()
{
    const auto lo = u8();
    return static_cast<std::uint16_t> (lo | (u8() << 8));
}

std::uint32_t Reader::u32()
{
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i)
        v |= static_cast<std::uint32_t> (u8()) << (8 * i);
    return v;
}

void putGrid (std::vector<std::byte>& out, timing::TimeSignature signature, timing::Resolution resolution)
{
    putU8 (out, static_cast<std::uint8_t> (signature.numerator));
    putU8 (out, static_cast<std::uint8_t> (signature.denominator));
    putU8 (out, static_cast<std::uint8_t> (resolution));
}

bool getGrid (Reader& in, timing::TimeSignature& signature, timing::Resolution& resolution)
{
    signature.numerator = in.u8();
    signature.denominator = in.u8();
    const auto r = in.u8();
    if (! in.ok || r >= timing::resolutionCount || ! timing::supported (signature))
        return false;
    resolution = static_cast<timing::Resolution> (r);
    return timing::capacity (signature, resolution).availability != timing::Availability::unavailable;
}

std::uint32_t packCell (const kit::PatternStep& c) noexcept
{
    // Values outside the fields (never in a valid pattern) are clamped.
    const auto ratchet = static_cast<std::uint32_t> (std::clamp<int> (c.ratchet, 1, kit::maxRatchet) - 1);
    const auto velocity = static_cast<std::uint32_t> (std::min<int> (c.velocity, 127));
    const auto probability = static_cast<std::uint32_t> (std::min<int> (c.probability, 127));
    const auto micro = static_cast<std::uint32_t> (std::clamp<int> (c.microTicks, -256, 255)) & 0x1ffu;
    return (c.on ? 1u : 0u) | (c.accent ? 2u : 0u) | (c.locked ? 4u : 0u) | ratchet << 3 | velocity << 6 | probability << 13 | micro << 20;
}

bool unpackCell (std::uint32_t w, kit::PatternStep& c) noexcept
{
    if ((w >> 29) != 0)
        return false;   // the three reserved bits must be zero
    c.on = (w & 1u) != 0;
    c.accent = (w & 2u) != 0;
    c.locked = (w & 4u) != 0;
    c.ratchet = static_cast<std::uint8_t> (((w >> 3) & 7u) + 1u);
    c.velocity = static_cast<std::uint8_t> ((w >> 6) & 0x7fu);
    c.probability = static_cast<std::uint8_t> ((w >> 13) & 0x7fu);
    const auto micro = static_cast<int> ((w >> 20) & 0x1ffu);
    c.microTicks = static_cast<std::int16_t> (micro >= 256 ? micro - 512 : micro);   // 9-bit two's complement
    return true;
}

void putCell (std::vector<std::byte>& out, const kit::PatternStep& c)
{
    putU32 (out, packCell (c));
}

bool getCell (Reader& in, kit::PatternStep& c)
{
    const auto w = in.u32();
    return in.ok && unpackCell (w, c);
}
} // namespace gridcodec

namespace {

using gridcodec::putU32;
using gridcodec::putU8;
using gridcodec::Reader;

// DSEQ version 4: the first public layout (PR 137 §10.5). Versions 1-3 were
// development builds and are not decoded (#136 policy).
constexpr std::uint32_t sectionVersion = 4;

std::string where (const char* what, int lane, int bar, int slot)
{
    return std::string (what) + " (lane " + std::to_string (lane + 1) + ", bar " + std::to_string (bar + 1) + ", slot "
         + std::to_string (slot) + ")";
}

} // namespace

bool validLaneBar (const timing::BarTiming& t, const kit::PatternStep* slots, std::string* why)
{
    const auto fail = [&] (const char* what, int slot) {
        if (why != nullptr)
            *why = std::string (what) + " (slot " + std::to_string (slot) + ")";
        return false;
    };
    if (! t.valid())
        return fail ("invalid grid or subdivision mask", -1);
    const int per = t.slotsPerBeat();
    for (int s = 0; s < kit::slotStride; ++s)
    {
        const auto& c = slots[s];
        if (! t.slotActive (s))
        {
            if (! (c == kit::PatternStep {}))
                return fail ("content in an unused slot", s);
            continue;
        }
        if (c.velocity > 127 || c.probability < 1 || c.probability > 100 || c.ratchet < 1 || c.ratchet > kit::maxRatchet)
            return fail ("value out of range", s);
        const int cellTicks = t.cellTicks (s / per);
        if (2 * std::abs (static_cast<int> (c.microTicks)) > cellTicks)
            return fail ("microtiming beyond half a cell", s);
        if (! timing::subhitsDistinct (cellTicks, c.ratchet))
            return fail ("ratchet subhits collide", s);
    }
    return true;
}

kit::Pattern StoredPattern::view() const noexcept
{
    kit::Pattern p;
    p.lanes = kit::padCount;
    p.bars = bars;
    p.signature = signature;
    p.resolution = resolution;
    p.cells = cells.data();
    p.masks = masks.data();
    p.seed = seed;
    return p;
}

void StoredPattern::setBars (int newBars) noexcept
{
    newBars = std::clamp (newBars, 1, kit::maxPatternBars);
    for (int lane = 0; lane < kit::padCount; ++lane)
        for (int bar = 0; bar < kit::maxPatternBars; ++bar)
        {
            if (bar < std::min<int> (bars, newBars))
                continue;
            for (int slot = 0; slot < kit::slotStride; ++slot)
                cell (lane, bar, slot) = bar < newBars ? cell (lane, 0, slot) : kit::PatternStep {};
            mask (lane, bar) = bar < newBars ? mask (lane, 0) : std::uint16_t { 0 };
        }
    bars = static_cast<std::uint8_t> (newBars);
}

bool StoredPattern::valid (std::string* why) const
{
    if (bars < 1 || bars > kit::maxPatternBars || ! timing::BarTiming { signature, resolution, 0 }.valid())
    {
        if (why != nullptr)
            *why = "invalid grid or bar count";
        return false;
    }
    for (int lane = 0; lane < kit::padCount; ++lane)
        for (int bar = 0; bar < kit::maxPatternBars; ++bar)
        {
            const auto* slots = &cell (lane, bar, 0);
            if (bar >= bars)
            {
                // Bars past the count are empty.
                for (int s = 0; s < kit::slotStride; ++s)
                    if (! (slots[s] == kit::PatternStep {}))
                    {
                        if (why != nullptr)
                            *why = where ("content past the last bar", lane, bar, s);
                        return false;
                    }
                if (mask (lane, bar) != 0)
                {
                    if (why != nullptr)
                        *why = where ("subdivision mask past the last bar", lane, bar, -1);
                    return false;
                }
                continue;
            }
            std::string reason;
            if (! validLaneBar (timing (lane, bar), slots, &reason))
            {
                if (why != nullptr)
                    *why = "lane " + std::to_string (lane + 1) + ", bar " + std::to_string (bar + 1) + ": " + reason;
                return false;
            }
        }
    return true;
}

namespace {
const char* switchReason (timing::SwitchResult r) noexcept
{
    switch (r)
    {
        case timing::SwitchResult::ok: return "";
        case timing::SwitchResult::invalidTiming: return "the grid does not allow this";
        case timing::SwitchResult::collision: return "two events would share a cell";
        case timing::SwitchResult::offsetTooLarge: return "an event would be more than half a cell from its cell";
        case timing::SwitchResult::reservedSlotNotEmpty: return "content in a reserved cell";
    }
    return "";
}

// Moves one lane's bar of `p` from `from` to `to` (the full cell is the
// attribute that travels with each event). False with `why` on rejection;
// `p` is then partly changed (callers work on a copy).
template <typename Move>
bool moveLaneBar (StoredPattern& p, int lane, int bar, const timing::BarTiming& from, const timing::BarTiming& to, Move move,
                  std::string* why)
{
    timing::LaneBar events {};
    std::array<kit::PatternStep, kit::slotStride> attributes {};
    for (int s = 0; s < kit::slotStride; ++s)
    {
        attributes[static_cast<std::size_t> (s)] = p.cell (lane, bar, s);
        events[static_cast<std::size_t> (s)] = { attributes[static_cast<std::size_t> (s)].on, attributes[static_cast<std::size_t> (s)].microTicks };
    }
    const auto outcome = move (from, to, events, &attributes);
    if (outcome.result != timing::SwitchResult::ok)
    {
        if (why != nullptr)
            *why = std::string (switchReason (outcome.result)) + " (lane " + std::to_string (lane + 1) + ", bar " + std::to_string (bar + 1)
                 + (outcome.beat >= 0 ? ", beat " + std::to_string (outcome.beat + 1) + ", cell " + std::to_string (outcome.cell + 1) : std::string())
                 + ")";
        return false;
    }
    for (int s = 0; s < kit::slotStride; ++s)
    {
        auto c = events[static_cast<std::size_t> (s)].on ? attributes[static_cast<std::size_t> (s)] : kit::PatternStep {};
        c.microTicks = events[static_cast<std::size_t> (s)].microTicks;
        p.cell (lane, bar, s) = c;
    }
    p.mask (lane, bar) = to.alternativeBeats;
    return true;
}
} // namespace

bool StoredPattern::convertGrid (timing::TimeSignature newSignature, timing::Resolution newResolution, std::string* why)
{
    auto next = std::make_unique<StoredPattern> (*this);   // heap: ~33 KB; committed only when every bar converts
    next->signature = newSignature;
    next->resolution = newResolution;
    for (int lane = 0; lane < kit::padCount; ++lane)
        for (int bar = 0; bar < bars; ++bar)
        {
            const timing::BarTiming to { newSignature, newResolution, mask (lane, bar) };
            if (! moveLaneBar (*next, lane, bar, timing (lane, bar), to,
                               [] (const auto& f, const auto& t, auto& e, auto* x) { return timing::convertBar (f, t, e, x); }, why))
                return false;
        }
    if (! next->valid (why))
        return false;
    *this = *next;
    return true;
}

bool StoredPattern::switchBeats (int lane, int bar, std::uint16_t beats, std::string* why)
{
    if (lane < 0 || lane >= kit::padCount || bar < 0 || bar >= this->bars)
        return false;
    auto next = std::make_unique<StoredPattern> (*this);
    const auto from = timing (lane, bar);
    const timing::BarTiming to { signature, resolution, static_cast<std::uint16_t> (from.alternativeBeats ^ beats) };
    if (! moveLaneBar (*next, lane, bar, from, to,
                       [] (const auto& f, const auto& t, auto& e, auto* x) { return timing::switchSubdivision (f, t, e, x); }, why))
        return false;
    *this = *next;
    return true;
}

bool StoredPattern::operator== (const StoredPattern& o) const noexcept
{
    // Unused slots are empty in every valid pattern, so all storage compares.
    return signature == o.signature && resolution == o.resolution && followHost == o.followHost && bars == o.bars
        && seed == o.seed && masks == o.masks && cells == o.cells;
}

StoredPattern factorySlot (int index) noexcept
{
    StoredPattern p;
    factorySlot (index, p);
    return p;
}

void factorySlot (int index, StoredPattern& p) noexcept
{
    std::array<kit::PatternStep, 8 * 4 * 16> legacy {};   // 2 KB
    kit::factoryPattern (index, legacy);
    p.signature = {};
    p.resolution = timing::Resolution::sixteenth;
    p.followHost = true;
    p.masks.fill (0);
    const auto view = kit::fromLegacyGrid (legacy.data(), 8, 4, p.cells);
    p.bars = static_cast<std::uint8_t> (view.bars);
    p.seed = 1;
}

void PatternStore::factorySlots (std::array<StoredPattern, slotCount>& out)
{
    for (int i = 0; i < slotCount; ++i)
        factorySlot (i, out[static_cast<std::size_t> (i)]);
}

PatternStore::PatternStore()
{
    factorySlots (slots_);
    alternates_ = slots_;
}

void PatternStore::copyAlternate (int index, StoredPattern& out) const
{
    const juce::SpinLock::ScopedLockType sl (lock_);
    out = alternates_[static_cast<std::size_t> (std::clamp (index, 0, slotCount - 1))];
}

StoredPattern PatternStore::alternate (int index) const
{
    const juce::SpinLock::ScopedLockType sl (lock_);
    return alternates_[static_cast<std::size_t> (std::clamp (index, 0, slotCount - 1))];
}

void PatternStore::setAlternate (int index, const StoredPattern& pattern)
{
    if (index < 0 || index >= slotCount)
        return;
    const juce::SpinLock::ScopedLockType sl (lock_);
    alternates_[static_cast<std::size_t> (index)] = pattern;
    const auto g = generation_.load() + 1;
    alternateGeneration_[static_cast<std::size_t> (index)] = g;
    generation_.store (g);   // the editor shows A/B state
}

void PatternStore::copyAlternates (std::array<StoredPattern, slotCount>& out) const
{
    const juce::SpinLock::ScopedLockType sl (lock_);
    out = alternates_;
}

void PatternStore::setAlternates (const std::array<StoredPattern, slotCount>& alternates)
{
    const juce::SpinLock::ScopedLockType sl (lock_);
    alternates_ = alternates;
    const auto g = generation_.load() + 1;
    alternateGeneration_.fill (g);
    generation_.store (g);
}

StoredPattern PatternStore::slot (int index) const
{
    const juce::SpinLock::ScopedLockType sl (lock_);
    return slots_[static_cast<std::size_t> (std::clamp (index, 0, slotCount - 1))];
}

void PatternStore::copySlot (int index, StoredPattern& out) const
{
    const juce::SpinLock::ScopedLockType sl (lock_);
    out = slots_[static_cast<std::size_t> (std::clamp (index, 0, slotCount - 1))];
}

void PatternStore::copySlot (int index, StoredPattern& out, std::uint64_t& generation) const
{
    const juce::SpinLock::ScopedLockType sl (lock_);
    generation = generation_.load();
    out = slots_[static_cast<std::size_t> (std::clamp (index, 0, slotCount - 1))];
}

// The generation moves under the lock, together with the data it counts:
// whoever sees the new generation (outside the lock) finds the new data on
// its next locked read, and a locked read returns data and generation that
// belong together.
void PatternStore::setSlot (int index, const StoredPattern& pattern)
{
    if (index < 0 || index >= slotCount)
        return;
    if (beforeCommitForTesting)
        beforeCommitForTesting();
    const juce::SpinLock::ScopedLockType sl (lock_);
    const auto g = generation_.load() + 1;
    slots_[static_cast<std::size_t> (index)] = pattern;
    slotGeneration_[static_cast<std::size_t> (index)] = g;
    generation_.store (g);
}

void PatternStore::setAll (const std::array<StoredPattern, slotCount>& slots)
{
    if (beforeCommitForTesting)
        beforeCommitForTesting();
    const juce::SpinLock::ScopedLockType sl (lock_);
    const auto g = generation_.load() + 1;
    slots_ = slots;
    slotGeneration_.fill (g);
    generation_.store (g);
}

void PatternStore::copyAll (std::array<StoredPattern, slotCount>& out) const
{
    const juce::SpinLock::ScopedLockType sl (lock_);
    out = slots_;
}

preset::Section PatternStore::encodeCurrent() const
{
    // Message/host thread; the audio thread only try-locks, so encoding
    // under the lock never makes it wait.
    const juce::SpinLock::ScopedLockType sl (lock_);
    return encode (slots_, alternates_);
}

const kit::Pattern& PatternStore::pull (int index, Live& live) noexcept
{
    index = std::clamp (index, 0, slotCount - 1);
    const juce::SpinLock::ScopedTryLockType tl (lock_);
    if (tl.isLocked())
    {
        const auto g = slotGeneration_[static_cast<std::size_t> (index)];
        if (live.index != index || live.generation != g)
        {
            *live.pattern = slots_[static_cast<std::size_t> (index)];
            live.index = index;
            live.generation = g;
            live.view = live.pattern->view();
        }
    }
    return live.view;
}

//==============================================================================
// DSEQ version 4 (PR 137 §10.5): u32 version, u32 slot count (5), then the
// 5 slots (A) and their 5 alternates (B), each: grid (u8 numerator, u8
// denominator, u8 resolution), u8 followHost, u8 bars (1..4), u32 seed,
// 16 lanes x bars u16 subdivision masks (lane-major), then 16 lanes x bars x
// slotsPerBar cells (lane-major, the bar's storage slots in order), each a
// 32-bit word (gridcodec::packCell). Strict: an unsupported grid, a mask
// beyond the beats or not allowed, a value out of range, a |microTicks| over
// half a cell, nonzero reserved bits or content in a reserved slot rejects
// the section. A full 64-slot pattern of 4 bars is 9 + 128 + 16 x 4 x 64 x 4
// bytes.
namespace {

void encodePattern (std::vector<std::byte>& out, const StoredPattern& p)
{
    gridcodec::putGrid (out, p.signature, p.resolution);
    putU8 (out, p.followHost ? 1 : 0);
    putU8 (out, p.bars);
    putU32 (out, p.seed);
    for (int lane = 0; lane < kit::padCount; ++lane)
        for (int bar = 0; bar < p.bars; ++bar)
            gridcodec::putU16 (out, p.mask (lane, bar));
    const int slots = p.slotsPerBar();
    for (int lane = 0; lane < kit::padCount; ++lane)
        for (int bar = 0; bar < p.bars; ++bar)
            for (int s = 0; s < slots; ++s)
                gridcodec::putCell (out, p.cell (lane, bar, s));
}

bool decodePattern (Reader& in, StoredPattern& p)
{
    p = {};
    const bool grid = gridcodec::getGrid (in, p.signature, p.resolution);
    const auto followHost = in.u8(), bars = in.u8();
    p.seed = in.u32();
    if (! in.ok || ! grid || followHost > 1 || bars < 1 || bars > kit::maxPatternBars)
        return false;
    p.followHost = followHost != 0;
    p.bars = bars;
    for (int lane = 0; lane < kit::padCount; ++lane)
        for (int bar = 0; bar < p.bars; ++bar)
            p.mask (lane, bar) = in.u16();
    const int slots = p.slotsPerBar();
    for (int lane = 0; lane < kit::padCount; ++lane)
        for (int bar = 0; bar < p.bars; ++bar)
            for (int s = 0; s < slots; ++s)
                if (! gridcodec::getCell (in, p.cell (lane, bar, s)))
                    return false;
    return in.ok && p.valid();
}

} // namespace

preset::Section PatternStore::encode (const std::array<StoredPattern, slotCount>& slots,
                                      const std::array<StoredPattern, slotCount>& alternates)
{
    preset::Section s;
    s.tag = sequenceSectionTag;
    putU32 (s.data, sectionVersion);
    putU32 (s.data, slotCount);
    for (const auto& p : slots)
        encodePattern (s.data, p);
    for (const auto& p : alternates)
        encodePattern (s.data, p);
    return s;
}

bool PatternStore::decode (const preset::Section& section, std::array<StoredPattern, slotCount>& out,
                           std::array<StoredPattern, slotCount>& alternates, juce::String& error)
{
    Reader in { section.data };
    const auto version = in.u32();
    if (version != sectionVersion || in.u32() != static_cast<std::uint32_t> (slotCount) || ! in.ok)
    {
        error = "unsupported pattern section";
        return false;
    }
    // Decoded into the heap, then committed: a rejection changes nothing.
    auto next = std::make_unique<std::array<StoredPattern, slotCount>>();
    auto nextB = std::make_unique<std::array<StoredPattern, slotCount>>();
    for (auto* set : { next.get(), nextB.get() })
        for (auto& p : *set)
            if (! decodePattern (in, p))
            {
                error = "invalid pattern section";
                return false;
            }
    if (in.at != section.data.size())
    {
        error = "invalid pattern section";
        return false;
    }
    out = *next;
    alternates = *nextB;
    return true;
}

//==============================================================================
void PatternHistory::push (std::unique_ptr<Entry> entry)
{
    undo_.push_back (std::move (entry));
    if (undo_.size() > capacity)
        undo_.erase (undo_.begin());
    redo_.clear();
}

bool PatternHistory::amendLast (int slot, const StoredPattern& after)
{
    if (undo_.empty() || ! redo_.empty() || undo_.back()->slot != slot || undo_.back()->alternate || undo_.back()->hasSong
        || undo_.back()->allBefore != nullptr)
        return false;
    undo_.back()->after = after;
    return true;
}

bool PatternHistory::amendLastSong (const SongData& after)
{
    if (undo_.empty() || ! redo_.empty() || ! undo_.back()->hasSong || undo_.back()->slot >= 0 || undo_.back()->allBefore != nullptr)
        return false;
    undo_.back()->songAfter = after;
    return true;
}

int PatternHistory::undo (PatternStore& store)
{
    if (undo_.empty())
        return -1;
    auto e = std::move (undo_.back());
    undo_.pop_back();
    if (e->allBefore != nullptr)
        store.setEverything (*e->allBefore);
    if (e->slot >= 0)
        store.setSlot (e->slot, e->before);
    if (e->slot >= 0 && e->alternate)
        store.setAlternate (e->slot, e->beforeB);
    if (e->hasSong)
        store.setSong (e->songBefore);
    if (e->hasRoles && roleSink_)
        roleSink_ (e->rolesBefore);
    if (e->hasSettings && settingsSink_)
        settingsSink_ (e->settingsBefore);
    const int slot = e->slot;
    redo_.push_back (std::move (e));
    return slot;
}

int PatternHistory::redo (PatternStore& store)
{
    if (redo_.empty())
        return -1;
    auto e = std::move (redo_.back());
    redo_.pop_back();
    if (e->allAfter != nullptr)
        store.setEverything (*e->allAfter);
    if (e->slot >= 0)
        store.setSlot (e->slot, e->after);
    if (e->slot >= 0 && e->alternate)
        store.setAlternate (e->slot, e->afterB);
    if (e->hasSong)
        store.setSong (e->songAfter);
    if (e->hasRoles && roleSink_)
        roleSink_ (e->rolesAfter);
    if (e->hasSettings && settingsSink_)
        settingsSink_ (e->settingsAfter);
    const int slot = e->slot;
    undo_.push_back (std::move (e));
    return slot;
}

} // namespace vox::drums
