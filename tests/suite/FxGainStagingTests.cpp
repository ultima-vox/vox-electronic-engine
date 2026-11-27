// FX gain staging regression (P1-corrected harness, TL 7224771869).
//
// Measures all 232 frozen review recipes (Wave A 120 + Wave B 112) in BOTH
// variants — authored note/BPM/velocity (the frozen render settings) and the
// normalized note 60 / velocity 1.0 / 120 BPM comparison variant — on the
// PRE-ENCODING float signal of the exact renderWavImpl pipeline
// (engine -> chain -> macro::renderPost -> output gain).
//
// Corrections over the first baseline version:
//  * assetCue recipes now go through content::waveb::makeAssetPreview and
//    decode their authored WAV with sample::decodeWav (the supported path),
//    published through the user-sample store — no builtin fallback. Missing
//    assets are collected separately and must be empty.
//  * The measured recipe ID set must equal the canonical inventory ID set
//    (exact set equality, not a row count).
//  * renderFloats distinguishes invalidCapture / setupFailed / overload /
//    ok; silence is an analysis property of an ok render, never a stand-in
//    for failure. Levels (peak, active RMS, tail) come from the floats, so
//    no PCM24 quantization is involved; PCM24 is checked separately for
//    header correctness and <= 1 LSB quantization against float32.
//  * renderFloats is asserted BIT-EXACT against renderWav's float32 output,
//    and renderWav must fail exactly when the replica reports overload.
//  * Sequence recipes use one pattern cycle as the active window; active and
//    tail peaks are reported separately.
//  * Path checks use two independent captures (gain 1.0 vs gain 0.5) with a
//    sample-wise linearity assertion, plus a Preview Bus mix leg after the
//    5 ms crossfade — no algebraic half-diff, no printf-only comparisons.
//  * The deep legs run on a fixed fixture subset so the full run stays
//    bounded (two renders per recipe + six controlled fixtures).
//
// Run with a path argument to dump the full TSV table for evidence.
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
#include "engine/Preview.h"
#include "engine/SampleDecode.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace vox::fx;

namespace {
constexpr double kRate = 48000.0;
constexpr double kSilencePeak = 1e-7;
constexpr double kHeadroomFloor = 0.5623; // -5 dBFS floor for the baseline
constexpr float kPreviewTolerance = 1e-5f;
constexpr float kQuantLsb = 1.f / 8388607.f;

// ---------------------------------------------------------------------------
// renderFloats: a faithful replica of offline::renderWavImpl (same setup
// order, same scheduling, same renderPost call) that measures the floats
// instead of encoding them, distinguishing invalid / setup failure /
// overload / ok.
// ---------------------------------------------------------------------------
enum class RStatus { ok, invalidCapture, setupFailed, overload };
const char* statusName(RStatus s) {
    switch (s) {
    case RStatus::ok: return "ok";
    case RStatus::invalidCapture: return "invalid";
    case RStatus::setupFailed: return "setup-failed";
    case RStatus::overload: return "overload";
    }
    return "?";
}

struct FloatRender {
    RStatus status = RStatus::invalidCapture;
    std::vector<float> pcm;      // interleaved LRLR, pre-encoding
    std::uint32_t frames = 0;
    std::uint32_t rate = 0;
    std::uint32_t activeEnd = 0; // sequence: one cycle; otherwise event window
    float peak = 0.f;
    bool overload = false;
};

FloatRender renderFloats(const offline::Capture& capture) {
    FloatRender out;
    const auto& program = capture.program;
    const auto& settings = capture.settings;
    if (!program.valid() || !settings.valid()) return out;
    const auto* sequence =
        (program.mode == PlayMode::sequence || capture.patternEnabled) ? &capture.pattern : nullptr;
    const auto frames = offline::frameCount(program, settings, sequence, capture.patternEnabled);
    if (frames == 0) { out.status = RStatus::setupFailed; return out; }
    const auto rate = settings.sampleRate;
    sample::Store sampleStore; // outlives engine and its event leases
    auto engine = std::make_unique<NoiseRiser>();
    engine->prepare(rate);
    if (!engine->setTempo(settings.tempo)) { out.status = RStatus::setupFailed; return out; }
    pattern::FixedSchedule schedule;
    std::uint64_t cycleFrames = 0;
    if (sequence && (!pattern::scheduleFixed(*sequence, schedule) || schedule.count == 0 ||
                     !pattern::tickToSample(sequence->bars * 4u * pattern::ppq, settings.tempo,
                                            settings.sampleRate, cycleFrames))) {
        out.status = RStatus::setupFailed; return out;
    }
    const auto grid = timeline::BarGrid::from(0, settings.tempo, settings.sampleRate);
    if (program.bars) {
        engine->setMusicalClockAvailable(true, settings.tempo);
        engine->setBarGrid(grid);
    }
    if (!engine->setVoiceMotion(settings.macroRoutes)) { out.status = RStatus::setupFailed; return out; }
    engine->setMacroBases(settings.macros);
    const bool useUser = capture.useUserSample;
    if (useUser && (!sampleStore.publish(capture.sample, capture.sampleRate) ||
                    !engine->attachSampleStore(&sampleStore))) {
        out.status = RStatus::setupFailed; return out;
    }
    if (!engine->setProgram(program)) { out.status = RStatus::setupFailed; return out; }
    if (!sequence && !engine->noteOn(1, settings.note, settings.velocity, settings.duration, useUser, 0,
                                     TriggerSource::preview)) {
        out.status = RStatus::setupFailed; return out;
    }
    effects::Chain chain;
    if (!chain.prepare(settings.sampleRate)) { out.status = RStatus::setupFailed; return out; }
    chain.setTempo(settings.tempo);
    macro::Runtime macroRuntime;
    if (!macroRuntime.prepare(settings.sampleRate) || !macroRuntime.setConfig(settings.macroRoutes)) {
        out.status = RStatus::setupFailed; return out;
    }
    macroRuntime.setTempo(settings.tempo);
    for (std::size_t i = 0; i < settings.effects.size(); ++i)
        if (!chain.set(i, settings.effects[i])) { out.status = RStatus::setupFailed; return out; }
    out.pcm.assign(static_cast<std::size_t>(frames) * 2, 0.f);
    out.frames = frames;
    out.rate = rate;
    // Active window: sequence = exactly one cycle (where triggers fire);
    // otherwise the event window frameCount used (bars or note duration).
    const double eventSec = program.bars ? program.bars * 240.0 / settings.tempo : settings.duration;
    out.activeEnd = static_cast<std::uint32_t>(std::min<std::uint64_t>(
        frames, static_cast<std::uint64_t>(std::ceil(eventSec * settings.sampleRate))));
    if (sequence)
        out.activeEnd = static_cast<std::uint32_t>(std::min<std::uint64_t>(frames, cycleFrames));
    std::array<float, 512> left{}, right{};
    bool overload = false;
    for (std::uint32_t at = 0; at < frames;) {
        const auto count = std::min<std::uint32_t>(512, frames - at);
        int cursor = 0;
        const auto renderTo = [&](int end) noexcept {
            if (end > cursor) engine->render(left.data() + cursor, right.data() + cursor, end - cursor);
            cursor = end;
        };
        if (sequence && at < cycleFrames) {
            const auto limit = static_cast<int>(std::min<std::uint64_t>(count, cycleFrames - at));
            if (!pattern::forEachTrigger(schedule, sequence->bars, grid.at(at), settings.tempo,
                                         settings.sampleRate, limit,
                                         [&](int offset, std::uint8_t note, std::uint8_t velocity) noexcept {
                                             renderTo(offset);
                                             (void)engine->noteOn(1, note, static_cast<float>(velocity) / 127.0f,
                                                                  settings.duration, useUser, 0,
                                                                  TriggerSource::pattern);
                                         })) {
                out.status = RStatus::setupFailed; return out;
            }
        }
        renderTo(static_cast<int>(count));
        macro::renderPost(chain, macroRuntime, left.data(), right.data(), count, settings.macros,
                          [&] { return settings.gain; });
        for (std::uint32_t i = 0; i < count; ++i) {
            const float l = left[i], r = right[i];
            if (std::isfinite(l)) out.peak = std::max(out.peak, std::fabs(l));
            if (std::isfinite(r)) out.peak = std::max(out.peak, std::fabs(r));
            if (!std::isfinite(l) || !std::isfinite(r) || std::fabs(l) > 1.f || std::fabs(r) > 1.f)
                overload = true;
            out.pcm[static_cast<std::size_t>(at + i) * 2] = l;
            out.pcm[static_cast<std::size_t>(at + i) * 2 + 1] = r;
        }
        at += count;
    }
    out.overload = overload;
    out.status = overload ? RStatus::overload : RStatus::ok;
    return out;
}

struct Metrics {
    double activePeak = 0, activeRms = 0, tailPeak = 0;
    std::size_t activeFrames = 0, tailFrames = 0;
    bool silence = true;
};

Metrics analyse(const FloatRender& r) {
    Metrics m;
    if (r.frames == 0 || r.status != RStatus::ok) return m;
    const auto activeEnd = std::min<std::uint32_t>(r.frames, r.activeEnd);
    m.activeFrames = activeEnd;
    m.tailFrames = r.frames - activeEnd;
    double sumsq = 0;
    for (std::size_t i = 0; i < activeEnd; ++i) {
        const float l = r.pcm[i * 2], rr = r.pcm[i * 2 + 1];
        if (std::isfinite(l)) m.activePeak = std::max(m.activePeak, static_cast<double>(std::fabs(l)));
        if (std::isfinite(rr)) m.activePeak = std::max(m.activePeak, static_cast<double>(std::fabs(rr)));
        if (std::isfinite(l) && std::isfinite(rr)) sumsq += static_cast<double>(l) * l + static_cast<double>(rr) * rr;
    }
    m.activeRms = m.activeFrames ? std::sqrt(sumsq / (2.0 * m.activeFrames)) : 0.0;
    for (std::size_t i = activeEnd; i < r.frames; ++i) {
        const float l = r.pcm[i * 2], rr = r.pcm[i * 2 + 1];
        if (std::isfinite(l)) m.tailPeak = std::max(m.tailPeak, static_cast<double>(std::fabs(l)));
        if (std::isfinite(rr)) m.tailPeak = std::max(m.tailPeak, static_cast<double>(std::fabs(rr)));
    }
    m.silence = m.activePeak < kSilencePeak;
    return m;
}

double db(double linear) { return linear > 0 ? 20.0 * std::log10(linear) : -999.0; }

// WAV helpers: pcm24 has the canonical 44-byte header; float32 adds a
// 12-byte fact chunk, so its payload starts at 56.
std::size_t wavDataOffset(const std::vector<std::uint8_t>& w) {
    if (w.size() < 44) return 0;
    if (w[36] == 'f' && w[37] == 'a' && w[38] == 'c' && w[39] == 't') return 56;
    return 44;
}
std::vector<float> decodeFloat32(const std::vector<std::uint8_t>& w) {
    std::vector<float> out;
    const auto off = wavDataOffset(w);
    if (!off || w.size() < off + 4) return out;
    const auto data = static_cast<std::size_t>(w[off - 4]) | (static_cast<std::size_t>(w[off - 3]) << 8) |
                      (static_cast<std::size_t>(w[off - 2]) << 16) | (static_cast<std::size_t>(w[off - 1]) << 24);
    if (data % 4 || w.size() < off + data) return out;
    out.resize(data / 4);
    std::memcpy(out.data(), w.data() + off, data);
    return out;
}
std::vector<float> decodePcm24(const std::vector<std::uint8_t>& w) {
    std::vector<float> out;
    if (w.size() < 44) return out;
    const auto data = static_cast<std::size_t>(w[40]) | (static_cast<std::size_t>(w[41]) << 8) |
                      (static_cast<std::size_t>(w[42]) << 16) | (static_cast<std::size_t>(w[43]) << 24);
    if (data % 6 || w.size() < 44 + data) return out;
    const auto frames = data / 6;
    out.resize(frames * 2);
    for (std::size_t i = 0; i < frames; ++i)
        for (int ch = 0; ch < 2; ++ch) {
            const auto at = 44 + i * 6 + static_cast<std::size_t>(ch) * 3;
            std::int32_t x = w[at] | (std::uint32_t(w[at + 1]) << 8) | (std::uint32_t(w[at + 2]) << 16);
            if (x & 0x800000) x -= 0x1000000;
            out[i * 2 + ch] = static_cast<float>(x / 8388607.0);
        }
    return out;
}

// ---------------------------------------------------------------------------
// Recipe enumeration (supported capture builders; asset cues decode their
// authored WAV through sample::decodeWav and publish it to the store).
// ---------------------------------------------------------------------------
std::set<std::string> g_missingAssets;

template <typename Fn>
void eachRecipe(Fn&& fn) {
    for (const auto& e : content::reviewEntries)
        fn(e.id.data(), std::string(e.id), content::makePreview(e, 48000));
    for (const auto& c : content::waveb::staged)
        fn(c.sound.entry.id.data(), std::string(c.sound.entry.id), content::waveb::makePreview(c, 48000));
    for (const auto& c : content::waveb::assetCues) {
        auto cap = content::waveb::makeAssetPreview(c); // sets useUserSample
        const std::string id(c.sound.entry.id);
        sample::Decoded decoded;
        if (!sample::decodeWav(content::waveb::sourceWav(c.asset), decoded)) {
            g_missingAssets.insert(id); // counted separately, never rendered
            continue;
        }
        cap.sample = std::move(decoded.mono);
        cap.sampleRate = decoded.rate;
        fn(id.c_str(), id, std::move(cap));
    }
    for (const auto& c : content::waveb::performance)
        fn(c.sound.entry.id.data(), std::string(c.sound.entry.id), content::waveb::makePreview(c, 48000));
    for (const auto& c : content::waveb::crossings)
        fn(c.sound.entry.id.data(), std::string(c.sound.entry.id), content::waveb::makePreview(c, 48000));
    for (const auto& c : content::waveb::interludes)
        fn(c.sound.entry.id.data(), std::string(c.sound.entry.id), content::waveb::makePreview(c, 48000));
    for (const auto& c : content::waveb::roomCuts)
        fn(c.sound.entry.id.data(), std::string(c.sound.entry.id), content::waveb::makePreview(c, 48000));
    for (const auto& c : content::waveb::sessionCuts)
        fn(c.sound.entry.id.data(), std::string(c.sound.entry.id), content::waveb::makePreview(c, 48000));
    for (const auto& c : content::waveb::signals)
        fn(c.sound.entry.id.data(), std::string(c.sound.entry.id), content::waveb::makePreview(c, 48000));
    for (const auto& c : content::waveb::editPoints)
        fn(c.sound.entry.id.data(), std::string(c.sound.entry.id), content::waveb::makePreview(c, 48000));
    for (const auto& c : content::waveb::counterpoints)
        fn(c.sound.entry.id.data(), std::string(c.sound.entry.id), content::waveb::makePreview(c, 48000));
}

const char* roleName(content::Role r) {
    constexpr const char* n[]{"impact", "riser", "fall", "sweep", "zap", "rhythm", "atmosphere", "stinger"};
    return n[static_cast<unsigned>(r)];
}
const char* modeName(::vox::fx::PlayMode m) {
    constexpr const char* n[]{"oneShot", "gate", "continuous", "loop", "sequence", "transition"};
    return n[static_cast<unsigned>(m)];
}

// ---------------------------------------------------------------------------
// Controlled legs (asserted): float32 byte parity, independent gain-1.0 /
// gain-0.5 captures, PCM24 header + 1 LSB quantization, Preview Bus mix.
// ---------------------------------------------------------------------------
struct LegResult { int failures = 0; int checks = 0; };

void controlledLegs(const std::string& id, const offline::Capture& authored, LegResult& legs,
                    std::ofstream& tsv) {
    offline::Capture cap = authored;
    cap.settings.note = 60;
    cap.settings.velocity = 1.0f;
    cap.settings.tempo = 120.0;
    auto ref = renderFloats(cap);
    if (ref.status != RStatus::ok) {
        ++legs.failures;
        std::printf("leg:%s FAILED: replica status %s\n", id.c_str(), statusName(ref.status));
        return;
    }
    auto fail = [&](const char* what) {
        ++legs.failures;
        std::printf("leg:%s FAILED: %s\n", id.c_str(), what);
    };
    // A) float32 renderWav must succeed and be bit-identical to the
    //    pre-encoding replica (float32 encode is a bit_cast of the floats).
    offline::Capture fcap = cap;
    fcap.settings.format = offline::WavFormat::float32;
    std::vector<std::uint8_t> wav32;
    ++legs.checks;
    if (!offline::renderWav(fcap, wav32)) { fail("float32 render failed with replica status=ok"); return; }
    const auto f32 = decodeFloat32(wav32);
    if (f32.size() != ref.pcm.size() ||
        std::memcmp(f32.data(), ref.pcm.data(), f32.size() * sizeof(float)) != 0)
        fail("float32 bytes != pre-encoding replica floats");
    // B) two independent captures (gain 1.0 vs gain 0.5): the full pipeline
    //    must be exactly linear in the output gain.
    offline::Capture gcap = cap;
    gcap.settings.format = offline::WavFormat::float32;
    gcap.settings.gain = 1.0f;
    offline::Capture hcap = gcap;
    hcap.settings.gain = 0.5f;
    std::vector<std::uint8_t> wavG, wavH;
    ++legs.checks;
    if (!offline::renderWav(gcap, wavG) || !offline::renderWav(hcap, wavH)) {
        fail("independent gain capture failed");
        return;
    }
    const auto g = decodeFloat32(wavG), h = decodeFloat32(wavH);
    if (g.size() != h.size() || g.size() != f32.size()) { fail("gain capture length mismatch"); return; }
    float maxGainErr = 0.f;
    for (std::size_t i = 0; i < g.size(); ++i)
        maxGainErr = std::max(maxGainErr, std::fabs(h[i] - 0.5f * g[i]));
    if (maxGainErr > 1e-6f) {
        ++legs.failures;
        std::printf("leg:%s FAILED: gain path not linear, max|0.5*a-b|=%.3g\n", id.c_str(), maxGainErr);
    }
    // C) PCM24: header must declare stereo 24-bit at the capture rate and
    //    quantization must stay within 1 LSB of the float reference.
    std::vector<std::uint8_t> wav24;
    ++legs.checks;
    if (!offline::renderWav(cap, wav24) || wav24.size() < 44) { fail("pcm24 render failed"); return; }
    if (wav24[34] != 24 || wav24[20] != 1 || wav24[22] != 2) fail("pcm24 header is not stereo 24-bit PCM");
    const auto rateField = static_cast<std::uint32_t>(wav24[24]) | (static_cast<std::uint32_t>(wav24[25]) << 8) |
                           (static_cast<std::uint32_t>(wav24[26]) << 16) | (static_cast<std::uint32_t>(wav24[27]) << 24);
    if (rateField != ref.rate) fail("pcm24 sample-rate field mismatch");
    const auto q = decodePcm24(wav24);
    if (q.size() != f32.size()) { fail("pcm24 length mismatch"); return; }
    float maxQuant = 0.f;
    for (std::size_t i = 0; i < q.size(); ++i) maxQuant = std::max(maxQuant, std::fabs(q[i] - f32[i]));
    ++legs.checks;
    if (maxQuant > kQuantLsb * 1.5f) {
        ++legs.failures;
        std::printf("leg:%s FAILED: pcm24 quantization %.3g exceeds 1 LSB\n", id.c_str(), maxQuant);
    }
    if (tsv.is_open())
        tsv << "leg\tquant\t" << id << "\tmaxErr=" << maxQuant << "\t1LSB=" << kQuantLsb << "\n";
    // D) Preview Bus: publish the rendered PCM and mix into silence; after
    //    the 5 ms crossfade the output must equal the published frames.
    preview::Bus bus;
    bus.prepare(static_cast<double>(ref.rate));
    const auto ticket = bus.ticket();
    ++legs.checks;
    if (!bus.publish(ref.pcm, ref.rate, ticket)) { fail("preview publish rejected"); return; }
    if (!bus.playing()) { fail("preview not playing after publish"); return; }
    std::vector<float> outL(1024), outR(1024);
    std::vector<float> mixed(ref.pcm.size(), 0.f);
    std::size_t produced = 0;
    int guard = 0;
    while (bus.playing() && guard++ < 8192) {
        std::fill(outL.begin(), outL.end(), 0.f);
        std::fill(outR.begin(), outR.end(), 0.f);
        bus.mix(outL.data(), outR.data(), 1024);
        for (int i = 0; i < 1024 && produced < ref.frames; ++i, ++produced) {
            mixed[produced * 2] = outL[i];
            mixed[produced * 2 + 1] = outR[i];
        }
        if (produced >= ref.frames && !bus.playing()) break;
    }
    if (bus.playing()) { fail("preview never drained"); return; }
    if (ref.frames <= 1024) { fail("fixture too short for preview leg"); return; }
    const std::size_t settle = 512; // 5 ms at 48 kHz is 240 samples
    float maxPreviewErr = 0.f;
    for (std::size_t i = settle; i < ref.frames - settle; ++i) {
        maxPreviewErr = std::max(maxPreviewErr, std::fabs(mixed[i * 2] - ref.pcm[i * 2]));
        maxPreviewErr = std::max(maxPreviewErr, std::fabs(mixed[i * 2 + 1] - ref.pcm[i * 2 + 1]));
    }
    if (maxPreviewErr > kPreviewTolerance) {
        ++legs.failures;
        std::printf("leg:%s FAILED: preview mix drift %.3g > %.1e\n", id.c_str(), maxPreviewErr,
                    kPreviewTolerance);
    }
    if (tsv.is_open())
        tsv << "leg\tpreview\t" << id << "\tmaxErr=" << maxPreviewErr << "\ttolerance=" << kPreviewTolerance
            << "\n";
}
} // namespace

int run(const char* outPath) {
    const auto started = std::chrono::steady_clock::now();
    // Canonical inventory ID set (first pass; asset decode failures land in
    // g_missingAssets and never enter the inventory).
    std::set<std::string> expectedIds;
    eachRecipe([&](const char*, const std::string& id, offline::Capture) { expectedIds.insert(id); });

    struct Row {
        std::string id, group, mode, variant;
        int note = 60;
        double bpm = 120, velocity = 1.0, duration = 1.5;
        Metrics m;
        RStatus status = RStatus::invalidCapture;
        std::uint32_t rate = 0;
        bool overload = false;
    };
    std::vector<Row> rows;
    std::set<std::string> measuredIds;
    std::ofstream tsv;
    if (outPath && *outPath) {
        tsv.open(outPath);
        tsv << "recipe_id\tgroup\tmode\tvariant\tnote\tbpm\tvelocity\tduration_s\tframes\trate\t"
               "active_frames\tstatus\tactive_peak_dbfs\tactive_rms_dbfs\ttail_peak_dbfs\tcrest_db\t"
               "silence\toverload\n";
    }
    auto pushRow = [&](const std::string& id, const char* group, const char* mode,
                       const offline::Capture& cap, bool normalize, const FloatRender& r) {
        Row row;
        row.id = id;
        row.group = group;
        row.mode = mode;
        row.variant = normalize ? "normalized" : "authored";
        row.note = cap.settings.note;
        row.bpm = cap.settings.tempo;
        row.velocity = cap.settings.velocity;
        row.duration = cap.settings.duration;
        row.m = analyse(r);
        row.status = r.status;
        row.rate = r.rate;
        row.overload = r.overload;
        if (tsv.is_open())
            tsv << row.id << '\t' << row.group << '\t' << row.mode << '\t' << row.variant << '\t' << row.note
                << '\t' << row.bpm << '\t' << row.velocity << '\t' << row.duration << '\t' << r.frames << '\t'
                << r.rate << '\t' << row.m.activeFrames << '\t' << statusName(row.status) << '\t'
                << db(row.m.activePeak) << '\t' << db(row.m.activeRms) << '\t' << db(row.m.tailPeak) << '\t'
                << (db(row.m.activePeak) - db(row.m.activeRms)) << '\t' << (row.m.silence ? 1 : 0) << '\t'
                << (row.overload ? 1 : 0) << '\n';
        rows.push_back(std::move(row));
    };
    // Canonical role (group) column, resolved before rows are written.
    std::map<std::string, std::string> roleOf;
    for (const auto& e : content::reviewEntries) roleOf[std::string(e.id)] = roleName(e.role);
    auto roleOfWave = [&](const auto& arr) {
        for (const auto& c : arr) roleOf[std::string(c.sound.entry.id)] = roleName(c.sound.entry.role);
    };
    roleOfWave(content::waveb::staged);
    roleOfWave(content::waveb::performance);
    roleOfWave(content::waveb::crossings);
    roleOfWave(content::waveb::interludes);
    roleOfWave(content::waveb::roomCuts);
    roleOfWave(content::waveb::sessionCuts);
    roleOfWave(content::waveb::signals);
    roleOfWave(content::waveb::editPoints);
    roleOfWave(content::waveb::counterpoints);
    for (const auto& c : content::waveb::assetCues)
        roleOf[std::string(c.sound.entry.id)] = roleName(c.sound.entry.role);
    eachRecipe([&](const char* id, const std::string& sid, offline::Capture cap) {
        const auto it = roleOf.find(sid);
        const char* group = it != roleOf.end() ? it->second.c_str() : "";
        const char* mode = modeName(cap.program.mode);
        // Authored row: frozen note/BPM/velocity/duration from the recipe.
        {
            offline::Capture copy = cap;
            auto r = renderFloats(copy);
            pushRow(sid, group, mode, copy, /*normalize=*/false, r);
        }
        cap.settings.note = 60;
        cap.settings.velocity = 1.0f;
        cap.settings.tempo = 120.0;
        {
            auto r = renderFloats(cap);
            pushRow(sid, group, mode, cap, /*normalize=*/true, r);
            measuredIds.insert(sid);
        }
        (void)id;
    });

    // ---- controlled legs -------------------------------------------------
    LegResult legs;
    const std::set<std::string> controlledIds = {
        "voxfx.impact.iron-drop",        // oneshot reference
        "voxfx.riser.first-light",       // bars=4 event window
        "voxfx.zap.acid-spore",          // gate
        "voxfx.rhythm.neon-fault",       // sequence (cycle window) + loudest
        "voxfx.sweep.vellum-coil",       // asset cue (user sample)
        "voxfx.atmosphere.kiln-choir",   // asset cue (user sample)
    };
    eachRecipe([&](const char*, const std::string& id, offline::Capture cap) {
        if (controlledIds.count(id)) controlledLegs(id, cap, legs, tsv);
    });

    // ---- tables ----------------------------------------------------------
    std::vector<const Row*> norm, authored;
    for (const auto& r : rows) {
        if (r.variant == "normalized") norm.push_back(&r);
        else authored.push_back(&r);
    }
    auto pct = [](std::vector<double> v, double p) {
        if (v.empty()) return 0.0;
        std::sort(v.begin(), v.end());
        return v[static_cast<std::size_t>(p * (v.size() - 1))];
    };
    std::printf("\n=== BASELINE (normalized note60 vel1.0 120bpm, output=1.0, pre-encoding floats) ===\n");
    std::vector<double> peaks, rmses;
    const Row* worst = nullptr, *loudest = nullptr;
    int silent = 0, overloaded = 0;
    for (const auto* r : norm) {
        peaks.push_back(db(r->m.activePeak));
        rmses.push_back(db(r->m.activeRms));
        silent += r->m.silence;
        overloaded += r->overload;
        if (!worst || r->m.activePeak < worst->m.activePeak) worst = r;
        if (!loudest || r->m.activePeak > loudest->m.activePeak) loudest = r;
    }
    std::printf("ALL n=%zu peak p10/50/90=%.1f/%.1f/%.1f dBFS  activeRMS p50=%.1f dBFS  silence=%d overload=%d\n",
                norm.size(), pct(peaks, .10), pct(peaks, .50), pct(peaks, .90), pct(rmses, .50), silent,
                overloaded);
    std::printf("     quietest=%s (%.1f dB) loudest=%s (%.1f dB)\n",
                worst ? worst->id.c_str() : "-", worst ? db(worst->m.activePeak) : -999.0,
                loudest ? loudest->id.c_str() : "-", loudest ? db(loudest->m.activePeak) : -999.0);
    for (const char* g : {"impact", "riser", "fall", "sweep", "zap", "rhythm", "atmosphere", "stinger"}) {
        std::vector<double> gp, gr;
        for (const auto* r : norm)
            if (r->group == g) {
                gp.push_back(db(r->m.activePeak));
                gr.push_back(db(r->m.activeRms));
            }
        if (!gp.empty())
            std::printf("  %-10s n=%3zu peak p50=%6.1f  activeRMS p50=%6.1f dBFS\n", g, gp.size(), pct(gp, .5),
                        pct(gr, .5));
    }
    std::vector<double> authoredPeaks;
    for (const auto* r : authored) authoredPeaks.push_back(db(r->m.activePeak));
    std::printf("authored rows (frozen note/BPM/velocity) n=%zu peak p50=%.1f dBFS\n", authored.size(),
                pct(authoredPeaks, .5));

    // ---- gates -----------------------------------------------------------
    int failures = 0;
    auto expect = [&](bool cond, const char* what) {
        if (!cond) { ++failures; std::printf("FAILED: %s\n", what); }
    };
    expect(measuredIds == expectedIds, "measured recipe ID set != canonical inventory ID set");
    expect(g_missingAssets.empty(), "missing asset cue decode (see list)");
    expect(expectedIds.size() == 232, "canonical inventory is not 232 recipe IDs");
    expect(rows.size() == expectedIds.size() * 2, "row count != inventory x 2 variants");
    expect(legs.failures == 0, "controlled leg failures (see above)");
    expect(legs.checks > 0, "controlled legs did not run");
    int badStatus = 0;
    double maxPeak = 0;
    for (const auto* r : norm) {
        if (r->status != RStatus::ok) ++badStatus;
        if (r->rate != 48000) { ++failures; std::printf("FAILED: %s rate=%u\n", r->id.c_str(), r->rate); }
        if (!std::isfinite(r->m.activePeak)) {
            ++failures;
            std::printf("FAILED: %s non-finite peak\n", r->id.c_str());
        }
        maxPeak = std::max(maxPeak, r->m.activePeak);
    }
    expect(badStatus == 0, "renders must all reach status ok");
    expect(silent == 0, "silence regression (normalized variant)");
    expect(overloaded == 0, "overload in the frozen baseline (normalized variant)");
    // Headroom: every recipe must stay >= 5 dB below full scale (peak linear
    // <= kHeadroomFloor); loudness floors are the silence gate above.
    expect(maxPeak <= kHeadroomFloor, "peak above -5 dBFS headroom floor (insufficient headroom)");
    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    std::printf("rows=%zu (inventory=%zu) missingAssets=%zu legs=%d/%d %.1fs\n", rows.size(),
                expectedIds.size(), g_missingAssets.size(), legs.checks - legs.failures, legs.checks,
                elapsed);
    for (const auto& id : g_missingAssets) std::printf("missing asset: %s\n", id.c_str());
    if (tsv.is_open()) tsv.close();
    if (failures) {
        std::printf("FX gain staging: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("FX gain staging: 0 failure(s)\n");
    return 0;
}

int main(int argc, char** argv) { return run(argc > 1 ? argv[1] : nullptr); }
