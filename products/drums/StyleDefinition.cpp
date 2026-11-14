#include "StyleDefinition.h"

#include <juce_cryptography/juce_cryptography.h>

#include <charconv>
#include <cmath>
#include <cstring>

namespace vox::drums::style {

namespace {

const char* const partKeys[partCount] = { "kick", "snare", "clap", "closedHat", "openHat", "tom", "rim", "cymbal",
                                          "perc1", "perc2", "perc3", "perc4", "perc5", "perc6", "perc7", "perc8" };
const char* const paramKeys[paramCount] = { "density", "groove", "syncopation", "complexity", "fills", "variation", "swing", "humanize" };

//== formulas: tokens, then precedence climbing to postfix ======================
struct Token {
    enum class Kind { number, name, plus, minus, times, open, close, end } kind = Kind::end;
    double number = 0.0;
    juce::String name;
};

bool tokenize (const juce::String& text, std::vector<Token>& out, juce::String& error)
{
    const auto utf8 = text.toStdString();
    std::size_t i = 0;
    while (i < utf8.size())
    {
        const char ch = utf8[i];
        if (ch == ' ' || ch == '\t')
        {
            ++i;
            continue;
        }
        Token t;
        if ((ch >= '0' && ch <= '9') || ch == '.')
        {
            const auto* begin = utf8.data() + i;
            const auto* end = utf8.data() + utf8.size();
            const auto [ptr, ec] = std::from_chars (begin, end, t.number, std::chars_format::fixed);
            if (ec != std::errc() || ! std::isfinite (t.number))
            {
                error = "bad number at " + juce::String (static_cast<int> (i + 1));
                return false;
            }
            t.kind = Token::Kind::number;
            i += static_cast<std::size_t> (ptr - begin);
        }
        else if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z'))
        {
            std::size_t j = i;
            while (j < utf8.size() && ((utf8[j] >= 'a' && utf8[j] <= 'z') || (utf8[j] >= 'A' && utf8[j] <= 'Z')))
                ++j;
            t.kind = Token::Kind::name;
            t.name = juce::String (utf8.substr (i, j - i));
            i = j;
        }
        else
        {
            switch (ch)
            {
                case '+': t.kind = Token::Kind::plus; break;
                case '-': t.kind = Token::Kind::minus; break;
                case '*': t.kind = Token::Kind::times; break;
                case '(': t.kind = Token::Kind::open; break;
                case ')': t.kind = Token::Kind::close; break;
                default:
                    error = juce::String ("unexpected '") + juce::String::charToString (static_cast<juce::juce_wchar> (static_cast<unsigned char> (ch)))
                          + "' at " + juce::String (static_cast<int> (i + 1));
                    return false;
            }
            ++i;
        }
        out.push_back (t);
        if (static_cast<int> (out.size()) > maxExprTokens)
        {
            error = "formula longer than " + juce::String (maxExprTokens) + " tokens";
            return false;
        }
    }
    out.push_back ({});
    return true;
}

struct ExprParser {
    const std::vector<Token>& tokens;
    std::size_t at = 0;
    std::vector<Expr::Node>& rpn;
    juce::String& error;
    int depth = 0;

    const Token& peek() const { return tokens[at]; }

    bool primary()
    {
        const auto& t = peek();
        if (t.kind == Token::Kind::number)
        {
            rpn.push_back ({ Expr::Node::Op::number, t.number, Param::density });
            ++at;
            return true;
        }
        if (t.kind == Token::Kind::name)
        {
            for (int p = 0; p < paramCount; ++p)
                if (t.name == paramKeys[p])
                {
                    rpn.push_back ({ Expr::Node::Op::param, 0.0, static_cast<Param> (p) });
                    ++at;
                    return true;
                }
            error = "unknown setting '" + t.name + "'";
            return false;
        }
        if (t.kind == Token::Kind::open)
        {
            if (++depth > maxExprDepth)
            {
                error = "formula nested deeper than " + juce::String (maxExprDepth);
                return false;
            }
            ++at;
            if (! sum())
                return false;
            if (peek().kind != Token::Kind::close)
            {
                error = "missing ')'";
                return false;
            }
            ++at;
            --depth;
            return true;
        }
        error = "expected a number, a setting or '('";
        return false;
    }
    bool product()
    {
        if (! primary())
            return false;
        while (peek().kind == Token::Kind::times)
        {
            ++at;
            if (! primary())
                return false;
            rpn.push_back ({ Expr::Node::Op::mul, 0.0, Param::density });
        }
        return true;
    }
    bool sum()
    {
        if (! product())
            return false;
        while (peek().kind == Token::Kind::plus || peek().kind == Token::Kind::minus)
        {
            const auto op = peek().kind == Token::Kind::plus ? Expr::Node::Op::add : Expr::Node::Op::sub;
            ++at;
            if (! product())
                return false;
            rpn.push_back ({ op, 0.0, Param::density });
        }
        return true;
    }
};

//== JSON helpers ===============================================================
struct Reader {
    juce::String error;

    bool fail (const juce::String& path, const juce::String& what)
    {
        if (error.isEmpty())
            error = path + ": " + what;
        return false;
    }
    // Only `allowed` keys may appear in `object`.
    bool keys (const juce::var& object, const juce::String& path, std::initializer_list<const char*> allowed)
    {
        auto* o = object.getDynamicObject();
        if (o == nullptr)
            return fail (path, "must be an object");
        for (const auto& property : o->getProperties())
        {
            bool known = false;
            for (const auto* k : allowed)
                known = known || property.name.toString() == k;
            if (! known)
                return fail (path, "unknown key '" + property.name.toString() + "'");
        }
        return true;
    }
    bool number (const juce::var& v, const juce::String& path, double lo, double hi, double& out)
    {
        if (! (v.isDouble() || v.isInt() || v.isInt64()))
            return fail (path, "must be a number");
        out = static_cast<double> (v);
        if (! std::isfinite (out) || out < lo || out > hi)
            return fail (path, juce::String (out) + " is outside " + juce::String (lo) + ".." + juce::String (hi));
        return true;
    }
    bool integer (const juce::var& v, const juce::String& path, int lo, int hi, int& out)
    {
        double d = 0.0;
        if (! number (v, path, lo, hi, d))
            return false;
        if (d != std::floor (d))
            return fail (path, "must be a whole number");
        out = static_cast<int> (d);
        return true;
    }
    bool text (const juce::var& v, const juce::String& path, juce::String& out, int maxLength = 64)
    {
        if (! v.isString() || v.toString().isEmpty() || v.toString().length() > maxLength)
            return fail (path, "must be a text of 1.." + juce::String (maxLength) + " characters");
        out = v.toString();
        return true;
    }
    bool part (const juce::var& v, const juce::String& path, int& out)
    {
        juce::String key;
        if (! text (v, path, key))
            return false;
        out = partFromKey (key);
        return out >= 0 || fail (path, "unknown part '" + key + "'");
    }
    bool formula (const juce::var& v, const juce::String& path, Expr& out)
    {
        juce::String t, why;
        if (! text (v, path, t, 256))
            return false;
        return parseExpr (t, out, why) || fail (path, why);
    }
    bool condition (const juce::var& v, const juce::String& path, Condition& out)
    {
        juce::String t;
        if (! text (v, path, t, 256))
            return false;
        // "<formula> <op> <number>"; the operator is the last comparison sign.
        static const char* ops[4] = { ">=", "<=", ">", "<" };
        static const Condition::Cmp cmps[4] = { Condition::Cmp::greaterEqual, Condition::Cmp::lessEqual, Condition::Cmp::greater,
                                                Condition::Cmp::less };
        for (int i = 0; i < 4; ++i)
        {
            const int at = t.lastIndexOf (ops[i]);
            if (at <= 0)
                continue;
            juce::String why;
            if (! parseExpr (t.substring (0, at), out.left, why))
                return fail (path, why);
            const auto rhs = t.substring (at + static_cast<int> (std::strlen (ops[i]))).trim().toStdString();
            const auto [ptr, ec] = std::from_chars (rhs.data(), rhs.data() + rhs.size(), out.right, std::chars_format::fixed);
            if (ec != std::errc() || ptr != rhs.data() + rhs.size() || ! std::isfinite (out.right))
                return fail (path, "the right side must be a number");
            out.cmp = cmps[i];
            return true;
        }
        return fail (path, "expected '<formula> > <number>' (>, >=, <, <=)");
    }
    bool positions (const juce::var& v, const juce::String& path, std::uint16_t& out)
    {
        const auto* a = v.getArray();
        if (a == nullptr || a->isEmpty() || a->size() > 16)
            return fail (path, "must be a list of 1..16 positions");
        out = 0;
        for (int i = 0; i < a->size(); ++i)
        {
            int p = 0;
            if (! integer ((*a)[i], path + "[" + juce::String (i) + "]", 1, 16, p))
                return false;
            const auto bit = static_cast<std::uint16_t> (1u << (p - 1));
            if ((out & bit) != 0)
                return fail (path + "[" + juce::String (i) + "]", "position " + juce::String (p) + " appears twice");
            out = static_cast<std::uint16_t> (out | bit);
        }
        return true;
    }
};

bool readRule (Reader& in, const juce::var& v, const juce::String& path, Rule& r)
{
    if (! in.keys (v, path, { "part", "at", "kind", "when", "chance", "salt", "velocity", "accent", "bars", "ratchet" }))
        return false;
    if (! in.part (v["part"], path + ".part", r.part) || ! in.positions (v["at"], path + ".at", r.positions))
        return false;
    if (v.hasProperty ("kind"))
    {
        juce::String k;
        if (! in.text (v["kind"], path + ".kind", k))
            return false;
        if (k == "ghost") r.kind = Rule::Kind::ghost;
        else if (k != "hit") return in.fail (path + ".kind", "must be \"hit\" or \"ghost\"");
    }
    if (v.hasProperty ("when"))
    {
        const auto* a = v["when"].getArray();
        if (a == nullptr || a->isEmpty() || a->size() > maxConditions)
            return in.fail (path + ".when", "must be a list of 1.." + juce::String (maxConditions) + " conditions");
        for (int i = 0; i < a->size(); ++i)
        {
            Condition c;
            if (! in.condition ((*a)[i], path + ".when[" + juce::String (i) + "]", c))
                return false;
            r.when.push_back (std::move (c));
        }
    }
    if (v.hasProperty ("chance") && ! in.formula (v["chance"], path + ".chance", r.chance))
        return false;
    if (r.kind == Rule::Kind::ghost && r.chance.empty())
        return in.fail (path + ".chance", "a ghost rule needs a chance");
    if (v.hasProperty ("salt") && ! in.integer (v["salt"], path + ".salt", 0, 100000, r.salt))
        return false;
    double velocity = 0.8;
    if (v.hasProperty ("velocity") && ! in.number (v["velocity"], path + ".velocity", 0.0, 1.0, velocity))
        return false;
    r.velocity = static_cast<float> (velocity);
    if (v.hasProperty ("accent"))
    {
        if (! v["accent"].isBool())
            return in.fail (path + ".accent", "must be true or false");
        r.accent = static_cast<bool> (v["accent"]);
    }
    if (v.hasProperty ("bars"))
    {
        juce::String b;
        if (! in.text (v["bars"], path + ".bars", b))
            return false;
        if (b == "first") r.bars = Bars::first;
        else if (b == "odd") r.bars = Bars::odd;
        else if (b != "all") return in.fail (path + ".bars", "must be \"all\", \"first\" or \"odd\"");
    }
    if (v.hasProperty ("ratchet"))
    {
        const auto& rv = v["ratchet"];
        const auto rp = path + ".ratchet";
        if (! in.keys (rv, rp, { "chance", "salt", "valueSalt", "below", "otherwise" }))
            return false;
        Ratchet rt;
        if (! in.formula (rv["chance"], rp + ".chance", rt.chance) || ! in.integer (rv["salt"], rp + ".salt", 0, 100000, rt.salt)
            || ! in.integer (rv["valueSalt"], rp + ".valueSalt", 0, 100000, rt.valueSalt)
            || ! in.integer (rv["otherwise"], rp + ".otherwise", 1, 8, rt.otherwise))
            return false;
        if (rv.hasProperty ("below"))
        {
            const auto* a = rv["below"].getArray();
            if (a == nullptr || a->size() > 8)
                return in.fail (rp + ".below", "must be a list of at most 8 [threshold, value] pairs");
            for (int i = 0; i < a->size(); ++i)
            {
                const auto* pair = (*a)[i].getArray();
                const auto ip = rp + ".below[" + juce::String (i) + "]";
                double threshold = 0.0;
                int value = 1;
                if (pair == nullptr || pair->size() != 2)
                    return in.fail (ip, "must be [threshold, value]");
                if (! in.number ((*pair)[0], ip + "[0]", 0.0, 1.0, threshold) || ! in.integer ((*pair)[1], ip + "[1]", 1, 8, value))
                    return false;
                rt.below.emplace_back (threshold, value);
            }
        }
        r.ratchet = std::move (rt);
    }
    return true;
}

//== hashing: a deterministic little-endian form ==================================
struct Bytes {
    juce::MemoryBlock block;
    void u8 (std::uint32_t v) { const auto b = static_cast<std::uint8_t> (v); block.append (&b, 1); }
    void u32 (std::uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            u8 ((v >> (8 * i)) & 0xffu);
    }
    void f64 (double d)
    {
        std::uint64_t bits = 0;
        std::memcpy (&bits, &d, sizeof bits);
        for (int i = 0; i < 8; ++i)
            u8 (static_cast<std::uint32_t> ((bits >> (8 * i)) & 0xffu));
    }
    void f32 (float f)
    {
        std::uint32_t bits = 0;
        std::memcpy (&bits, &f, sizeof bits);
        u32 (bits);
    }
    void text (const juce::String& s)
    {
        const auto utf8 = s.toStdString();
        u32 (static_cast<std::uint32_t> (utf8.size()));
        block.append (utf8.data(), utf8.size());
    }
    void expr (const Expr& e)
    {
        u32 (static_cast<std::uint32_t> (e.rpn.size()));
        for (const auto& n : e.rpn)
        {
            u8 (static_cast<std::uint32_t> (n.op));
            if (n.op == Expr::Node::Op::number) f64 (n.number);
            if (n.op == Expr::Node::Op::param) u8 (static_cast<std::uint32_t> (n.param));
        }
    }
};

} // namespace

const char* partKey (int part) noexcept
{
    return part >= 0 && part < partCount ? partKeys[part] : "?";
}

int partFromKey (const juce::String& key) noexcept
{
    for (int p = 0; p < partCount; ++p)
        if (key == partKeys[p])
            return p;
    return -1;
}

double Expr::evaluate (const ParamValues& values) const noexcept
{
    // At most maxExprTokens values on the stack: no allocation.
    std::array<double, maxExprTokens> stack {};
    std::size_t top = 0;
    for (const auto& n : rpn)
    {
        switch (n.op)
        {
            case Node::Op::number: stack[top++] = n.number; break;
            case Node::Op::param: stack[top++] = values[n.param]; break;
            case Node::Op::add: --top; stack[top - 1] = stack[top - 1] + stack[top]; break;
            case Node::Op::sub: --top; stack[top - 1] = stack[top - 1] - stack[top]; break;
            case Node::Op::mul: --top; stack[top - 1] = stack[top - 1] * stack[top]; break;
        }
    }
    return top == 1 ? stack[0] : 0.0;
}

bool parseExpr (const juce::String& text, Expr& out, juce::String& error)
{
    std::vector<Token> tokens;
    if (! tokenize (text, tokens, error))
        return false;
    Expr e;
    ExprParser p { tokens, 0, e.rpn, error };
    if (! p.sum())
        return false;
    if (p.peek().kind != Token::Kind::end)
    {
        error = "unexpected text after the formula";
        return false;
    }
    out = std::move (e);
    return true;
}

bool Condition::holds (const ParamValues& p) const noexcept
{
    const double x = left.evaluate (p);
    switch (cmp)
    {
        case Cmp::greater: return x > right;
        case Cmp::greaterEqual: return x >= right;
        case Cmp::less: return x < right;
        case Cmp::lessEqual: return x <= right;
    }
    return false;
}

juce::Result parse (const juce::String& json, Definition& out)
{
    if (json.getNumBytesAsUTF8() > maxTextBytes)
        return juce::Result::fail ("style: larger than " + juce::String (static_cast<int> (maxTextBytes / 1024)) + " KiB");
    juce::var root;
    const auto parsed = juce::JSON::parse (json, root);
    if (parsed.failed())
        return juce::Result::fail ("style: not valid JSON: " + parsed.getErrorMessage());
    Reader in;
    Definition d;
    const auto ok = [&]() -> bool {
        if (! in.keys (root, "style", { "format", "version", "key", "name", "rules", "probability", "fill" }))
            return false;
        if (root["format"].toString() != "vox-drums-style")
            return in.fail ("format", "must be \"vox-drums-style\"");
        int version = 0;
        if (! in.integer (root["version"], "version", 1, 1, version))
            return false;
        if (! in.text (root["key"], "key", d.key) || ! in.text (root["name"], "name", d.name))
            return false;
        const auto* rules = root["rules"].getArray();
        if (rules == nullptr || rules->isEmpty() || rules->size() > maxRules)
            return in.fail ("rules", "must be a list of 1.." + juce::String (maxRules) + " rules");
        std::array<int, partCount> perPart {};
        for (int i = 0; i < rules->size(); ++i)
        {
            Rule r;
            if (! readRule (in, (*rules)[i], "rules[" + juce::String (i) + "]", r))
                return false;
            if (++perPart[static_cast<std::size_t> (r.part)] > maxRulesPerPart)
                return in.fail ("rules[" + juce::String (i) + "]", "more than " + juce::String (maxRulesPerPart) + " rules for " + partKey (r.part));
            d.rules.push_back (std::move (r));
        }
        if (root.hasProperty ("probability"))
        {
            const auto& pv = root["probability"];
            if (! in.keys (pv, "probability", { "value", "min", "max", "except" }))
                return false;
            ProbabilityOverride po;
            if (! in.formula (pv["value"], "probability.value", po.value) || ! in.integer (pv["min"], "probability.min", 1, 100, po.min)
                || ! in.integer (pv["max"], "probability.max", 1, 100, po.max))
                return false;
            if (po.min > po.max)
                return in.fail ("probability", "min is above max");
            if (pv.hasProperty ("except"))
            {
                const auto* a = pv["except"].getArray();
                if (a == nullptr || a->size() > partCount)
                    return in.fail ("probability.except", "must be a list of parts");
                for (int i = 0; i < a->size(); ++i)
                {
                    int part = 0;
                    if (! in.part ((*a)[i], "probability.except[" + juce::String (i) + "]", part))
                        return false;
                    po.exceptParts = static_cast<std::uint16_t> (po.exceptParts | (1u << part));
                }
            }
            d.probability = std::move (po);
        }
        const auto& fv = root["fill"];
        if (! in.keys (fv, "fill", { "cycle", "ratchet" }))
            return false;
        const auto* cycle = fv["cycle"].getArray();
        if (cycle == nullptr || cycle->isEmpty() || cycle->size() > 8)
            return in.fail ("fill.cycle", "must be a list of 1..8 parts");
        for (int i = 0; i < cycle->size(); ++i)
        {
            int part = 0;
            if (! in.part ((*cycle)[i], "fill.cycle[" + juce::String (i) + "]", part))
                return false;
            d.fill.cycle.push_back (part);
        }
        return in.integer (fv["ratchet"], "fill.ratchet", 1, 8, d.fill.ratchet);
    }();
    if (! ok)
        return juce::Result::fail ("style: " + in.error);
    out = std::move (d);
    return juce::Result::ok();
}

juce::String contentHash (const Definition& d)
{
    Bytes b;
    b.text ("vox-drums-style/1");
    b.text (d.key);
    b.text (d.name);
    b.u32 (static_cast<std::uint32_t> (d.rules.size()));
    for (const auto& r : d.rules)
    {
        b.u8 (static_cast<std::uint32_t> (r.part));
        b.u32 (r.positions);
        b.u8 (static_cast<std::uint32_t> (r.kind));
        b.u32 (static_cast<std::uint32_t> (r.when.size()));
        for (const auto& c : r.when)
        {
            b.expr (c.left);
            b.u8 (static_cast<std::uint32_t> (c.cmp));
            b.f64 (c.right);
        }
        b.expr (r.chance);
        b.u32 (static_cast<std::uint32_t> (r.salt));
        b.f32 (r.velocity);
        b.u8 (r.accent ? 1u : 0u);
        b.u8 (static_cast<std::uint32_t> (r.bars));
        b.u8 (r.ratchet.has_value() ? 1u : 0u);
        if (r.ratchet)
        {
            b.expr (r.ratchet->chance);
            b.u32 (static_cast<std::uint32_t> (r.ratchet->salt));
            b.u32 (static_cast<std::uint32_t> (r.ratchet->valueSalt));
            b.u32 (static_cast<std::uint32_t> (r.ratchet->below.size()));
            for (const auto& [t, v] : r.ratchet->below)
            {
                b.f64 (t);
                b.u8 (static_cast<std::uint32_t> (v));
            }
            b.u8 (static_cast<std::uint32_t> (r.ratchet->otherwise));
        }
    }
    b.u8 (d.probability.has_value() ? 1u : 0u);
    if (d.probability)
    {
        b.expr (d.probability->value);
        b.u32 (static_cast<std::uint32_t> (d.probability->min));
        b.u32 (static_cast<std::uint32_t> (d.probability->max));
        b.u32 (d.probability->exceptParts);
    }
    b.u32 (static_cast<std::uint32_t> (d.fill.cycle.size()));
    for (auto p : d.fill.cycle)
        b.u8 (static_cast<std::uint32_t> (p));
    b.u8 (static_cast<std::uint32_t> (d.fill.ratchet));
    return juce::SHA256 (b.block).toHexString();
}

} // namespace vox::drums::style
