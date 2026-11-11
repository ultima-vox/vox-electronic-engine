// DRM-05 Song mode: the song value types, their state section (DSNG), the
// engine view and the song part of PatternStore (see PatternStore.h).
#include "PatternStore.h"

#include <algorithm>
#include <atomic>
#include <cstring>

namespace vox::drums {
namespace {

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

bool validSteps (int steps) noexcept
{
    return steps == 16 || steps == 32 || steps == 64;
}

bool sameStep (const kit::PatternStep& a, const kit::PatternStep& b) noexcept
{
    return a.on == b.on && a.accent == b.accent && a.velocity == b.velocity && a.probability == b.probability
        && a.ratchet == b.ratchet && a.micro == b.micro && a.locked == b.locked;
}

constexpr std::uint32_t songVersion = 1;

} // namespace

std::uint64_t BarCells::nextId() noexcept
{
    static std::atomic<std::uint64_t> next { 1 };
    return next.fetch_add (1);
}

bool BarCells::sameContent (const BarCells& o) const noexcept
{
    if (steps != o.steps || seed != o.seed)
        return false;
    for (int lane = 0; lane < kit::padCount; ++lane)
        for (int st = 0; st < steps; ++st)
            if (! sameStep (cell (lane, st), o.cell (lane, st)))
                return false;
    return true;
}

bool SongBar::operator== (const SongBar& o) const noexcept
{
    if (kind != o.kind)
        return false;
    if (kind == Kind::pattern)
        return slot == o.slot && variation == o.variation && patternBar == o.patternBar;
    if (kind == Kind::unique)
        return cells == o.cells || (cells != nullptr && o.cells != nullptr && cells->sameContent (*o.cells));
    return true;
}

bool SongData::operator== (const SongData& o) const noexcept
{
    if (songMode != o.songMode || length != o.length || loopOn != o.loopOn || loopStart != o.loopStart || loopLength != o.loopLength)
        return false;
    for (std::size_t i = 0; i < bars.size(); ++i)
        if (! (bars[i] == o.bars[i]))
            return false;
    return true;
}

SongData SongData::initial()
{
    SongData s;
    s.length = 16;
    for (int bar = 0; bar < 16; bar += 4)
        s.place (bar, 0, 0, 4);
    return s;
}

int SongData::place (int bar, int slot, int variation, int patternBars)
{
    int placed = 0;
    for (int k = 0; k < std::clamp (patternBars, 1, kit::maxPatternBars) && bar + k < length; ++k)
    {
        if (bar + k < 0)
            continue;
        auto& b = bars[static_cast<std::size_t> (bar + k)];
        b = {};
        b.kind = SongBar::Kind::pattern;
        b.slot = static_cast<std::uint8_t> (std::clamp (slot, 0, slotCountForSong - 1));
        b.variation = static_cast<std::uint8_t> (variation != 0 ? 1 : 0);
        b.patternBar = static_cast<std::uint8_t> (k);
        ++placed;
    }
    return placed;
}

std::shared_ptr<const BarCells> resolveBar (const SongBar& bar, const std::array<StoredPattern, slotCountForSong>& slots,
                                           const std::array<StoredPattern, slotCountForSong>& alternates)
{
    if (bar.kind == SongBar::Kind::unique)
        return bar.cells;
    if (bar.kind != SongBar::Kind::pattern)
        return nullptr;
    const auto index = static_cast<std::size_t> (bar.slot < slotCountForSong ? static_cast<int> (bar.slot) : slotCountForSong - 1);
    const StoredPattern& p = bar.variation != 0 ? alternates[index] : slots[index];
    auto cells = std::make_shared<BarCells>();
    cells->id = BarCells::nextId();
    cells->steps = p.steps;
    cells->seed = p.seed;
    const int pb = bar.patternBar % std::max<int> (1, p.bars);
    for (int lane = 0; lane < kit::padCount; ++lane)
        for (int st = 0; st < p.steps; ++st)
            cells->cell (lane, st) = p.cell (lane, pb, st);
    return cells;
}

SongTable tableOf (const SongData& song) noexcept
{
    SongTable t;
    t.length = song.length;
    t.loopOn = song.loopOn;
    t.loopStart = song.loopStart;
    t.loopLength = song.loopLength;
    for (std::size_t i = 0; i < t.bars.size(); ++i)
        t.bars[i] = { song.bars[i].kind, song.bars[i].slot, song.bars[i].variation, song.bars[i].patternBar };
    return t;
}

void buildSongView (const SongTable& table, const StoredPattern* slots, const StoredPattern* alternates,
                    const BarCells* const* unique, bool applyLoop, SongView& out) noexcept
{
    const int length = SongData::validLength (table.length) ? table.length : 16;
    for (int i = 0; i < kit::maxSongBars; ++i)
    {
        auto& view = out.views[static_cast<std::size_t> (i)];
        view = {};
        if (i >= length)
            continue;
        const auto& bar = table.bars[static_cast<std::size_t> (i)];
        if (bar.kind == SongBar::Kind::pattern)
        {
            const int index = bar.slot < slotCountForSong ? static_cast<int> (bar.slot) : slotCountForSong - 1;
            const StoredPattern& p = bar.variation != 0 ? alternates[index] : slots[index];
            if (p.bars == 0 || p.steps == 0)
                continue;
            const int pb = bar.patternBar % p.bars;
            view.cells = p.cells.data() + pb * p.steps;
            view.laneStride = static_cast<std::uint32_t> (p.bars) * p.steps;
            view.steps = p.steps;
            view.seed = p.seed;
        }
        else if (bar.kind == SongBar::Kind::unique && unique != nullptr && unique[i] != nullptr)
        {
            view.cells = unique[i]->cells.data();
            view.laneStride = BarCells::stride;
            view.steps = unique[i]->steps;
            view.seed = unique[i]->seed;
        }
    }
    out.song = {};
    out.song.bars = static_cast<std::uint32_t> (length);
    out.song.views = out.views.data();
    if (applyLoop && table.loopOn && table.loopStart < length)
    {
        out.song.loopStart = table.loopStart;
        out.song.loopLength = static_cast<std::uint32_t> (std::clamp<int> (table.loopLength, 1, length - table.loopStart));
    }
}

//==============================================================================
namespace songedit {
namespace {
void range (const SongData& song, int& from, int& count) noexcept
{
    from = std::clamp (from, 0, song.length - 1);
    count = std::clamp (count, 0, song.length - from);
}
} // namespace

SongData insertEmpty (const SongData& song, int from, int count)
{
    range (song, from, count);
    SongData out = song;
    for (int i = song.length - 1; i >= from; --i)
        out.bars[static_cast<std::size_t> (i)] = i - count >= from ? song.bars[static_cast<std::size_t> (i - count)] : SongBar {};
    return out;
}

SongData removeBars (const SongData& song, int from, int count)
{
    range (song, from, count);
    SongData out = song;
    for (int i = from; i < song.length; ++i)
        out.bars[static_cast<std::size_t> (i)] = i + count < song.length ? song.bars[static_cast<std::size_t> (i + count)] : SongBar {};
    return out;
}

SongData duplicate (const SongData& song, int from, int count)
{
    range (song, from, count);
    return paste (song, from + count, copy (song, from, count));
}

SongData clear (const SongData& song, int from, int count)
{
    range (song, from, count);
    SongData out = song;
    for (int i = from; i < from + count; ++i)
        out.bars[static_cast<std::size_t> (i)] = {};
    return out;
}

std::vector<SongBar> copy (const SongData& song, int from, int count)
{
    range (song, from, count);
    return { song.bars.begin() + from, song.bars.begin() + from + count };
}

SongData paste (const SongData& song, int at, const std::vector<SongBar>& bars)
{
    SongData out = song;
    for (std::size_t i = 0; i < bars.size(); ++i)
    {
        const int to = at + static_cast<int> (i);
        if (to >= 0 && to < song.length)
            out.bars[static_cast<std::size_t> (to)] = bars[i];
    }
    return out;
}

SongData makeUnique (const SongData& song, int from, int count, const std::array<StoredPattern, slotCountForSong>& slots,
                     const std::array<StoredPattern, slotCountForSong>& alternates)
{
    range (song, from, count);
    SongData out = song;
    for (int i = from; i < from + count; ++i)
    {
        auto& bar = out.bars[static_cast<std::size_t> (i)];
        if (bar.kind != SongBar::Kind::pattern)
            continue;
        bar.cells = resolveBar (bar, slots, alternates);
        bar.kind = SongBar::Kind::unique;
        bar.slot = bar.variation = bar.patternBar = 0;
    }
    return out;
}

void barToPattern (const std::shared_ptr<const BarCells>& bar, StoredPattern& out)
{
    out.cells.fill ({});
    out.bars = 1;
    out.steps = bar != nullptr ? bar->steps : std::uint8_t { 16 };
    out.seed = bar != nullptr ? bar->seed : 1u;
    if (bar != nullptr)
        for (int lane = 0; lane < kit::padCount; ++lane)
            for (int st = 0; st < bar->steps; ++st)
                out.cell (lane, 0, st) = bar->cell (lane, st);
}

std::shared_ptr<const BarCells> patternToBar (const StoredPattern& pattern)
{
    auto cells = std::make_shared<BarCells>();
    cells->id = BarCells::nextId();
    cells->steps = pattern.steps;
    cells->seed = pattern.seed;
    for (int lane = 0; lane < kit::padCount; ++lane)
        for (int st = 0; st < pattern.steps; ++st)
            cells->cell (lane, st) = pattern.cell (lane, 0, st);
    return cells;
}

} // namespace songedit

//==============================================================================
preset::Section encodeSong (const SongData& song)
{
    preset::Section s;
    s.tag = songSectionTag;
    putU32 (s.data, songVersion);
    putU8 (s.data, song.songMode ? 1 : 0);
    putU8 (s.data, song.length);
    putU8 (s.data, song.loopOn ? 1 : 0);
    putU8 (s.data, song.loopStart);
    putU8 (s.data, song.loopLength);
    for (const auto& bar : song.bars)
    {
        // A unique bar without cells cannot exist in a valid song; written as empty.
        const auto kind = bar.kind == SongBar::Kind::unique && bar.cells == nullptr ? SongBar::Kind::empty : bar.kind;
        putU8 (s.data, static_cast<std::uint8_t> (kind));
        if (kind == SongBar::Kind::pattern)
        {
            putU8 (s.data, bar.slot);
            putU8 (s.data, bar.variation);
            putU8 (s.data, bar.patternBar);
        }
        else if (kind == SongBar::Kind::unique)
        {
            putU8 (s.data, bar.cells->steps);
            putU32 (s.data, bar.cells->seed);
            for (int lane = 0; lane < kit::padCount; ++lane)
                for (int st = 0; st < bar.cells->steps; ++st)
                {
                    const auto& c = bar.cells->cell (lane, st);
                    putU8 (s.data, static_cast<std::uint8_t> ((c.on ? 1 : 0) | (c.accent ? 2 : 0) | (c.locked ? 4 : 0)));
                    putU8 (s.data, c.velocity);
                    putU8 (s.data, c.probability);
                    putU8 (s.data, c.ratchet);
                    putU8 (s.data, static_cast<std::uint8_t> (c.micro));
                }
        }
    }
    return s;
}

bool decodeSong (const preset::Section& section, SongData& out, juce::String& error)
{
    Reader in { section.data };
    if (in.u32() != songVersion || ! in.ok)
    {
        error = "unsupported song section";
        return false;
    }
    SongData song;
    const auto mode = in.u8(), length = in.u8(), loopOn = in.u8(), loopStart = in.u8(), loopLength = in.u8();
    if (! in.ok || mode > 1 || ! SongData::validLength (length) || loopOn > 1 || loopStart >= length || loopLength < 1
        || loopStart + loopLength > length)
    {
        error = "invalid song section";
        return false;
    }
    song.songMode = mode != 0;
    song.length = length;
    song.loopOn = loopOn != 0;
    song.loopStart = loopStart;
    song.loopLength = loopLength;
    for (auto& bar : song.bars)
    {
        const auto kind = in.u8();
        if (! in.ok || kind > 2)
        {
            error = "invalid song section";
            return false;
        }
        bar.kind = static_cast<SongBar::Kind> (kind);
        if (bar.kind == SongBar::Kind::pattern)
        {
            bar.slot = in.u8();
            bar.variation = in.u8();
            bar.patternBar = in.u8();
            if (! in.ok || bar.slot >= slotCountForSong || bar.variation > 1 || bar.patternBar >= kit::maxPatternBars)
            {
                error = "invalid song section";
                return false;
            }
        }
        else if (bar.kind == SongBar::Kind::unique)
        {
            auto cells = std::make_shared<BarCells>();
            cells->id = BarCells::nextId();
            const int steps = in.u8();
            cells->seed = in.u32();
            if (! in.ok || ! validSteps (steps))
            {
                error = "invalid song section";
                return false;
            }
            cells->steps = static_cast<std::uint8_t> (steps);
            for (int lane = 0; lane < kit::padCount; ++lane)
                for (int st = 0; st < steps; ++st)
                {
                    auto& c = cells->cell (lane, st);
                    const auto flags = in.u8();
                    c.velocity = in.u8();
                    c.probability = in.u8();
                    c.ratchet = in.u8();
                    c.micro = static_cast<std::int8_t> (in.u8());
                    c.on = (flags & 1u) != 0;
                    c.accent = (flags & 2u) != 0;
                    c.locked = (flags & 4u) != 0;
                    if (! in.ok || flags > 7 || c.velocity > 127 || c.probability < 1 || c.probability > 100 || c.ratchet < 1
                        || c.ratchet > kit::maxRatchet || c.micro < -50 || c.micro > 50)
                    {
                        error = "invalid song section";
                        return false;
                    }
                }
            bar.cells = std::move (cells);
        }
    }
    if (in.at != section.data.size())
    {
        error = "invalid song section";
        return false;
    }
    out = std::move (song);
    return true;
}

//==============================================================================
SongData PatternStore::song() const
{
    const juce::SpinLock::ScopedLockType sl (lock_);
    return song_;
}

void PatternStore::copySongBar (int bar, StoredPattern& out, std::uint64_t& generation) const
{
    std::shared_ptr<const BarCells> cells;
    {
        const juce::SpinLock::ScopedLockType sl (lock_);
        generation = generation_.load();
        const auto& b = song_.bars[static_cast<std::size_t> (std::clamp (bar, 0, kit::maxSongBars - 1))];
        if (b.kind == SongBar::Kind::unique)
            cells = b.cells;
        else if (b.kind == SongBar::Kind::pattern)
        {
            // Copied under the lock into `out` directly (no allocation here).
            const auto index = static_cast<std::size_t> (b.slot < slotCount ? static_cast<int> (b.slot) : slotCount - 1);
            const StoredPattern& p = b.variation != 0 ? alternates_[index] : slots_[index];
            const int pb = b.patternBar % std::max<int> (1, p.bars);
            out.cells.fill ({});
            out.bars = 1;
            out.steps = p.steps;
            out.seed = p.seed;
            for (int lane = 0; lane < kit::padCount; ++lane)
                for (int st = 0; st < p.steps; ++st)
                    out.cell (lane, 0, st) = p.cell (lane, pb, st);
            return;
        }
    }
    songedit::barToPattern (cells, out);
}

void PatternStore::setSong (const SongData& song)
{
    SongData next = song;   // copied (and the old one destroyed) outside the lock
    {
        const juce::SpinLock::ScopedLockType sl (lock_);
        std::swap (song_, next);
        const auto g = generation_.load() + 1;
        songGeneration_ = g;
        generation_.store (g);
    }
}

void PatternStore::copyEverything (Everything& out) const
{
    const juce::SpinLock::ScopedLockType sl (lock_);
    out.slots = slots_;
    out.alternates = alternates_;
    out.song = song_;
}

void PatternStore::setEverything (const Everything& in)
{
    if (beforeCommitForTesting)
        beforeCommitForTesting();
    SongData next = in.song;
    {
        const juce::SpinLock::ScopedLockType sl (lock_);
        const auto g = generation_.load() + 1;
        slots_ = in.slots;
        alternates_ = in.alternates;
        std::swap (song_, next);
        slotGeneration_.fill (g);
        alternateGeneration_.fill (g);
        songGeneration_ = g;
        generation_.store (g);
    }
}

PatternStore::SongLive::SongLive()
    : slots (std::make_unique<std::array<StoredPattern, slotCount>>()),
      alternates (std::make_unique<std::array<StoredPattern, slotCount>>()),
      unique (std::make_unique<std::array<BarCells, kit::maxSongBars>>())
{
    slotGeneration.fill (~std::uint64_t { 0 });
    alternateGeneration.fill (~std::uint64_t { 0 });
    for (std::size_t i = 0; i < uniquePointers.size(); ++i)
        uniquePointers[i] = &(*unique)[i];
    buildSongView (table, slots->data(), alternates->data(), uniquePointers.data(), true, view);
}

// Realtime: tryEnter only; copies what changed into storage the SongLive
// already owns (worst case, after a project restore: 10 patterns and 64
// bars, ~0.7 MB, once). No allocation: the shared cells are read, never
// copied as shared_ptr.
const kit::Song* PatternStore::pullSong (SongLive& live) noexcept
{
    const juce::SpinLock::ScopedTryLockType tl (lock_);
    if (tl.isLocked())
    {
        bool changed = false;
        for (std::size_t i = 0; i < static_cast<std::size_t> (slotCount); ++i)
        {
            if (live.slotGeneration[i] != slotGeneration_[i])
            {
                (*live.slots)[i] = slots_[i];
                live.slotGeneration[i] = slotGeneration_[i];
                changed = true;
            }
            if (live.alternateGeneration[i] != alternateGeneration_[i])
            {
                (*live.alternates)[i] = alternates_[i];
                live.alternateGeneration[i] = alternateGeneration_[i];
                changed = true;
            }
        }
        if (live.songGeneration != songGeneration_)
        {
            live.table = tableOf (song_);
            live.songMode = song_.songMode;
            for (std::size_t i = 0; i < song_.bars.size(); ++i)
            {
                const auto& bar = song_.bars[i];
                if (bar.kind == SongBar::Kind::unique && bar.cells != nullptr && (*live.unique)[i].id != bar.cells->id)
                    (*live.unique)[i] = *bar.cells;
                if (bar.kind == SongBar::Kind::unique && bar.cells == nullptr)
                    live.table.bars[i].kind = SongBar::Kind::empty;
            }
            live.songGeneration = songGeneration_;
            changed = true;
        }
        if (changed)
            buildSongView (live.table, live.slots->data(), live.alternates->data(), live.uniquePointers.data(), true, live.view);
    }
    return live.songMode ? &live.view.song : nullptr;
}

} // namespace vox::drums
