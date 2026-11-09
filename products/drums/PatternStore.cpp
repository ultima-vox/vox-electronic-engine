#include "PatternStore.h"

#include <algorithm>
#include <cstring>

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

constexpr std::uint32_t sectionVersion = 1;

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
    const StoredPattern old = *this;
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
            || a.ratchet != b.ratchet || a.micro != b.micro)
            return false;
    }
    return true;
}

StoredPattern factorySlot (int index) noexcept
{
    std::array<kit::PatternStep, 8 * 4 * 16> legacy {};
    const auto view = kit::factoryPattern (index, legacy);
    StoredPattern p;
    p.steps = static_cast<std::uint8_t> (view.steps);
    p.bars = static_cast<std::uint8_t> (view.bars);
    p.seed = 1;
    for (std::uint32_t lane = 0; lane < view.lanes; ++lane)
        for (std::uint32_t bar = 0; bar < view.bars; ++bar)
            for (std::uint32_t st = 0; st < view.steps; ++st)
                p.cell (static_cast<int> (lane), static_cast<int> (bar), static_cast<int> (st))
                    = legacy[(lane * view.bars + bar) * view.steps + st];
    return p;
}

std::array<StoredPattern, PatternStore::slotCount> PatternStore::factorySlots()
{
    std::array<StoredPattern, slotCount> slots;
    for (int i = 0; i < slotCount; ++i)
        slots[static_cast<std::size_t> (i)] = factorySlot (i);
    return slots;
}

PatternStore::PatternStore()
{
    slots_ = factorySlots();
}

StoredPattern PatternStore::slot (int index) const
{
    const juce::SpinLock::ScopedLockType sl (lock_);
    return slots_[static_cast<std::size_t> (std::clamp (index, 0, slotCount - 1))];
}

void PatternStore::setSlot (int index, const StoredPattern& pattern)
{
    if (index < 0 || index >= slotCount)
        return;
    const auto g = generation_.fetch_add (1) + 1;
    const juce::SpinLock::ScopedLockType sl (lock_);
    slots_[static_cast<std::size_t> (index)] = pattern;
    slotGeneration_[static_cast<std::size_t> (index)] = g;
}

void PatternStore::setAll (const std::array<StoredPattern, slotCount>& slots)
{
    const auto g = generation_.fetch_add (1) + 1;
    const juce::SpinLock::ScopedLockType sl (lock_);
    slots_ = slots;
    slotGeneration_.fill (g);
}

std::array<StoredPattern, PatternStore::slotCount> PatternStore::all() const
{
    const juce::SpinLock::ScopedLockType sl (lock_);
    return slots_;
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
            live.pattern = slots_[static_cast<std::size_t> (index)];
            live.index = index;
            live.generation = g;
            live.view = live.pattern.view();
        }
    }
    return live.view;
}

//==============================================================================
// DSEQ: u32 version (1), u32 slot count (5); per slot: u8 steps, u8 bars,
// u32 seed, then 16 lanes x bars x steps cells in storage order: u8 flags
// (bit 0 on, bit 1 accent), u8 velocity, u8 probability, u8 ratchet,
// u8 micro (two's complement).
preset::Section PatternStore::encode (const std::array<StoredPattern, slotCount>& slots)
{
    preset::Section s;
    s.tag = sequenceSectionTag;
    putU32 (s.data, sectionVersion);
    putU32 (s.data, slotCount);
    for (const auto& p : slots)
    {
        putU8 (s.data, p.steps);
        putU8 (s.data, p.bars);
        putU32 (s.data, p.seed);
        const int n = kit::padCount * p.steps * p.bars;
        for (int i = 0; i < n; ++i)
        {
            const auto& c = p.cells[static_cast<std::size_t> (i)];
            putU8 (s.data, static_cast<std::uint8_t> ((c.on ? 1 : 0) | (c.accent ? 2 : 0)));
            putU8 (s.data, c.velocity);
            putU8 (s.data, c.probability);
            putU8 (s.data, c.ratchet);
            putU8 (s.data, static_cast<std::uint8_t> (c.micro));
        }
    }
    return s;
}

bool PatternStore::decode (const preset::Section& section, std::array<StoredPattern, slotCount>& out, juce::String& error)
{
    Reader in { section.data };
    if (in.u32() != sectionVersion || in.u32() != static_cast<std::uint32_t> (slotCount) || ! in.ok)
    {
        error = "unsupported pattern section";
        return false;
    }
    for (auto& p : out)
    {
        p = {};
        const int steps = in.u8(), bars = in.u8();
        p.seed = in.u32();
        if (! in.ok || ! validSteps (steps) || bars < 1 || bars > kit::maxPatternBars)
        {
            error = "invalid pattern section";
            return false;
        }
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
            if (! in.ok || flags > 3 || c.velocity > 127 || c.probability < 1 || c.probability > 100 || c.ratchet < 1
                || c.ratchet > kit::maxRatchet || c.micro < -50 || c.micro > 50)
            {
                error = "invalid pattern section";
                return false;
            }
        }
    }
    if (in.at != section.data.size())
    {
        error = "invalid pattern section";
        return false;
    }
    return true;
}

} // namespace vox::drums
