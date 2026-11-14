#pragma once

// VOX Drums declarative styles (issue 136, design PR 137 §11): a style is
// data, run by one interpreter in the generator. The factory styles are
// written in this form too (StyleRegistry); nothing about a style is code.
//
// This is the FOUNDATION step: the in-memory model, a strict bounded JSON
// parser and a deterministic content hash. The on-disk/import format, its
// embedding in projects (DSTL) and DGEN v2 are not frozen yet: they wait
// for the Architecture Guard review of PR 137 §§2, 3, 10, 11.
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
    double evaluate (const ParamValues&) const noexcept;
    bool empty() const noexcept { return rpn.empty(); }
};
// Parses `text` (e.g. "0.15 + 0.5 * syncopation * density"). On failure
// returns false and sets `error`.
bool parseExpr (const juce::String& text, Expr& out, juce::String& error);

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
    std::uint16_t positions = 0;   // bit n: 16th n (0-based) of a 4/4 bar
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

struct Definition {
    juce::String key, name;
    std::vector<Rule> rules;   // evaluated in order, per part
    std::optional<ProbabilityOverride> probability;
    Fill fill;
};

// Limits of the parser (bounded input; the import format will reuse them).
inline constexpr std::size_t maxTextBytes = 64 * 1024;
inline constexpr int maxRules = 512, maxRulesPerPart = 64, maxExprTokens = 64, maxExprDepth = 8, maxConditions = 4;

// Strict: an unknown key, a wrong type, an unknown part or setting, a
// position outside 1..16, a duplicate position, a value out of range or a
// text over the limits fails with an error naming the field.
juce::Result parse (const juce::String& json, Definition& out);

// SHA-256 (hex) of a deterministic binary form of the definition: equal
// definitions give equal hashes on every platform.
juce::String contentHash (const Definition&);

} // namespace vox::drums::style
