#include "PatternStore.h"

#include <algorithm>
#include <cstring>
#include <memory>

namespace vox::drums {
namespace {

int timingFor (int steps) noexcept
{
    return steps == 64 ? 4 : steps == 32 ? 2 : 0;   // 1/64, 1/32, 1/16
}

bool validSteps (int steps) noexcept
{
    return steps == 16 || steps == 32 || steps == 64;
}

void putU32 (std::vector<std::byte>& out, std::uint32_t v)
{
    for (int s = 0; s < 32; s += 8)
        out.push_back (static_cast<std::byte> ((v >> s) & 0xffu));
}

void putU8 (std::vector<std::byte>& out, std::uint8_t v)
{
    out.push_back (static_cast<std::byte> (v));
}

struct Reader {
    const std::vector<std::byte>& data;
    std::size_t at = 0;
    bool ok = true;
    std::uint8_t u8()
    {
        if (at >= data.size())
        {
            ok = false;
            return 0;
        }
        return std::to_integer<std::uint8_t> (data[at++]);
    }
    std::uint32_t u32()
    {
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i)
            v |= static_cast<std::uint32_t> (u8()) << (8 * i);
        return v;
    }
};

constexpr std::uint32_t sectionVersion = 3;

} // namespace

kit::Pattern StoredPattern::view() const noexcept
{
    kit::Pattern p;
    p.lanes = kit::padCount;
    p.steps = steps;
    p.bars = bars;
    p.timing = static_cast<std::uint32_t> (timingFor (steps));
    p.cells = cells.data();
    p.seed = seed;
    return p;
}

void StoredPattern::resize (int newSteps, int newBars) noexcept
{
    if (! validSteps (newSteps))
        newSteps = steps;
    newBars = std::clamp (newBars, 1, kit::maxPatternBars);
    const auto copy = std::make_unique<StoredPattern> (*this);   // heap: ~24 KB
    const StoredPattern& old = *copy;
    cells.fill ({});
    steps = static_cast<std::uint8_t> (newSteps);
    bars = static_cast<std::uint8_t> (newBars);
    for (int lane = 0; lane < kit::padCount; ++lane)
        for (int bar = 0; bar < newBars; ++bar)
        {
            const int from = bar < old.bars ? bar : 0;
            for (int st = 0; st < old.steps; ++st)
            {
                // Grid position st / old.steps of the bar.
                const int to = st * newSteps / old.steps;
                if (to * old.steps != st * newSteps)
                    continue;   // between the new grid's steps: dropped
                cell (lane, bar, to) = old.cell (lane, from, st);
            }
        }
}

bool StoredPattern::operator== (const StoredPattern& o) const noexcept
{
    if (steps != o.steps || bars != o.bars || seed != o.seed)
        return false;
    const int n = kit::padCount * steps * bars;
    for (int i = 0; i < n; ++i)
    {
        const auto& a = cells[static_cast<std::size_t> (i)];
        const auto& b = o.cells[static_cast<std::size_t> (i)];
        if (a.on != b.on || a.accent != b.accent || a.velocity != b.velocity || a.probability != b.probability
            || a.ratchet != b.ratchet || a.micro != b.micro || a.locked != b.locked)
            return false;
    }
    return true;
}

StoredPattern factorySlot (int index) noexcept
{
    StoredPattern p;
    factorySlot (index, p);
    return p;
}

void factorySlot (int index, StoredPattern& p) noexcept
{
    std::array<kit::PatternStep, 8 * 4 * 16> legacy {};   // 3 KB
    const auto view = kit::factoryPattern (index, legacy);
    p.cells.fill ({});
    p.steps = static_cast<std::uint8_t> (view.steps);
    p.bars = static_cast<std::uint8_t> (view.bars);
    p.seed = 1;
    for (std::uint32_t lane = 0; lane < view.lanes; ++lane)
        for (std::uint32_t bar = 0; bar < view.bars; ++bar)
            for (std::uint32_t st = 0; st < view.steps; ++st)
                p.cell (static_cast<int> (lane), static_cast<int> (bar), static_cast<int> (st))
                    = legacy[(lane * view.bars + bar) * view.steps + st];
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
// DSEQ: u32 version (1), u32 slot count (5); per slot: u8 steps, u8 bars,
// u32 seed, then 16 lanes x bars x steps cells in storage order: u8 flags
// (bit 0 on, bit 1 accent), u8 velocity, u8 probability, u8 ratchet,
// u8 micro (two's complement).
namespace {

void encodePattern (std::vector<std::byte>& out, const StoredPattern& p)
{
    putU8 (out, p.steps);
    putU8 (out, p.bars);
    putU32 (out, p.seed);
    const int n = kit::padCount * p.steps * p.bars;
    for (int i = 0; i < n; ++i)
    {
        const auto& c = p.cells[static_cast<std::size_t> (i)];
        putU8 (out, static_cast<std::uint8_t> ((c.on ? 1 : 0) | (c.accent ? 2 : 0) | (c.locked ? 4 : 0)));
        putU8 (out, c.velocity);
        putU8 (out, c.probability);
        putU8 (out, c.ratchet);
        putU8 (out, static_cast<std::uint8_t> (c.micro));
    }
}

bool decodePattern (Reader& in, StoredPattern& p, std::uint32_t version)
{
    p = {};
    const int steps = in.u8(), bars = in.u8();
    p.seed = in.u32();
    if (! in.ok || ! validSteps (steps) || bars < 1 || bars > kit::maxPatternBars)
        return false;
    p.steps = static_cast<std::uint8_t> (steps);
    p.bars = static_cast<std::uint8_t> (bars);
    const int n = kit::padCount * steps * bars;
    for (int i = 0; i < n; ++i)
    {
        auto& c = p.cells[static_cast<std::size_t> (i)];
        const auto flags = in.u8();
        c.velocity = in.u8();
        c.probability = in.u8();
        c.ratchet = in.u8();
        c.micro = static_cast<std::int8_t> (in.u8());
        c.on = (flags & 1u) != 0;
        c.accent = (flags & 2u) != 0;
        c.locked = (flags & 4u) != 0;
        if (! in.ok || flags > (version >= 3 ? 7u : 3u) || c.velocity > 127 || c.probability < 1 || c.probability > 100 || c.ratchet < 1
            || c.ratchet > kit::maxRatchet || c.micro < -50 || c.micro > 50)
            return false;
    }
    return true;
}

} // namespace

//==============================================================================
// DSEQ: u32 version, u32 slot count (5); per slot: u8 steps, u8 bars,
// u32 seed, then 16 lanes x bars x steps cells in storage order: u8 flags
// (bit 0 on, bit 1 accent), u8 velocity, u8 probability, u8 ratchet,
// u8 micro (two's complement). Version 2 (DRM-04b) follows the 5 slots with
// their 5 alternates (B) in the same layout; version 1 (DRM-04a) has none.
// Version 3 (DRM-05) adds flag bit 2: the step is locked for the generator.
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
    if ((version < 1 || version > 3) || in.u32() != static_cast<std::uint32_t> (slotCount) || ! in.ok)
    {
        error = "unsupported pattern section";
        return false;
    }
    for (auto& p : out)
        if (! decodePattern (in, p, version))
        {
            error = "invalid pattern section";
            return false;
        }
    if (version == 1)
        alternates = out;
    else
        for (auto& p : alternates)
            if (! decodePattern (in, p, version))
            {
                error = "invalid pattern section";
                return false;
            }
    if (in.at != section.data.size())
    {
        error = "invalid pattern section";
        return false;
    }
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
    const int slot = e->slot;
    undo_.push_back (std::move (e));
    return slot;
}

} // namespace vox::drums
