#pragma once

// VOX Drums declarative styles (issue 136, design PR 137 §11): a style is
// data, run by one interpreter in the generator. The factory styles are
// written in this form too (StyleRegistry); nothing about a style is code.
//
// The canonical public `.voxstyle` v1 schema (PR 137 §11.1, AG-1 PASS in
// review 5906187802): ONE schema, parser and AST for factory and user
// styles; a canonical serializer (sorted keys, shortest round-trip
// numbers) so parse -> write -> parse is exact. Rules for meters other than
// 4/4 and tuplet rules are parsed, validated and serialized; the generator
// runs them once the shared timing engine (PR 137 §10.5a) exists.
//
// Message thread only (parsing, hashing). The audio thread never sees it.

#include <juce_core/juce_core.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace vox::drums::style {

// The generator's parts (role slots of PadRoles.h RoleMap): 0..7 kick,
// snare, clap, closed hat, open hat, tom, rim, cymbal; 8..15 percussion 1-8.
inline constexpr int partCount = 16;
const char* partKey (int part) noexcept;   // "kick", "closedHat", "perc1", ...
int partFromKey (const juce::String& key) noexcept;   // -1 if unknown

// The generator settings a formula may read.
enum class Param : std::uint8_t { density, groove, syncopation, complexity, fills, variation, swing, humanize };
inline constexpr int paramCount = 8;
struct ParamValues {
    std::array<double, paramCount> v {};
    double operator[] (Param p) const noexcept { return v[static_cast<std::size_t> (p)]; }
};

// A small arithmetic formula over the settings: numbers, settings, + - *
// and parentheses, evaluated left to right with the usual precedence, in
// double precision. No other operators, functions or variables.
struct Expr {
    struct Node {
        enum class Op : std::uint8_t { number, param, add, sub, mul } op = Op::number;
        double number = 0.0;
        Param param = Param::density;
    };
    std::vector<Node> rpn;   // postfix
    juce::String text;       // canonical source: tokens, single spaces, shortest numbers
    double evaluate (const ParamValues&) const noexcept;
    bool empty() const noexcept { return rpn.empty(); }
};
// Parses `text` (e.g. "0.15 + 0.5 * syncopation * density"). On failure
// returns false and sets `error`.
bool parseExpr (const juce::String& text, Expr& out, juce::String& error);

// Meters a style may define (4/4 is required).
enum class Meter : std::uint8_t { m4_4, m3_4, m6_8, m12_8 };
inline constexpr int meterCount = 4;
const char* meterKey (Meter) noexcept;              // "4/4", ...
int gridPositions (Meter) noexcept;                 // 16th positions per bar: 16, 12, 12, 24
int meterBeats (Meter) noexcept;                    // 4, 3, 2, 4
bool compoundMeter (Meter) noexcept;                // 6/8, 12/8

struct Condition {
    Expr left;
    enum class Cmp : std::uint8_t { greater, greaterEqual, less, lessEqual } cmp = Cmp::greater;
    double right = 0.0;
    bool holds (const ParamValues& p) const noexcept;
};

// Which bars of a pattern a rule plays in.
enum class Bars : std::uint8_t { all, first, odd };   // odd: bars 1, 3 (0-based even)

struct Ratchet {
    Expr chance;       // probability of a ratchet at this position
    int salt = 0;      // of the chance
    int valueSalt = 0; // of the draw choosing the value
    std::vector<std::pair<double, int>> below;   // draw < threshold -> value, in order
    int otherwise = 2;
};

struct Rule {
    int part = 0;
    Meter meter = Meter::m4_4;
    // Straight: bit n = 16th n (0-based) of the meter's grid (up to 24).
    // Tuplet: bit n = tuplet cell n within each listed beat (triplet 0..5
    // in x/4 meters, duplet 0..3 in compound meters, at the 16th level).
    std::uint32_t positions = 0;
    std::uint16_t tupletBeats = 0;   // 0: a straight rule; bit b: beat b (0-based)
    enum class Kind : std::uint8_t { hit, ghost } kind = Kind::hit;
    std::vector<Condition> when;   // all must hold
    Expr chance;                   // hit: optional gate; ghost: required
    int salt = 0;
    float velocity = 0.8f;
    bool accent = false;
    Bars bars = Bars::all;
    std::optional<Ratchet> ratchet;
};

// A step probability for every hit of the style except some parts.
struct ProbabilityOverride {
    Expr value;
    int min = 1, max = 100;
    std::uint16_t exceptParts = 0;   // bit n: part n keeps 100 %
};

struct Fill {
    std::vector<int> cycle;   // the parts of the fill, part = cycle[index % size]
    int ratchet = 2;          // ratchet of a complex fill hit
};

struct Meta {
    juce::String author, group;
    int tempoMin = 0, tempoMax = 0;   // 0/0: no hint; information only, never sent to the host
};

// Recommended generator settings for the style ("Style defaults").
struct Defaults {
    std::array<float, paramCount> values { 0.5f, 0.5f, 0.3f, 0.4f, 0.5f, 0.4f, 0.0f, 0.2f };   // Param order
};

// A style's own structural self-checks (PR 137 §11.3): they prove only that
// the file agrees with itself, never genre or quality.
struct Assertion {
    Meter meter = Meter::m4_4;
    int part = 0;
    std::uint32_t always = 0, never = 0;   // grid positions as Rule::positions
};

struct Definition {
    juce::String id;    // canonical lower-case UUID "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"
    juce::String key;   // [a-z0-9-], 1..48
    juce::String name;
    Meta meta;
    std::uint8_t meters = 1;   // bit m: Meter m; bit 0 (4/4) always set
    Defaults defaults;
    std::vector<Rule> rules;   // evaluated in order, per part
    std::optional<ProbabilityOverride> probability;
    Fill fill;
    std::uint16_t variationKeep = 0x0007;   // parts a Variation B keeps from A (kick, snare, clap)
    bool songBreakWithoutKick = true;       // the Song's break slot has no kick
    std::vector<Assertion> assertions;
};

// Limits of the parser (bounded input; the import format will reuse them).
inline constexpr std::size_t maxTextBytes = 64 * 1024;
inline constexpr int maxRules = 512, maxRulesPerPart = 64, maxExprTokens = 64, maxExprDepth = 8, maxConditions = 4, maxAssertions = 64,
                     maxMetaText = 64;

// Strict: an unknown key, a wrong type, an unknown part or setting, a
// position outside 1..16, a duplicate position, a value out of range or a
// text over the limits fails with an error naming the field.
juce::Result parse (const juce::String& json, Definition& out);

// The canonical text: keys sorted, no spaces outside formulas, numbers in
// their shortest round-trip form, arrays in canonical order (rules in file
// order). parse (serialize (d)) gives d again, and serialize of that gives
// the same text.
juce::String serialize (const Definition&);

// SHA-256 (hex) of the generation-relevant data (rules, probability, fill,
// variation, song, meters, defaults) in a deterministic binary form: equal
// on every platform for equal definitions.
juce::String contentHash (const Definition&);
// SHA-256 (hex) of the canonical text: identifies the whole document (id,
// names and metadata included); used for library and project identity.
juce::String documentHash (const Definition&);

bool isCanonicalUuid (const juce::String&) noexcept;

} // namespace vox::drums::style
