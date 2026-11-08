// DRM-00: golden corpus of the legacy 8-pad Drums module, frozen before any
// port (FOUND-00 §6.1, owner decision D3: donor = integration/rack-1.0
// f9c8bc6, not the unreviewed #109 optimisation).
//
// The module source and its C ABI header are byte-identical copies of
// f9c8bc6 (tests/suite/legacy-reference/drums-f9c8bc6, PROVENANCE.csv). They
// are test-only: no plugin links them, and the ABI is not a suite interface.
// This test drives the module through that ABI the way the Rack did, using
// the deterministic scenarios of the #109 render-equivalence harness
// (be102f8:tests/drums/DrumsRenderEquivalenceTests.c, adapted):
//   6 configurations (defaults; tweaked tune/character/pan/filters/
//   transients/sends/outputs/kick; sample + layer sources; dense retriggering
//   with stealing and chokes; Internal sequencer in Both mode; mid-block
//   automation) x 44.1/48/96 kHz x 64/128/512 frames, stereo main + 9 stereo
//   aux buses, with a release-all in the middle (tails and restarts).
//
// Checks: finite output, signal present on main and aux, a second render is
// bit-identical (determinism), and per-bus metrics match
// tests/suite/golden/drums-legacy-f9c8bc6-v1.csv. DRM-02 must reproduce this
// corpus with the ported engine before any sound change.
//
// Modes: VOX_GOLDEN_UPDATE=1 rewrites the manifest; VOX_GOLDEN_OUT=<dir>
// writes main-bus WAVs (48 kHz / 128 frames, one per configuration);
// --dump-parameters <csv> writes the module's parameter table.
#include "instrument/abi/VoxModuleAbiV2.h"
#include "GoldenRender.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

// The module's entry point (declared in the ABI header only for module builds).
extern "C" VoxResult VOX_CALL voxModuleEntryV2 (uint32_t hostAbiVersion, const VoxHostServicesV2* host,
                                               const VoxModuleV2** module);

namespace {

constexpr const char* drumsId = "com.ultimavox.fullon-drums";
constexpr std::uint32_t auxBuses = 9;
constexpr std::uint32_t maxEvents = 512;
constexpr int sampleFrames = 30000;

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
};

const VoxParameterDescriptorV2* param (const Module& m, std::uint32_t i)
{
    return reinterpret_cast<const VoxParameterDescriptorV2*> (
        reinterpret_cast<const std::uint8_t*> (m.descriptor->parameters) + std::size_t (i) * m.descriptor->parameterStride);
}

std::uint32_t indexOf (const Module& m, const std::string& id)
{
    for (std::uint32_t i = 0; i < m.descriptor->parameterCount; ++i)
        if (id == param (m, i)->id)
            return i;
    std::printf ("FAIL: no parameter %s\n", id.c_str());
    std::exit (EXIT_FAILURE);
}

constexpr const char* padKeys[8] = { "kick", "snare", "clap", "chat", "ohat", "tom", "rim", "cymbal" };
constexpr std::uint8_t padNotes[8] = { 36, 38, 39, 42, 46, 45, 37, 49 };

enum Config { defaults, tweaked, samples, dense, internal, automation, configCount };
constexpr const char* configNames[configCount] = { "defaults", "tweaked", "samples", "dense", "internal", "automation" };

void setPad (const Module& m, std::vector<float>& v, int pad, const char* key, float x)
{
    v[indexOf (m, std::string (padKeys[pad]) + "." + key)] = x;
}

void configure (const Module& m, std::vector<float>& v, int config)
{
    for (std::uint32_t i = 0; i < m.descriptor->parameterCount; ++i)
        v[i] = param (m, i)->defaultValue;
    if (config == tweaked || config == samples || config == dense || config == automation)
    {
        for (int pad = 0; pad < 8; ++pad)
        {
            const auto p = static_cast<float> (pad);
            setPad (m, v, pad, "tune", -7.0f + 2.1f * p);
            setPad (m, v, pad, "character", 0.1f + 0.11f * p);
            setPad (m, v, pad, "pan", -0.8f + 0.2f * p);
            setPad (m, v, pad, "filter", static_cast<float> (pad % 6));
            setPad (m, v, pad, "cutoff", 300.0f + 900.0f * p);
            setPad (m, v, pad, "resonance", 0.1f * p);
            setPad (m, v, pad, "fine", -40.0f + 11.0f * p);
            setPad (m, v, pad, "transient_attack", pad % 2 ? 0.6f : -0.4f);
            setPad (m, v, pad, "transient_sustain", pad % 3 ? -0.3f : 0.5f);
            setPad (m, v, pad, "output", static_cast<float> (pad % 4));
            setPad (m, v, pad, "send1", 0.1f * p);
            setPad (m, v, pad, "send2", 0.05f * static_cast<float> (8 - pad));
        }
        v[indexOf (m, "kick.pitch_amount")] = 36.0f;
        v[indexOf (m, "kick.drive")] = 4.0f;
        v[indexOf (m, "kick.sub")] = 0.7f;
        v[indexOf (m, "kit.velocity")] = 0.5f;
    }
    if (config == samples || config == dense)
        for (int pad = 0; pad < 8; ++pad)
        {
            setPad (m, v, pad, "source", static_cast<float> (1 + pad % 2));   // Sample / Layer
            setPad (m, v, pad, "sample_loop", pad % 3 == 0 ? 1.0f : 0.0f);
            setPad (m, v, pad, "sample_reverse", pad % 4 == 1 ? 1.0f : 0.0f);
            setPad (m, v, pad, "sample_start", 0.05f * static_cast<float> (pad % 3));
            setPad (m, v, pad, "sample_end", 1.0f - 0.1f * static_cast<float> (pad % 2));
            setPad (m, v, pad, "sample_normalize", pad % 2 ? 1.0f : 0.0f);
        }
    if (config == internal)
        v[indexOf (m, "seq.mode")] = 2.0f;   // Both
}

std::vector<float> sampleL (sampleFrames), sampleR (sampleFrames);
const float* sampleChannels[2] = { nullptr, nullptr };

std::uint32_t rng = 0;
std::uint32_t rnd()
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

struct Scenario {
    int config;
    double rate;
    std::uint32_t block;
    std::uint64_t frames;
    bool blockIndependentEvents = false;   // same hits whatever the block size
};

// Planar output: (2 + 2 * auxBuses) channels x frames.
std::vector<std::vector<float>> render (const Module& m, const Scenario& sc)
{
    constexpr std::uint32_t channels = 2;
    VoxInstance s = nullptr;
    const VoxCreateParamsV2 cp = { sizeof cp, VOX_MODULE_ABI_V2, 0, 0 };
    check (m.module->create (drumsId, &cp, &s) == VOX_OK, "create");
    const VoxPrepareSpecV2 ps = { sizeof ps, VOX_MODULE_ABI_V2, sc.rate, sc.block, channels,
                                  m.descriptor->parameterCount, maxEvents };
    check (m.module->prepare (s, &ps) == VOX_OK, "prepare");
    std::vector<float> values (m.descriptor->parameterCount);
    configure (m, values, sc.config);
    check (m.module->setAllParameters (s, values.data(), m.descriptor->parameterCount) == VOX_OK, "parameters");

    VoxSampleV2 slots[8] {};
    for (int p = 0; p < 8; ++p)
    {
        slots[p].structSize = sizeof (VoxSampleV2);
        slots[p].version = VOX_MODULE_ABI_V2;
        slots[p].id = static_cast<std::uint64_t> (p + 1);
        slots[p].channels = sampleChannels;
        slots[p].channelCount = p % 2 ? 2u : 1u;
        slots[p].frames = static_cast<std::uint64_t> (sampleFrames - 1000 * p);
        slots[p].sampleRate = p % 2 ? 44100.0 : 48000.0;
        slots[p].peak = 0.8f;
    }
    static VoxPatternStepV2 cells[8 * 4 * 16];
    VoxPatternV2 pattern = { sizeof pattern, VOX_MODULE_ABI_V2, 0, 0, 0, 0,
                             sizeof (VoxPatternStepV2), 8 * 4 * 16, cells };
    const char* name = nullptr;
    check (m.module->content->getFactoryPattern (drumsId, 1, &name, &pattern) == VOX_OK, "factory pattern");

    VoxTransportV2 tr {};
    tr.structSize = sizeof tr;
    tr.version = VOX_MODULE_ABI_V2;
    tr.tempoBpm = 145.0;
    tr.timeSigNumerator = 4;
    tr.timeSigDenominator = 4;
    tr.flags = VOX_TRANSPORT_HAS_TEMPO | VOX_TRANSPORT_HAS_PPQ | VOX_TRANSPORT_HAS_BAR_START
             | VOX_TRANSPORT_HAS_TIME_SIGNATURE | VOX_TRANSPORT_HAS_TIME
             | (sc.config == internal ? VOX_TRANSPORT_PLAYING : 0u);

    std::vector<VoxMidiEventV2> events (maxEvents);
    VoxParamChangeV2 changes[8] {};
    std::uint64_t ids[16] {};
    VoxSampleUsageV2 usage = { sizeof usage, VOX_MODULE_ABI_V2, 16, 0, ids, 0, 0 };

    const std::uint32_t outputs = channels + auxBuses * channels;
    std::vector<std::vector<float>> out (outputs, std::vector<float> (sc.frames, 0.0f));
    std::vector<float> block (std::size_t (outputs) * sc.block);
    float* mainOut[2] = { block.data(), block.data() + sc.block };
    std::vector<float*> aux (auxBuses * channels);
    for (std::uint32_t k = 0; k < aux.size(); ++k)
        aux[k] = block.data() + std::size_t (channels + k) * sc.block;

    // The #109 harness seeds the hit generator with the block size, so its
    // corpus is per block size; the invariance check below uses one seed.
    rng = 12345u + static_cast<std::uint32_t> (sc.config) * 77u + (sc.blockIndependentEvents ? 0u : sc.block);
    const double every = sc.config == dense ? 0.0004 : 0.031;   // seconds between hits
    const auto period = static_cast<std::uint64_t> (every * sc.rate) + 1;
    for (std::uint64_t pos = 0; pos < sc.frames;)
    {
        const auto n = static_cast<std::uint32_t> (std::min<std::uint64_t> (sc.frames - pos, sc.block));
        std::uint32_t ne = 0, np = 0;
        for (std::uint32_t i = 0; i < n && ne < maxEvents; ++i)
            if ((pos + i) % period == 0)
            {
                const int pad = static_cast<int> (rnd() % 8);
                events[ne] = {};
                events[ne].sampleOffset = i;
                events[ne].size = 3;
                events[ne].bytes[0] = 0x90;
                events[ne].bytes[1] = padNotes[pad];
                events[ne].bytes[2] = static_cast<std::uint8_t> (20 + rnd() % 108);
                ++ne;
            }
        if (sc.config == automation && (pos / sc.block) % 3 == 0)
        {
            changes[np++] = { indexOf (m, "kick.tune"), n / 3, static_cast<float> (static_cast<int> (rnd() % 24) - 12), 0 };
            changes[np++] = { indexOf (m, "snare.cutoff"), n / 3, 200.0f + static_cast<float> (rnd() % 10000), 0 };
            changes[np++] = { indexOf (m, "chat.decay"), n - 1, 0.02f + static_cast<float> (rnd() % 400) / 1000.0f, 0 };
        }
        tr.ppqPosition = static_cast<double> (pos) / sc.rate * tr.tempoBpm / 60.0;
        tr.barStartPpq = std::floor (tr.ppqPosition / 4.0) * 4.0;
        tr.timeInSamples = static_cast<std::int64_t> (pos);

        VoxProcessContextV2 c {};
        c.structSize = sizeof c;
        c.version = VOX_MODULE_ABI_V2;
        c.numSamples = n;
        c.outputChannelCount = channels;
        c.outputs = mainOut;
        c.paramChanges = changes;
        c.paramChangeCount = np;
        c.paramChangeStride = sizeof (VoxParamChangeV2);
        c.midiEvents = events.data();
        c.midiEventCount = ne;
        c.midiEventStride = sizeof (VoxMidiEventV2);
        c.samplePosition = pos;
        c.transport = &tr;
        c.pattern = &pattern;
        c.patternGeneration = 1;
        c.auxOutputs = aux.data();
        c.auxBusCount = auxBuses;
        const bool sampled = sc.config == samples || sc.config == dense;
        c.samples = sampled ? slots : nullptr;
        c.sampleSlotCount = sampled ? 8u : 0u;
        c.sampleUsage = &usage;
        if (pos >= sc.frames / 2 && pos < sc.frames / 2 + n)
            c.releaseFlags = VOX_RELEASE_ALL_NOTES_AT_END;   // tails, then new hits
        check (m.module->process (s, &c) == VOX_OK, "process");
        for (std::uint32_t k = 0; k < outputs; ++k)
            std::memcpy (out[k].data() + pos, block.data() + std::size_t (k) * sc.block, sizeof (float) * n);
        pos += n;
    }
    m.module->destroy (s);
    return out;
}

Module openModule()
{
    static const VoxHostServicesV2 services = { sizeof (VoxHostServicesV2), VOX_MODULE_ABI_V2, nullptr, nullptr };
    Module m;
    check (voxModuleEntryV2 (VOX_MODULE_ABI_V2, &services, &m.module) == VOX_OK && m.module != nullptr, "entry");
    check (m.module != nullptr && m.module->getInstrument (0, &m.descriptor) == VOX_OK, "descriptor");
    return m;
}

std::string csvText (const char* text)
{
    std::string out = "\"";
    for (const char* c = text != nullptr ? text : ""; *c != 0; ++c)
        out += *c == '"' ? std::string ("\"\"") : std::string (1, *c);
    return out + "\"";
}

// The legacy parameter table: what every pool/UI parameter means. DRM-01
// maps these meanings to the new suite parameter IDs.
std::string parameterTable (const Module& m)
{
    std::ostringstream csv;
    csv << "index,id,name,unit,group,type,choices,minimum,maximum,default,step,poolIndex,flags\n";
    for (std::uint32_t i = 0; i < m.descriptor->parameterCount; ++i)
    {
        const auto* p = param (m, i);
        std::string choices;
        for (std::uint32_t c = 0; c < p->choiceCount && p->choices != nullptr; ++c)
            choices += (c ? "|" : "") + std::string (p->choices[c]);
        csv << i << ',' << p->id << ',' << csvText (p->name) << ',' << csvText (p->unit) << ','
            << csvText (p->group) << ',' << p->type << ',' << csvText (choices.c_str()) << ','
            << p->minimum << ',' << p->maximum << ',' << p->defaultValue << ',' << p->step << ','
            << p->poolIndex << ',' << p->flags << '\n';
    }
    return csv.str();
}

std::string busName (std::uint32_t pair)
{
    return pair == 0 ? std::string ("main") : "aux" + std::to_string (pair);
}

} // namespace

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf ("usage: %s <manifest.csv> [--dump-parameters <csv>] [--parameters <csv>]\n", argv[0]);
        return EXIT_FAILURE;
    }
    const std::string manifestPath = argv[1];
    rng = 777u;
    for (int i = 0; i < sampleFrames; ++i)
    {
        sampleL[i] = static_cast<float> (0.7 * std::sin (i * 0.05) * std::exp (-i / 8000.0)
                                         + 0.1 * (static_cast<int> (rnd() % 2001) - 1000) / 1000.0);
        sampleR[i] = static_cast<float> (0.6 * std::sin (i * 0.031 + 1.0) * std::exp (-i / 6000.0));
    }
    sampleChannels[0] = sampleL.data();
    sampleChannels[1] = sampleR.data();

    const Module m = openModule();
    if (failures)
        return EXIT_FAILURE;

    // The legacy parameter table is documented next to the corpus; DRM-01
    // maps it to the new suite IDs. Keep the committed copy truthful.
    for (int i = 2; i + 1 < argc; i += 2)
    {
        const std::string flag = argv[i];
        if (flag == "--dump-parameters")
            std::ofstream (argv[i + 1]) << parameterTable (m);
        else if (flag == "--parameters")
        {
            std::ifstream in (argv[i + 1]);
            std::stringstream committed;
            committed << in.rdbuf();
            check (committed.str() == parameterTable (m), "committed legacy parameter table matches the module");
        }
    }
    std::printf ("legacy Drums module: %u parameters\n", m.descriptor->parameterCount);

    const bool update = std::getenv ("VOX_GOLDEN_UPDATE") != nullptr;
    const char* wavDir = std::getenv ("VOX_GOLDEN_OUT");
    std::map<std::string, vox::testkit::ManifestRow> rows;
    double auxPeak = 0.0;
    int scenarios = 0;
    for (int config = 0; config < configCount; ++config)
        for (double rate : { 44100.0, 48000.0, 96000.0 })
            for (std::uint32_t blockSize : { 64u, 128u, 512u })
            {
                const Scenario sc { config, rate, blockSize,
                                    static_cast<std::uint64_t> (rate * (config == dense ? 0.6 : 1.5)) };
                const auto out = render (m, sc);
                const auto again = render (m, sc);
                const std::string key = std::string (configNames[config]) + "@" + std::to_string (static_cast<int> (rate))
                                      + "/" + std::to_string (blockSize);
                check (out == again, key + ": deterministic re-render");
                for (std::uint32_t pair = 0; pair <= auxBuses; ++pair)
                {
                    const auto left = vox::testkit::measure (out[2 * pair], rate);
                    const auto right = vox::testkit::measure (out[2 * pair + 1], rate);
                    check (left.finite && right.finite, key + " " + busName (pair) + ": finite");
                    if (pair > 0)
                        auxPeak = std::max ({ auxPeak, left.peak, right.peak });
                    rows[key + "/" + busName (pair)] = { left, right };
                }
                check (rows[key + "/main"].left.peak > 0.0, key + ": main bus has signal");
                if (wavDir != nullptr && rate == 48000.0 && blockSize == 128)
                    vox::testkit::writeWav (std::string (wavDir) + "/drums-legacy-" + configNames[config] + "-48k.wav",
                                            { out[0], out[1] }, rate);
                ++scenarios;
            }
    check (auxPeak > 0.0, "aux buses carry signal");

    // Block-size invariance with identical events. Measured on f9c8bc6: the
    // same hits rendered in blocks of 64 and 512 are bit-identical on every
    // bus up to the release-all point (that flag acts at the end of its
    // block by ABI design; automation is per block, so it is excluded).
    // Pinned so the port in DRM-02 cannot silently lose it.
    for (int config : { defaults, tweaked, samples, dense, internal })
    {
        Scenario a { config, 48000.0, 64, 48000, true };
        Scenario b = a;
        b.block = 512;
        const auto ra = render (m, a);
        const auto rb = render (m, b);
        // The release-all flag acts at the end of the block that contains the
        // midpoint, i.e. at a block-dependent sample; compare up to there.
        const std::size_t compared = a.frames / 2 - 512;
        double worst = 0.0, peak = 0.0;
        for (std::size_t k = 0; k < ra.size(); ++k)
            for (std::size_t i = 0; i < compared; ++i)
            {
                const double d = std::fabs (static_cast<double> (ra[k][i]) - rb[k][i]);
                if (d > worst)
                    worst = d;
                peak = std::max (peak, static_cast<double> (std::fabs (ra[k][i])));
            }

        std::printf ("block invariance %-9s 64 vs 512: max |d| %.3g (peak %.3g)\n", configNames[config], worst, peak);
        check (worst == 0.0, std::string (configNames[config]) + ": bit-identical in blocks of 64 and 512");
    }

    if (update)
    {
        check (vox::testkit::writeManifest (manifestPath, rows), "write manifest");
        std::printf ("wrote %zu rows to %s\n", rows.size(), manifestPath.c_str());
    }
    else
    {
        const auto expected = vox::testkit::readManifest (manifestPath);
        check (expected.size() == rows.size(), "manifest has " + std::to_string (rows.size()) + " rows");
        for (const auto& [key, row] : rows)
        {
            const auto it = expected.find (key);
            if (it == expected.end())
            {
                check (false, key + ": missing from manifest");
                continue;
            }
            for (int side = 0; side < 2; ++side)
                if (auto mismatch = vox::testkit::compare (side == 0 ? it->second.left : it->second.right,
                                                           side == 0 ? row.left : row.right))
                    check (false, key + (side == 0 ? " L: " : " R: ") + *mismatch);
        }
    }

    if (failures == 0)
        std::printf ("legacy Drums golden corpus: %d scenarios, %zu bus rows, all deterministic\n",
                     scenarios, rows.size());
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
