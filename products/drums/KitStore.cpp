#include "KitStore.h"

#include "DrumsParameters.h"

#include <cmath>
#include <cstring>

namespace vox::drums {
namespace {

void putU32 (std::vector<std::byte>& out, std::uint32_t v)
{
    for (int s = 0; s < 32; s += 8)
        out.push_back (static_cast<std::byte> ((v >> s) & 0xffu));
}
void putF32 (std::vector<std::byte>& out, float v)
{
    std::uint32_t bits = 0;
    std::memcpy (&bits, &v, 4);
    putU32 (out, bits);
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
    float f32()
    {
        const auto bits = u32();
        float v;
        std::memcpy (&v, &bits, 4);
        return v;
    }
};

constexpr std::uint32_t padSectionVersion = 1;

} // namespace

KitStore::KitStore()
{
    structure_ = defaultKit();
}

kit::PadSettings KitStore::pad (int pad) const
{
    const juce::SpinLock::ScopedLockType sl (lock_);
    return structure_.pads[static_cast<std::size_t> (std::clamp (pad, 0, kit::padCount - 1))];
}

void KitStore::setPad (int pad, const kit::PadSettings& settings)
{
    if (pad < 0 || pad >= kit::padCount)
        return;
    kit::KitSettings next = snapshot();
    auto& p = next.pads[static_cast<std::size_t> (pad)];
    const auto host = p;
    p = settings;
    // Host fields stay as they are (they live in parameters).
    p.level = host.level;
    p.tune = host.tune;
    p.decay = host.decay;
    p.pan = host.pan;
    p.cutoff = host.cutoff;
    p.mute = host.mute;
    p.laneOn = host.laneOn;
    setAll (next);
}

kit::KitSettings KitStore::snapshot() const
{
    const juce::SpinLock::ScopedLockType sl (lock_);
    return structure_;
}

void KitStore::setAll (const kit::KitSettings& settings)
{
    {
        const juce::SpinLock::ScopedLockType sl (lock_);
        copyStructure (settings, structure_);
        dirty_ = true;
    }
    generation_.fetch_add (1);
}

void KitStore::copyStructure (const kit::KitSettings& from, kit::KitSettings& to) noexcept
{
    for (int i = 0; i < kit::padCount; ++i)
    {
        const auto& f = from.pads[static_cast<std::size_t> (i)];
        auto& t = to.pads[static_cast<std::size_t> (i)];
        t.model = f.model;
        t.note = f.note;
        t.chokeGroup = f.chokeGroup;
        t.polyphony = f.polyphony;
        t.fine = f.fine;
        t.character = f.character;
        t.filter = f.filter;
        t.resonance = f.resonance;
        t.attack = f.attack;
        t.sustain = f.sustain;
        t.output = f.output;
        t.send1 = f.send1;
        t.send2 = f.send2;
        t.pitchAmount = f.pitchAmount;
        t.pitchDecay = f.pitchDecay;
        t.drive = f.drive;
        t.sub = f.sub;
        t.tail = f.tail;
        t.source = f.source;
        t.sampleMode = f.sampleMode;
        t.velocitySplit = f.velocitySplit;
        t.players = f.players;
    }
}

void KitStore::pull (kit::KitSettings& live) noexcept
{
    const juce::SpinLock::ScopedTryLockType tl (lock_);
    if (! tl.isLocked() || ! dirty_)
        return;
    copyStructure (structure_, live);
    dirty_ = false;
}

//==============================================================================
// DPAD: u32 version (1), u32 pad count (16), then per pad: u8 model, note,
// choke group, polyphony, filter, source, sample mode, velocity split; f32
// fine, character, resonance, attack, sustain, pitch amount, pitch decay,
// drive, sub, tail; then players A and B: f32 start, end, level; u8 loop,
// reverse, normalize.
preset::Section KitStore::encode (const kit::KitSettings& k)
{
    preset::Section s;
    s.tag = padSectionTag;
    putU32 (s.data, padSectionVersion);
    putU32 (s.data, kit::padCount);
    for (const auto& p : k.pads)
    {
        putU8 (s.data, static_cast<std::uint8_t> (p.model));
        putU8 (s.data, p.note);
        putU8 (s.data, p.chokeGroup);
        putU8 (s.data, p.polyphony);
        putU8 (s.data, static_cast<std::uint8_t> (p.filter));
        putU8 (s.data, static_cast<std::uint8_t> (p.source));
        putU8 (s.data, static_cast<std::uint8_t> (p.sampleMode));
        putU8 (s.data, p.velocitySplit);
        for (float v : { p.fine, p.character, p.resonance, p.attack, p.sustain, p.pitchAmount, p.pitchDecay, p.drive, p.sub, p.tail })
            putF32 (s.data, v);
        for (const auto& pl : p.players)
        {
            putF32 (s.data, pl.start);
            putF32 (s.data, pl.end);
            putF32 (s.data, pl.level);
            putU8 (s.data, pl.loop ? 1 : 0);
            putU8 (s.data, pl.reverse ? 1 : 0);
            putU8 (s.data, pl.normalize ? 1 : 0);
        }
    }
    return s;
}

bool KitStore::decode (const preset::Section& section, kit::KitSettings& out, juce::String& error)
{
    Reader in { section.data };
    if (in.u32() != padSectionVersion || in.u32() != static_cast<std::uint32_t> (kit::padCount) || ! in.ok)
    {
        error = "unsupported pad section";
        return false;
    }
    const auto inRange = [] (float v, float lo, float hi) { return std::isfinite (v) && v >= lo && v <= hi; };
    for (auto& p : out.pads)
    {
        const auto model = in.u8();
        p.note = in.u8();
        p.chokeGroup = in.u8();
        p.polyphony = in.u8();
        const auto filter = in.u8();
        const auto source = in.u8();
        const auto mode = in.u8();
        p.velocitySplit = in.u8();
        p.fine = in.f32();
        p.character = in.f32();
        p.resonance = in.f32();
        p.attack = in.f32();
        p.sustain = in.f32();
        p.pitchAmount = in.f32();
        p.pitchDecay = in.f32();
        p.drive = in.f32();
        p.sub = in.f32();
        p.tail = in.f32();
        bool playersOk = true;
        for (auto& pl : p.players)
        {
            pl.start = in.f32();
            pl.end = in.f32();
            pl.level = in.f32();
            const auto loop = in.u8(), reverse = in.u8(), normalize = in.u8();
            playersOk = playersOk && loop <= 1 && reverse <= 1 && normalize <= 1 && inRange (pl.start, 0.0f, 1.0f)
                     && inRange (pl.end, 0.0f, 1.0f) && inRange (pl.level, 0.0f, 2.0f);
            pl.loop = loop != 0;
            pl.reverse = reverse != 0;
            pl.normalize = normalize != 0;
        }
        const bool ok = in.ok && model < kit::modelCount && p.note < 128 && p.polyphony >= 1 && p.polyphony <= kit::maxVoicesPerPad
                     && filter <= 5 && source <= 2 && mode <= 2 && p.velocitySplit >= 1 && p.velocitySplit <= 127
                     && inRange (p.fine, -100.0f, 100.0f) && inRange (p.character, 0.0f, 1.0f) && inRange (p.resonance, 0.0f, 1.0f)
                     && inRange (p.attack, -1.0f, 1.0f) && inRange (p.sustain, -1.0f, 1.0f) && inRange (p.pitchAmount, 0.0f, 48.0f)
                     && inRange (p.pitchDecay, 0.005f, 0.2f) && inRange (p.drive, 1.0f, 8.0f) && inRange (p.sub, 0.0f, 1.0f)
                     && inRange (p.tail, 0.03f, 1.5f) && playersOk;
        if (! ok)
        {
            error = "invalid pad section";
            return false;
        }
        p.model = static_cast<kit::Model> (model);
        p.filter = static_cast<kit::Filter> (filter);
        p.source = static_cast<kit::Source> (source);
        p.sampleMode = static_cast<kit::SampleMode> (mode);
    }
    if (in.at != section.data.size())
    {
        error = "invalid pad section";
        return false;
    }
    return true;
}

void KitStore::migrateLegacyParameters (const std::map<std::string, float>& values, kit::KitSettings& out)
{
    const auto get = [&] (const std::string& id, float& target) {
        const auto it = values.find (id);
        if (it != values.end() && std::isfinite (it->second))
            target = it->second;
    };
    for (int pad = 0; pad < 8; ++pad)
    {
        auto& p = out.pads[static_cast<std::size_t> (pad)];
        const auto prefix = padPrefix (pad) + ".";
        get (prefix + "fine", p.fine);
        get (prefix + "character", p.character);
        get (prefix + "resonance", p.resonance);
        get (prefix + "transient_attack", p.attack);
        get (prefix + "transient_sustain", p.sustain);
        float filter = static_cast<float> (p.filter), source = static_cast<float> (p.source);
        get (prefix + "filter", filter);
        get (prefix + "source", source);
        p.filter = static_cast<kit::Filter> (std::clamp (static_cast<int> (filter), 0, 5));
        p.source = static_cast<kit::Source> (std::clamp (static_cast<int> (source), 0, 2));
        auto& a = p.players[0];
        float loop = a.loop ? 1.0f : 0.0f, reverse = a.reverse ? 1.0f : 0.0f, normalize = a.normalize ? 1.0f : 0.0f;
        get (prefix + "sample_start", a.start);
        get (prefix + "sample_end", a.end);
        get (prefix + "sample_level", a.level);
        get (prefix + "sample_loop", loop);
        get (prefix + "sample_reverse", reverse);
        get (prefix + "sample_normalize", normalize);
        a.loop = loop >= 0.5f;
        a.reverse = reverse >= 0.5f;
        a.normalize = normalize >= 0.5f;
    }
    auto& kick = out.pads[0];
    get ("drm.p01.pitch_amount", kick.pitchAmount);
    get ("drm.p01.pitch_decay", kick.pitchDecay);
    get ("drm.p01.drive", kick.drive);
    get ("drm.p01.sub", kick.sub);
    get ("drm.p01.tail", kick.tail);
}

} // namespace vox::drums
