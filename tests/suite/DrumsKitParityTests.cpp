// DRM-03: the C++ kit engine against the frozen DRM-00 reference module.
//
// The kit engine runs the legacy kit (legacyKit(): pads 1-8 as the legacy
// pads, 16 voices, one voice per pad); the reference module is driven
// through its own C ABI with the same parameter values (mapped by ID), the
// same MIDI, transport, factory pattern and samples. Both render the main
// bus and the 9 aux buses (2 sends, Out 2..8). Every sample of every bus
// must be bit-identical, for:
//   - defaults, and 6 randomised kits (every legacy parameter drawn in its
//     range: tune/fine, decay, pan, character, filter type/cutoff/resonance,
//     transient, output bus, sends, kick body, choke, velocity, volume),
//   - host MIDI hits on all pads (dense: retriggers, chokes, voice stealing),
//     CC120 / CC123,
//   - samples on every pad (Sample and Layer, region, loop, reverse,
//     normalize, level), a 44.1 kHz stereo and a 48 kHz mono buffer,
//   - the internal sequencer with every factory pattern in Internal and Both
//     mode, swing and accent, and a loop back jump,
//   - automation: parameter changes at block starts,
//   - panic at a block start and release-all (stop and fade) at block ends,
// at 44.1 / 48 / 96 kHz with 64 / 333 / 512-frame blocks.
#include "instrument/abi/VoxModuleAbiV2.h"

#include "engine/KitEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <random>
#include <string>
#include <vector>

extern "C" VoxResult VOX_CALL voxModuleEntryV2 (uint32_t hostAbiVersion, const VoxHostServicesV2* host,
                                               const VoxModuleV2** module);

namespace {

namespace kit = vox::drums::kit;
constexpr const char* drumsId = "com.ultimavox.fullon-drums";
int failures = 0;

void check (bool ok, const std::string& what)
{
    if (! ok)
    {
        std::printf ("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

struct Module {
    const VoxModuleV2* module = nullptr;
    const VoxInstrumentDescriptorV2* descriptor = nullptr;
    const VoxParameterDescriptorV2& param (std::uint32_t i) const
    {
        return *reinterpret_cast<const VoxParameterDescriptorV2*> (
            reinterpret_cast<const std::uint8_t*> (descriptor->parameters) + std::size_t (i) * descriptor->parameterStride);
    }
    std::uint32_t count() const { return descriptor->parameterCount; }
};

const Module& legacy()
{
    static Module m = [] {
        static const VoxHostServicesV2 services = { sizeof (VoxHostServicesV2), VOX_MODULE_ABI_V2, nullptr, nullptr };
        Module r;
        voxModuleEntryV2 (VOX_MODULE_ABI_V2, &services, &r.module);
        r.module->getInstrument (0, &r.descriptor);
        return r;
    }();
    return m;
}

constexpr const char* padKeys[8] = { "kick", "snare", "clap", "chat", "ohat", "tom", "rim", "cymbal" };

// Legacy parameter values (by descriptor index) -> kit settings.
kit::KitSettings toKit (const std::vector<float>& v)
{
    const auto& m = legacy();
    std::map<std::string, float> byId;
    for (std::uint32_t i = 0; i < m.count(); ++i)
        byId[m.param (i).id] = v[i];
    auto k = kit::legacyKit();
    k.volume = byId["kit.volume"];
    k.velocitySensitivity = byId["kit.velocity"];
    k.chokeEnabled = byId["kit.hat_choke"] >= 0.5f;
    k.sequencerMode = static_cast<std::uint8_t> (byId["seq.mode"]);
    k.swing = byId["seq.swing"];
    k.accent = byId["seq.accent"];
    for (int p = 0; p < 8; ++p)
    {
        auto& pad = k.pads[static_cast<std::size_t> (p)];
        const std::string key = padKeys[p];
        const auto get = [&] (const char* name) { return byId[key + "." + name]; };
        pad.level = get ("level");
        pad.tune = get ("tune");
        pad.decay = get ("decay");
        pad.pan = get ("pan");
        pad.character = get ("character");
        pad.send1 = get ("send1");
        pad.send2 = get ("send2");
        pad.cutoff = get ("cutoff");
        pad.resonance = get ("resonance");
        pad.fine = get ("fine");
        pad.filter = static_cast<kit::Filter> (static_cast<int> (get ("filter")));
        pad.attack = get ("transient_attack");
        pad.sustain = get ("transient_sustain");
        pad.output = static_cast<std::uint8_t> (get ("output"));
        pad.source = static_cast<kit::Source> (static_cast<int> (get ("source")));
        auto& a = pad.players[0];
        a.start = get ("sample_start");
        a.end = get ("sample_end");
        a.loop = get ("sample_loop") >= 0.5f;
        a.reverse = get ("sample_reverse") >= 0.5f;
        a.normalize = get ("sample_normalize") >= 0.5f;
        a.level = get ("sample_level");
        pad.laneOn = byId[std::string ("seq.lane.") + padKeys[p]] >= 0.5f;
    }
    auto& kick = k.pads[0];
    kick.pitchAmount = byId["kick.pitch_amount"];
    kick.pitchDecay = byId["kick.pitch_decay"];
    kick.drive = byId["kick.drive"];
    kick.sub = byId["kick.sub"];
    kick.tail = byId["kick.tail"];
    return k;
}

std::vector<float> defaults()
{
    std::vector<float> v;
    for (std::uint32_t i = 0; i < legacy().count(); ++i)
        v.push_back (legacy().param (i).defaultValue);
    return v;
}

float randomValue (std::mt19937& rng, const VoxParameterDescriptorV2& d)
{
    std::uniform_real_distribution<float> u (0.0f, 1.0f);
    if (d.type == VOX_PARAM_BOOLEAN)
        return u (rng) < 0.5f ? 0.0f : 1.0f;
    if (d.type == VOX_PARAM_CHOICE || d.type == VOX_PARAM_INTEGER)
        return std::floor (d.minimum + u (rng) * (d.maximum - d.minimum + 0.999f));
    return d.minimum + u (rng) * (d.maximum - d.minimum);
}

struct Buffer {
    std::vector<std::vector<float>> data;
    std::vector<const float*> pointers;
    std::uint64_t id;
    double rate;
    float peak = 0.0f;
};

Buffer makeSample (std::uint64_t id, int channels, double rate, int frames)
{
    Buffer b;
    b.id = id;
    b.rate = rate;
    b.data.assign (static_cast<std::size_t> (channels), std::vector<float> (static_cast<std::size_t> (frames)));
    for (int ch = 0; ch < channels; ++ch)
        for (int i = 0; i < frames; ++i)
        {
            const double t = i / rate;
            const float x = static_cast<float> ((0.7 - 0.2 * ch) * std::exp (-t * (6.0 + 3 * ch))
                                                * std::sin (2.0 * 3.14159265358979 * (150.0 + 400.0 * ch) * t));
            b.data[static_cast<std::size_t> (ch)][static_cast<std::size_t> (i)] = x;
            b.peak = std::max (b.peak, std::abs (x));
        }
    for (auto& c : b.data)
        b.pointers.push_back (c.data());
    return b;
}

struct Scenario {
    std::string name;
    int seed = 0;            // 0 = defaults
    int mode = 0;            // sequencer: 0 host, 1 internal, 2 both
    int pattern = 0;
    bool samples = false;
    bool automation = false;
    bool loopBack = false;
    bool dense = false;
};

void run (const Scenario& sc, double rate, std::uint32_t block)
{
    const std::string label = sc.name + " @ " + std::to_string (static_cast<int> (rate)) + "/" + std::to_string (block);
    const auto& m = legacy();
    std::mt19937 rng (static_cast<unsigned> (sc.seed * 7919 + static_cast<int> (rate) + static_cast<int> (block)));

    // Parameters.
    auto values = defaults();
    std::map<std::string, std::uint32_t> index;
    for (std::uint32_t i = 0; i < m.count(); ++i)
        index[m.param (i).id] = i;
    if (sc.seed != 0)
        for (std::uint32_t i = 0; i < m.count(); ++i)
        {
            const std::string id = m.param (i).id;
            if (id.rfind ("seq.", 0) == 0)
                continue;
            values[i] = randomValue (rng, m.param (i));
        }
    values[index["kit.volume"]] = std::max (values[index["kit.volume"]], 0.5f);   // audible
    values[index["seq.mode"]] = static_cast<float> (sc.mode);
    if (sc.mode != 0)
    {
        values[index["seq.swing"]] = 0.21f;
        values[index["seq.accent"]] = 0.7f;
    }
    if (sc.samples)
        for (int p = 0; p < 8; ++p)
        {
            const std::string key = padKeys[p];
            values[index[key + ".source"]] = p % 3 == 2 ? 2.0f : 1.0f;
            values[index[key + ".sample_loop"]] = p == 3 ? 1.0f : 0.0f;
            values[index[key + ".sample_reverse"]] = p == 5 ? 1.0f : 0.0f;
            values[index[key + ".sample_normalize"]] = p == 6 ? 1.0f : 0.0f;
            values[index[key + ".sample_start"]] = p == 7 ? 0.2f : 0.0f;
            values[index[key + ".sample_end"]] = p == 7 ? 0.7f : 1.0f;
        }

    // Samples: two buffers alternating over the pads.
    std::vector<Buffer> buffers;
    buffers.push_back (makeSample (11, 2, 44100.0, 20000));
    buffers.push_back (makeSample (12, 1, 48000.0, 9000));
    std::array<VoxSampleV2, 8> legacySamples {};
    std::array<kit::Sample, kit::sampleSlots> kitSamples {};
    for (int p = 0; p < 8; ++p)
    {
        auto& ls = legacySamples[static_cast<std::size_t> (p)];
        ls.structSize = sizeof ls;
        ls.version = VOX_MODULE_ABI_V2;
        if (! sc.samples)
            continue;
        const auto& b = buffers[static_cast<std::size_t> (p % 2)];
        ls.id = b.id;
        ls.channels = b.pointers.data();
        ls.channelCount = static_cast<std::uint32_t> (b.pointers.size());
        ls.frames = b.data[0].size();
        ls.sampleRate = b.rate;
        ls.peak = b.peak;
        auto& ks = kitSamples[static_cast<std::size_t> (p * 2)];
        ks.id = b.id;
        ks.channels = b.pointers.data();
        ks.channelCount = ls.channelCount;
        ks.frames = ls.frames;
        ks.sampleRate = b.rate;
        ks.peak = b.peak;
    }

    // Factory pattern from the reference's content API.
    std::array<VoxPatternStepV2, 8 * 4 * 16> legacyCells {};
    VoxPatternV2 legacyPattern = { sizeof (VoxPatternV2), VOX_MODULE_ABI_V2, 0, 0, 0, 0,
                                   sizeof (VoxPatternStepV2), static_cast<std::uint32_t> (legacyCells.size()), legacyCells.data() };
    const char* patternName = nullptr;
    m.module->content->getFactoryPattern (drumsId, static_cast<std::uint32_t> (sc.pattern), &patternName, &legacyPattern);
    std::array<kit::PatternStep, 8 * 4 * 16> kitCells {};
    for (std::size_t i = 0; i < kitCells.size(); ++i)
        kitCells[i] = { (legacyCells[i].flags & VOX_STEP_ON) != 0, (legacyCells[i].flags & VOX_STEP_ACCENT) != 0,
                        legacyCells[i].velocity };
    const kit::Pattern kitPattern { legacyPattern.laneCount, legacyPattern.stepsPerBar, legacyPattern.barCount,
                                    legacyPattern.timing, kitCells.data() };

    // Instances.
    VoxInstance ref = nullptr;
    const VoxCreateParamsV2 cp = { sizeof cp, VOX_MODULE_ABI_V2, 0, 0 };
    m.module->create (drumsId, &cp, &ref);
    const VoxPrepareSpecV2 spec = { sizeof spec, VOX_MODULE_ABI_V2, rate, block, 2u, m.count(), 512u };
    m.module->prepare (ref, &spec);
    m.module->setAllParameters (ref, values.data(), m.count());
    kit::KitEngine engine;
    engine.prepare (rate);
    engine.setMaxVoices (16);
    auto settings = toKit (values);

    std::vector<std::vector<float>> refBus (20, std::vector<float> (block)), kitBus (20, std::vector<float> (block));
    std::array<float*, kit::auxBuses * 2> kitAux {};
    for (int b = 0; b < kit::auxBuses * 2; ++b)
        kitAux[static_cast<std::size_t> (b)] = kitBus[static_cast<std::size_t> (b + 2)].data();
    float* refOut[2] = { refBus[0].data(), refBus[1].data() };
    float* refAux[18];
    for (int b = 0; b < 18; ++b)
        refAux[b] = refBus[static_cast<std::size_t> (b + 2)].data();
    std::array<std::uint64_t, 32> usageIds {};
    VoxSampleUsageV2 usage { sizeof (VoxSampleUsageV2), VOX_MODULE_ABI_V2, 32, 0, usageIds.data(), 0, 0 };

    const double bpm = 145.0;
    const auto total = static_cast<std::uint32_t> (rate * 5.0);
    double ppq = 0.0, lastPpq = -1.0e300;
    bool jumped = false;
    std::size_t mismatch = 0, first = 0;
    double peak = 0.0, auxPeak = 0.0;
    const auto& notes = std::array<std::uint8_t, 8> { 36, 38, 39, 42, 46, 45, 37, 49 };
    int b = 0;
    for (std::uint32_t done = 0; done < total; done += block, ++b)
    {
        if (sc.loopBack && ! jumped && ppq >= 6.0)
        {
            ppq = 2.0;
            jumped = true;
        }
        // Automation at the block start.
        std::vector<VoxParamChangeV2> changes;
        if (sc.automation && b % 4 == 2)
            for (int k = 0; k < 4; ++k)
            {
                const auto i = static_cast<std::uint32_t> (rng() % m.count());
                const std::string id = m.param (i).id;
                if (id.rfind ("seq.", 0) == 0 || id.find (".source") != std::string::npos)
                    continue;
                values[i] = randomValue (rng, m.param (i));
                changes.push_back ({ i, 0, values[i], 0 });
            }
        if (! changes.empty())
            settings = toKit (values);

        // MIDI.
        std::vector<VoxMidiEventV2> refMidi;
        std::vector<kit::Event> kitMidi;
        const auto add = [&] (std::uint32_t off, std::uint8_t b0, std::uint8_t b1, std::uint8_t b2) {
            VoxMidiEventV2 e {};
            e.sampleOffset = off;
            e.size = 3;
            e.bytes[0] = b0;
            e.bytes[1] = b1;
            e.bytes[2] = b2;
            refMidi.push_back (e);
            kit::Event k;
            k.offset = off;
            k.size = 3;
            k.bytes[0] = b0;
            k.bytes[1] = b1;
            k.bytes[2] = b2;
            kitMidi.push_back (k);
        };
        if (sc.mode != 1 && (sc.dense || b % 3 == 0))
        {
            std::vector<std::uint32_t> offsets;
            const int hitsThisBlock = sc.dense ? 6 : 2;
            for (int k = 0; k < hitsThisBlock; ++k)
                offsets.push_back (static_cast<std::uint32_t> (rng() % block));
            std::sort (offsets.begin(), offsets.end());
            for (auto off : offsets)
                add (off, 0x90, notes[rng() % 8], static_cast<std::uint8_t> (1 + rng() % 127));
            if (b % 23 == 11)
                add (block / 2, 0xB0, 123, 0);
            if (b % 37 == 20)
                add (block - 1, 0xB0, 120, 0);
        }
        const bool panic = b % 41 == 40;
        const std::uint32_t releaseFlags = b % 29 == 28 ? VOX_RELEASE_ALL_SOUND_AT_END : (b % 17 == 16 ? VOX_RELEASE_ALL_NOTES_AT_END : 0u);

        // Transport.
        VoxTransportV2 tr {};
        tr.structSize = sizeof tr;
        tr.version = VOX_MODULE_ABI_V2;
        tr.flags = VOX_TRANSPORT_PLAYING | VOX_TRANSPORT_HAS_TEMPO | VOX_TRANSPORT_HAS_PPQ;
        if (ppq < lastPpq)
            tr.flags |= VOX_TRANSPORT_DISCONTINUITY;
        lastPpq = ppq;
        tr.tempoBpm = bpm;
        tr.ppqPosition = ppq;
        kit::Transport ktr;
        ktr.playing = ktr.hasTempo = ktr.hasPpq = true;
        ktr.bpm = bpm;
        ktr.ppq = ppq;

        VoxProcessContextV2 c {};
        c.structSize = sizeof c;
        c.version = VOX_MODULE_ABI_V2;
        c.numSamples = block;
        c.outputChannelCount = 2;
        c.outputs = refOut;
        c.paramChanges = changes.data();
        c.paramChangeCount = static_cast<std::uint32_t> (changes.size());
        c.paramChangeStride = sizeof (VoxParamChangeV2);
        c.midiEvents = refMidi.data();
        c.midiEventCount = static_cast<std::uint32_t> (refMidi.size());
        c.midiEventStride = sizeof (VoxMidiEventV2);
        c.releaseFlags = (panic ? VOX_PANIC_AT_START : 0u) | releaseFlags;
        c.transport = &tr;
        c.pattern = &legacyPattern;
        c.patternGeneration = static_cast<std::uint64_t> (sc.pattern + 1);
        c.auxOutputs = refAux;
        c.auxBusCount = 9;
        c.samples = legacySamples.data();
        c.sampleSlotCount = 8;
        c.sampleUsage = &usage;
        check (m.module->process (ref, &c) == VOX_OK, label + ": reference processes");

        kit::Outputs out { kitBus[0].data(), kitBus[1].data(), &kitAux };
        const auto release = releaseFlags == VOX_RELEASE_ALL_SOUND_AT_END ? kit::KitEngine::Release::stopAtEnd
                           : releaseFlags == VOX_RELEASE_ALL_NOTES_AT_END ? kit::KitEngine::Release::fadeAtEnd
                                                                           : kit::KitEngine::Release::none;
        engine.process (settings, out, block, kitMidi.data(), static_cast<std::uint32_t> (kitMidi.size()), ktr,
                        &kitPattern, kitSamples.data(), panic, release);

        for (std::size_t bus = 0; bus < 20; ++bus)
            for (std::uint32_t i = 0; i < block; ++i)
            {
                const float a = refBus[bus][i], k = kitBus[bus][i];
                if (bus < 2)
                    peak = std::max (peak, static_cast<double> (std::abs (a)));
                else
                    auxPeak = std::max (auxPeak, static_cast<double> (std::abs (a)));
                if (std::memcmp (&a, &k, 4) != 0 && mismatch++ == 0)
                    first = done + i + bus * 1000000000ull;
            }
        ppq += block / rate * bpm / 60.0;
    }
    m.module->destroy (ref);
    check (peak > 1.0e-3, label + ": makes sound");
    if (sc.seed != 0)
        check (auxPeak > 1.0e-4, label + ": aux buses carry signal");
    check (mismatch == 0, label + ": bit-identical on every bus (" + std::to_string (mismatch) + " samples differ, first bus "
                              + std::to_string (first / 1000000000ull) + " sample " + std::to_string (first % 1000000000ull) + ")");
}

} // namespace

int main()
{
    std::vector<Scenario> scenarios = {
        { "defaults, host MIDI", 0, 0, 0, false, false, false, false },
        { "defaults, dense host MIDI", 0, 0, 0, false, false, false, true },
        { "defaults, samples", 0, 0, 0, true, false, false, true },
        { "internal, Full-on Basic", 0, 1, 0, false, false, false, false },
        { "internal, Rolling Offbeat, loop back", 0, 1, 2, false, false, true, false },
        { "both, Breakdown, samples", 0, 2, 3, true, false, false, false },
    };
    for (int seed = 1; seed <= 6; ++seed)
        scenarios.push_back ({ "random kit " + std::to_string (seed) + (seed % 2 ? ", samples" : ""), seed, seed % 3,
                               seed % 4, seed % 2 == 1, true, seed == 4, seed % 3 == 0 });
    int runs = 0;
    for (const auto& sc : scenarios)
        for (double rate : { 44100.0, 48000.0, 96000.0 })
            for (std::uint32_t block : { 64u, 333u, 512u })
            {
                run (sc, rate, block);
                ++runs;
            }
    if (failures == 0)
        std::printf ("VOX Drums kit engine: %d renders bit-identical to the DRM-00 reference on all 10 buses\n", runs);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
