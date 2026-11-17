#include "engine/NoiseRiser.h"
#include "FxNoiseRiserReference.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>
#include <chrono>
#include <limits>

int main() {
    int failures = 0;
    auto check = [&](bool ok, const char* message) { if (!ok) { std::printf("FAIL: %s\n", message); ++failures; } };
    for (double rate : {44100.0, 48000.0, 96000.0}) {
        for (int block : {64, 128, 512}) {
            vox::fx::NoiseRiser a, b;
            a.prepare(rate); b.prepare(rate);
            check(a.noteOn(1, 60, 1.0f) && b.noteOn(1, 60, 1.0f), "trigger");
            std::array<float, 512> l{}, r{}, l2{}, r2{};
            double energy = 0;
            for (int n = 0; n < 20; ++n) {
                a.render(l.data(), r.data(), block);
                b.render(l2.data(), r2.data(), block);
                for (int i = 0; i < block; ++i) {
                    check(std::isfinite(l[i]) && std::abs(l[i]) < 1.0f, "finite bounded audio");
                    check(l[i] == r[i] && l[i] == l2[i] && l2[i] == r2[i], "deterministic stereo");
                    energy += l[i] * l[i];
                }
            }
            check(energy > 1e-8, "audible output");
            a.allSoundOff(1);
            for (int n = 0; n < 20; ++n) a.render(l.data(), r.data(), block);
            check(std::all_of(l.begin(), l.begin() + block, [](float x) { return x == 0; }), "panic release reaches silence");
        }
    }
    // Compare all four seconds against the pre-optimization transcendental
    // reference, including the envelope end, at every supported sample rate.
    for (double rate : {44100.0, 48000.0, 96000.0}) {
        vox::fx::NoiseRiser fast; vox::fx::ReferenceNoiseRiser reference;
        fast.prepare(rate); reference.prepare(rate);
        fast.noteOn(1, 60, 1); reference.noteOn(1, 60, 1);
        std::array<float, 128> fl{}, fr{}, rl{}, rr{};
        double errorEnergy = 0, referenceEnergy = 0, earlyDiff = 0, lateDiff = 0;
        float maxError = 0, previous = 0;
        const int total = static_cast<int>(rate * 4) + 128;
        for (int at = 0; at < total; at += 128) {
            const int count = std::min(128, total - at);
            fast.render(fl.data(), fr.data(), count); reference.render(rl.data(), rr.data(), count);
            for (int i = 0; i < count; ++i) {
                const double e = fl[i] - rl[i];
                maxError = std::max(maxError, static_cast<float>(std::abs(e)));
                errorEnergy += e * e; referenceEnergy += rl[i] * rl[i];
                const double d = fl[i] - previous; previous = fl[i];
                if (at + i >= rate * 0.5 && at + i < rate * 1.0) earlyDiff += d * d;
                if (at + i >= rate * 3.0 && at + i < rate * 3.5) lateDiff += d * d;
                check(std::isfinite(fl[i]) && std::abs(fl[i]) < 1.0f, "full-duration finite bound");
                if (at + i >= rate * 4) check(fl[i] == 0, "event ends at four seconds");
            }
        }
        const double relative = std::sqrt(errorEnergy / referenceEnergy);
        std::printf("rate %.0f reference max error %.9g relative RMS %.9g spectral-difference ratio %.3f\n",
                    rate, maxError, relative, lateDiff / earlyDiff);
        check(maxError < 1e-6f && relative < 1e-5, "full-render reference tolerance");
        check(lateDiff > earlyDiff * 3, "rising high-frequency energy");
    }
    // Callback timing sample (not a real host or release performance gate).
    for (double rate : {44100.0, 48000.0, 96000.0}) for (int voices : {1, 8, 9}) {
        std::vector<double> us; us.reserve(300);
        vox::fx::NoiseRiser bench;
        std::array<float, 128> left{}, right{};
        for (int iteration = 0; iteration < 300; ++iteration) {
            bench.prepare(rate);
            for (int v = 0; v < voices; ++v) bench.noteOn(1, 60 + v, 1);
            const auto start = std::chrono::steady_clock::now();
            bench.render(left.data(), right.data(), 128);
            const auto end = std::chrono::steady_clock::now();
            us.push_back(std::chrono::duration<double, std::micro>(end - start).count());
        }
        std::sort(us.begin(), us.end());
        std::printf("128 samples %.0f Hz %d voices: p50 %.2f p95 %.2f p99 %.2f max %.2f us\n",
                    rate, voices, us[150], us[285], us[297], us.back());
    }
    // The DSP event duration is snapshotted per trigger, without changing the
    // plugin's interim four-second default.
    {
        vox::fx::NoiseRiser shortEvent; shortEvent.prepare(48000);
        check(!shortEvent.noteOn(1, 60, 1, 0.049), "reject short event");
        check(!shortEvent.noteOn(1, 60, 1, 32.1), "reject long event");
        check(shortEvent.noteOn(1, 60, 1, 0.05), "minimum event duration");
        std::array<float, 128> l{}, r{};
        for (int i = 0; i < 20; ++i) shortEvent.render(l.data(), r.data(), 128);
        check(std::all_of(l.begin() + 100, l.end(), [](float v) { return v == 0; }), "minimum event ends");
    }
    vox::fx::NoiseRiser invalid; invalid.prepare(std::numeric_limits<double>::quiet_NaN());
    check(!invalid.noteOn(1, 60, std::numeric_limits<float>::quiet_NaN()), "NaN velocity rejected");
    check(!invalid.noteOn(1, 60, std::numeric_limits<float>::infinity()), "infinite velocity rejected");
    check(!invalid.noteOn(1, 60, -1), "negative velocity rejected");
    check(!invalid.noteOn(0, 60, 1), "invalid channel rejected");
    check(!invalid.noteOn(1, 128, 1), "invalid note rejected");
    invalid.prepare(std::numeric_limits<double>::max());
    check(invalid.noteOn(1, 60, 1), "invalid sample rate falls back safely");
    invalid.allSoundOff(0);
    {
        vox::fx::NoiseRiser poly; poly.prepare(96000);
        for (int i = 0; i < 8; ++i) check(poly.noteOn(i % 2 + 1, 60 + i, 1), "poly note");
        check(poly.noteOn(1, 72, 1), "nine voices including retiree");
        std::array<float, 512> l{}, r{};
        for (int b = 0; b < 751; ++b) {
            poly.render(l.data(), r.data(), 512);
            for (auto sample : l) check(std::isfinite(sample) && std::abs(sample) < 1, "poly full-event bound");
        }
        poly.render(l.data(), r.data(), 512);
        check(std::all_of(l.begin(), l.end(), [](float v) { return v == 0; }), "poly event ends");
    }
    {
        vox::fx::NoiseRiser channels; channels.prepare(48000);
        channels.noteOn(1, 60, 1); channels.noteOn(2, 61, 1);
        channels.allSoundOff(1);
        std::array<float, 512> l{}, r{}; channels.render(l.data(), r.data(), 512);
        check(std::any_of(l.begin() + 300, l.end(), [](float v) { return v != 0; }), "other channel survives panic");
        channels.allSoundOff(2); channels.render(l.data(), r.data(), 512);
        check(std::all_of(l.begin() + 300, l.end(), [](float v) { return v == 0; }), "target channel fades out");
    }
    // The sample clock, not block boundaries, defines the event timeline.
    vox::fx::NoiseRiser x, y; x.prepare(48000); y.prepare(48000);
    x.noteOn(1, 60, 0.8f); y.noteOn(1, 60, 0.8f);
    std::array<float, 512> xl{}, xr{}, yl{}, yr{};
    x.render(xl.data(), xr.data(), 512);
    for (int offset = 0; offset < 512; offset += 64)
        y.render(yl.data() + offset, yr.data() + offset, 64);
    check(xl == yl && xr == yr, "block-boundary invariant");
    vox::fx::NoiseRiser p; p.prepare(48000);
    for (int i = 0; i < 8; ++i) check(p.noteOn(1, 60 + i, 1), "eight events");
    check(p.noteOn(1, 72, 1), "bounded stealing");
    check(!p.noteOn(1, 73, 1), "reject when retirement slot occupied");
    std::array<float, 512> l{}, r{};
    p.render(l.data(), r.data(), 512);
    check(p.noteOn(1, 73, 1), "retirement slot reused");
    p.prepare(48000); p.render(l.data(), r.data(), 512);
    check(std::all_of(l.begin(), l.end(), [](float x) { return x == 0; }), "prepare resets all voices");
    std::printf("FX noise riser: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
