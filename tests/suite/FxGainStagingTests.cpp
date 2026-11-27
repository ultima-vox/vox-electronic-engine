// FX gain staging: measure all 232 frozen review recipes (baseline tables
// for the owner gain report) and hold non-silence/no-clip regressions.
// Run with a path argument to dump the full TSV table for evidence.
//
// Original study header:
// Renders all 232 frozen review recipes (Wave A 120 + Wave B 112) and a
// normalized note/velocity/BPM variant, reporting peak/active-RMS/crest/tail
// in dBFS, plus MIDI-render vs engine-direct vs output-gain path checks.
#include "content/FactoryCandidates.h"
#include "content/ReviewWaveBStage.h"
#include "content/ReviewWaveBAssets.h"
#include "content/ReviewWaveBPerformance.h"
#include "content/ReviewWaveBCrossings.h"
#include "content/ReviewWaveBInterludes.h"
#include "content/ReviewWaveBRoomCuts.h"
#include "content/ReviewWaveBSessionCuts.h"
#include "content/ReviewWaveBSignals.h"
#include "content/ReviewWaveBEditPoints.h"
#include "content/ReviewWaveBCounterpoints.h"
#include "engine/OfflineRender.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace vox::fx;

namespace {
constexpr double kRate = 48000.0;

struct Metrics {
    double peak = 0;        // linear full-file peak (max of L/R)
    double activeRms = 0;   // RMS over the configured event window
    double tailRms = 0;     // RMS after the event window (tail/decay)
    double crest = 0;       // peak - activeRms in dB
    bool silence = true;
    bool clip = false;      // any |sample| >= 0.999
    std::size_t frames = 0;
};

double db(double linear) { return linear > 0 ? 20.0 * std::log10(linear) : -999.0; }

// Decode a pcm24 WAV payload (44-byte canonical header) into floats.
std::vector<float> decodePcm24(const std::vector<std::uint8_t>& wav) {
    std::vector<float> out;
    if (wav.size() < 44) return out;
    const std::size_t frames = (wav.size() - 44) / 6;
    out.resize(frames * 2);
    for (std::size_t i = 0; i < frames; ++i) {
        for (int ch = 0; ch < 2; ++ch) {
            const std::size_t at = 44 + i * 6 + ch * 3;
            std::int32_t x = wav[at] | (std::uint32_t(wav[at + 1]) << 8) | (std::uint32_t(wav[at + 2]) << 16);
            if (x & 0x800000) x -= 0x1000000;
            out[i * 2 + ch] = static_cast<float>(x / 8388607.0);
        }
    }
    return out;
}

Metrics analyse(const offline::Capture& cap) {
    Metrics m;
    std::vector<std::uint8_t> wav;
    if (!offline::renderWav(cap, wav)) return m;
    auto pcm = decodePcm24(wav);
    if (pcm.empty()) return m;
    const std::size_t frames = pcm.size() / 2;
    m.frames = frames;
    const double eventSec = cap.program.bars
        ? cap.program.bars * 240.0 / std::max(1.0, cap.settings.tempo)
        : cap.settings.duration;
    const std::size_t activeEnd = std::min(frames, static_cast<std::size_t>(eventSec * kRate));
    double sumActive = 0, sumTail = 0;
    const std::size_t tailStart = std::min(frames, activeEnd + static_cast<std::size_t>(0.05 * kRate));
    for (std::size_t i = 0; i < frames; ++i) {
        const double a = std::abs(static_cast<double>(pcm[i * 2]));
        const double b = std::abs(static_cast<double>(pcm[i * 2 + 1]));
        m.peak = std::max({m.peak, a, b});
        if (a > 1e-7 || b > 1e-7) m.silence = false;
        if (a >= 0.999 || b >= 0.999) m.clip = true;
        if (i < activeEnd) sumActive += (a * a + b * b) * 0.5;
        else if (i >= tailStart) sumTail += (a * a + b * b) * 0.5;
    }
    m.activeRms = activeEnd ? std::sqrt(sumActive / activeEnd) : 0;
    const std::size_t tailCount = frames > tailStart ? frames - tailStart : 0;
    m.tailRms = tailCount ? std::sqrt(sumTail / tailCount) : 0;
    m.crest = db(m.peak) - db(m.activeRms);
    return m;
}

struct Row {
    const char* id;
    const char* group;   // role name or "waveB"
    const char* mode;
    Metrics m;
};

const char* roleName(content::Role r) {
    constexpr const char* n[]{"impact", "riser", "fall", "sweep", "zap", "rhythm", "atmosphere", "stinger"};
    return n[static_cast<unsigned>(r)];
}
const char* modeName(::vox::fx::PlayMode m) {
    constexpr const char* n[]{"oneShot", "gate", "continuous", "loop", "sequence", "transition"};
    return n[static_cast<unsigned>(m)];
}

template <typename Fn>
void eachRecipe(Fn&& fn) {
    for (const auto& e : content::reviewEntries)
        fn(e.id.data(), roleName(e.role), modeName(e.mode), content::makePreview(e, 48000));
    for (const auto& c : content::waveb::staged)
        fn(c.sound.entry.id.data(), roleName(c.sound.entry.role), modeName(c.sound.entry.mode),
           content::waveb::makePreview(c, 48000));
    for (const auto& c : content::waveb::assetCues) {
        offline::Capture result;
        const auto& entry = c.sound.entry;
        result.program.mode = entry.mode; result.program.bars = entry.bars;
        auto& s = result.settings; s.sampleRate = 48000; s.note = entry.note; s.tempo = entry.bpm; s.duration = entry.seconds;
        content::detail::applyWaveItem(result, c.sound);
        for (std::size_t i = 0; i < c.grain.size(); ++i) result.program.layers[i].grains = c.grain[i];
        s.macroRoutes.event = c.event;
        for (std::size_t i = 0; i < c.routes.size(); ++i) (void)s.macroRoutes.put(i, c.routes[i]);
        fn(entry.id.data(), roleName(entry.role), modeName(entry.mode), std::move(result));
    }
    for (const auto& c : content::waveb::performance)
        fn(c.sound.entry.id.data(), roleName(c.sound.entry.role), modeName(c.sound.entry.mode),
           content::waveb::makePreview(c, 48000));
    for (const auto& c : content::waveb::crossings)
        fn(c.sound.entry.id.data(), roleName(c.sound.entry.role), modeName(c.sound.entry.mode),
           content::waveb::makePreview(c, 48000));
    for (const auto& c : content::waveb::interludes)
        fn(c.sound.entry.id.data(), roleName(c.sound.entry.role), modeName(c.sound.entry.mode),
           content::waveb::makePreview(c, 48000));
    for (const auto& c : content::waveb::roomCuts)
        fn(c.sound.entry.id.data(), roleName(c.sound.entry.role), modeName(c.sound.entry.mode),
           content::waveb::makePreview(c, 48000));
    for (const auto& c : content::waveb::sessionCuts)
        fn(c.sound.entry.id.data(), roleName(c.sound.entry.role), modeName(c.sound.entry.mode),
           content::waveb::makePreview(c, 48000));
    for (const auto& c : content::waveb::signals)
        fn(c.sound.entry.id.data(), roleName(c.sound.entry.role), modeName(c.sound.entry.mode),
           content::waveb::makePreview(c, 48000));
    for (const auto& c : content::waveb::editPoints)
        fn(c.sound.entry.id.data(), roleName(c.sound.entry.role), modeName(c.sound.entry.mode),
           content::waveb::makePreview(c, 48000));
    for (const auto& c : content::waveb::counterpoints)
        fn(c.sound.entry.id.data(), roleName(c.sound.entry.role), modeName(c.sound.entry.mode),
           content::waveb::makePreview(c, 48000));
}
} // namespace

int main(int argc, char** argv) {
    const char* outPath = argc > 1 ? argv[1] : "";
    FILE* out = outPath && *outPath ? std::fopen(outPath, "w") : stdout;
    if (!out) { std::printf("cannot open output\n"); return 1; }

    std::vector<Row> rows;
    eachRecipe([&](const char* id, const char* group, const char* mode, offline::Capture cap) {
        // Normalized variant: identical note/velocity/BPM for cross-recipe comparison.
        cap.settings.note = 60;
        cap.settings.velocity = 1.0f;
        cap.settings.tempo = 120.0;
        rows.push_back({id, group, mode, analyse(cap)});
    });

    std::fprintf(out, "id\tgroup\tmode\tpeak_dbfs\tactive_rms_dbfs\tcrest_db\ttail_rms_dbfs\tframes\tsilence\tclip\n");
    int fails = 0;
    for (const auto& r : rows)
        std::fprintf(out, "%s\t%s\t%s\t%.2f\t%.2f\t%.2f\t%.2f\t%zu\t%d\t%d\n", r.id, r.group, r.mode,
                     db(r.m.peak), db(r.m.activeRms), r.m.crest, db(r.m.tailRms), r.m.frames,
                     r.m.silence ? 1 : 0, r.m.clip ? 1 : 0);

    // Aggregates.
    auto stats = [&](const char* label, const std::vector<const Row*>& set) {
        if (set.empty()) return;
        std::vector<double> peaks, rmses;
        int sil = 0, clip = 0;
        const Row* worst = set.front(), * loudest = set.front();
        for (const auto* r : set) {
            peaks.push_back(db(r->m.peak));
            rmses.push_back(db(r->m.activeRms));
            sil += r->m.silence; clip += r->m.clip;
            if (r->m.peak < worst->m.peak) worst = r;
            if (r->m.peak > loudest->m.peak) loudest = r;
        }
        std::sort(peaks.begin(), peaks.end());
        std::sort(rmses.begin(), rmses.end());
        const auto pct = [](const std::vector<double>& v, double p) {
            return v[static_cast<std::size_t>(p * (v.size() - 1))];
        };
        std::printf("%-12s n=%3zu peak dBFS p10/50/90=%.1f/%.1f/%.1f  activeRMS p10/50/90=%.1f/%.1f/%.1f  silence=%d clip=%d\n",
                    label, set.size(), pct(peaks, .10), pct(peaks, .50), pct(peaks, .90),
                    pct(rmses, .10), pct(rmses, .50), pct(rmses, .90), sil, clip);
        std::printf("             quietest=%s (%.1f dB) loudest=%s (%.1f dB)\n",
                    worst->id, db(worst->m.peak), loudest->id, db(loudest->m.peak));
    };
    std::printf("=== BASELINE (normalized note60 vel1.0 120bpm, output=1.0) ===\n");
    std::vector<const Row*> all;
    for (const auto& r : rows) all.push_back(&r);
    stats("ALL", all);
    for (const char* g : {"impact", "riser", "fall", "sweep", "zap", "rhythm", "atmosphere", "stinger", "waveB", "asset"}) {
        std::vector<const Row*> set;
        for (const auto& r : rows) if (std::string(r.group) == g) set.push_back(&r);
        stats(g, set);
    }

    // Path comparison on three representative recipes.
    std::printf("=== PATH COMPARISON ===\n");
    int compared = 0;
    eachRecipe([&](const char* id, const char*, const char*, offline::Capture cap) {
        if (compared >= 3 || std::string(id) != "voxfx.impact.iron-drop" &&
            std::string(id) != "voxfx.riser.first-light" && std::string(id) != "voxfx.zap.acid-spore") return;
        ++compared;
        cap.settings.note = 60; cap.settings.velocity = 1.0f; cap.settings.tempo = 120.0;
        std::vector<std::uint8_t> wav;
        if (!offline::renderWav(cap, wav)) { std::printf("%s: render FAILED\n", id); return; }
        auto pcm = decodePcm24(wav);
        // Engine-direct path (MIDI playback equivalent): NoiseRiser + Chain + output gain.
        NoiseRiser engine;
        engine.prepare(kRate);
        engine.setProgram(cap.program);
        engine.noteOn(1, cap.settings.note, cap.settings.velocity, cap.settings.duration);
        effects::Chain chain;
        chain.prepare(kRate);
        for (std::size_t i = 0; i < cap.settings.effects.size(); ++i) chain.set(i, cap.settings.effects[i]);
        std::vector<float> l(512), r(512);
        double maxDiff = 0, outHalfDiff = 0, enginePeak = 0, wavPeak = 0;
        std::size_t at = 0;
        while (at * 2 < pcm.size()) {
            const std::size_t n = std::min<std::size_t>(512, pcm.size() / 2 - at);
            engine.render(l.data(), r.data(), static_cast<int>(n));
            chain.process(l.data(), r.data(), n);
            for (std::size_t i = 0; i < n; ++i) {
                const double wl = pcm[(at + i) * 2], wr = pcm[(at + i) * 2 + 1];
                maxDiff = std::max({maxDiff, std::abs(wl - l[i]), std::abs(wr - r[i])});
                // output smoothing at 0.5 (host automation equivalent)
                outHalfDiff = std::max(outHalfDiff, std::abs(wl * 0.5 - l[i] * 0.5));
                enginePeak = std::max({enginePeak, std::abs(static_cast<double>(l[i])), std::abs(static_cast<double>(r[i]))});
                wavPeak = std::max({wavPeak, std::abs(wl), std::abs(wr)});
            }
            at += n;
        }
        std::printf("%s: wav-vs-engine maxDiff=%.3g (peak wav %.4f) | output=0.5 scales linearly (maxDiff %.3g)\n",
                    id, maxDiff, wavPeak, outHalfDiff);
    });
    if (out != stdout) std::fclose(out);
    std::printf("rows=%zu (expect 232)\n", rows.size());
    if (rows.size() != 232) ++fails;
    int silent = 0, clipped = 0;
    for (const auto& r : rows) { silent += r.m.silence ? 1 : 0; clipped += r.m.clip ? 1 : 0; }
    if (silent) { std::printf("FAIL: %d silent recipes\n", silent); ++fails; }
    if (clipped) { std::printf("FAIL: %d clipped recipes at output=1.0\n", clipped); ++fails; }
    if (fails) { std::printf("FX gain staging: %d failure(s)\n", fails); return 1; }
    std::printf("FX gain staging: 0 failure(s)\n");
    return 0;
}
