#include "StyleDefinition.h"

#include <juce_cryptography/juce_cryptography.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>

namespace vox::drums::style {

namespace {

const char* const partKeys[partCount] = { "kick", "snare", "clap", "closedHat", "openHat", "tom", "rim", "cymbal",
                                          "perc1", "perc2", "perc3", "perc4", "perc5", "perc6", "perc7", "perc8" };
const char* const paramKeys[paramCount] = { "density", "groove", "syncopation", "complexity", "fills", "variation", "swing", "humanize" };
const char* const meterKeys[meterCount] = { "4/4", "3/4", "6/8", "12/8" };

// Shortest text that reads back to exactly the same value.
juce::String shortest (double d)
{
    char buffer[64] {};
    const auto r = std::to_chars (buffer, buffer + sizeof buffer, d);
    return juce::String (buffer, static_cast<std::size_t> (r.ptr - buffer));
}
juce::String shortest (float f)
{
    char buffer[64] {};
    const auto r = std::to_chars (buffer, buffer + sizeof buffer, f);
    return juce::String (buffer, static_cast<std::size_t> (r.ptr - buffer));
}

juce::String quoted (const juce::String& text)
{
    juce::String out ("\"");
    for (auto ch : text)
    {
        if (ch == '"') out << "\\\"";
        else if (ch == '\\') out << "\\\\";
        else if (ch < 0x20) out << "\\u" << juce::String::toHexString (static_cast<int> (ch)).paddedLeft ('0', 4);
        else out << juce::String::charToString (ch);
    }
    return out + "\"";
}

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
    // Positions 1..`count`, unique, as a bit mask (bit 0 = position 1).
    bool positions (const juce::var& v, const juce::String& path, int count, std::uint32_t& out, bool allowEmpty = false)
    {
        const auto* a = v.getArray();
        if (a == nullptr || (a->isEmpty() && ! allowEmpty) || a->size() > count)
            return fail (path, "must be a list of " + juce::String (allowEmpty ? 0 : 1) + ".." + juce::String (count) + " positions");
        out = 0;
        for (int i = 0; i < a->size(); ++i)
        {
            int p = 0;
            if (! integer ((*a)[i], path + "[" + juce::String (i) + "]", 1, count, p))
                return false;
            const auto bit = 1u << (p - 1);
            if ((out & bit) != 0)
                return fail (path + "[" + juce::String (i) + "]", "position " + juce::String (p) + " appears twice");
            out |= bit;
        }
        return true;
    }
    bool meter (const juce::var& v, const juce::String& path, Meter& out)
    {
        juce::String t;
        if (! text (v, path, t))
            return false;
        for (int m = 0; m < meterCount; ++m)
            if (t == meterKeys[m])
            {
                out = static_cast<Meter> (m);
                return true;
            }
        return fail (path, "unknown meter '" + t + "' (4/4, 3/4, 6/8, 12/8)");
    }
    bool partList (const juce::var& v, const juce::String& path, std::uint16_t& out)
    {
        const auto* a = v.getArray();
        if (a == nullptr || a->size() > partCount)
            return fail (path, "must be a list of parts");
        out = 0;
        for (int i = 0; i < a->size(); ++i)
        {
            int part = 0;
            if (! this->part ((*a)[i], path + "[" + juce::String (i) + "]", part))
                return false;
            if ((out & (1u << part)) != 0)
                return fail (path + "[" + juce::String (i) + "]", juce::String ("part ") + partKey (part) + " appears twice");
            out = static_cast<std::uint16_t> (out | (1u << part));
        }
        return true;
    }
};

bool readRule (Reader& in, const juce::var& v, const juce::String& path, std::uint8_t meters, Rule& r)
{
    if (! in.keys (v, path, { "part", "meter", "beats", "subdivision", "at", "kind", "when", "chance", "salt", "velocity", "accent", "bars", "ratchet" }))
        return false;
    if (! in.part (v["part"], path + ".part", r.part))
        return false;
    if (v.hasProperty ("meter"))
    {
        if (! in.meter (v["meter"], path + ".meter", r.meter))
            return false;
        if ((meters & (1u << static_cast<int> (r.meter))) == 0)
            return in.fail (path + ".meter", juce::String (meterKey (r.meter)) + " is not in the style's meters");
    }
    if (v.hasProperty ("beats") != v.hasProperty ("subdivision"))
        return in.fail (path, "\"beats\" and \"subdivision\" go together");
    if (v.hasProperty ("beats"))
    {
        const auto sub = v["subdivision"].toString();
        const bool compound = compoundMeter (r.meter);
        if (sub != (compound ? "duplet" : "triplet"))
            return in.fail (path + ".subdivision", juce::String ("must be \"") + (compound ? "duplet" : "triplet") + "\" in " + meterKey (r.meter));
        std::uint32_t beats = 0;
        if (! in.positions (v["beats"], path + ".beats", meterBeats (r.meter), beats))
            return false;
        r.tupletBeats = static_cast<std::uint16_t> (beats);
        if (! in.positions (v["at"], path + ".at", compound ? 4 : 6, r.positions))
            return false;
    }
    else if (! in.positions (v["at"], path + ".at", gridPositions (r.meter), r.positions))
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
    // Canonical text: binary operators between single spaces, parentheses
    // tight, numbers shortest. Reading it back gives the same postfix form.
    juce::String canonical;
    for (const auto& t : tokens)
        switch (t.kind)
        {
            case Token::Kind::number: canonical << shortest (t.number); break;
            case Token::Kind::name: canonical << t.name; break;
            case Token::Kind::plus: canonical << " + "; break;
            case Token::Kind::minus: canonical << " - "; break;
            case Token::Kind::times: canonical << " * "; break;
            case Token::Kind::open: canonical << "("; break;
            case Token::Kind::close: canonical << ")"; break;
            case Token::Kind::end: break;
        }
    e.text = canonical;
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
        if (! in.keys (root, "style", { "format", "version", "id", "key", "name", "meta", "meters", "defaults", "rules", "probability", "fill",
                                        "variation", "song", "assertions" }))
            return false;
        if (root["format"].toString() != "vox-drums-style")
            return in.fail ("format", "must be \"vox-drums-style\"");
        int version = 0;
        if (! in.integer (root["version"], "version", 1, 1, version))
            return false;
        if (! in.text (root["id"], "id", d.id, 36) || ! isCanonicalUuid (d.id))
            return in.fail ("id", "must be a lower-case UUID like \"0123abcd-0123-4567-89ab-0123456789ab\"");
        if (! in.text (root["key"], "key", d.key, 48) || ! d.key.containsOnly ("abcdefghijklmnopqrstuvwxyz0123456789-"))
            return in.fail ("key", "must be 1..48 characters a-z, 0-9 or -");
        if (! in.text (root["name"], "name", d.name, maxMetaText))
            return false;
        if (root.hasProperty ("meta"))
        {
            const auto& mv = root["meta"];
            if (! in.keys (mv, "meta", { "author", "group", "tempo" }))
                return false;
            if (mv.hasProperty ("author") && ! in.text (mv["author"], "meta.author", d.meta.author, maxMetaText))
                return false;
            if (mv.hasProperty ("group") && ! in.text (mv["group"], "meta.group", d.meta.group, maxMetaText))
                return false;
            if (mv.hasProperty ("tempo"))
            {
                const auto& tv = mv["tempo"];
                if (! in.keys (tv, "meta.tempo", { "min", "max" }) || ! in.integer (tv["min"], "meta.tempo.min", 20, 400, d.meta.tempoMin)
                    || ! in.integer (tv["max"], "meta.tempo.max", 20, 400, d.meta.tempoMax))
                    return false;
                if (d.meta.tempoMin > d.meta.tempoMax)
                    return in.fail ("meta.tempo", "min is above max");
            }
        }
        {
            const auto* a = root["meters"].getArray();
            if (a == nullptr || a->isEmpty() || a->size() > meterCount)
                return in.fail ("meters", "must be a list of meters including \"4/4\"");
            d.meters = 0;
            for (int i = 0; i < a->size(); ++i)
            {
                Meter m;
                if (! in.meter ((*a)[i], "meters[" + juce::String (i) + "]", m))
                    return false;
                const auto bit = static_cast<std::uint8_t> (1u << static_cast<int> (m));
                if ((d.meters & bit) != 0)
                    return in.fail ("meters[" + juce::String (i) + "]", juce::String (meterKey (m)) + " appears twice");
                d.meters = static_cast<std::uint8_t> (d.meters | bit);
            }
            if ((d.meters & 1u) == 0)
                return in.fail ("meters", "must include \"4/4\" (every style has full 4/4 rules)");
        }
        if (root.hasProperty ("defaults"))
        {
            const auto& dv = root["defaults"];
            if (! in.keys (dv, "defaults", { "density", "groove", "syncopation", "complexity", "fills", "variation", "swing", "humanize" }))
                return false;
            for (int i = 0; i < paramCount; ++i)
                if (dv.hasProperty (paramKeys[i]))
                {
                    double x = 0.0;
                    if (! in.number (dv[paramKeys[i]], juce::String ("defaults.") + paramKeys[i], 0.0, 1.0, x))
                        return false;
                    d.defaults.values[static_cast<std::size_t> (i)] = static_cast<float> (x);
                }
        }
        const auto* rules = root["rules"].getArray();
        if (rules == nullptr || rules->isEmpty() || rules->size() > maxRules)
            return in.fail ("rules", "must be a list of 1.." + juce::String (maxRules) + " rules");
        std::array<int, partCount> perPart {};
        for (int i = 0; i < rules->size(); ++i)
        {
            Rule r;
            if (! readRule (in, (*rules)[i], "rules[" + juce::String (i) + "]", d.meters, r))
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
            if (pv.hasProperty ("except") && ! in.partList (pv["except"], "probability.except", po.exceptParts))
                return false;
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
        if (! in.integer (fv["ratchet"], "fill.ratchet", 1, 8, d.fill.ratchet))
            return false;
        if (root.hasProperty ("variation"))
        {
            if (! in.keys (root["variation"], "variation", { "keep" }) || ! in.partList (root["variation"]["keep"], "variation.keep", d.variationKeep))
                return false;
        }
        if (root.hasProperty ("song"))
        {
            const auto& sv = root["song"];
            if (! in.keys (sv, "song", { "breakWithoutKick" }))
                return false;
            if (! sv["breakWithoutKick"].isBool())
                return in.fail ("song.breakWithoutKick", "must be true or false");
            d.songBreakWithoutKick = static_cast<bool> (sv["breakWithoutKick"]);
        }
        if (root.hasProperty ("assertions"))
        {
            const auto* a = root["assertions"].getArray();
            if (a == nullptr || a->size() > maxAssertions)
                return in.fail ("assertions", "must be a list of at most " + juce::String (maxAssertions) + " assertions");
            for (int i = 0; i < a->size(); ++i)
            {
                const auto& av = (*a)[i];
                const auto ap = "assertions[" + juce::String (i) + "]";
                Assertion as;
                if (! in.keys (av, ap, { "part", "meter", "always", "never" }) || ! in.part (av["part"], ap + ".part", as.part))
                    return false;
                if (av.hasProperty ("meter") && ! in.meter (av["meter"], ap + ".meter", as.meter))
                    return false;
                if ((d.meters & (1u << static_cast<int> (as.meter))) == 0)
                    return in.fail (ap + ".meter", juce::String (meterKey (as.meter)) + " is not in the style's meters");
                const int n = gridPositions (as.meter);
                if (av.hasProperty ("always") && ! in.positions (av["always"], ap + ".always", n, as.always, true))
                    return false;
                if (av.hasProperty ("never") && ! in.positions (av["never"], ap + ".never", n, as.never, true))
                    return false;
                if ((as.always & as.never) != 0)
                    return in.fail (ap, "a position is both always and never");
                d.assertions.push_back (as);
            }
        }
        return true;
    }();
    if (! ok)
        return juce::Result::fail ("style: " + in.error);
    out = std::move (d);
    return juce::Result::ok();
}

juce::String contentHash (const Definition& d)
{
    Bytes b;
    b.text ("vox-drums-style/1/content");
    b.u8 (d.meters);
    for (auto v : d.defaults.values)
        b.f32 (v);
    b.u32 (d.variationKeep);
    b.u8 (d.songBreakWithoutKick ? 1u : 0u);
    b.u32 (static_cast<std::uint32_t> (d.rules.size()));
    for (const auto& r : d.rules)
    {
        b.u8 (static_cast<std::uint32_t> (r.part));
        b.u8 (static_cast<std::uint32_t> (r.meter));
        b.u32 (r.tupletBeats);
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

const char* meterKey (Meter m) noexcept
{
    const auto i = static_cast<int> (m);
    return i >= 0 && i < meterCount ? meterKeys[i] : "?";
}

int gridPositions (Meter m) noexcept
{
    static constexpr int n[meterCount] = { 16, 12, 12, 24 };
    return n[static_cast<int> (m) & 3];
}

int meterBeats (Meter m) noexcept
{
    static constexpr int n[meterCount] = { 4, 3, 2, 4 };
    return n[static_cast<int> (m) & 3];
}

bool compoundMeter (Meter m) noexcept
{
    return m == Meter::m6_8 || m == Meter::m12_8;
}

bool isCanonicalUuid (const juce::String& s) noexcept
{
    if (s.length() != 36)
        return false;
    for (int i = 0; i < 36; ++i)
    {
        const auto ch = s[i];
        const bool dash = i == 8 || i == 13 || i == 18 || i == 23;
        if (dash ? ch != '-' : ! ((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f')))
            return false;
    }
    return true;
}

namespace {
juce::String positionList (std::uint32_t mask)
{
    juce::StringArray items;
    for (int i = 0; i < 32; ++i)
        if ((mask & (1u << i)) != 0)
            items.add (juce::String (i + 1));
    return "[" + items.joinIntoString (",") + "]";
}
juce::String partListText (std::uint16_t mask)
{
    juce::StringArray items;
    for (int p = 0; p < partCount; ++p)
        if ((mask & (1u << p)) != 0)
            items.add (quoted (partKey (p)));
    return "[" + items.joinIntoString (",") + "]";
}
// An object with its members in sorted key order.
juce::String object (std::vector<std::pair<juce::String, juce::String>> members)
{
    std::sort (members.begin(), members.end(), [] (const auto& a, const auto& b) { return a.first.compare (b.first) < 0; });
    juce::StringArray items;
    for (const auto& [k, v] : members)
        items.add (quoted (k) + ":" + v);
    return "{" + items.joinIntoString (",") + "}";
}
const char* const cmpText[4] = { " > ", " >= ", " < ", " <= " };
} // namespace

juce::String serialize (const Definition& d)
{
    std::vector<std::pair<juce::String, juce::String>> top;
    top.emplace_back ("format", quoted ("vox-drums-style"));
    top.emplace_back ("version", "1");
    top.emplace_back ("id", quoted (d.id));
    top.emplace_back ("key", quoted (d.key));
    top.emplace_back ("name", quoted (d.name));
    {
        std::vector<std::pair<juce::String, juce::String>> meta;
        if (d.meta.author.isNotEmpty()) meta.emplace_back ("author", quoted (d.meta.author));
        if (d.meta.group.isNotEmpty()) meta.emplace_back ("group", quoted (d.meta.group));
        if (d.meta.tempoMin > 0) meta.emplace_back ("tempo", object ({ { "min", juce::String (d.meta.tempoMin) }, { "max", juce::String (d.meta.tempoMax) } }));
        if (! meta.empty()) top.emplace_back ("meta", object (meta));
    }
    {
        juce::StringArray m;
        for (int i = 0; i < meterCount; ++i)
            if ((d.meters & (1u << i)) != 0)
                m.add (quoted (meterKeys[i]));
        top.emplace_back ("meters", "[" + m.joinIntoString (",") + "]");
    }
    {
        std::vector<std::pair<juce::String, juce::String>> def;
        for (int i = 0; i < paramCount; ++i)
            def.emplace_back (paramKeys[i], shortest (d.defaults.values[static_cast<std::size_t> (i)]));
        top.emplace_back ("defaults", object (def));
    }
    {
        juce::StringArray rules;
        for (const auto& r : d.rules)
        {
            std::vector<std::pair<juce::String, juce::String>> m;
            m.emplace_back ("part", quoted (partKey (r.part)));
            if (r.meter != Meter::m4_4) m.emplace_back ("meter", quoted (meterKey (r.meter)));
            if (r.tupletBeats != 0)
            {
                m.emplace_back ("beats", positionList (r.tupletBeats));
                m.emplace_back ("subdivision", quoted (compoundMeter (r.meter) ? "duplet" : "triplet"));
            }
            m.emplace_back ("at", positionList (r.positions));
            if (r.kind == Rule::Kind::ghost) m.emplace_back ("kind", quoted ("ghost"));
            if (! r.when.empty())
            {
                juce::StringArray w;
                for (const auto& c : r.when)
                    w.add (quoted (c.left.text + cmpText[static_cast<int> (c.cmp)] + shortest (c.right)));
                m.emplace_back ("when", "[" + w.joinIntoString (",") + "]");
            }
            if (! r.chance.empty()) m.emplace_back ("chance", quoted (r.chance.text));
            if (r.salt != 0) m.emplace_back ("salt", juce::String (r.salt));
            m.emplace_back ("velocity", shortest (r.velocity));
            if (r.accent) m.emplace_back ("accent", "true");
            if (r.bars != Bars::all) m.emplace_back ("bars", quoted (r.bars == Bars::first ? "first" : "odd"));
            if (r.ratchet)
            {
                juce::StringArray below;
                for (const auto& [t, v] : r.ratchet->below)
                    below.add ("[" + shortest (t) + "," + juce::String (v) + "]");
                m.emplace_back ("ratchet", object ({ { "chance", quoted (r.ratchet->chance.text) },
                                                     { "salt", juce::String (r.ratchet->salt) },
                                                     { "valueSalt", juce::String (r.ratchet->valueSalt) },
                                                     { "below", "[" + below.joinIntoString (",") + "]" },
                                                     { "otherwise", juce::String (r.ratchet->otherwise) } }));
            }
            rules.add (object (m));
        }
        top.emplace_back ("rules", "[" + rules.joinIntoString (",") + "]");
    }
    if (d.probability)
        top.emplace_back ("probability", object ({ { "value", quoted (d.probability->value.text) },
                                                   { "min", juce::String (d.probability->min) },
                                                   { "max", juce::String (d.probability->max) },
                                                   { "except", partListText (d.probability->exceptParts) } }));
    {
        juce::StringArray cycle;
        for (auto p : d.fill.cycle)
            cycle.add (quoted (partKey (p)));
        top.emplace_back ("fill", object ({ { "cycle", "[" + cycle.joinIntoString (",") + "]" }, { "ratchet", juce::String (d.fill.ratchet) } }));
    }
    top.emplace_back ("variation", object ({ { "keep", partListText (d.variationKeep) } }));
    top.emplace_back ("song", object ({ { "breakWithoutKick", d.songBreakWithoutKick ? "true" : "false" } }));
    if (! d.assertions.empty())
    {
        juce::StringArray items;
        for (const auto& a : d.assertions)
        {
            std::vector<std::pair<juce::String, juce::String>> m;
            m.emplace_back ("part", quoted (partKey (a.part)));
            if (a.meter != Meter::m4_4) m.emplace_back ("meter", quoted (meterKey (a.meter)));
            if (a.always != 0) m.emplace_back ("always", positionList (a.always));
            if (a.never != 0) m.emplace_back ("never", positionList (a.never));
            items.add (object (m));
        }
        top.emplace_back ("assertions", "[" + items.joinIntoString (",") + "]");
    }
    return object (top);
}

juce::String documentHash (const Definition& d)
{
    const auto utf8 = serialize (d).toStdString();
    return juce::SHA256 (utf8.data(), utf8.size()).toHexString();
}

} // namespace vox::drums::style
