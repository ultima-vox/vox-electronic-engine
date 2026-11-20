#pragma once

// SYN-04 VA waves, the second filter, the shared wavetable and unison.
// Saw, square and sine still call BlepOscillator::next. Oversampling stays off.

#include "dspcore/Primitives.h"

#include <algorithm>
#include <cmath>

namespace vox::syn::engine {

struct VaOsc {
    vstengine::dspcore::BlepOscillator core;
    double tri = 0.0;
    void reset() noexcept
    {
        core.reset();
        tri = 0.0;
    }
    float next (int wave, double dt) noexcept
    {
        return next (wave, dt, 0.5f);
    }
    float next (int wave, double dt, float pulseWidth) noexcept
    {
        const int index = std::clamp (wave, 0, 4);
        if (index <= 2)
            return static_cast<float> (core.next (index, dt));
        const double phase = core.phase;
        double out;
        if (index == 4)
        {
            const double width = std::clamp (static_cast<double> (pulseWidth), 0.05, 0.95);
            out = (phase < width ? 1.0 : -1.0)
                + vstengine::dspcore::BlepOscillator::polyBlep (phase, dt)
                - vstengine::dspcore::BlepOscillator::polyBlep (std::fmod (phase + 1.0 - width, 1.0), dt);
        }
        else
        {
            const double square = (phase < 0.5 ? 1.0 : -1.0)
                                + vstengine::dspcore::BlepOscillator::polyBlep (phase, dt)
                                - vstengine::dspcore::BlepOscillator::polyBlep (std::fmod (phase + 0.5, 1.0), dt);
            tri = 0.999 * tri + 4.0 * dt * square;
            tri = std::clamp (tri, -1.5, 1.5);
            out = tri;
        }
        core.phase = phase + dt;
        core.phase -= std::floor (core.phase);
        return static_cast<float> (out);
    }
};

// 0 LP12, 1 LP24, 2 HP12, 3 HP24, 4 BP12, 5 notch.
// The default VA low-pass stays on SvfLowpass.
struct VaFilter {
    double s1 = 0.0, s2 = 0.0, s3 = 0.0, s4 = 0.0;
    double fc_ = -1.0, res_ = -1.0, sr_ = -1.0, g_ = 0.0, k_ = 0.0;
    void reset() noexcept
    {
        s1 = s2 = s3 = s4 = 0.0;
        fc_ = -1.0;
    }
    struct Stage { double lp, bp, hp, notch; };
    static Stage tick (double x, double g, double k, double& ic1, double& ic2) noexcept
    {
        const double a1 = 1.0 / (1.0 + g * (g + k));
        const double v3 = x - ic2;
        const double v1 = a1 * ic1 + g * a1 * v3;
        const double v2 = ic2 + g * v1;
        ic1 = 2.0 * v1 - ic1;
        ic2 = 2.0 * v2 - ic2;
        Stage out;
        out.lp = v2;
        out.bp = v1;
        out.hp = x - k * v1 - v2;
        out.notch = x - k * v1;
        return out;
    }
    double next (double x, double cutoffHz, double resonance, double sampleRate, int mode) noexcept
    {
        if (! std::isfinite (x) || ! std::isfinite (cutoffHz) || sampleRate < 1.0)
        {
            reset();
            return 0.0;
        }
        const double fc = std::min (std::max (cutoffHz, 20.0), 0.45 * sampleRate);
        const double res = std::clamp (resonance, 0.0, 1.0);
        if (fc != fc_ || res != res_ || sampleRate != sr_)
        {
            fc_ = fc;
            res_ = res;
            sr_ = sampleRate;
            g_ = std::tan (vstengine::dspcore::pi * fc / sampleRate);
            k_ = 2.0 - 1.9 * res;
        }
        const double g = g_;
        const double k = k_;
        const int m = std::clamp (mode, 0, 5);
        double y;
        if (m == 1 || m == 3)
        {
            const Stage a = tick (x, g, k, s1, s2);
            const Stage b = tick (m == 1 ? a.lp : a.hp, g, 2.0, s3, s4);
            y = m == 1 ? b.lp : b.hp;
        }
        else
        {
            const Stage a = tick (x, g, k, s1, s2);
            y = m == 2 ? a.hp : m == 4 ? a.bp : m == 5 ? a.notch : a.lp;
        }
        if (! std::isfinite (y))
        {
            reset();
            return 0.0;
        }
        return y;
    }
};

// Wavetable: one shared read-only table, 256 frames x 2048 samples.
// Built once, off the callback. Playback only reads it.
// Unison: 1 to 4 copies, and each copy spends one slot of the 16-voice pool.
struct Wavetable {
    static constexpr int frames = 256;
    static constexpr int cycle = 2048;
    float data[frames][cycle] {};
    Wavetable() noexcept;
    [[nodiscard]] int frame (float position, double cyclesPerSample) const noexcept;
    [[nodiscard]] float readFrame (int frame, double phase) const noexcept;
    [[nodiscard]] float read (float position, double phase, double cyclesPerSample) const noexcept
    {
        return readFrame (frame (position, cyclesPerSample), phase);
    }
};

[[nodiscard]] const Wavetable& sharedWavetable() noexcept;
[[nodiscard]] constexpr int wavetableBytes() noexcept
{
    return Wavetable::frames * Wavetable::cycle * static_cast<int> (sizeof (float));
}

} // namespace vox::syn::engine
