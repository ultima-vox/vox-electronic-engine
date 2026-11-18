#include "Generator.h"

#include "StyleRegistry.h"

#include <juce_cryptography/juce_cryptography.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>

namespace vox::drums {
static_assert (factoryStyleCount == style::registrySize);
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
    style::Meter meter = style::Meter::m4_4;
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

// A beat and an off-beat 16th on the meter's grid: quarter beats in 4/4 and
// 3/4 (4 sixteenths), dotted-quarter beats in 6/8 and 12/8 (6 sixteenths,
// the off-beat on the middle eighth). Tuplet cells (keys >= 32) are neither.
bool beat (int pos, style::Meter m = style::Meter::m4_4) noexcept
{
    return pos < 32 && pos % (style::compoundMeter (m) ? 6 : 4) == 0;
}
bool offBeat (int pos, style::Meter m = style::Meter::m4_4) noexcept
{
    return pos < 32 && (style::compoundMeter (m) ? pos % 6 == 3 : pos % 4 == 2);
}

// The random key of a tuplet cell (beat 0..15, cell 0..5): apart from the
// straight 16th positions 0..23.
constexpr int tupletKey (int beat, int cell) noexcept { return 32 + beat * 8 + cell; }

// The style being generated: its declarative definition (StyleRegistry).
const style::Definition* definitionOf (const GeneratorSettings& g)
{
    return g.custom != nullptr ? g.custom.get() : style::factoryDefinition (static_cast<int> (g.style));
}

static_assert (std::tuple_size_v<GeneratorValues> == 8, "PatternHistory stores the eight generator values");

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
// `pos` is the 16th position on the meter's grid (straight), or with
// `tupletBeat` >= 0 the tuplet cell within that beat; `key` the random key
// (pos for straight cells, tupletKey for tuplet cells).
Hit styleHit (const style::Definition& def, const style::ParamValues& values, const Context& c, int lane, int pos, int tupletBeat = -1) noexcept
{
    const int key = tupletBeat < 0 ? pos : tupletKey (tupletBeat, pos);
    const double cx = values[style::Param::complexity];
    Hit h;
    const auto on = [&] (float velocity, bool accent) {
        h.on = true;
        h.velocity = velocity;
        h.accent = accent;
    };
    for (const auto& rule : def.rules)
    {
        // The rules of the pattern's meter; straight rules for straight cells,
        // tuplet rules for the cells of their beats (PR 137 §10.6).
        if (rule.part != lane || rule.meter != c.meter || (rule.positions & (1u << pos)) == 0)
            continue;
        if (tupletBeat < 0 ? rule.tupletBeats != 0 : ((rule.tupletBeats >> tupletBeat) & 1u) == 0)
            continue;
        if (! style::barApplies (rule.bars, c.bar, c.bars))
            continue;
        bool when = true;
        for (const auto& condition : rule.when)
            when = when && condition.holds (values);
        if (! when)
            continue;
        if (rule.kind == style::Rule::Kind::ghost)
        {
            if (! h.on && c.chance (rule.chance.evaluate (values), lane, key, rule.salt + 40))
            {
                on (rule.velocity, false);
                if (cx > 0.5 && c.chance (cx - 0.5, lane, key, rule.salt + 41))
                    h.probability = 60;
            }
            continue;
        }
        if (! rule.chance.empty() && ! c.chance (rule.chance.evaluate (values), lane, key, rule.salt))
            continue;
        on (rule.velocity, rule.accent);
        if (rule.ratchet && c.chance (rule.ratchet->chance.evaluate (values), lane, key, rule.ratchet->salt))
        {
            const double draw = c.r (lane, key, rule.ratchet->valueSalt);
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

// The storage slot of the cell of `t` that starts at `tick` from the bar
// start, or -1 when no cell starts there; `beatOut` gets its beat.
int slotAtTick (const timing::BarTiming& t, int tick, int* beatOut = nullptr) noexcept
{
    const int beatTicks = timing::beatTicks (timing::beatKind (t.signature));
    const int beat = tick / beatTicks;
    if (tick < 0 || beat >= t.beats())
        return -1;
    const int cellTicks = t.cellTicks (beat), within = tick - beat * beatTicks;
    if (cellTicks <= 0 || within % cellTicks != 0)
        return -1;
    if (beatOut != nullptr)
        *beatOut = beat;
    return t.slot (beat, within / cellTicks);
}

// The cell of 16th position `pos` on the bar's straight grid.
int slotOf16th (const timing::BarTiming& t, int pos) noexcept
{
    return slotAtTick (t, pos * timing::ticksPerQuarter / 4);
}

// The style meter of a pattern signature (4/4, 3/4, 6/8, 12/8), else none.
std::optional<style::Meter> styleMeter (timing::TimeSignature s) noexcept
{
    if (s == timing::TimeSignature { 4, 4 }) return style::Meter::m4_4;
    if (s == timing::TimeSignature { 3, 4 }) return style::Meter::m3_4;
    if (s == timing::TimeSignature { 6, 8 }) return style::Meter::m6_8;
    if (s == timing::TimeSignature { 12, 8 }) return style::Meter::m12_8;
    return std::nullopt;
}

// The pattern is in a meter the style defines, on a valid grid (PR 137
// §10.6: anything else is refused).
bool generatorGrid (const style::Definition& def, const StoredPattern& p) noexcept
{
    const auto m = styleMeter (p.signature);
    return m && (def.meters & (1u << static_cast<int> (*m))) != 0 && timing::BarTiming { p.signature, p.resolution, 0 }.valid();
}

// The tuplet cells per beat at the 16th level: triplet 6 in a quarter beat,
// duplet 4 in a dotted-quarter beat.
int tupletCells16 (style::Meter m) noexcept
{
    return style::compoundMeter (m) ? 4 : 6;
}

// `cellTicks`: the length of the cell the step goes to (micro is a percent
// of it, quantised once to ticks).
kit::PatternStep toStep (const Context& c, int lane, int pos, const Hit& h, int cellTicks) noexcept
{
    kit::PatternStep s;
    if (! h.on)
        return s;
    const auto& g = c.g;
    // Groove: accents lean to the off-beats; humanize: variance.
    double velocity = h.velocity;
    velocity *= 1.0 + g.groove * (offBeat (pos, c.meter) ? 0.18 : beat (pos, c.meter) ? 0.0 : -0.12);
    velocity *= 1.0 + g.humanize * 0.35 * (c.r (lane, pos, 90) - 0.5);
    s.on = true;
    s.accent = h.accent;
    s.velocity = static_cast<std::uint8_t> (std::clamp (static_cast<int> (std::lround (velocity * 127.0)), 1, 127));
    s.probability = static_cast<std::uint8_t> (std::clamp (h.probability, 1, 100));
    s.ratchet = static_cast<std::uint8_t> (std::clamp (h.ratchet, 1, kit::maxRatchet));
    double micro = 0.0;
    if (pos < 32 && pos % 2 == 1)   // odd straight 16ths; tuplet cells are not swung
        micro += 50.0 * std::clamp (g.swing, 0.0f, 1.0f);
    if (lane != kick || ! beat (pos, c.meter))   // the downbeat kick stays on the grid
        micro += g.humanize * 16.0 * (c.r (lane, pos, 91) - 0.5);
    s.microTicks = static_cast<std::int16_t> (
        kit::percentToTicks (std::clamp (static_cast<int> (std::lround (micro)), -50, 50), cellTicks));
    return s;
}

// Writes `step` unless the cell or its lane is locked.
void put (const GeneratorSettings& g, StoredPattern& p, int lane, int bar, int slot, const kit::PatternStep& value) noexcept
{
    auto& cell = p.cell (lane, bar, slot);
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
    const auto meter = styleMeter (p.signature).value_or (style::Meter::m4_4);
    const int positions = style::gridPositions (meter);
    const int n16 = tupletCells16 (meter);
    const bool tuplets = timing::capacity (p.signature, p.resolution).availability == timing::Availability::full;
    const int beatTicks = timing::beatTicks (timing::beatKind (p.signature));
    bool open = false;
    // The styles are written on 16ths: a cell starting on a 16th gets that
    // position's hit (a tuplet cell: its tuplet rules); every other cell of
    // the lane is cleared.
    for (int lane = 0; lane < kit::padCount; ++lane)
    {
        if ((laneMask & (1u << lane)) == 0)
            continue;
        const int slot = roles.slotOfLane[static_cast<std::size_t> (lane)];
        const bool laneLocked = (g.laneLocks & (1u << lane)) != 0;
        for (int bar = 0; bar < p.bars; ++bar)
        {
            const Context c { g, bar, p.bars, meter };
            // PR 137 §10.6: the beats this part plays as tuplets in this bar
            // (a tuplet rule of the meter whose bars and conditions apply). A
            // beat changes its subdivision only when none of its cells is
            // locked; it is then generated anew.
            if (slot >= 0 && def != nullptr && tuplets && ! laneLocked)
            {
                std::uint16_t want = 0;
                for (const auto& rule : def->rules)
                {
                    if (rule.part != slot || rule.meter != meter || rule.tupletBeats == 0)
                        continue;
                    if (! style::barApplies (rule.bars, bar, p.bars))
                        continue;
                    bool when = true;
                    for (const auto& condition : rule.when)
                        when = when && condition.holds (values);
                    if (when)
                        want = static_cast<std::uint16_t> (want | rule.tupletBeats);
                }
                const auto before = p.timing (lane, bar);
                want = static_cast<std::uint16_t> (want & ((1u << before.beats()) - 1u));
                auto mask = before.alternativeBeats;
                for (int b = 0; b < before.beats(); ++b)
                {
                    const auto bit = static_cast<std::uint16_t> (1u << b);
                    if (((want ^ mask) & bit) == 0)
                        continue;
                    bool locked = false;
                    for (int k = 0; k < before.slotsPerBeat(); ++k)
                        locked = locked || p.cell (lane, bar, before.slot (b, k)).locked;
                    if (locked)
                        continue;
                    for (int k = 0; k < before.slotsPerBeat(); ++k)
                        p.cell (lane, bar, before.slot (b, k)) = {};
                    mask = static_cast<std::uint16_t> (mask ^ bit);
                }
                p.mask (lane, bar) = mask;
            }
            const auto t = p.timing (lane, bar);
            for (int beat = 0; beat < t.beats(); ++beat)
            {
                const bool alternative = ((t.alternativeBeats >> beat) & 1u) != 0;
                for (int cell = 0; cell < t.cells (beat); ++cell)
                {
                    const int start = t.cellStartTick (beat, cell);
                    const int at = t.slot (beat, cell);
                    kit::PatternStep value;
                    if (slot >= 0 && def != nullptr)
                    {
                        if (alternative)
                        {
                            // Tuplet cell k of the beat at the 16th level.
                            const int within = start - beat * beatTicks, unit = beatTicks / n16;
                            if (within % unit == 0)
                                value = toStep (c, slot, tupletKey (beat, within / unit),
                                                styleHit (*def, values, c, slot, within / unit, beat), t.cellTicks (beat));
                        }
                        else
                        {
                            const int pos = start % (timing::ticksPerQuarter / 4) == 0 ? start / (timing::ticksPerQuarter / 4) : -1;
                            if (pos >= 0 && pos < positions)
                                value = toStep (c, slot, pos, styleHit (*def, values, c, slot, pos), t.cellTicks (beat));
                        }
                    }
                    open = open || ! (p.cell (lane, bar, at).locked || laneLocked);
                    put (g, p, lane, bar, at, value);
                }
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

namespace {
// The assertion check plays every part the style declares (at most 16, §2.4)
// on a pad of its own, so assertions on ride / tom 2 / tom 3 (no pad in the
// identity map) are checked too.
RoleMap assertionMap (const style::Definition& d) noexcept
{
    RoleMap m;
    m.slotOfLane.fill (-1);
    m.laneOfSlot.fill (-1);
    const auto declared = d.declaredParts();
    int lane = 0;
    for (int part = 0; part < RoleMap::parts && lane < kit::padCount; ++part)
        if ((declared >> part) & 1u)
        {
            m.slotOfLane[static_cast<std::size_t> (lane)] = static_cast<std::int8_t> (part);
            m.laneOfSlot[static_cast<std::size_t> (part)] = static_cast<std::int8_t> (lane);
            ++lane;
        }
    return m;
}
} // namespace

juce::StringArray checkStyleAssertions (const std::shared_ptr<const style::Definition>& definition, int maxViolations)
{
    juce::StringArray out;
    if (definition == nullptr)
        return { "no style" };
    const auto map = assertionMap (*definition);
    auto p = std::make_unique<StoredPattern>();
    static constexpr timing::TimeSignature signatures[style::meterCount] = { { 4, 4 }, { 3, 4 }, { 6, 8 }, { 12, 8 } };
    for (int m = 0; m < style::meterCount; ++m)
    {
        if ((definition->meters & (1u << m)) == 0)
            continue;
        const auto meter = static_cast<style::Meter> (m);
        for (std::uint32_t seed = 1; seed <= 6; ++seed)
            for (float density : { 0.0f, 0.5f, 1.0f })
                for (float complexity : { 0.0f, 0.5f, 1.0f })
                {
                    GeneratorSettings g;
                    g.custom = definition;
                    g.seed = seed;
                    g.density = density;
                    g.complexity = complexity;
                    g.fills = 0.0f;
                    *p = {};   // 16ths, 4 bars, empty
                    p->signature = signatures[m];
                    generatePattern (g, *p, map);
                    for (const auto& a : definition->assertions)
                    {
                        const int lane = map.laneOfSlot[static_cast<std::size_t> (a.part)];
                        if (a.meter != meter)
                            continue;
                        for (int bar = 0; bar < p->bars; ++bar)
                            for (int pos = 0; pos < style::gridPositions (meter); ++pos)
                            {
                                const int at = lane < 0 ? -1 : slotOf16th (p->timing (lane, bar), pos);
                                const bool on = at >= 0 && p->cell (lane, bar, at).on;
                                const bool wrong = ((a.always & (1u << pos)) != 0 && ! on) || ((a.never & (1u << pos)) != 0 && on);
                                if (wrong && out.size() < maxViolations)
                                    out.add (juce::String (style::partKey (a.part)) + (on ? " plays" : " is missing") + " at " + juce::String (pos + 1)
                                             + " in " + style::meterKey (meter) + " (bar " + juce::String (bar + 1) + ", seed " + juce::String (seed)
                                             + ", density " + juce::String (density) + ", complexity " + juce::String (complexity) + ")");
                            }
                    }
                }
    }
    return out;
}

GeneratorValues generatorValues (const GeneratorSettings& g) noexcept
{
    return { g.density, g.groove, g.syncopation, g.complexity, g.fills, g.variation, g.swing, g.humanize };
}

void setGeneratorValues (GeneratorSettings& g, const GeneratorValues& v) noexcept
{
    float* fields[] = { &g.density, &g.groove, &g.syncopation, &g.complexity, &g.fills, &g.variation, &g.swing, &g.humanize };
    for (std::size_t i = 0; i < v.size(); ++i)
        *fields[i] = std::clamp (v[i], 0.0f, 1.0f);
}

std::optional<GeneratorValues> styleDefaults (const GeneratorSettings& g)
{
    const auto* def = definitionOf (g);
    if (def == nullptr)
        return std::nullopt;
    return def->defaults.values;
}

bool styleAvailable (Style style)
{
    return style::available (static_cast<int> (style));
}

juce::String generatorRefusal (const GeneratorSettings& g, const StoredPattern& pattern)
{
    const auto* def = definitionOf (g);
    if (def == nullptr)
        return "the style is not available";
    if (! generatorGrid (*def, pattern))
    {
        juce::StringArray meters;
        for (int m = 0; m < style::meterCount; ++m)
            if ((def->meters >> m) & 1u)
                meters.add (style::meterKey (static_cast<style::Meter> (m)));
        return "the style is written for " + meters.joinIntoString (", ") + ", not "
             + juce::String (pattern.signature.numerator) + "/" + juce::String (pattern.signature.denominator);
    }
    return {};
}

bool generatePattern (const GeneratorSettings& g, StoredPattern& pattern, const RoleMap& roles)
{
    if (definitionOf (g) == nullptr || ! generatorGrid (*definitionOf (g), pattern))
        return false;   // before anything changes
    // The probability seed follows the generator's only when the generator
    // wrote something: a fully locked pattern stays exactly as it is.
    if (generateLanes (g, 0xffffu, pattern, roles))
        pattern.seed = g.seed;
    if (g.fills > 0.05f && pattern.bars > 1)
        generateFill (g, pattern, -1, roles);
    return true;
}

bool mutateLanes (const GeneratorSettings& g, std::uint32_t laneMask, StoredPattern& pattern, const RoleMap& roles)
{
    if (definitionOf (g) == nullptr || ! generatorGrid (*definitionOf (g), pattern))
        return false;
    generateLanes (g, laneMask & 0xffffu, pattern, roles);
    return true;
}

bool generateFill (const GeneratorSettings& g, StoredPattern& p, int bar, const RoleMap& roles)
{
    const auto laneOf = [&] (int slot) { return static_cast<int> (roles.laneOfSlot[static_cast<std::size_t> (slot)]); };
    const auto* def = definitionOf (g);
    if (def == nullptr || ! generatorGrid (*def, p))
        return false;
    const auto meter = styleMeter (p.signature).value_or (style::Meter::m4_4);
    const int positions = style::gridPositions (meter);
    if (bar < 0 || bar >= p.bars)
        bar = p.bars - 1;
    // The fill takes the last 2..8 sixteenths of the bar (straight cells).
    const int length = std::clamp (static_cast<int> (std::lround (2.0 + 6.0 * g.fills)), 2, def->fill.maxLength);
    const Context c { g, bar, p.bars, meter };
    for (int pos = positions - length; pos < positions; ++pos)
    {
        const int index = pos - (positions - length);
        const int lane = fillLane (*def, index + static_cast<int> (c.r (0, pos, 70) * 2.0));
        // Thin out the groove under the fill, then write the roll rising to the bar line.
        for (int other : { static_cast<int> (closedHat), static_cast<int> (openHat), static_cast<int> (perc1), static_cast<int> (perc2) })
            if (c.r (other, pos, 71) < 0.5 * g.fills && laneOf (other) >= 0)
                if (const int at = slotOf16th (p.timing (laneOf (other), bar), pos); at >= 0)
                    put (g, p, laneOf (other), bar, at, {});
        if (c.r (lane, pos, 72) > 0.35 + 0.65 * g.fills && index < length - 1)
            continue;
        Hit h;
        h.on = true;
        h.velocity = std::min (def->fill.maxVelocity, static_cast<float> (0.45 + 0.5 * (index + 1) / length));
        h.accent = index == length - 1;
        if (g.complexity > 0.3f && c.r (lane, pos, 73) < g.complexity - 0.2)
            h.ratchet = def->fill.ratchet;
        if (laneOf (lane) < 0)   // no pad with this role: the hit is not written
            continue;
        const auto t = p.timing (laneOf (lane), bar);
        if (const int at = slotOf16th (t, pos); at >= 0)   // a 16th the lane's grid has
        {
            int beatOfPos = 0;
            slotAtTick (t, pos * timing::ticksPerQuarter / 4, &beatOfPos);
            put (g, p, laneOf (lane), bar, at, toStep (c, lane, pos, h, t.cellTicks (beatOfPos)));
        }
    }
    return true;
}

bool generateVariation (const GeneratorSettings& g, const StoredPattern& a, StoredPattern& b, const RoleMap& roles)
{
    if (definitionOf (g) == nullptr || ! generatorGrid (*definitionOf (g), a))
        return false;   // before B is copied from A
    // B keeps its own locked cells: build from A, then put B's locked cells back.
    auto locked = std::make_unique<StoredPattern> (b);
    const bool sameGrid = b.signature == a.signature && b.resolution == a.resolution && b.bars == a.bars && b.masks == a.masks;
    if (! sameGrid)
    {
        // B would take A's grid, where its locked cells have no place: with
        // anything locked in B, B is left exactly as it is.
        bool anyLocked = false;
        for (int lane = 0; lane < kit::padCount && ! anyLocked; ++lane)
            for (int bar = 0; bar < b.bars && ! anyLocked; ++bar)
                for (int slot = 0; slot < kit::slotStride && ! anyLocked; ++slot)
                    anyLocked = b.cell (lane, bar, slot).locked
                             || ((g.laneLocks & (1u << lane)) != 0 && b.cell (lane, bar, slot).on);
        if (anyLocked)
            return true;   // available; B kept because of its locks
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
            || ((definitionOf (g)->variationKeep & (1u << slot)) == 0
                && rnd (v.seed, static_cast<std::uint32_t> (slot), 77) < 0.25 + 0.75 * g.variation))
            mask |= 1u << lane;
    }
    generateLanes (v, mask, b, roles);
    generateFill (v, b, -1, roles);
    b.seed = v.seed;
    if (sameGrid)
        for (int lane = 0; lane < kit::padCount; ++lane)
            for (int bar = 0; bar < b.bars; ++bar)
                for (int slot = 0; slot < kit::slotStride; ++slot)
                    if (locked->cell (lane, bar, slot).locked || (g.laneLocks & (1u << lane)) != 0)
                        b.cell (lane, bar, slot) = locked->cell (lane, bar, slot);
    return true;
}

bool generateSong (const GeneratorSettings& g, PatternStore::Everything& all, const RoleMap& roles)
{
    if (definitionOf (g) == nullptr
        || std::any_of (all.slots.begin(), all.slots.end(), [&] (const StoredPattern& p) { return ! generatorGrid (*definitionOf (g), p); }))
        return false;   // before any slot, variation or song bar changes
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
        if (! parts[slot].kickOn && kickLane >= 0 && definitionOf (g)->songBreakWithoutKick)
            for (int bar = 0; bar < a.bars; ++bar)
                for (int cell = 0; cell < kit::slotStride; ++cell)
                    put (g, a, kickLane, bar, cell, {});
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
    return true;
}

//==============================================================================
// DGEN: u32 version (1), u8 style, 8 x f32 (density, groove, syncopation,
// swing, humanize, complexity, fills, variation), u32 seed, u16 lane locks.
namespace {
constexpr std::uint32_t generatorVersion = 2;   // DGEN v2: style by UUID (no v1 decoder, pre-release policy)
}

namespace {

// The 16 bytes of a canonical UUID text, in the order of its hex digits.
std::array<std::uint8_t, 16> uuidBytes (const juce::String& uuid)
{
    std::array<std::uint8_t, 16> out {};
    const auto hex = uuid.removeCharacters ("-");
    for (int i = 0; i < 16 && hex.length() == 32; ++i)
        out[static_cast<std::size_t> (i)] = static_cast<std::uint8_t> (hex.substring (2 * i, 2 * i + 2).getHexValue32());
    return out;
}

juce::String uuidText (const std::byte* b)
{
    juce::String hex;
    for (int i = 0; i < 16; ++i)
    {
        hex << juce::String::toHexString (std::to_integer<int> (b[i])).paddedLeft ('0', 2);
        if (i == 3 || i == 5 || i == 7 || i == 9)
            hex << "-";
    }
    return hex.toLowerCase();
}

std::uint32_t readU32 (const std::vector<std::byte>& d, std::size_t at)
{
    std::uint32_t v = 0;
    for (std::size_t i = 0; i < 4; ++i)
        v |= std::to_integer<std::uint32_t> (d[at + i]) << (8 * i);
    return v;
}

void writeU32 (std::vector<std::byte>& d, std::uint32_t v)
{
    for (int shift = 0; shift < 32; shift += 8)
        d.push_back (static_cast<std::byte> ((v >> shift) & 0xffu));
}

constexpr std::uint32_t styleSectionVersion = 1;
constexpr std::size_t maxStyleText = 65536;

} // namespace

juce::String styleUuidOf (const GeneratorSettings& g)
{
    if (g.custom != nullptr)
        return g.custom->id;
    const auto* info = style::infoById (static_cast<int> (g.style));
    return info != nullptr ? juce::String (info->uuid) : juce::String();
}

preset::Section encodeGenerator (const GeneratorSettings& g)
{
    preset::Section s;
    s.tag = generatorSectionTag;
    writeU32 (s.data, generatorVersion);
    for (auto b : uuidBytes (styleUuidOf (g)))
        s.data.push_back (static_cast<std::byte> (b));
    for (float v : { g.density, g.groove, g.syncopation, g.swing, g.humanize, g.complexity, g.fills, g.variation })
    {
        std::uint32_t bits = 0;
        std::memcpy (&bits, &v, sizeof bits);
        writeU32 (s.data, bits);
    }
    writeU32 (s.data, g.seed);
    s.data.push_back (static_cast<std::byte> (g.laneLocks & 0xffu));
    s.data.push_back (static_cast<std::byte> (g.laneLocks >> 8));
    return s;
}

bool decodeGenerator (const preset::Section& section, GeneratorSettings& out, juce::String& error,
                      const std::shared_ptr<const style::Definition>& embedded)
{
    const auto& d = section.data;
    if (d.size() != 4 + 16 + 8 * 4 + 4 + 2 || readU32 (d, 0) != generatorVersion)
    {
        error = "unsupported generator section";
        return false;
    }
    GeneratorSettings g;
    const auto uuid = uuidText (d.data() + 4);
    if (const auto* info = style::infoByUuid (uuid))
        g.style = static_cast<Style> (info->id);
    else if (embedded != nullptr && embedded->id == uuid)
        g.custom = embedded;
    else
    {
        error = "unknown generator style " + uuid;
        return false;
    }
    if (embedded != nullptr && g.custom == nullptr)
    {
        error = "the embedded style is not the generator's style";
        return false;
    }
    float* fields[8] = { &g.density, &g.groove, &g.syncopation, &g.swing, &g.humanize, &g.complexity, &g.fills, &g.variation };
    bool ok = true;
    for (std::size_t i = 0; i < 8; ++i)
    {
        const auto bits = readU32 (d, 20 + i * 4);
        std::memcpy (fields[i], &bits, sizeof bits);
        ok = ok && std::isfinite (*fields[i]) && *fields[i] >= 0.0f && *fields[i] <= 1.0f;
    }
    if (! ok)
    {
        error = "invalid generator section";
        return false;
    }
    g.seed = readU32 (d, 52);
    g.laneLocks = static_cast<std::uint16_t> (std::to_integer<std::uint16_t> (d[56]) | (std::to_integer<std::uint16_t> (d[57]) << 8));
    out = g;
    return true;
}

preset::Section encodeStyleSection (const style::Definition& def)
{
    preset::Section s;
    s.tag = styleSectionTag;
    const auto text = style::serialize (def).toStdString();
    writeU32 (s.data, styleSectionVersion);
    const juce::SHA256 hash (text.data(), text.size());
    const auto raw = hash.getRawData();
    for (std::size_t i = 0; i < raw.getSize(); ++i)
        s.data.push_back (static_cast<std::byte> (static_cast<const std::uint8_t*> (raw.getData())[i]));
    writeU32 (s.data, static_cast<std::uint32_t> (text.size()));
    for (char c : text)
        s.data.push_back (static_cast<std::byte> (c));
    return s;
}

bool decodeStyleSection (const preset::Section& section, std::shared_ptr<const style::Definition>& out, juce::String& error)
{
    const auto& d = section.data;
    if (d.size() < 40 || readU32 (d, 0) != styleSectionVersion)
    {
        error = "unsupported style section";
        return false;
    }
    const auto length = readU32 (d, 36);
    if (length == 0 || length > maxStyleText || d.size() != 40 + static_cast<std::size_t> (length))
    {
        error = "invalid style section length";
        return false;
    }
    const auto* text = reinterpret_cast<const char*> (d.data() + 40);
    const juce::SHA256 hash (text, length);
    const auto raw = hash.getRawData();
    if (raw.getSize() != 32 || std::memcmp (raw.getData(), d.data() + 4, 32) != 0)
    {
        error = "style section hash mismatch";
        return false;
    }
    auto def = std::make_shared<style::Definition>();
    const auto source = juce::String::fromUTF8 (text, static_cast<int> (length));
    const auto parsed = style::parse (source, *def);
    if (parsed.failed())
    {
        error = parsed.getErrorMessage();
        return false;
    }
    if (style::serialize (*def).toStdString() != std::string (text, length))
    {
        error = "style section is not in canonical form";
        return false;
    }
    if (style::infoByUuid (def->id) != nullptr)
    {
        error = "a factory style cannot be embedded";
        return false;
    }
    out = std::move (def);
    return true;
}

} // namespace vox::drums
