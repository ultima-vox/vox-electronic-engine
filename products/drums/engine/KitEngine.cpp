#include "KitEngine.h"

// Every numeric expression below that also exists in the legacy module
// (VoxFullOnDrums.c @ f9c8bc6) is kept in the same form and order: the kit
// parity test requires bit-identical output. Comments name the legacy
// function a block comes from.

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace vox::drums::kit {
namespace {

constexpr double pi = 3.14159265358979323846;
constexpr double silence = 1e-4;   // -80 dB: a voice ends below this
constexpr std::uint32_t maxChunk = 8192;
constexpr int maxSequencerHits = 4096;

// 808-style metallic oscillator ratios (Hz at tune 0).
constexpr double metalHz[6] = { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };

// Per-sample multiplier that falls 60 dB in `seconds` (decays are T60).
double t60 (double seconds, double sampleRate)
{
    return std::exp (-6.907755278982137 / (seconds * sampleRate));
}

double noise (std::uint32_t* state)   // xorshift32 -> [-1, 1)
{
    std::uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return static_cast<double> (x) * (2.0 / 4294967296.0) - 1.0;
}

// One-pole high-pass, coefficient from cutoff.
double highPass (double x, double* prev, double* state, double cutoff, double sr)
{
    const double a = std::exp (-2.0 * pi * cutoff / sr);
    *state = a * (*state + x - *prev);
    *prev = x;
    return *state;
}

// TPT state-variable band-pass.
double bandPass (double x, double* ic1, double* ic2, double fc, double k, double sr)
{
    const double g = std::tan (pi * std::fmin (fc, 0.45 * sr) / sr);
    const double a1 = 1.0 / (1.0 + g * (g + k));
    const double v1 = a1 * *ic1 + g * a1 * (x - *ic2);
    const double v2 = *ic2 + g * v1;
    *ic1 = 2.0 * v1 - *ic1;
    *ic2 = 2.0 * v2 - *ic2;
    return v1;
}

struct Player {
    bool on = false;
    std::uint64_t id = 0;
    const float* const* data = nullptr;
    std::uint32_t channels = 0;
    double pos = 0.0, step = 0.0, begin = 0.0, end = 0.0, gain = 0.0;
    bool loop = false, reverse = false;
};

struct Voice {
    bool active = false;
    int pad = 0;
    Model model = Model::kick;
    std::int64_t n = 0;   // samples since trigger
    double gainL = 0.0, gainR = 0.0;
    double sr = 0.0, f0 = 0.0, envMul = 0.0, env = 0.0, env2Mul = 0.0, env2 = 0.0, character = 0.0;
    double kickPitch = 0.0, kickPitchDecay = 0.0, kickDrive = 0.0, kickDriveNorm = 0.0, kickSub = 0.0;
    double phase[6] {};
    std::uint32_t noise = 0;
    double lp = 0.0, hp1 = 0.0, hp2 = 0.0, hpPrev1 = 0.0, hpPrev2 = 0.0, bp1 = 0.0, bp2 = 0.0;
    int fadeTotal = 0, fadeLeft = 0;
    Filter filter = Filter::off;
    double fg = 0.0, fk = 0.0, fs1[2][2] {}, fs2[2][2] {};
    double attack = 0.0, sustain = 0.0, attackEnv = 0.0, attackMul = 0.0, sustainEnv = 0.0, sustainMul = 0.0;
    int bus = 0;
    double send1 = 0.0, send2 = 0.0;
    bool synthOn = false;
    Player players[samplesPerPad] {};
    // Perc model: carrier/modulator phases and FM index envelope.
    double fmIndex = 0.0, fmMul = 0.0;
};

bool sampleOn (const Voice& v)
{
    return v.players[0].on || v.players[1].on;
}

// renderSample (legacy): one frame of a player (linear interpolation inside
// the region, forward or reverse, one shot or loop).
void renderPlayer (Player& p, double* l, double* r)
{
    const double pos = p.pos;
    const double base = std::floor (pos);
    const double frac = pos - base;
    const auto i0 = static_cast<std::uint64_t> (base);
    const std::uint64_t i1 = base + 1.0 < p.end ? i0 + 1u : (p.loop ? static_cast<std::uint64_t> (p.begin) : i0);
    const float* c0 = p.data[0];
    const float* c1 = p.data[p.channels > 1 ? 1 : 0];
    *l = (c0[i0] + (c0[i1] - c0[i0]) * frac) * p.gain;
    *r = (c1[i0] + (c1[i1] - c1[i0]) * frac) * p.gain;
    const double length = p.end - p.begin;
    if (p.reverse)
    {
        p.pos -= p.step;
        if (p.pos < p.begin)
        {
            if (p.loop)
                p.pos += length * std::ceil ((p.begin - p.pos) / length);
            else
                p.on = false;
        }
    }
    else
    {
        p.pos += p.step;
        if (p.pos >= p.end)
        {
            if (p.loop)
                p.pos -= length * std::ceil ((p.pos - p.end + 1e-12) / length);
            else
                p.on = false;
        }
    }
}

double metal (Voice& v, double scale)
{
    double sum = 0.0;
    for (int i = 0; i < 6; ++i)
    {
        v.phase[i] += metalHz[i] * v.f0 * scale / v.sr;
        v.phase[i] -= std::floor (v.phase[i]);
        sum += v.phase[i] < 0.5 ? 1.0 : -1.0;
    }
    return sum / 6.0;
}

// renderVoice (legacy): one sample of a voice's synthesis.
double renderVoice (Voice& v)
{
    const double sr = v.sr;
    const double t = static_cast<double> (v.n) / sr;
    double x = 0.0, level = v.env;
    switch (v.model)
    {
        case Model::kick:
        {
            const double attack = std::fmin (1.0, t / 0.0025);
            const double u = std::fmin (1.0, t / v.kickPitchDecay);
            const double sweep = v.kickPitch * (1.0 - u) * (1.0 - u);
            const double inc = 2.0 * pi * v.f0 * std::pow (2.0, sweep / 12.0) / sr;
            v.phase[0] += inc;
            if (v.phase[0] >= 2.0 * pi)
                v.phase[0] -= 2.0 * pi;
            v.phase[1] += 0.5 * inc;
            if (v.phase[1] >= 2.0 * pi)
                v.phase[1] -= 2.0 * pi;
            const double n = noise (&v.noise);
            v.lp += (n - v.lp) * 0.3;
            const double click = t < 0.004 ? (1.0 - t / 0.004) * (0.6 * n + 0.4 * v.lp) : 0.0;
            x = std::sin (v.phase[0]) * v.env + std::sin (v.phase[1]) * v.kickSub * v.env2 + click * v.character;
            x = std::tanh (x * attack * v.kickDrive) / v.kickDriveNorm;
            level = std::fmax (v.env, v.env2 * v.kickSub);
            v.env2 *= v.env2Mul;
            break;
        }
        case Model::snare:
        {
            const double drop = 1.0 + 0.3 * std::exp (-t / 0.01);
            v.phase[0] += v.f0 * drop / sr;
            v.phase[0] -= std::floor (v.phase[0]);
            v.phase[1] += 1.78 * v.f0 * drop / sr;
            v.phase[1] -= std::floor (v.phase[1]);
            const double tone = 0.6 * std::sin (2.0 * pi * v.phase[0]) + 0.4 * std::sin (2.0 * pi * v.phase[1]);
            double n = highPass (noise (&v.noise), &v.hpPrev1, &v.hp1, 900.0, sr);
            v.lp += (n - v.lp) * 0.7;
            n = v.lp;
            x = tone * v.env2 * (1.0 - 0.7 * v.character) + n * v.env * 1.3 * v.character;
            level = std::fmax (v.env, v.env2);
            v.env2 *= v.env2Mul;
            break;
        }
        case Model::clap:
        {
            double burst = 0.0;
            for (int i = 0; i < 3; ++i)
            {
                const double start = 0.010 * i;
                if (t >= start)
                    burst = std::fmax (burst, std::exp (-(t - start) / 0.008));
            }
            const double env = t < 0.03 ? burst : v.env;
            const double n = bandPass (noise (&v.noise), &v.bp1, &v.bp2, v.f0, 0.7, sr);
            x = 2.2 * n * env;
            level = t < 0.03 ? 1.0 : v.env;
            break;
        }
        case Model::hat:
        case Model::cymbal:
        {
            const double m = metal (v, 1.0);
            const double mix = (1.0 - 0.5 * v.character) * m + 0.5 * v.character * noise (&v.noise);
            const double cut = (v.model == Model::cymbal ? 3500.0 : 5500.0) + 3000.0 * v.character;
            double y = highPass (mix, &v.hpPrev1, &v.hp1, cut, sr);
            y = highPass (y, &v.hpPrev2, &v.hp2, cut, sr);
            x = 5.0 * y * v.env;
            break;
        }
        case Model::tom:
        {
            const double f = v.f0 * (1.0 + 1.5 * v.character * std::exp (-t / 0.04));
            v.phase[0] += f / sr;
            v.phase[0] -= std::floor (v.phase[0]);
            const double click = t < 0.002 ? (1.0 - t / 0.002) * noise (&v.noise) * 0.3 : 0.0;
            x = std::sin (2.0 * pi * v.phase[0]) * v.env + click;
            break;
        }
        case Model::rim:
        {
            v.phase[0] += v.f0 / sr;
            v.phase[0] -= std::floor (v.phase[0]);
            v.phase[1] += 3.3 * v.f0 / sr;
            v.phase[1] -= std::floor (v.phase[1]);
            const double tone = 0.5 * std::sin (2.0 * pi * v.phase[0]) + 0.5 * std::sin (2.0 * pi * v.phase[1]);
            const double click = highPass (noise (&v.noise), &v.hpPrev1, &v.hp1, 2000.0, sr) * v.env2;
            x = (tone * v.character + click * (1.0 - v.character) * 1.5) * v.env;
            v.env2 *= v.env2Mul;
            break;
        }
        case Model::perc:
        {
            // New in DRM-03 (no legacy counterpart): a bounded two-operator FM
            // blip plus a band-passed noise burst. Character moves from the
            // pure tone (0) to a bright, noisy zap (1). The FM index decays
            // with its own envelope and is capped, so the spectrum stays
            // bounded at every tune.
            v.phase[1] += 1.41 * v.f0 / sr;
            v.phase[1] -= std::floor (v.phase[1]);
            const double index = v.fmIndex * (0.5 + 3.5 * v.character);
            v.phase[0] += v.f0 / sr;
            v.phase[0] -= std::floor (v.phase[0]);
            const double tone = std::sin (2.0 * pi * v.phase[0] + index * std::sin (2.0 * pi * v.phase[1]));
            const double burst = t < 0.015 ? (1.0 - t / 0.015) : 0.0;
            const double n = bandPass (noise (&v.noise), &v.bp1, &v.bp2, std::fmin (2.5 * v.f0, 0.45 * sr), 1.2, sr);
            x = (tone * (1.0 - 0.4 * v.character) + n * burst * 1.5 * v.character) * v.env;
            v.fmIndex *= v.fmMul;
            break;
        }
    }
    v.env *= v.envMul;
    v.n += 1;
    if (level < silence && t > 0.005)
        v.synthOn = false;
    return x;
}

// transientGain (legacy).
double transientGain (Voice& v)
{
    if (v.attack == 0.0 && v.sustain == 0.0)
        return 1.0;
    const double gain = 1.0 + v.attack * v.attackEnv + v.sustain * (1.0 - v.sustainEnv);
    v.attackEnv *= v.attackMul;
    v.sustainEnv *= v.sustainMul;
    return gain > 0.0 ? gain : 0.0;
}

// filterChannel (legacy).
double filterChannel (Voice& v, int ch, double x)
{
    if (v.filter == Filter::off)
        return x;
    const int stages = v.filter == Filter::lp24 || v.filter == Filter::hp24 ? 2 : 1;
    const double g = v.fg, k = v.fk;
    const double a1 = 1.0 / (1.0 + g * (g + k));
    for (int st = 0; st < stages; ++st)
    {
        const double v1 = a1 * v.fs1[ch][st] + g * a1 * (x - v.fs2[ch][st]);
        const double v2 = v.fs2[ch][st] + g * v1;
        v.fs1[ch][st] = 2.0 * v1 - v.fs1[ch][st];
        v.fs2[ch][st] = 2.0 * v2 - v.fs2[ch][st];
        switch (v.filter)
        {
            case Filter::lp12:
            case Filter::lp24: x = v2; break;
            case Filter::hp12:
            case Filter::hp24: x = x - k * v1 - v2; break;
            default: x = v1; break;
        }
    }
    return x;
}

void startFade (Voice& v, double sampleRate, double seconds)
{
    if (! v.active)
        return;
    const int total = static_cast<int> (seconds * sampleRate) + 1;
    if (v.fadeTotal > 0 && v.fadeLeft <= total)
        return;   // already ending sooner
    v.fadeTotal = total;
    v.fadeLeft = total;
}

// Every setting into its documented range (review 6737829467). Values in
// range are returned unchanged, bit for bit, so a valid kit renders exactly
// as before; NaN takes the fallback; enums outside their set take the first
// value; an output bus outside 0..8 plays on Main.
float clampOr (float v, float lo, float hi, float fallback) noexcept
{
    if (! std::isfinite (v))
        return fallback;
    return v < lo ? lo : (v > hi ? hi : v);
}

template <typename E>
E enumOr (E v, int count) noexcept
{
    return static_cast<int> (v) >= 0 && static_cast<int> (v) < count ? v : E {};
}

void sanitize (KitSettings& k) noexcept
{
    k.volume = clampOr (k.volume, 0.0f, 1.0f, 0.6f);
    k.velocitySensitivity = clampOr (k.velocitySensitivity, 0.0f, 1.0f, 0.8f);
    k.sequencerMode = k.sequencerMode <= 2 ? k.sequencerMode : std::uint8_t { 0 };
    k.swing = clampOr (k.swing, 0.0f, 0.5f, 0.0f);
    k.accent = clampOr (k.accent, 0.0f, 1.0f, 0.5f);
    for (auto& p : k.pads)
    {
        p.model = enumOr (p.model, modelCount);
        p.note &= 0x7f;
        p.polyphony = static_cast<std::uint8_t> (std::clamp<int> (p.polyphony, 1, maxVoicesPerPad));
        p.level = clampOr (p.level, 0.0f, 1.0f, 0.0f);
        p.tune = clampOr (p.tune, -24.0f, 24.0f, 0.0f);
        p.fine = clampOr (p.fine, -100.0f, 100.0f, 0.0f);
        p.decay = clampOr (p.decay, 0.005f, 10.0f, 0.3f);
        p.pan = clampOr (p.pan, -1.0f, 1.0f, 0.0f);
        p.character = clampOr (p.character, 0.0f, 1.0f, 0.5f);
        p.filter = enumOr (p.filter, 6);
        p.cutoff = clampOr (p.cutoff, 20.0f, 20000.0f, 20000.0f);
        p.resonance = clampOr (p.resonance, 0.0f, 1.0f, 0.0f);
        p.attack = clampOr (p.attack, -1.0f, 1.0f, 0.0f);
        p.sustain = clampOr (p.sustain, -1.0f, 1.0f, 0.0f);
        p.output = p.output <= auxOutputs ? p.output : std::uint8_t { 0 };
        p.send1 = clampOr (p.send1, 0.0f, 1.0f, 0.0f);
        p.send2 = clampOr (p.send2, 0.0f, 1.0f, 0.0f);
        p.pitchAmount = clampOr (p.pitchAmount, 0.0f, 48.0f, 24.0f);
        p.pitchDecay = clampOr (p.pitchDecay, 0.005f, 0.2f, 0.035f);
        p.drive = clampOr (p.drive, 1.0f, 8.0f, 1.6f);
        p.sub = clampOr (p.sub, 0.0f, 1.0f, 0.3f);
        p.tail = clampOr (p.tail, 0.03f, 1.5f, 0.25f);
        p.source = enumOr (p.source, 3);
        p.sampleMode = enumOr (p.sampleMode, 3);
        p.velocitySplit = static_cast<std::uint8_t> (std::clamp<int> (p.velocitySplit, 1, 127));
        for (auto& pl : p.players)
        {
            pl.start = clampOr (pl.start, 0.0f, 1.0f, 0.0f);
            pl.end = clampOr (pl.end, 0.0f, 1.0f, 1.0f);
            pl.level = clampOr (pl.level, 0.0f, 2.0f, 1.0f);
        }
    }
}

} // namespace

//==============================================================================
KitSettings legacyKit()
{
    KitSettings kit;
    struct Legacy {
        Model model;
        std::uint8_t note, choke;
        float level, decay, pan, character;
    };
    // legacy descriptor defaults and pad types (VoxFullOnDrums.c PAD(...)).
    constexpr Legacy pads[8] = {
        { Model::kick, 36, 0, 0.85f, 0.38f, 0.0f, 0.5f },  { Model::snare, 38, 0, 0.6f, 0.25f, 0.0f, 0.6f },
        { Model::clap, 39, 0, 0.55f, 0.35f, -0.1f, 0.5f }, { Model::hat, 42, 1, 0.5f, 0.07f, 0.2f, 0.6f },
        { Model::hat, 46, 1, 0.45f, 0.45f, 0.2f, 0.6f },   { Model::tom, 45, 0, 0.6f, 0.5f, -0.25f, 0.4f },
        { Model::rim, 37, 0, 0.5f, 0.08f, 0.3f, 0.5f },    { Model::cymbal, 49, 0, 0.4f, 1.8f, -0.3f, 0.5f },
    };
    for (int i = 0; i < padCount; ++i)
    {
        auto& p = kit.pads[static_cast<std::size_t> (i)];
        if (i < 8)
        {
            const auto& l = pads[i];
            p.model = l.model;
            p.note = l.note;
            p.chokeGroup = l.choke;
            p.level = l.level;
            p.decay = l.decay;
            p.pan = l.pan;
            p.character = l.character;
        }
        else
        {
            p.model = Model::perc;
            p.note = static_cast<std::uint8_t> (50 + (i - 8));
            p.level = 0.6f;
            p.decay = 0.3f;
        }
    }
    return kit;
}

//==============================================================================
// buildPattern (legacy module), unchanged: bars 1..3 evolve, bar 4 is the fill.
namespace {
constexpr int padKick = 0, padSnare = 1, padClap = 2, padClosedHat = 3, padOpenHat = 4, padTom = 5, padRim = 6, padCymbal = 7;
constexpr int bars = 4, fillBar = 3, steps = 16;
constexpr const char* patternNames[factoryPatternCount] = { "Full-on Basic", "Full-on Driving", "Rolling Offbeat",
                                                            "Breakdown", "Kick & Offbeat" };
} // namespace

const char* factoryPatternName (int index) noexcept
{
    return index >= 0 && index < factoryPatternCount ? patternNames[index] : "";
}

Pattern factoryPattern (int index, std::array<PatternStep, 8 * 4 * 16>& cells) noexcept
{
    cells.fill ({});
    const auto set = [&] (int bar, int pad, int st, int vel, bool acc) {
        cells[static_cast<std::size_t> ((pad * bars + bar) * steps + st)] = { true, acc, static_cast<std::uint8_t> (vel) };
    };
    index = std::clamp (index, 0, factoryPatternCount - 1);
    for (int bar = 0; bar < bars; ++bar)
    {
        const bool fill = bar == fillBar;
        const int evo = fill ? bars : bar;   // the fill is not an evolution bar
        for (int st = 0; st < steps; ++st)
        {
            const bool beat = st % 4 == 0, off = st % 4 == 2;
            switch (index)
            {
                case 0:   // Full-on Basic
                case 1:   // Full-on Driving
                    if (beat && ! (fill && st == 12))
                        set (bar, padKick, st, 120, st == 0);
                    if (off)
                        set (bar, padOpenHat, st, 92, true);
                    else if (! (fill && st >= 12))
                        set (bar, padClosedHat, st, st % 2 ? 66 : 92, false);
                    if (st == 4 || st == 12)
                    {
                        set (bar, padSnare, st, 110, false);
                        set (bar, padClap, st, 100, false);
                    }
                    if (index == 1 && st % 4 == 3)
                        set (bar, padRim, st, 58, false);
                    if (evo == 1 && st == 7)
                        set (bar, padRim, st, 80, true);
                    if (evo == 0 && st == 0)
                        set (bar, padCymbal, st, 96, false);
                    if (fill && st >= 12)
                        set (bar, padTom, st, 84 + 10 * (st - 12), st == 15);
                    if (fill && (st == 13 || st == 14 || st == 15))
                        set (bar, padSnare, st, 70 + 12 * (st - 13), false);
                    break;
                case 2:   // Rolling Offbeat: e/a hats, clap only, ghost toms
                    if (beat)
                        set (bar, padKick, st, 118, st == 0);
                    if (off)
                        set (bar, padOpenHat, st, 96, true);
                    if (st % 2 == 1)
                        set (bar, padClosedHat, st, 72, false);
                    if (st == 4 || st == 12)
                        set (bar, padClap, st, 104, false);
                    if ((evo == 2 || fill) && (st == 11 || st == 15))
                        set (bar, padTom, st, 64, false);
                    break;
                case 3:   // Breakdown: no kick
                    if (off)
                        set (bar, padOpenHat, st, 80, false);
                    if (st % 2 == 0 && ! off)
                        set (bar, padClosedHat, st, 60, false);
                    if (st == 12)
                        set (bar, padClap, st, 90, false);
                    if (evo == 0 && st == 0)
                        set (bar, padCymbal, st, 90, false);
                    if (fill && st >= 8 && st % 2 == 0)
                        set (bar, padSnare, st, 60 + 8 * (st - 8), st == 14);
                    break;
                default:   // Kick & Offbeat: space for a rolling bass
                    if (beat)
                        set (bar, padKick, st, 120, st == 0);
                    if (off)
                        set (bar, padOpenHat, st, 90, true);
                    break;
            }
        }
    }
    return Pattern { 8, steps, bars, 0, cells.data() };
}

//==============================================================================
double stepLength (const Pattern& pattern) noexcept
{
    static constexpr double stepPpq[5] = { 0.25, 0.5, 0.125, 1.0 / 3.0, 0.0625 };
    return stepPpq[std::min<std::uint32_t> (pattern.timing, 4u)];
}

namespace {
// Deterministic 0..99 from the seed, the absolute step and the lane.
int chance (std::uint32_t seed, std::int64_t g, std::uint32_t lane) noexcept
{
    std::uint64_t x = (static_cast<std::uint64_t> (seed) << 32) ^ static_cast<std::uint64_t> (g) * 0x9E3779B97F4A7C15ull
                    ^ static_cast<std::uint64_t> (lane) * 0xD1B54A32D192ED03ull;
    x ^= x >> 30;
    x *= 0xBF58476D1CE4E5B9ull;
    x ^= x >> 27;
    x *= 0x94D049BB133111EBull;
    x ^= x >> 31;
    return static_cast<int> (x % 100u);
}
} // namespace

namespace {
// The one cell rule (pattern and song): the hits of `cell` on `lane` at grid
// time `t` (swing included) with step length `step`; probability decided
// by (seed, key, lane).
inline void cellHits (const PatternStep& cell, std::uint32_t lane, double t, double step, std::uint32_t seed,
                      std::int64_t key, const StepContext& context, ScheduledHit* out, int& count) noexcept
{
    if (! cell.on || ((context.laneMask >> lane) & 1u) == 0)
        return;
    const int probability = std::clamp<int> (cell.probability, 0, 100);
    if (probability < 100 && chance (seed, key, lane) >= probability)
        return;
    double velocity = cell.velocity;
    if (cell.accent)
        velocity += context.accent * (127.0 - velocity);
    const auto v = static_cast<std::uint8_t> (std::fmin (127.0, std::floor (velocity + 0.5)));
    const int ratchet = std::clamp<int> (cell.ratchet, 1, maxRatchet);
    const int microPercent = std::clamp<int> (cell.micro, -50, 50);
    const double micro = microPercent * 0.01 * step;
    for (int k = 0; k < ratchet; ++k)
    {
        // The first hit without micro is exactly the legacy time.
        out[count].ppq = k == 0 && microPercent == 0 ? t : t + micro + static_cast<double> (k) * step / ratchet;
        out[count].pad = static_cast<std::uint8_t> (lane);
        out[count].velocity = v;
        ++count;
    }
}
} // namespace

int stepHits (const Pattern& pattern, const StepContext& context, std::int64_t g, ScheduledHit* out) noexcept
{
    if (g < 0 || pattern.cells == nullptr || pattern.lanes == 0 || pattern.steps == 0 || pattern.bars == 0)
        return 0;
    const double step = stepLength (pattern);
    // The legacy rule: odd steps late by swing; accent raises the velocity
    // towards 127 by the accent amount.
    const double t = static_cast<double> (g) * step + ((g & 1) ? context.swing * step : 0.0);
    const auto steps = static_cast<std::int64_t> (pattern.steps);
    const auto bars = static_cast<std::int64_t> (pattern.bars);
    const auto st = static_cast<std::uint32_t> (g % steps);
    const auto bar = static_cast<std::uint32_t> ((g / steps) % bars);
    const auto lanes = std::min<std::uint32_t> (pattern.lanes, padCount);
    int count = 0;
    for (std::uint32_t lane = 0; lane < lanes; ++lane)
        cellHits (pattern.cells[(lane * pattern.bars + bar) * pattern.steps + st], lane, t, step, pattern.seed, g, context, out, count);
    return count;
}

std::uint32_t songBarAt (const Song& song, std::int64_t absoluteBar) noexcept
{
    if (song.bars == 0 || absoluteBar < 0)
        return 0;
    const auto bars = static_cast<std::int64_t> (std::min<std::uint32_t> (song.bars, maxSongBars));
    if (song.loopLength > 0 && song.loopStart < song.bars)
    {
        const auto length = static_cast<std::int64_t> (std::min<std::uint32_t> (song.loopLength, song.bars - song.loopStart));
        return static_cast<std::uint32_t> (song.loopStart + absoluteBar % length);
    }
    return static_cast<std::uint32_t> (absoluteBar % bars);
}

int songHits (const Song& song, const StepContext& context, std::int64_t absoluteBar, std::uint32_t step, ScheduledHit* out) noexcept
{
    if (absoluteBar < 0 || song.views == nullptr || song.bars == 0)
        return 0;
    const auto& bar = song.views[songBarAt (song, absoluteBar)];
    if (bar.cells == nullptr || bar.steps == 0 || step >= bar.steps)
        return 0;
    const double length = 4.0 / static_cast<double> (bar.steps);
    const double t = 4.0 * static_cast<double> (absoluteBar) + static_cast<double> (step) * length
                   + ((step & 1u) ? context.swing * length : 0.0);
    int count = 0;
    for (std::uint32_t lane = 0; lane < static_cast<std::uint32_t> (padCount); ++lane)
        cellHits (bar.cells[lane * bar.laneStride + step], lane, t, length, bar.seed,
                  absoluteBar * 128 + static_cast<std::int64_t> (step), context, out, count);
    return count;
}

//==============================================================================
struct KitEngine::Impl {
    KitSettings safe;   // this block's settings, in range (sanitize)
    double sampleRate = 48000.0;
    int voiceLimit = maxVoices;
    std::array<Voice, maxVoices> voices {};
    // Sequencer: position state only. `horizon` is the latest hit time
    // already played; a block plays the hits after it that fall in the block.
    double horizon = -HUGE_VAL;
    double lastPpqStart = -HUGE_VAL;
    int scheduledCount = 0;           // hits of the last chunk (tests)
    std::uint32_t scheduledBase = 0;  // its first frame in the block
    double lastPpqSeen = -1.0e300;
    struct Hit {
        std::uint32_t offset;
        std::uint8_t pad, velocity;
    };
    std::array<Hit, maxSequencerHits> hits {};
    std::array<std::int8_t, 128> noteToPad {};
    std::array<int, padCount> fireOrder {};
    std::array<double, padCount> gate {};   // mute gate per pad, 1 = open (set in the constructor)

    void stopAll() noexcept
    {
        for (auto& v : voices)
            v.active = false;
    }

    void fadeAll (double seconds) noexcept
    {
        for (auto& v : voices)
            startFade (v, sampleRate, seconds);
    }

    void resetSequencer() noexcept
    {
        horizon = -HUGE_VAL;
        lastPpqStart = -HUGE_VAL;
        scheduledCount = 0;
    }

    // Pads hit on the same sample fire in pad order, except that the members
    // of a choke group fire together at the first member's place, highest
    // pad first: on a coincident hit the lowest pad of the group fires last
    // and cuts the others (the legacy fireOrder: open hat, pad 5, before
    // closed hat, pad 4, which therefore wins).
    void buildOrder (const KitSettings& kit) noexcept
    {
        std::array<bool, padCount> placed {};
        int n = 0;
        for (int p = 0; p < padCount; ++p)
        {
            if (placed[static_cast<std::size_t> (p)])
                continue;
            const auto group = kit.pads[static_cast<std::size_t> (p)].chokeGroup;
            if (group == 0)
            {
                fireOrder[static_cast<std::size_t> (n++)] = p;
                placed[static_cast<std::size_t> (p)] = true;
                continue;
            }
            int members[padCount];
            int m = 0;
            for (int q = p; q < padCount; ++q)
                if (kit.pads[static_cast<std::size_t> (q)].chokeGroup == group)
                    members[m++] = q;
            std::reverse (members, members + m);
            for (int k = 0; k < m; ++k)
            {
                fireOrder[static_cast<std::size_t> (n++)] = members[k];
                placed[static_cast<std::size_t> (members[k])] = true;
            }
        }
    }

    void buildNoteMap (const KitSettings& kit) noexcept
    {
        noteToPad.fill (-1);
        for (int p = padCount - 1; p >= 0; --p)   // the lowest pad wins a shared note
            noteToPad[kit.pads[static_cast<std::size_t> (p)].note & 0x7f] = static_cast<std::int8_t> (p);
    }

    // trigger (legacy), generalised: models, per-pad polyphony, two players.
    void trigger (const KitSettings& kit, const Sample* samples, int pad, int velocity) noexcept
    {
        const double sr = sampleRate;
        const auto& ps = kit.pads[static_cast<std::size_t> (pad)];
        const int group = kit.chokeEnabled ? ps.chokeGroup : 0;
        if (ps.polyphony <= 1)
        {
            for (int i = 0; i < voiceLimit; ++i)
            {
                auto& v = voices[static_cast<std::size_t> (i)];
                if (v.active && (v.pad == pad || (group != 0 && kit.pads[static_cast<std::size_t> (v.pad)].chokeGroup == group)))
                    startFade (v, sr, 0.003);
            }
        }
        else
        {
            // Poly: the pad keeps up to `polyphony` hits; the oldest one
            // beyond that fades. Choke groups still cut the other members.
            int count = 0;
            Voice* oldest = nullptr;
            for (int i = 0; i < voiceLimit; ++i)
            {
                auto& v = voices[static_cast<std::size_t> (i)];
                if (! v.active)
                    continue;
                if (v.pad == pad && v.fadeTotal == 0)
                {
                    ++count;
                    if (oldest == nullptr || v.n > oldest->n)
                        oldest = &v;
                }
                else if (v.pad != pad && group != 0 && kit.pads[static_cast<std::size_t> (v.pad)].chokeGroup == group)
                    startFade (v, sr, 0.003);
            }
            if (count >= std::min<int> (ps.polyphony, maxVoicesPerPad) && oldest != nullptr)
                startFade (*oldest, sr, 0.003);
        }
        Voice* v = nullptr;
        for (int i = 0; i < voiceLimit && v == nullptr; ++i)
            if (! voices[static_cast<std::size_t> (i)].active)
                v = &voices[static_cast<std::size_t> (i)];
        if (v == nullptr)   // bounded polyphony: steal the oldest
        {
            v = &voices[0];
            for (int i = 1; i < voiceLimit; ++i)
                if (voices[static_cast<std::size_t> (i)].n > v->n)
                    v = &voices[static_cast<std::size_t> (i)];
        }
        *v = Voice {};
        v->active = true;
        v->pad = pad;
        v->model = ps.model;
        v->sr = sr;
        v->noise = 0x9E3779B9u ^ (0x85EBCA6Bu * static_cast<std::uint32_t> (pad + 1));   // deterministic

        const double sens = kit.velocitySensitivity;
        const double vel = (1.0 - sens) + sens * static_cast<double> (velocity) / 127.0;
        const double level = ps.level * kit.volume * vel;
        const double angle = (ps.pan + 1.0) * 0.25 * pi;
        v->gainL = level * std::cos (angle) * 1.41421356237;
        v->gainR = level * std::sin (angle) * 1.41421356237;
        const double tune = std::pow (2.0, (ps.tune + ps.fine / 100.0) / 12.0);
        v->filter = ps.filter;
        if (v->filter != Filter::off)
        {
            const double fc = std::fmin (static_cast<double> (ps.cutoff), 0.45 * sr);
            v->fg = std::tan (pi * fc / sr);
            v->fk = 2.0 - 1.96 * ps.resonance;   // Q 0.5 .. 25
        }
        v->attack = ps.attack;
        v->sustain = ps.sustain;
        v->attackEnv = 1.0;
        v->attackMul = std::exp (-1.0 / (0.010 * sr));
        v->sustainEnv = 1.0;
        v->sustainMul = std::exp (-1.0 / (0.030 * sr));
        v->bus = ps.output;
        v->send1 = ps.send1;
        v->send2 = ps.send2;

        // Source. A pad set to Sample without a usable sample keeps playing
        // its synthesis (a missing file never silences a pad).
        bool sampled = false;
        if (ps.source != Source::synth && samples != nullptr)
        {
            const bool useA = ps.sampleMode != SampleMode::velocitySwitch || velocity < ps.velocitySplit;
            const bool useB = ps.sampleMode == SampleMode::mix
                           || (ps.sampleMode == SampleMode::velocitySwitch && velocity >= ps.velocitySplit);
            for (int k = 0; k < samplesPerPad; ++k)
            {
                if ((k == 0 && ! useA) || (k == 1 && ! useB))
                    continue;
                const auto& smp = samples[pad * samplesPerPad + k];
                const auto& pl = ps.players[static_cast<std::size_t> (k)];
                if (smp.id == 0 || smp.channels == nullptr || smp.channelCount == 0 || smp.frames <= 1 || ! (smp.sampleRate > 0.0))
                    continue;
                const double frames = static_cast<double> (smp.frames);
                const double begin = std::floor (static_cast<double> (pl.start) * frames);
                double end = std::ceil (static_cast<double> (pl.end) * frames);
                if (end > frames)
                    end = frames;
                if (end - begin < 2.0)
                    continue;
                auto& p = v->players[k];
                p.on = true;
                p.id = smp.id;
                p.data = smp.channels;
                p.channels = smp.channelCount > 1 ? 2u : 1u;
                p.begin = begin;
                p.end = end;
                p.loop = pl.loop;
                p.reverse = pl.reverse;
                p.pos = p.reverse ? end - 1.0 : begin;
                p.step = smp.sampleRate / sr * tune;
                p.gain = pl.level;
                if (pl.normalize && smp.peak > 0.0f)
                    p.gain /= smp.peak;
                sampled = true;
            }
        }
        v->synthOn = ! sampled || ps.source == Source::layer;
        const double decay = ps.decay;
        v->character = ps.character;
        v->env = 1.0;
        v->envMul = t60 (decay, sr);
        v->env2 = 1.0;
        switch (ps.model)
        {
            case Model::kick:
                v->f0 = 50.0 * tune;
                v->env2Mul = t60 (ps.tail, sr);
                v->kickPitch = ps.pitchAmount;
                v->kickPitchDecay = ps.pitchDecay;
                v->kickDrive = ps.drive;
                v->kickDriveNorm = std::tanh (v->kickDrive);
                v->kickSub = ps.sub * 0.7;
                break;
            case Model::snare:
                v->f0 = 185.0 * tune;
                v->env2Mul = t60 (0.5 * decay, sr);   // tone body
                break;
            case Model::clap:
                v->f0 = 1200.0 * tune * (0.6 + 0.8 * v->character);
                break;
            case Model::tom:
                v->f0 = 120.0 * tune;
                break;
            case Model::rim:
                v->f0 = 520.0 * tune;
                v->env2Mul = t60 (0.01, sr);   // click
                break;
            case Model::hat:
                v->f0 = 1.7 * tune;
                break;
            case Model::cymbal:
                v->f0 = 3.1 * tune;
                break;
            case Model::perc:
                v->f0 = 330.0 * tune;
                v->fmIndex = 1.0;
                v->fmMul = t60 (std::fmax (0.01, 0.4 * decay), sr);
                break;
        }
    }

    // The sequencer for one chunk: every hit of the canonical rule
    // (stepHits for a pattern, songHits for a song) whose frame offset
    // (legacy rounding) is in [0, frames) and that is later than the
    // horizon. Hits are ordered by offset (stable: lane order within a step,
    // as the legacy module). Bounded: at most 4 bars x 64 steps are looked at.
    int schedule (const KitSettings& kit, const Pattern* pattern, const Song* song, const Transport& tr, double ppq0,
                  bool discontinuity, std::uint32_t frames) noexcept
    {
        const bool hasSong = song != nullptr && song->views != nullptr && song->bars > 0;
        const bool hasPattern = pattern != nullptr && pattern->cells != nullptr && pattern->lanes != 0 && pattern->steps != 0
                             && pattern->bars != 0;
        if (kit.sequencerMode == 0 || ! (hasSong || hasPattern) || ! tr.playing || ! tr.hasTempo || ! tr.hasPpq
            || ! (tr.bpm > 0.0) || tr.bpm > 1000.0 || ! std::isfinite (ppq0) || frames == 0)
            return 0;
        if (discontinuity && ppq0 < lastPpqStart)
            horizon = -HUGE_VAL;   // moved backwards: hits may play again
        lastPpqStart = ppq0;

        const double samplesPerPpq = 60.0 * sampleRate / tr.bpm;
        StepContext context;
        context.swing = kit.swing;
        context.accent = kit.accent;
        context.laneMask = 0;
        for (int lane = 0; lane < padCount; ++lane)
            if (kit.pads[static_cast<std::size_t> (lane)].laneOn)
                context.laneMask |= 1u << lane;
        const auto offsetOf = [&] (double t) { return std::ceil ((t - ppq0) * samplesPerPpq - 1e-6); };
        int count = 0;
        double latest = horizon;
        std::array<ScheduledHit, padCount * maxRatchet> stepOut;
        const auto take = [&] (int n) {
            for (int k = 0; k < n && count < maxSequencerHits; ++k)
            {
                const auto& h = stepOut[static_cast<std::size_t> (k)];
                const double at = offsetOf (h.ppq);
                if (at < 0.0 || at >= static_cast<double> (frames) || ! (h.ppq > horizon))
                    continue;
                // Insert by offset after equal offsets (stable).
                int pos = count;
                const auto offset = static_cast<std::uint32_t> (at);
                while (pos > 0 && hits[static_cast<std::size_t> (pos - 1)].offset > offset)
                {
                    hits[static_cast<std::size_t> (pos)] = hits[static_cast<std::size_t> (pos - 1)];
                    --pos;
                }
                hits[static_cast<std::size_t> (pos)] = { offset, h.pad, h.velocity };
                ++count;
                latest = std::fmax (latest, h.ppq);
            }
        };
        if (hasSong)
        {
            // A step's hits lie within [s - 0.5, s + 2) steps of its grid
            // time, so look 3 steps back and 1 ahead of the chunk, across
            // bar lines (each bar has its own step length).
            const double ppqEnd = ppq0 + static_cast<double> (frames) / samplesPerPpq;
            const auto firstBar = static_cast<std::int64_t> (std::floor (ppq0 / 4.0)) - 1;
            // The next bar's first step can sound up to half a (1/16) step
            // before the bar line (negative microtiming).
            const auto lastBar = static_cast<std::int64_t> (std::floor ((ppqEnd + 0.125) / 4.0));
            for (std::int64_t bar = std::max<std::int64_t> (0, firstBar); bar <= lastBar; ++bar)
            {
                const auto& view = song->views[songBarAt (*song, bar)];
                if (view.cells == nullptr || view.steps == 0)
                    continue;
                const double length = 4.0 / static_cast<double> (view.steps);
                const double barPpq = 4.0 * static_cast<double> (bar);
                const auto from = static_cast<std::int64_t> (std::floor ((ppq0 - barPpq) / length)) - 3;
                const auto to = static_cast<std::int64_t> (std::floor ((ppqEnd - barPpq) / length)) + 1;
                const auto lo = std::max<std::int64_t> (0, from);
                const auto hi = std::min<std::int64_t> (static_cast<std::int64_t> (view.steps) - 1, to);
                for (std::int64_t st = lo; st <= hi; ++st)
                    take (songHits (*song, context, bar, static_cast<std::uint32_t> (st), stepOut.data()));
            }
        }
        else
        {
            const double step = stepLength (*pattern);
            // A step's hits lie within [g - 0.5, g + 2) steps of its grid time
            // (micro, swing, ratchets), so start 3 steps back and stop once a
            // step cannot reach this chunk.
            for (std::int64_t g = static_cast<std::int64_t> (std::floor (ppq0 / step)) - 3;; ++g)
            {
                if (offsetOf ((static_cast<double> (g) - 0.5) * step) >= static_cast<double> (frames))
                    break;
                take (stepHits (*pattern, context, g, stepOut.data()));
            }
        }
        horizon = latest;
        return count;
    }
};

int KitEngine::scheduledCount() const noexcept
{
    return impl_->scheduledCount;
}

void KitEngine::scheduled (int i, std::uint32_t& offset, std::uint8_t& pad, std::uint8_t& velocity) const noexcept
{
    const auto& h = impl_->hits[static_cast<std::size_t> (std::clamp (i, 0, maxSequencerHits - 1))];
    offset = impl_->scheduledBase + h.offset;
    pad = h.pad;
    velocity = h.velocity;
}

KitEngine::KitEngine() : impl_ (std::make_unique<Impl>())
{
    impl_->gate.fill (1.0);
}
KitEngine::~KitEngine() = default;

void KitEngine::prepare (double sampleRate)
{
    impl_->sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    impl_->stopAll();
    impl_->resetSequencer();
}

void KitEngine::setMaxVoices (int voices)
{
    impl_->stopAll();
    impl_->voiceLimit = std::clamp (voices, 1, maxVoices);
}

void KitEngine::reset() noexcept
{
    impl_->stopAll();
    impl_->resetSequencer();
}

int KitEngine::activeVoices() const noexcept
{
    int n = 0;
    for (const auto& v : impl_->voices)
        n += v.active ? 1 : 0;
    return n;
}

void KitEngine::process (const KitSettings& settings, const Outputs& out, std::uint32_t frames,
                         const Event* events, std::uint32_t eventCount, const Transport& transport,
                         const Pattern* pattern, const Sample* samples, bool panic, Release release,
                         const Song* song) noexcept
{
    auto& s = *impl_;
    s.safe = settings;   // fixed-size copy, no allocation
    sanitize (s.safe);
    const KitSettings& kit = s.safe;
    sequencerHits_ = 0;
    padPeak_.fill (0.0f);
    soundOff_ = panic;
    if (panic)
        s.stopAll();
    // Mute gate: 3 ms linear ramp per pad (exactly 1 while open, so an
    // unmuted kit renders as before).
    const double gateStep = 1.0 / std::max (1.0, 0.003 * s.sampleRate);
    s.buildNoteMap (kit);
    s.buildOrder (kit);

    // A loop or seek moves the position backwards; steps after the new
    // position may then play again.
    const bool discontinuity = transport.hasPpq && transport.ppq < s.lastPpqSeen;
    if (transport.hasPpq)
        s.lastPpqSeen = transport.ppq;

    std::uint32_t nextEvent = 0;
    for (std::uint32_t done = 0; done < frames;)
    {
        const std::uint32_t n = std::min (maxChunk, frames - done);
        const double ppq0 = transport.ppq + (transport.hasTempo ? static_cast<double> (done) * transport.bpm / 60.0 / s.sampleRate : 0.0);
        const int seqCount = s.schedule (kit, pattern, song, transport, ppq0, discontinuity && done == 0, n);
        s.scheduledCount = seqCount;
        s.scheduledBase = done;
        int nextHit = 0;
        for (int k = 0; k < seqCount; ++k)
            sequencerHits_ |= 1u << s.hits[static_cast<std::size_t> (k)].pad;

        for (std::uint32_t i = 0; i < n; ++i)
        {
            const std::uint32_t at = done + i;
            int hits[padCount] = {};
            while (nextEvent < eventCount && events[nextEvent].offset <= at)
            {
                const auto& e = events[nextEvent++];
                if (e.pad >= 0)
                {
                    const int velocity = e.bytes[2] & 0x7F;
                    if (e.pad < padCount && velocity > hits[e.pad])
                        hits[e.pad] = velocity;
                    continue;
                }
                if (e.size < 3)
                    continue;
                const std::uint8_t status = e.bytes[0] & 0xF0u;
                const int data1 = e.bytes[1] & 0x7F;
                const int data2 = e.bytes[2] & 0x7F;
                if (status == 0x90u && data2 > 0)
                {
                    if (kit.sequencerMode == 1)
                        continue;   // Internal: the sequencer alone plays
                    const int pad = s.noteToPad[static_cast<std::size_t> (data1)];
                    if (pad >= 0 && data2 > hits[pad])
                        hits[pad] = data2;
                }
                else if (status == 0xB0u)
                {
                    if (data1 == 120)
                    {
                        s.stopAll();   // All Sound Off: immediate
                        soundOff_ = true;
                    }
                    else if (data1 == 123)
                        s.fadeAll (0.05);   // All Notes Off: bounded 50 ms tail
                }
            }
            while (nextHit < seqCount && s.hits[static_cast<std::size_t> (nextHit)].offset <= i)
            {
                const auto& h = s.hits[static_cast<std::size_t> (nextHit)];
                if (h.velocity > hits[h.pad])
                    hits[h.pad] = h.velocity;
                ++nextHit;
            }
            for (int k = 0; k < padCount; ++k)
            {
                const int pad = s.fireOrder[static_cast<std::size_t> (k)];
                if (hits[pad] > 0 && ! kit.pads[static_cast<std::size_t> (pad)].mute)
                    s.trigger (kit, samples, pad, hits[pad]);
            }

            for (int pad = 0; pad < padCount; ++pad)
            {
                auto& g = s.gate[static_cast<std::size_t> (pad)];
                if (kit.pads[static_cast<std::size_t> (pad)].mute)
                    g = g > gateStep ? g - gateStep : 0.0;
                else if (g < 1.0)
                    g = g + gateStep < 1.0 ? g + gateStep : 1.0;
            }
            double l = 0.0, r = 0.0;
            double busL[auxBuses] = {}, busR[auxBuses] = {};
            for (int k = 0; k < s.voiceLimit; ++k)
            {
                auto& v = s.voices[static_cast<std::size_t> (k)];
                if (! v.active)
                    continue;
                double sl = 0.0, sr = 0.0;
                const double synth = v.synthOn ? renderVoice (v) : 0.0;
                if (sampleOn (v))
                {
                    bool anyLoop = false, first = true;
                    for (auto& p : v.players)
                    {
                        if (! p.on)
                            continue;
                        double pl = 0.0, pr = 0.0;
                        renderPlayer (p, &pl, &pr);
                        if (p.loop)   // a loop follows the pad's decay envelope
                        {
                            pl *= v.env;
                            pr *= v.env;
                            anyLoop = true;
                        }
                        // The first player is assigned, not added: one player
                        // then gives exactly the legacy value (sign of zero too).
                        sl = first ? pl : sl + pl;
                        sr = first ? pr : sr + pr;
                        first = false;
                    }
                    if (anyLoop)
                    {
                        if (! v.synthOn)
                            v.env *= v.envMul;
                        if (v.env < silence && v.n > static_cast<std::int64_t> (0.005 * v.sr))
                            for (auto& p : v.players)
                                if (p.loop)
                                    p.on = false;
                    }
                    if (! v.synthOn)
                        v.n += 1;   // age for voice stealing, as the synthesis does
                }
                const double transient = transientGain (v);
                double xL = filterChannel (v, 0, (synth + sl) * transient);
                double xR = filterChannel (v, 1, (synth + sr) * transient);
                if (v.fadeTotal > 0)
                {
                    const double fade = static_cast<double> (v.fadeLeft) / static_cast<double> (v.fadeTotal);
                    xL *= fade;
                    xR *= fade;
                    if (--v.fadeLeft <= 0)
                        v.active = false;
                }
                if (! v.synthOn && ! sampleOn (v))
                    v.active = false;
                const double gate = s.gate[static_cast<std::size_t> (v.pad)];
                const double xl = xL * v.gainL * gate, xr = xR * v.gainR * gate;
                {
                    const auto peak = static_cast<float> (std::fmax (std::fabs (xl), std::fabs (xr)));
                    if (peak > padPeak_[static_cast<std::size_t> (v.pad)])
                        padPeak_[static_cast<std::size_t> (v.pad)] = peak;
                }
                // One dry destination per pad (main or its output bus); sends
                // are post level/pan and independent of it.
                if (out.aux != nullptr && v.bus > 0 && v.bus <= auxOutputs)   // defensive: sanitize keeps 0..8
                {
                    busL[auxSends + v.bus - 1] += xl;
                    busR[auxSends + v.bus - 1] += xr;
                }
                else
                {
                    l += xl;
                    r += xr;
                }
                if (out.aux != nullptr)
                {
                    busL[0] += xl * v.send1;
                    busR[0] += xr * v.send1;
                    busL[1] += xl * v.send2;
                    busR[1] += xr * v.send2;
                }
            }
            out.left[at] = static_cast<float> (l);
            out.right[at] = static_cast<float> (r);
            if (out.aux != nullptr)
                for (int b = 0; b < auxBuses; ++b)
                {
                    (*out.aux)[static_cast<std::size_t> (b * 2)][at] = static_cast<float> (busL[b]);
                    (*out.aux)[static_cast<std::size_t> (b * 2 + 1)][at] = static_cast<float> (busR[b]);
                }
        }
        done += n;
    }
    if (release == Release::stopAtEnd)
        s.stopAll();
    else if (release == Release::fadeAtEnd)
        s.fadeAll (0.05);

    // Every buffer a voice still holds, once each.
    usage_.count = 0;
    for (const auto& v : s.voices)
    {
        if (! v.active)
            continue;
        for (const auto& p : v.players)
        {
            if (! p.on)
                continue;
            bool seen = false;
            for (std::uint32_t j = 0; j < usage_.count; ++j)
                seen = seen || usage_.ids[j] == p.id;
            if (! seen && usage_.count < usage_.ids.size())
                usage_.ids[usage_.count++] = p.id;
        }
    }
}

} // namespace vox::drums::kit
