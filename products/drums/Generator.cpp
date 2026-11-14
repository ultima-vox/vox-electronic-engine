#include "Generator.h"

#include "StyleRegistry.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace vox::drums {
namespace {

enum Lane { kick, snare, clap, closedHat, openHat, tom, rim, cymbal, perc1, perc2, perc3, perc4, perc5, perc6, perc7, perc8 };

// Deterministic hash of the seed and a position; uniform in [0, 1).
double rnd (std::uint32_t seed, std::uint32_t a, std::uint32_t b = 0, std::uint32_t c = 0) noexcept
{
    std::uint64_t x = (static_cast<std::uint64_t> (seed) << 32) ^ (static_cast<std::uint64_t> (a) * 0x9E3779B97F4A7C15ull)
                    ^ (static_cast<std::uint64_t> (b) * 0xC2B2AE3D27D4EB4Full) ^ (static_cast<std::uint64_t> (c) * 0x165667B19E3779F9ull);
    x ^= x >> 30;
    x *= 0xBF58476D1CE4E5B9ull;
    x ^= x >> 27;
    x *= 0x94D049BB133111EBull;
    x ^= x >> 31;
    return static_cast<double> (x >> 11) * (1.0 / 9007199254740992.0);
}

// One generated cell on the 16th grid of a bar (pos 0..15, plus finer
// positions for 32/64-step grids through `sub`).
struct Hit {
    bool on = false;
    float velocity = 0.8f;   // 0..1
    bool accent = false;
    int probability = 100, ratchet = 1;
};

struct Context {
    const GeneratorSettings& g;
    int bar = 0, bars = 1;
    bool lastBar() const noexcept { return bar == bars - 1; }
    double r (int lane, int pos, int salt = 0) const noexcept
    {
        return rnd (g.seed, static_cast<std::uint32_t> (lane * 131 + salt), static_cast<std::uint32_t> (pos),
                    static_cast<std::uint32_t> (bar));
    }
    // The same for every bar: the pattern's identity. Bars differ by `variation`.
    double base (int lane, int pos, int salt = 0) const noexcept
    {
        return rnd (g.seed, static_cast<std::uint32_t> (lane * 131 + salt), static_cast<std::uint32_t> (pos), 9999u);
    }
    bool chance (double p, int lane, int pos, int salt = 0) const noexcept
    {
        // A bar deviates from the pattern's base decision with probability `variation`.
        const bool varied = bar > 0 && r (lane, pos, salt + 500) < 0.5 * g.variation;
        return (varied ? r (lane, pos, salt) : base (lane, pos, salt)) < p;
    }
};

bool beat (int pos) noexcept { return pos % 4 == 0; }
bool offBeat (int pos) noexcept { return pos % 4 == 2; }

// The style being generated: its declarative definition (StyleRegistry).
const style::Definition* definitionOf (const GeneratorSettings& g)
{
    return style::factoryDefinition (static_cast<int> (g.style));
}

style::ParamValues paramsOf (const GeneratorSettings& g) noexcept
{
    style::ParamValues p;
    p.v = { static_cast<double> (g.density), static_cast<double> (g.groove), static_cast<double> (g.syncopation),
            static_cast<double> (g.complexity), static_cast<double> (g.fills), static_cast<double> (g.variation),
            static_cast<double> (g.swing), static_cast<double> (g.humanize) };
    return p;
}

// The hit of part `lane` at 16th `pos`: the style's rules for that part, in
// order. A hit rule sets the hit (velocity, accent); a ghost rule only fills
// an empty position, and with complexity above 0.5 may make it a 60 % step.
Hit styleHit (const style::Definition& def, const style::ParamValues& values, const Context& c, int lane, int pos) noexcept
{
    const double cx = values[style::Param::complexity];
    Hit h;
    const auto on = [&] (float velocity, bool accent) {
        h.on = true;
        h.velocity = velocity;
        h.accent = accent;
    };
    for (const auto& rule : def.rules)
    {
        if (rule.part != lane || (rule.positions & (1u << pos)) == 0)
            continue;
        if ((rule.bars == style::Bars::first && c.bar != 0) || (rule.bars == style::Bars::odd && c.bar % 2 != 0))
            continue;
        bool when = true;
        for (const auto& condition : rule.when)
            when = when && condition.holds (values);
        if (! when)
            continue;
        if (rule.kind == style::Rule::Kind::ghost)
        {
            if (! h.on && c.chance (rule.chance.evaluate (values), lane, pos, rule.salt + 40))
            {
                on (rule.velocity, false);
                if (cx > 0.5 && c.chance (cx - 0.5, lane, pos, rule.salt + 41))
                    h.probability = 60;
            }
            continue;
        }
        if (! rule.chance.empty() && ! c.chance (rule.chance.evaluate (values), lane, pos, rule.salt))
            continue;
        on (rule.velocity, rule.accent);
        if (rule.ratchet && c.chance (rule.ratchet->chance.evaluate (values), lane, pos, rule.ratchet->salt))
        {
            const double draw = c.r (lane, pos, rule.ratchet->valueSalt);
            h.ratchet = rule.ratchet->otherwise;
            for (const auto& [threshold, value] : rule.ratchet->below)
                if (draw < threshold)
                {
                    h.ratchet = value;
                    break;
                }
        }
    }
    if (def.probability && h.on && (def.probability->exceptParts & (1u << lane)) == 0)
        h.probability = std::clamp (static_cast<int> (std::lround (def.probability->value.evaluate (values))), def.probability->min,
                                    def.probability->max);
    return h;
}

kit::PatternStep toStep (const Context& c, int lane, int pos, const Hit& h) noexcept
{
    kit::PatternStep s;
    if (! h.on)
        return s;
    const auto& g = c.g;
    // Groove: accents lean to the off-beats; humanize: variance.
    double velocity = h.velocity;
    velocity *= 1.0 + g.groove * (offBeat (pos) ? 0.18 : beat (pos) ? 0.0 : -0.12);
    velocity *= 1.0 + g.humanize * 0.35 * (c.r (lane, pos, 90) - 0.5);
    s.on = true;
    s.accent = h.accent;
    s.velocity = static_cast<std::uint8_t> (std::clamp (static_cast<int> (std::lround (velocity * 127.0)), 1, 127));
    s.probability = static_cast<std::uint8_t> (std::clamp (h.probability, 1, 100));
    s.ratchet = static_cast<std::uint8_t> (std::clamp (h.ratchet, 1, kit::maxRatchet));
    double micro = 0.0;
    if (pos % 2 == 1)
        micro += 50.0 * std::clamp (g.swing, 0.0f, 1.0f);
    if (lane != kick || ! beat (pos))   // the downbeat kick stays on the grid
        micro += g.humanize * 16.0 * (c.r (lane, pos, 91) - 0.5);
    s.micro = static_cast<std::int8_t> (std::clamp (static_cast<int> (std::lround (micro)), -50, 50));
    return s;
}

// Writes `step` unless the cell or its lane is locked.
void put (const GeneratorSettings& g, StoredPattern& p, int lane, int bar, int step, const kit::PatternStep& value) noexcept
{
    auto& cell = p.cell (lane, bar, step);
    if (cell.locked || (g.laneLocks & (1u << lane)) != 0)
        return;
    cell = value;
}

// Returns whether any cell was open to the generator (not locked).
// `laneMask` selects pad lanes; each lane gets the result of its role slot
// (nothing for a lane without one). With the identity map slot == lane.
bool generateLanes (const GeneratorSettings& g, std::uint32_t laneMask, StoredPattern& p, const RoleMap& roles)
{
    const auto* def = definitionOf (g);
    const auto values = paramsOf (g);
    bool open = false;
    const int scale = p.steps / 16;   // the styles are written on 16ths
    for (int lane = 0; lane < kit::padCount; ++lane)
    {
        if ((laneMask & (1u << lane)) == 0)
            continue;
        const int slot = roles.slotOfLane[static_cast<std::size_t> (lane)];
        for (int bar = 0; bar < p.bars; ++bar)
        {
            const Context c { g, bar, p.bars };
            for (int step = 0; step < p.steps; ++step)
            {
                kit::PatternStep value;
                if (step % scale == 0 && slot >= 0 && def != nullptr)
                    value = toStep (c, slot, step / scale, styleHit (*def, values, c, slot, step / scale));
                open = open || ! (p.cell (lane, bar, step).locked || (g.laneLocks & (1u << lane)) != 0);
                put (g, p, lane, bar, step, value);
            }
        }
    }
    return open;
}

// The part a fill plays at `index`: the style's fill cycle.
int fillLane (const style::Definition& def, int index) noexcept
{
    return def.fill.cycle[static_cast<std::size_t> (index) % def.fill.cycle.size()];
}

} // namespace

const char* styleName (Style style) noexcept
{
    const auto* info = style::infoById (static_cast<int> (style));
    return info != nullptr ? info->name : "?";
}

bool GeneratorSettings::operator== (const GeneratorSettings& o) const noexcept
{
    return style == o.style && juce::exactlyEqual (density, o.density) && juce::exactlyEqual (groove, o.groove)
        && juce::exactlyEqual (syncopation, o.syncopation) && juce::exactlyEqual (swing, o.swing)
        && juce::exactlyEqual (humanize, o.humanize) && juce::exactlyEqual (complexity, o.complexity)
        && juce::exactlyEqual (fills, o.fills) && juce::exactlyEqual (variation, o.variation) && seed == o.seed
        && laneLocks == o.laneLocks;
}

void generatePattern (const GeneratorSettings& g, StoredPattern& pattern, const RoleMap& roles)
{
    // The probability seed follows the generator's only when the generator
    // wrote something: a fully locked pattern stays exactly as it is.
    if (generateLanes (g, 0xffffu, pattern, roles))
        pattern.seed = g.seed;
    if (g.fills > 0.05f && pattern.bars > 1)
        generateFill (g, pattern, -1, roles);
}

void mutateLanes (const GeneratorSettings& g, std::uint32_t laneMask, StoredPattern& pattern, const RoleMap& roles)
{
    generateLanes (g, laneMask & 0xffffu, pattern, roles);
}

void generateFill (const GeneratorSettings& g, StoredPattern& p, int bar, const RoleMap& roles)
{
    const auto laneOf = [&] (int slot) { return static_cast<int> (roles.laneOfSlot[static_cast<std::size_t> (slot)]); };
    const auto* def = definitionOf (g);
    if (def == nullptr)
        return;
    if (bar < 0 || bar >= p.bars)
        bar = p.bars - 1;
    const int scale = p.steps / 16;
    // The fill takes the last 2..8 sixteenths of the bar.
    const int length = std::clamp (static_cast<int> (std::lround (2.0 + 6.0 * g.fills)), 2, 8);
    const Context c { g, bar, p.bars };
    for (int pos = 16 - length; pos < 16; ++pos)
    {
        const int index = pos - (16 - length);
        const int lane = fillLane (*def, index + static_cast<int> (c.r (0, pos, 70) * 2.0));
        // Thin out the groove under the fill, then write the roll rising to the bar line.
        for (int other : { static_cast<int> (closedHat), static_cast<int> (openHat), static_cast<int> (perc1), static_cast<int> (perc2) })
            if (c.r (other, pos, 71) < 0.5 * g.fills && laneOf (other) >= 0)
                put (g, p, laneOf (other), bar, pos * scale, {});
        if (c.r (lane, pos, 72) > 0.35 + 0.65 * g.fills && index < length - 1)
            continue;
        Hit h;
        h.on = true;
        h.velocity = static_cast<float> (0.45 + 0.5 * (index + 1) / length);
        h.accent = index == length - 1;
        if (g.complexity > 0.3f && c.r (lane, pos, 73) < g.complexity - 0.2)
            h.ratchet = def->fill.ratchet;
        if (laneOf (lane) >= 0)   // no pad with this role: the hit is not written
            put (g, p, laneOf (lane), bar, pos * scale, toStep (c, lane, pos, h));
    }
}

void generateVariation (const GeneratorSettings& g, const StoredPattern& a, StoredPattern& b, const RoleMap& roles)
{
    // B keeps its own locked cells: build from A, then put B's locked cells back.
    auto locked = std::make_unique<StoredPattern> (b);
    const bool sameGrid = b.steps == a.steps && b.bars == a.bars;
    if (! sameGrid)
    {
        // B would take A's grid, where its locked cells have no place: with
        // anything locked in B, B is left exactly as it is.
        bool anyLocked = false;
        for (int lane = 0; lane < kit::padCount && ! anyLocked; ++lane)
            for (int bar = 0; bar < b.bars && ! anyLocked; ++bar)
                for (int step = 0; step < b.steps && ! anyLocked; ++step)
                    anyLocked = b.cell (lane, bar, step).locked
                             || ((g.laneLocks & (1u << lane)) != 0 && b.cell (lane, bar, step).on);
        if (anyLocked)
            return;
    }
    b = a;
    GeneratorSettings v = g;
    v.seed = g.seed * 2654435761u + 0x9E3779B9u;
    // The decorating lanes are regenerated with the derived seed; kick and the
    // back-beat stay. Decided per role slot. A lane without a slot (role none,
    // a duplicate) is always in the mask, so it is cleared like in full generation.
    std::uint32_t mask = 0;
    for (int lane = 0; lane < kit::padCount; ++lane)
    {
        const int slot = roles.slotOfLane[static_cast<std::size_t> (lane)];
        if (slot < 0
            || (slot != kick && slot != snare && slot != clap
                && rnd (v.seed, static_cast<std::uint32_t> (slot), 77) < 0.25 + 0.75 * g.variation))
            mask |= 1u << lane;
    }
    generateLanes (v, mask, b, roles);
    generateFill (v, b, -1, roles);
    b.seed = v.seed;
    if (sameGrid)
        for (int lane = 0; lane < kit::padCount; ++lane)
            for (int bar = 0; bar < b.bars; ++bar)
                for (int step = 0; step < b.steps; ++step)
                    if (locked->cell (lane, bar, step).locked || (g.laneLocks & (1u << lane)) != 0)
                        b.cell (lane, bar, step) = locked->cell (lane, bar, step);
}

void generateSong (const GeneratorSettings& g, PatternStore::Everything& all, const RoleMap& roles)
{
    const int kickLane = roles.laneOfSlot[static_cast<std::size_t> (kick)];
    // Slots: 1 intro (sparse), 2 main, 3 lift (denser), 4 break (no kick), 5 peak.
    struct Part { float density, complexity, fills; bool kickOn; };
    static constexpr Part parts[slotCountForSong] = {
        { 0.45f, 0.5f, 0.6f, true }, { 1.0f, 1.0f, 1.0f, true }, { 1.2f, 1.15f, 1.1f, true },
        { 0.6f, 0.9f, 0.8f, false }, { 1.4f, 1.3f, 1.25f, true }
    };
    for (int slot = 0; slot < slotCountForSong; ++slot)
    {
        GeneratorSettings s = g;
        s.seed = g.seed + static_cast<std::uint32_t> (slot) * 7919u;
        s.density = std::clamp (g.density * parts[slot].density, 0.0f, 1.0f);
        s.complexity = std::clamp (g.complexity * parts[slot].complexity, 0.0f, 1.0f);
        s.fills = std::clamp (g.fills * parts[slot].fills, 0.0f, 1.0f);
        auto& a = all.slots[static_cast<std::size_t> (slot)];
        generatePattern (s, a, roles);
        if (! parts[slot].kickOn && kickLane >= 0)
            for (int bar = 0; bar < a.bars; ++bar)
                for (int step = 0; step < a.steps; ++step)
                    put (g, a, kickLane, bar, step, {});
        generateVariation (s, a, all.alternates[static_cast<std::size_t> (slot)], roles);
    }
    // Timeline in eight-bar phrases: intro, main, lift, break, peak, ...;
    // the last four bars of every phrase are the B variation (it ends in a fill).
    auto& song = all.song;
    const int phrases = song.length / 8;
    static constexpr int order16[2] = { 1, 4 };
    static constexpr int order32[4] = { 0, 1, 3, 4 };
    static constexpr int order64[8] = { 0, 1, 2, 3, 1, 2, 4, 4 };
    for (int bar = 0; bar < kit::maxSongBars; ++bar)
        song.bars[static_cast<std::size_t> (bar)] = {};
    for (int phrase = 0; phrase < phrases; ++phrase)
    {
        const int slot = song.length == 16 ? order16[phrase] : song.length == 32 ? order32[phrase] : order64[phrase];
        song.place (phrase * 8, slot, 0, all.slots[static_cast<std::size_t> (slot)].bars);
        song.place (phrase * 8 + 4, slot, 1, all.alternates[static_cast<std::size_t> (slot)].bars);
        // Patterns shorter than four bars repeat to fill the phrase.
        const int bars = std::max<int> (1, all.slots[static_cast<std::size_t> (slot)].bars);
        for (int k = bars; k < 4; ++k)
        {
            song.place (phrase * 8 + k, slot, 0, 1);
            song.bars[static_cast<std::size_t> (phrase * 8 + k)].patternBar = static_cast<std::uint8_t> (k % bars);
            song.place (phrase * 8 + 4 + k, slot, 1, 1);
            song.bars[static_cast<std::size_t> (phrase * 8 + 4 + k)].patternBar = static_cast<std::uint8_t> (k % bars);
        }
    }
}

//==============================================================================
// DGEN: u32 version (1), u8 style, 8 x f32 (density, groove, syncopation,
// swing, humanize, complexity, fills, variation), u32 seed, u16 lane locks.
namespace {
constexpr std::uint32_t generatorVersion = 1;
}

preset::Section encodeGenerator (const GeneratorSettings& g)
{
    preset::Section s;
    s.tag = generatorSectionTag;
    const auto u32 = [&] (std::uint32_t v) {
        for (int shift = 0; shift < 32; shift += 8)
            s.data.push_back (static_cast<std::byte> ((v >> shift) & 0xffu));
    };
    u32 (generatorVersion);
    s.data.push_back (static_cast<std::byte> (g.style));
    for (float v : { g.density, g.groove, g.syncopation, g.swing, g.humanize, g.complexity, g.fills, g.variation })
    {
        std::uint32_t bits = 0;
        std::memcpy (&bits, &v, sizeof bits);
        u32 (bits);
    }
    u32 (g.seed);
    s.data.push_back (static_cast<std::byte> (g.laneLocks & 0xffu));
    s.data.push_back (static_cast<std::byte> (g.laneLocks >> 8));
    return s;
}

bool decodeGenerator (const preset::Section& section, GeneratorSettings& out, juce::String& error)
{
    const auto& d = section.data;
    const auto u32 = [&] (std::size_t at) {
        std::uint32_t v = 0;
        for (std::size_t i = 0; i < 4; ++i)
            v |= std::to_integer<std::uint32_t> (d[at + i]) << (8 * i);
        return v;
    };
    if (d.size() != 4 + 1 + 8 * 4 + 4 + 2 || u32 (0) != generatorVersion)
    {
        error = "unsupported generator section";
        return false;
    }
    GeneratorSettings g;
    const auto style = std::to_integer<std::uint8_t> (d[4]);
    float* fields[8] = { &g.density, &g.groove, &g.syncopation, &g.swing, &g.humanize, &g.complexity, &g.fills, &g.variation };
    bool ok = style < styleCount;
    for (std::size_t i = 0; i < 8; ++i)
    {
        const auto bits = u32 (5 + i * 4);
        std::memcpy (fields[i], &bits, sizeof bits);
        ok = ok && std::isfinite (*fields[i]) && *fields[i] >= 0.0f && *fields[i] <= 1.0f;
    }
    if (! ok)
    {
        error = "invalid generator section";
        return false;
    }
    g.style = static_cast<Style> (style);
    g.seed = u32 (37);
    g.laneLocks = static_cast<std::uint16_t> (std::to_integer<std::uint16_t> (d[41]) | (std::to_integer<std::uint16_t> (d[42]) << 8));
    out = g;
    return true;
}

} // namespace vox::drums
