#include "effects/EffectsChain.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <numeric>
#include <vector>

namespace {
int checks {};
#define REQUIRE(condition, message) do { ++checks; if (!(condition)) { \
    std::cerr << "FAILED: " << message << " line " << __LINE__ << '\n'; \
    return EXIT_FAILURE; } } while (false)

using Stereo = std::array<std::vector<float>, 2>;

Stereo source(std::size_t samples)
{
    Stereo audio { std::vector<float>(samples), std::vector<float>(samples) };
    for (std::size_t i = 0; i < samples; ++i) {
        const auto time = static_cast<float>(i) / 48000.0f;
        audio[0][i] = 0.35f * std::sin(2.0f * 3.14159265358979323846f
                                      * 233.0f * time);
        audio[1][i] = 0.31f * std::sin(2.0f * 3.14159265358979323846f
                                      * 311.0f * time);
    }
    audio[0][0] += 0.8f;
    audio[1][0] -= 0.7f;
    return audio;
}

void process(vstengine::effects::EffectsChain& chain, Stereo& audio)
{
    std::array<float*, 2> pointers { audio[0].data(), audio[1].data() };
    chain.process(pointers, static_cast<std::uint32_t>(audio[0].size()));
}

double difference(const Stereo& a, const Stereo& b)
{
    double result = 0.0;
    for (std::size_t channel = 0; channel < 2; ++channel)
        for (std::size_t i = 0; i < a[channel].size(); ++i)
            result += std::abs(a[channel][i] - b[channel][i]);
    return result;
}

bool finiteBounded(const Stereo& audio)
{
    for (const auto& channel : audio)
        for (const float sample : channel)
            if (!std::isfinite(sample) || std::abs(sample) > 4.0001f)
                return false;
    return true;
}
} // namespace

int main()
{
    using namespace vstengine::effects;
    EffectsChain chain;
    REQUIRE(!chain.prepare(0.0, 512, 2), "invalid prepare rejected");
    REQUIRE(chain.prepare(48000.0, 4096, 2), "stereo chain prepares");
    REQUIRE(chain.latencySamples() == 0 && chain.tailSamples() == 0,
            "clean chain latency and tail bounded");

    const auto dry = source(4096);
    for (std::size_t index = 0; index < EffectsChain::effectCount; ++index) {
        REQUIRE(chain.applyPreset("clean"), "clean preset applies");
        auto setting = chain.state().effects[index];
        setting.enabled = true;
        setting.mix = 1.0f;
        setting.amount = 0.73f;
        setting.rate = 0.41f;
        setting.character = 0.62f;
        REQUIRE(chain.setEffect(index, setting), "effect setting accepted");
        chain.reset();
        auto rendered = dry;
        process(chain, rendered);
        REQUIRE(finiteBounded(rendered), "effect render finite and bounded");
        REQUIRE(difference(dry, rendered) > 0.01,
                "each effect materially changes render");
        chain.reset();
        auto repeated = dry;
        process(chain, repeated);
        REQUIRE(rendered == repeated, "reset render deterministic");
    }

    REQUIRE(chain.applyPreset("deep-space"), "space preset applies");
    REQUIRE(chain.tailSamples() > 0
                && chain.tailSamples() <= 8u * 48000u,
            "time-effect tail explicitly bounded");
    std::array<std::byte, EffectsChain::encodedStateBytes> encoded {};
    std::uint32_t written {};
    REQUIRE(chain.saveState(encoded, written)
                && written == EffectsChain::encodedStateBytes,
            "versioned chain state saves");
    EffectsChain restored;
    REQUIRE(restored.prepare(48000.0, 4096, 2), "restored chain prepares");
    REQUIRE(restored.loadState(EffectsChain::stateVersion, encoded),
            "versioned chain state loads");
    std::array<std::byte, EffectsChain::encodedStateBytes> roundTrip {};
    REQUIRE(restored.saveState(roundTrip, written) && roundTrip == encoded,
            "chain state round-trip canonical");

    const auto beforeInvalid = restored.state();
    auto corrupt = encoded;
    corrupt[8] = std::byte { 0xff };
    REQUIRE(!restored.loadState(EffectsChain::stateVersion, corrupt),
            "invalid topology rejected");
    REQUIRE(restored.state().effects[0].type == beforeInvalid.effects[0].type
                && restored.state().effects[6].mix
                    == beforeInvalid.effects[6].mix,
            "invalid state load transactional");
    REQUIRE(!restored.loadState(EffectsChain::stateVersion + 1, encoded),
            "future schema rejected explicitly");
    REQUIRE(!restored.applyPreset("missing"), "unknown preset rejected");

    chain.reset();
    restored.reset();
    auto first = dry;
    auto second = dry;
    process(chain, first);
    process(restored, second);
    REQUIRE(first == second, "same state produces deterministic stereo render");

    REQUIRE(chain.applyPreset("psy-drive"), "drive preset applies");
    auto driven = dry;
    process(chain, driven);
    REQUIRE(difference(first, driven) > 1.0,
            "factory chain presets materially differ");

    // A full-buffer fill used to live in reset(). History must still match a
    // chain that has never sounded, including after the delay line wraps.
    for (const char* id : { "wide-motion", "deep-space" }) {
        EffectsChain kept, cleared;
        REQUIRE(kept.prepare(48000.0, 4096, 2) && cleared.prepare(48000.0, 4096, 2),
                "history pair prepares");
        REQUIRE(kept.applyPreset(id) && cleared.applyPreset(id), "history preset");
        auto warm = source(48000 * 3);
        for (std::size_t offset = 0; offset < warm[0].size(); ) {
            const auto count = std::min<std::size_t>(4096, warm[0].size() - offset);
            Stereo block { std::vector<float>(count), std::vector<float>(count) };
            for (std::size_t i = 0; i < count; ++i) {
                block[0][i] = warm[0][offset + i];
                block[1][i] = warm[1][offset + i];
            }
            auto other = block;
            process(kept, block);
            process(cleared, other);
            offset += count;
        }
        auto withHistory = dry;
        auto dropped = dry;
        cleared.reset();
        process(kept, withHistory);
        process(cleared, dropped);
        REQUIRE(difference(withHistory, dropped) > 0.01, "time-effect history is audible before reset");
        EffectsChain fresh;
        REQUIRE(fresh.prepare(48000.0, 4096, 2), "fresh chain prepares");
        REQUIRE(fresh.applyPreset(id), "fresh preset");
        auto fromFresh = dry;
        process(fresh, fromFresh);
        REQUIRE(dropped == fromFresh, "reset matches a chain with no prior tail");
    }

    std::cout << "Effects chain tests passed (" << checks << ")\n";
    return EXIT_SUCCESS;
}
