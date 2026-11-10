// Golden renders of the ported EffectsChain (vox_dsp, byte-identical to
// f9c8bc6): every effect type alone at 44.1/48/96 kHz, plus each factory
// preset at 48 kHz.
//
//  - Metrics (peak, RMS, DC, spectral centroid per channel) are compared with
//    tests/suite/golden/effects-chain-v1.csv. VOX_GOLDEN_UPDATE=1 rewrites
//    the manifest instead (only for an intended, reviewed sound change).
//  - Rendering in blocks of 64, 512 and an irregular pattern gives the same
//    audio (block-size invariance).
//  - process() makes 0 allocations on the calling thread (allocation probe,
//    where the platform has one).
//  - VOX_GOLDEN_OUT=<dir> writes every render as a 32-bit float WAV.
#include "effects/EffectsChain.h"
#include "GoldenRender.h"

#ifdef VOX_TEST_INTERPOSE_MALLOC
#include "AllocationProbe.h"
#endif

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <string>
#include <vector>

using vstengine::effects::EffectsChain;
using vox::testkit::ManifestRow;

namespace {
int failures = 0;
unsigned long probedAllocations = 0;

void check (bool condition, const std::string& what)
{
    if (! condition)
    {
        std::printf ("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

constexpr const char* typeNames[] = { "distortion", "wavefolder", "phaser", "flanger",
                                      "chorus", "bitcrusher", "delay", "reverb" };
constexpr std::uint32_t maxBlock = 512;

// 1 s of programme (two detuned saws, a noise burst and a click) followed by
// 3 s of silence so delay and reverb tails and the tail gate are rendered.
std::vector<std::vector<float>> stimulus (double rate)
{
    const auto active = static_cast<std::size_t> (rate);
    const auto frames = active * 4;
    std::vector<std::vector<float>> audio (2, std::vector<float> (frames, 0.0f));
    std::uint32_t seed = 0x12345678u;
    double phaseA = 0.0, phaseB = 0.0;
    for (std::size_t i = 0; i < active; ++i)
    {
        phaseA += 110.0 / rate;  phaseA -= std::floor (phaseA);
        phaseB += 165.3 / rate;  phaseB -= std::floor (phaseB);
        seed = seed * 1664525u + 1013904223u;
        const float noise = (static_cast<float> (seed >> 8) / 8388608.0f - 1.0f)
                          * (i < active / 8 ? 0.25f : 0.0f);
        audio[0][i] = static_cast<float> (0.3 * (2.0 * phaseA - 1.0)) + noise;
        audio[1][i] = static_cast<float> (0.3 * (2.0 * phaseB - 1.0)) - noise;
    }
    audio[0][active / 2] += 0.6f;
    audio[1][active / 2] -= 0.6f;
    return audio;
}

// Renders in place in blocks whose sizes cycle through `pattern`.
void render (EffectsChain& chain, std::vector<std::vector<float>>& audio,
             const std::vector<std::uint32_t>& pattern)
{
    const auto frames = audio[0].size();
    std::size_t offset = 0, step = 0;
    while (offset < frames)
    {
        const auto count = static_cast<std::uint32_t> (
            std::min<std::size_t> (pattern[step++ % pattern.size()], frames - offset));
        std::array<float*, 2> pointers { audio[0].data() + offset, audio[1].data() + offset };
#ifdef VOX_TEST_INTERPOSE_MALLOC
        voxProbeBegin();
        chain.process (pointers, count);
        probedAllocations += voxProbeEnd();
#else
        chain.process (pointers, count);
#endif
        offset += count;
    }
}

std::string outputDirectory()
{
    const char* value = std::getenv ("VOX_GOLDEN_OUT");
    return value != nullptr ? std::string (value) : std::string();
}

ManifestRow renderCase (const std::string& key, double rate,
                        const std::function<bool (EffectsChain&)>& configure)
{
    EffectsChain chain;
    check (chain.prepare (rate, maxBlock, 2), key + ": prepare");
    check (configure (chain), key + ": configure");
    chain.reset();

    auto reference = stimulus (rate);
    render (chain, reference, { maxBlock });

    for (const auto& pattern : { std::vector<std::uint32_t> { 64 },
                                 std::vector<std::uint32_t> { 1, 31, 512, 7, 100, 0, 257 } })
    {
        EffectsChain other;
        other.prepare (rate, maxBlock, 2);
        configure (other);
        other.reset();
        auto audio = stimulus (rate);
        render (other, audio, pattern);
        const double difference = std::max (vox::testkit::maxAbsDifference (reference[0], audio[0]),
                                            vox::testkit::maxAbsDifference (reference[1], audio[1]));
        check (difference == 0.0, key + ": block-size invariant render (max diff "
                                      + std::to_string (difference) + ")");
    }

    if (const auto dir = outputDirectory(); ! dir.empty())
        vox::testkit::writeWav (dir + "/" + key + ".wav", reference, rate);

    return { vox::testkit::measure (reference[0], rate), vox::testkit::measure (reference[1], rate) };
}
} // namespace

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf ("usage: %s <manifest.csv>\n", argv[0]);
        return EXIT_FAILURE;
    }
    const std::string manifestPath = argv[1];
    const bool update = std::getenv ("VOX_GOLDEN_UPDATE") != nullptr;

    std::map<std::string, ManifestRow> rows;
    for (double rate : { 44100.0, 48000.0, 96000.0 })
        for (std::size_t index = 0; index < EffectsChain::effectCount; ++index)
        {
            const auto key = std::string (typeNames[index]) + "@" + std::to_string (static_cast<int> (rate));
            rows[key] = renderCase (key, rate, [index] (EffectsChain& chain) {
                if (! chain.applyPreset ("clean"))
                    return false;
                auto setting = chain.state().effects[index];
                setting.enabled = true;
                setting.mix = 0.8f;
                setting.amount = 0.73f;
                setting.rate = 0.41f;
                setting.character = 0.62f;
                return chain.setEffect (index, setting);
            });
        }
    for (const char* preset : { "deep-space", "psy-drive" })
        rows[std::string ("preset-") + preset + "@48000"] =
            renderCase (std::string ("preset-") + preset + "@48000", 48000.0,
                        [preset] (EffectsChain& chain) { return chain.applyPreset (preset); });

#ifdef VOX_TEST_INTERPOSE_MALLOC
    if (voxProbeAvailable())
        check (probedAllocations == 0, "EffectsChain::process allocates nothing ("
                                           + std::to_string (probedAllocations) + " calls)");
    else
        std::printf ("allocation probe unavailable on this platform\n");
#else
    std::printf ("allocation probe not built\n");
#endif

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
        std::printf ("effects golden tests passed (%zu renders)\n", rows.size());
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
