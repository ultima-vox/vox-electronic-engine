#include "FactoryContent.h"

#include "DrumsParameters.h"
#include "KitStore.h"

#include <juce_cryptography/juce_cryptography.h>

#include <algorithm>
#include <cstring>
#include <sstream>

namespace vox::drums {
namespace {

using kit::Filter;
using kit::Model;

struct PadRow {
    Model model;
    std::uint8_t note, choke;
    float level, tune, decay, pan, character, cutoff;
    Filter filter;
    float resonance, attack, sustain, pitchAmount, pitchDecay, drive, sub, tail, send1, send2;
};

struct KitTable {
    FactoryKitInfo info;
    float volume;
    int delayTime;
    float delayFeedback, delayTone, reverbSize, reverbDamp;
    PadRow pads[kit::padCount];
};

// Pad layout shared by every kit (the generator's lane roles): kick, snare,
// clap, closed hat, open hat, tom, rim, cymbal, then eight percussion pads.
const KitTable tables[factoryKitCount] = {
    // Psy / Full-on: short punchy kick with a fast pitch drop, tight bright hats, dry.
    { { "Psy Full-on", "Psy / Full-on", { Style::fullOn, Style::goa }, 145.0f }, 0.6f, 3, 0.3f, 0.6f, 0.35f, 0.6f,
      {
          { Model::kick, 36, 0, 0.9f, 0.0f, 0.3f, 0.0f, 0.55f, 20000.0f, Filter::off, 0.0f, 0.2f, -0.1f, 36.0f, 0.022f, 2.2f, 0.25f, 0.2f, 0.0f, 0.0f },
          { Model::snare, 38, 0, 0.6f, 2.0f, 0.16f, 0.0f, 0.7f, 20000.0f, Filter::off, 0.0f, 0.1f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.0f, 0.15f },
          { Model::clap, 39, 0, 0.55f, 0.0f, 0.22f, -0.1f, 0.6f, 20000.0f, Filter::off, 0.0f, 0.0f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.1f, 0.2f },
          { Model::hat, 42, 1, 0.45f, 3.0f, 0.045f, 0.2f, 0.7f, 20000.0f, Filter::hp12, 0.1f, 0.2f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.0f, 0.0f },
          { Model::hat, 46, 1, 0.45f, 3.0f, 0.28f, 0.2f, 0.7f, 20000.0f, Filter::off, 0.0f, 0.0f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.0f, 0.1f },
          { Model::tom, 45, 0, 0.55f, -2.0f, 0.3f, -0.3f, 0.45f, 20000.0f, Filter::off, 0.0f, 0.0f, 0.0f, 30.0f, 0.05f, 1.8f, 0.3f, 0.25f, 0.15f, 0.1f },
          { Model::rim, 37, 0, 0.5f, 4.0f, 0.06f, 0.3f, 0.6f, 20000.0f, Filter::off, 0.0f, 0.0f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.25f, 0.0f },
          { Model::cymbal, 49, 0, 0.35f, 0.0f, 1.4f, -0.3f, 0.6f, 20000.0f, Filter::off, 0.0f, 0.0f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.0f, 0.3f },
          { Model::perc, 50, 0, 0.5f, 7.0f, 0.09f, -0.4f, 0.7f, 20000.0f, Filter::off, 0.0f, 0.0f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.3f, 0.0f },
          { Model::perc, 51, 0, 0.5f, 12.0f, 0.07f, 0.4f, 0.4f, 20000.0f, Filter::off, 0.0f, 0.0f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.3f, 0.0f },
          { Model::tom, 52, 0, 0.5f, 5.0f, 0.2f, 0.25f, 0.5f, 20000.0f, Filter::off, 0.0f, 0.0f, 0.0f, 30.0f, 0.04f, 1.6f, 0.3f, 0.25f, 0.1f, 0.1f },
          { Model::tom, 53, 0, 0.5f, 10.0f, 0.16f, -0.25f, 0.5f, 20000.0f, Filter::off, 0.0f, 0.0f, 0.0f, 30.0f, 0.04f, 1.6f, 0.3f, 0.25f, 0.1f, 0.1f },
          { Model::perc, 54, 0, 0.45f, -5.0f, 0.14f, 0.5f, 0.3f, 20000.0f, Filter::bp, 0.3f, 0.0f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.2f, 0.1f },
          { Model::rim, 55, 0, 0.45f, -3.0f, 0.1f, -0.5f, 0.4f, 20000.0f, Filter::off, 0.0f, 0.0f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.2f, 0.0f },
          { Model::hat, 56, 0, 0.4f, 8.0f, 0.03f, 0.6f, 0.8f, 20000.0f, Filter::hp24, 0.0f, 0.0f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.0f, 0.0f },
          { Model::cymbal, 57, 0, 0.35f, 5.0f, 0.6f, 0.0f, 0.4f, 20000.0f, Filter::off, 0.0f, 0.0f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.0f, 0.35f },
      } },
    // House / Techno: round longer kick, noisy clap with room, shuffling hats, rumble toms.
    { { "House Techno", "House / Techno", { Style::house, Style::techno }, 126.0f }, 0.6f, 2, 0.35f, 0.45f, 0.5f, 0.5f,
      {
          { Model::kick, 36, 0, 0.9f, -2.0f, 0.45f, 0.0f, 0.4f, 20000.0f, Filter::off, 0.0f, 0.0f, 0.2f, 20.0f, 0.05f, 1.4f, 0.5f, 0.4f, 0.0f, 0.0f },
          { Model::snare, 38, 0, 0.55f, -1.0f, 0.2f, 0.0f, 0.5f, 20000.0f, Filter::off, 0.0f, 0.0f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.0f, 0.25f },
          { Model::clap, 39, 0, 0.65f, -1.0f, 0.3f, 0.0f, 0.4f, 20000.0f, Filter::off, 0.0f, 0.0f, 0.1f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.0f, 0.35f },
          { Model::hat, 42, 1, 0.45f, 0.0f, 0.06f, 0.15f, 0.5f, 20000.0f, Filter::hp12, 0.0f, 0.0f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.0f, 0.05f },
          { Model::hat, 46, 1, 0.5f, -1.0f, 0.4f, 0.15f, 0.5f, 20000.0f, Filter::off, 0.0f, 0.0f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.0f, 0.15f },
          { Model::tom, 45, 0, 0.5f, -9.0f, 0.5f, 0.0f, 0.3f, 2500.0f, Filter::lp24, 0.2f, 0.0f, 0.2f, 12.0f, 0.08f, 2.5f, 0.6f, 0.5f, 0.0f, 0.2f },
          { Model::rim, 37, 0, 0.5f, 0.0f, 0.07f, 0.35f, 0.5f, 20000.0f, Filter::off, 0.0f, 0.0f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.3f, 0.1f },
          { Model::cymbal, 49, 0, 0.35f, -3.0f, 2.2f, -0.2f, 0.5f, 20000.0f, Filter::off, 0.0f, 0.0f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.0f, 0.3f },
          { Model::perc, 50, 0, 0.5f, 2.0f, 0.12f, -0.35f, 0.5f, 20000.0f, Filter::off, 0.0f, 0.0f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.25f, 0.1f },
          { Model::perc, 51, 0, 0.5f, 6.0f, 0.1f, 0.35f, 0.6f, 20000.0f, Filter::off, 0.0f, 0.0f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.25f, 0.1f },
          { Model::clap, 52, 0, 0.45f, 5.0f, 0.12f, 0.3f, 0.7f, 20000.0f, Filter::off, 0.0f, 0.0f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.0f, 0.3f },
          { Model::tom, 53, 0, 0.5f, 3.0f, 0.25f, -0.3f, 0.5f, 20000.0f, Filter::off, 0.0f, 0.0f, 0.0f, 24.0f, 0.04f, 1.6f, 0.3f, 0.25f, 0.1f, 0.15f },
          { Model::perc, 54, 0, 0.45f, -7.0f, 0.2f, 0.5f, 0.2f, 20000.0f, Filter::off, 0.0f, 0.0f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.2f, 0.2f },
          { Model::rim, 55, 0, 0.45f, 7.0f, 0.05f, -0.5f, 0.7f, 20000.0f, Filter::off, 0.0f, 0.0f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.35f, 0.0f },
          { Model::hat, 56, 0, 0.4f, 5.0f, 0.1f, 0.55f, 0.3f, 20000.0f, Filter::bp, 0.2f, 0.0f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.1f, 0.1f },
          { Model::cymbal, 57, 0, 0.35f, 2.0f, 0.9f, 0.0f, 0.7f, 20000.0f, Filter::off, 0.0f, 0.0f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.0f, 0.4f },
      } },
    // Hip-hop / Breaks: long sub kick, fat low snare, crisp rolling hats, wide room.
    { { "Hip-hop Breaks", "Hip-hop / Breaks", { Style::trap, Style::breaks }, 92.0f }, 0.6f, 4, 0.25f, 0.4f, 0.6f, 0.45f,
      {
          { Model::kick, 36, 0, 0.9f, -5.0f, 0.9f, 0.0f, 0.3f, 20000.0f, Filter::off, 0.0f, 0.0f, 0.4f, 16.0f, 0.07f, 3.0f, 0.8f, 0.9f, 0.0f, 0.0f },
          { Model::snare, 38, 0, 0.7f, -4.0f, 0.3f, 0.0f, 0.45f, 20000.0f, Filter::off, 0.0f, 0.3f, 0.1f, 24.0f, 0.035f, 2.0f, 0.3f, 0.25f, 0.0f, 0.2f },
          { Model::clap, 39, 0, 0.55f, -3.0f, 0.4f, 0.1f, 0.5f, 20000.0f, Filter::off, 0.0f, 0.0f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.0f, 0.3f },
          { Model::hat, 42, 1, 0.4f, 6.0f, 0.035f, 0.1f, 0.8f, 20000.0f, Filter::hp24, 0.0f, 0.1f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.0f, 0.0f },
          { Model::hat, 46, 1, 0.45f, 4.0f, 0.6f, 0.1f, 0.6f, 20000.0f, Filter::off, 0.0f, 0.0f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.0f, 0.15f },
          { Model::tom, 45, 0, 0.6f, -7.0f, 0.6f, -0.3f, 0.4f, 20000.0f, Filter::off, 0.0f, 0.0f, 0.0f, 24.0f, 0.06f, 1.8f, 0.4f, 0.4f, 0.0f, 0.2f },
          { Model::rim, 37, 0, 0.5f, -2.0f, 0.09f, 0.3f, 0.4f, 20000.0f, Filter::off, 0.0f, 0.0f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.15f, 0.15f },
          { Model::cymbal, 49, 0, 0.35f, -5.0f, 2.8f, -0.3f, 0.4f, 20000.0f, Filter::off, 0.0f, 0.0f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.0f, 0.3f },
          { Model::perc, 50, 0, 0.5f, -4.0f, 0.18f, -0.4f, 0.4f, 20000.0f, Filter::off, 0.0f, 0.0f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.2f, 0.15f },
          { Model::perc, 51, 0, 0.5f, 3.0f, 0.12f, 0.4f, 0.6f, 20000.0f, Filter::off, 0.0f, 0.0f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.2f, 0.15f },
          { Model::snare, 52, 0, 0.45f, 7.0f, 0.1f, 0.2f, 0.8f, 20000.0f, Filter::off, 0.0f, 0.0f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.1f, 0.1f },
          { Model::kick, 53, 0, 0.6f, 7.0f, 0.2f, 0.0f, 0.7f, 20000.0f, Filter::off, 0.0f, 0.0f, 0.0f, 30.0f, 0.02f, 4.0f, 0.1f, 0.1f, 0.0f, 0.0f },
          { Model::perc, 54, 0, 0.45f, -10.0f, 0.3f, 0.5f, 0.2f, 20000.0f, Filter::lp12, 0.3f, 0.0f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.1f, 0.25f },
          { Model::rim, 55, 0, 0.45f, 5.0f, 0.06f, -0.5f, 0.6f, 20000.0f, Filter::off, 0.0f, 0.0f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.3f, 0.0f },
          { Model::hat, 56, 0, 0.4f, 10.0f, 0.02f, 0.5f, 0.9f, 20000.0f, Filter::hp24, 0.0f, 0.0f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.0f, 0.0f },
          { Model::cymbal, 57, 0, 0.3f, 7.0f, 0.5f, 0.0f, 0.8f, 20000.0f, Filter::off, 0.0f, 0.0f, 0.0f, 24.0f, 0.035f, 1.6f, 0.3f, 0.25f, 0.0f, 0.4f },
      } },
};

const KitTable& table (int kit) noexcept
{
    return tables[std::clamp (kit, 0, factoryKitCount - 1)];
}

std::string sha (const void* data, std::size_t size)
{
    return juce::SHA256 (data, size).toHexString().toStdString();
}

} // namespace

const FactoryKitInfo& factoryKitInfo (int kit) noexcept
{
    return table (kit).info;
}

kit::KitSettings factoryKit (int index)
{
    const auto& t = table (index);
    kit::KitSettings k = defaultKit();
    k.volume = t.volume;
    for (int i = 0; i < kit::padCount; ++i)
    {
        const auto& r = t.pads[i];
        kit::PadSettings p;
        p.model = r.model;
        p.note = r.note;
        p.chokeGroup = r.choke;
        p.polyphony = r.model == Model::cymbal || r.model == Model::tom ? 2 : 1;
        p.level = r.level;
        p.tune = r.tune;
        p.decay = r.decay;
        p.pan = r.pan;
        p.character = r.character;
        p.cutoff = r.cutoff;
        p.filter = r.filter;
        p.resonance = r.resonance;
        p.attack = r.attack;
        p.sustain = r.sustain;
        p.pitchAmount = r.pitchAmount;
        p.pitchDecay = r.pitchDecay;
        p.drive = r.drive;
        p.sub = r.sub;
        p.tail = r.tail;
        p.send1 = r.send1;
        p.send2 = r.send2;
        k.pads[static_cast<std::size_t> (i)] = p;
    }
    return k;
}

std::map<std::string, float> factoryKitParameters (int index)
{
    const auto& t = table (index);
    std::map<std::string, float> v;
    for (int i = 0; i < kit::padCount; ++i)
    {
        const auto prefix = padPrefix (i);
        const auto& r = t.pads[i];
        v[prefix + ".level"] = r.level;
        v[prefix + ".tune"] = r.tune;
        v[prefix + ".decay"] = r.decay;
        v[prefix + ".pan"] = r.pan;
        v[prefix + ".cutoff"] = r.cutoff;
        v[prefix + ".send1"] = r.send1;
        v[prefix + ".send2"] = r.send2;
    }
    v["drm.kit.volume"] = t.volume;
    v["drm.fx.delay.time"] = static_cast<float> (t.delayTime);
    v["drm.fx.delay.feedback"] = t.delayFeedback;
    v["drm.fx.delay.tone"] = t.delayTone;
    v["drm.fx.delay.level"] = 0.8f;
    v["drm.fx.reverb.size"] = t.reverbSize;
    v["drm.fx.reverb.damp"] = t.reverbDamp;
    v["drm.fx.reverb.level"] = 0.8f;
    return v;
}

GeneratorSettings factoryGenerator (int kit, int slot) noexcept
{
    const auto& info = table (kit).info;
    GeneratorSettings g;
    // Slots 1-3 in the family's first style, 4-5 in its second.
    g.style = info.styles[slot < 3 ? 0 : 1];
    static constexpr float density[5] = { 0.45f, 0.6f, 0.8f, 0.5f, 0.75f };
    static constexpr float complexity[5] = { 0.25f, 0.4f, 0.6f, 0.45f, 0.65f };
    static constexpr float syncopation[5] = { 0.2f, 0.35f, 0.5f, 0.4f, 0.55f };
    g.density = density[std::clamp (slot, 0, 4)];
    g.complexity = complexity[std::clamp (slot, 0, 4)];
    g.syncopation = syncopation[std::clamp (slot, 0, 4)];
    g.groove = 0.5f;
    g.swing = g.style == Style::house ? 0.25f : g.style == Style::trap || g.style == Style::breaks ? 0.12f : 0.0f;
    g.humanize = g.style == Style::breaks ? 0.35f : 0.15f;
    g.fills = 0.5f;
    g.variation = 0.4f;
    g.seed = 1000u * static_cast<std::uint32_t> (kit + 1) + static_cast<std::uint32_t> (slot) * 17u + 1u;
    return g;
}

void factoryPatterns (int kit, PatternStore::Everything& out)
{
    for (int slot = 0; slot < slotCountForSong; ++slot)
    {
        auto& a = out.slots[static_cast<std::size_t> (slot)];
        a = {};
        a.steps = 16;
        a.bars = 4;
        const auto g = factoryGenerator (kit, slot);
        generatePattern (g, a);
        auto& b = out.alternates[static_cast<std::size_t> (slot)];
        b = {};
        b.steps = 16;
        b.bars = 4;
        generateVariation (g, a, b);
    }
    // A 16-bar song: slot 2 with its B, then slot 5 with its B.
    out.song = {};
    out.song.length = 16;
    out.song.loopLength = 16;
    out.song.place (0, 1, 0, 4);
    out.song.place (4, 1, 1, 4);
    out.song.place (8, 4, 0, 4);
    out.song.place (12, 4, 1, 4);
}

std::string factoryManifest()
{
    std::ostringstream out;
    out << "item,kind,sha256\n";
    const auto all = std::make_unique<PatternStore::Everything>();
    for (int kit = 0; kit < factoryKitCount; ++kit)
    {
        const auto name = std::string (factoryKitInfo (kit).name);
        {
            // The kit: its DPAD section followed by its parameter values in ID order.
            auto bytes = KitStore::encode (factoryKit (kit)).data;
            for (const auto& [id, value] : factoryKitParameters (kit))
            {
                for (const char c : id)
                    bytes.push_back (static_cast<std::byte> (c));
                std::uint32_t bits = 0;
                std::memcpy (&bits, &value, sizeof bits);
                for (int shift = 0; shift < 32; shift += 8)
                    bytes.push_back (static_cast<std::byte> ((bits >> shift) & 0xffu));
            }
            out << name << ",kit," << sha (bytes.data(), bytes.size()) << "\n";
        }
        factoryPatterns (kit, *all);
        const auto section = PatternStore::encode (all->slots, all->alternates);
        out << name << ",patterns," << sha (section.data.data(), section.data.size()) << "\n";
        const auto song = encodeSong (all->song);
        out << name << ",song," << sha (song.data.data(), song.data.size()) << "\n";
    }
    return out.str();
}

} // namespace vox::drums
