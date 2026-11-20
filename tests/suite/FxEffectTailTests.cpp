// FX-04 tails: measured, bounded retirement for the four shared slots.
//
// The host is told the plugin has a 41 s tail (Processor::getTailLengthSeconds).
// Over-declaring is safe for a host that truncates, but an unbounded or
// never-silent tail is not: it keeps CPU and reverb/delay memory alive and can
// smear the next event. These checks measure one short burst through worst-case
// serial chains and require the output to reach silence and stay silent.
#include "engine/Effects.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {
constexpr double quiet = 3.16e-5; // about -90 dBFS

struct TailResult { double seconds = 0; double lastNonZero = 0; double peak = 0; double finalSecond = 0; };

TailResult measure(double rate, const std::vector<vox::fx::effects::Settings>& slots, double tempo) {
    using namespace vox::fx::effects;
    TailResult result;
    Chain chain;
    if (!chain.prepare(rate)) return result;
    for (std::size_t i = 0; i < slots.size(); ++i) chain.set(i, slots[i]);
    chain.setTempo(tempo);
    const std::size_t burst = static_cast<std::size_t>(rate * .05);
    const std::size_t total = static_cast<std::size_t>(rate * 60);
    std::vector<float> left(512), right(512);
    std::size_t lastAudible = 0, lastNonZero = 0;
    for (std::size_t at = 0; at < total; at += 512) {
        const int n = static_cast<int>(std::min<std::size_t>(512, total - at));
        for (int i = 0; i < n; ++i) {
            const bool in = at + static_cast<std::size_t>(i) < burst;
            left[i] = right[i] = in ? static_cast<float>(.5 * std::sin(2 * 3.14159265358979323846 * 440.0 *
                                                                      static_cast<double>(at + static_cast<std::size_t>(i)) / rate))
                                    : 0.0f;
        }
        chain.process(left.data(), right.data(), n);
        for (int i = 0; i < n; ++i) {
            const double v = std::abs(static_cast<double>(left[i]));
            result.peak = std::max(result.peak, v);
            if (v > quiet) lastAudible = at + static_cast<std::size_t>(i);
            if (v > 0) lastNonZero = at + static_cast<std::size_t>(i);
            if (at + static_cast<std::size_t>(i) >= total - static_cast<std::size_t>(rate))
                result.finalSecond = std::max(result.finalSecond, v);
        }
    }
    result.seconds = static_cast<double>(lastAudible) / rate;
    result.lastNonZero = static_cast<double>(lastNonZero) / rate;
    return result;
}
} // namespace

int main() {
    using namespace vox::fx::effects;
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        if (!ok) { std::printf("FAIL: %s\n", what); ++failures; }
    };
    constexpr double declared = 41.0; // Processor::getTailLengthSeconds()
    struct Case { const char* name; std::vector<Settings> slots; double tempo; };
    std::vector<Case> cases{
        {"reverb only", {{Type::reverb, 1, 1}, {}, {}, {}}, 120},
        {"tempo delay max feedback", {{Type::tempoDelay, 1, 1}, {}, {}, {}}, 30},
        {"free delay max feedback", {{Type::delay, 1, 1}, {}, {}, {}}, 120},
        {"reverb into slow delay into reverb",
         {{Type::reverb, 1, 1}, {Type::tempoDelay, 1, 1}, {Type::reverb, 1, 1}, {Type::drive, 1, 1}}, 30},
        {"four reverbs", {{Type::reverb, 1, 1}, {Type::reverb, 1, 1}, {Type::reverb, 1, 1}, {Type::reverb, 1, 1}}, 120},
    };
    for (const double rate : {44100.0, 48000.0, 96000.0}) {
        for (const auto& c : cases) {
            const auto tail = measure(rate, c.slots, c.tempo);
            std::printf("%5.0f Hz %-36s audible tail %6.3f s, last nonzero %6.3f s, peak %.4f, final second %.3g\n",
                        rate, c.name, tail.seconds, tail.lastNonZero, tail.peak, tail.finalSecond);
            check(tail.peak > .001, "tail case is audible");
            check(tail.seconds < declared, "measured tail stays inside the declared host tail");
            // Retirement is logical (no buffer clear on the audio thread), but
            // it must actually produce zeros and stay at zero.
            check(tail.lastNonZero < declared, "chain stops writing nonzero audio inside the declared tail");
            check(tail.finalSecond == 0, "final second is digitally silent");
        }
    }
    // A silent chain must not generate anything at all.
    {
        Chain chain;
        check(chain.prepare(48000), "prepare");
        for (std::size_t i = 0; i < 4; ++i) chain.set(i, {Type::reverb, 1, 1});
        std::vector<float> left(512), right(512);
        double energy = 0;
        for (int block = 0; block < 400; ++block) {
            chain.process(left.data(), right.data(), 512);
            for (float v : left) energy += std::abs(static_cast<double>(v));
        }
        check(energy == 0, "silence in, silence out");
    }
    std::printf("FX effect tails: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
