#include "SendFx.h"

#include <algorithm>
#include <cmath>

namespace vox::drums::kit {
namespace {

constexpr int combLengths[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };   // at 44.1 kHz
constexpr int allPassLengths[4] = { 556, 441, 341, 225 };
constexpr int stereoSpread = 23;
constexpr float quiet = 1.0e-6f;   // -120 dB

void clear (std::vector<float>& v) noexcept
{
    std::fill (v.begin(), v.end(), 0.0f);
}

} // namespace

void SendFx::prepare (double sampleRate)
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    const double scale = sampleRate_ / 44100.0;
    for (auto& d : delay_)
        d.assign (static_cast<std::size_t> (std::ceil (maxDelaySeconds * sampleRate_)) + 4, 0.0f);
    for (int ch = 0; ch < 2; ++ch)
    {
        for (int i = 0; i < 8; ++i)
            combs_[static_cast<std::size_t> (ch)][static_cast<std::size_t> (i)].buffer.assign (
                static_cast<std::size_t> (std::lround ((combLengths[i] + ch * stereoSpread) * scale)), 0.0f);
        for (int i = 0; i < 4; ++i)
            allPasses_[static_cast<std::size_t> (ch)][static_cast<std::size_t> (i)].buffer.assign (
                static_cast<std::size_t> (std::lround ((allPassLengths[i] + ch * stereoSpread) * scale)), 0.0f);
    }
    // Longest tails: 0.85 feedback at 2 s reaches -120 dB after 85 repeats;
    // the reverb's longest decay is about 12 s.
    delayTail_ = static_cast<std::uint32_t> (sampleRate_ * maxDelaySeconds) + 64;
    reverbTail_ = static_cast<std::uint32_t> (sampleRate_ * 0.25);
    delayNow_ = 0.0;
    reset();
}

void SendFx::reset() noexcept
{
    for (auto& d : delay_)
        clear (d);
    delayLp_ = {};
    for (auto& channel : combs_)
        for (auto& c : channel)
        {
            clear (c.buffer);
            c.store = 0.0f;
            c.at = 0;
        }
    for (auto& channel : allPasses_)
        for (auto& a : channel)
        {
            clear (a.buffer);
            a.at = 0;
        }
    delayAt_ = 0;
    delayIdle_ = reverbIdle_ = true;
    delayQuiet_ = reverbQuiet_ = 0;
}

void SendFx::process (const FxSettings& s, const float* dl, const float* dr, const float* rl, const float* rr, float* mainL,
                      float* mainR, std::uint32_t frames) noexcept
{
    if (delay_[0].empty())
        return;
    //== delay
    {
        const auto size = delay_[0].size();
        const double target = std::clamp (s.delaySeconds, 0.01, maxDelaySeconds) * sampleRate_;
        if (delayNow_ <= 0.0 || delayIdle_)
            delayNow_ = target;   // nothing sounding: no glide to hear
        const double glide = 1.0 - std::exp (-1.0 / (0.05 * sampleRate_));
        const float feedback = std::clamp (s.delayFeedback, 0.0f, 0.85f);
        const double cutoff = 400.0 * std::pow (45.0, static_cast<double> (std::clamp (s.delayTone, 0.0f, 1.0f)));   // 400 Hz .. 18 kHz
        const auto lp = static_cast<float> (1.0 - std::exp (-2.0 * 3.141592653589793 * std::min (cutoff, 0.45 * sampleRate_) / sampleRate_));
        const float level = std::clamp (s.delayLevel, 0.0f, 1.0f);
        const float* in[2] = { dl, dr };
        float* out[2] = { mainL, mainR };
        for (std::uint32_t i = 0; i < frames; ++i)
        {
            const float inL = in[0][i], inR = in[1][i];
            if (delayIdle_)
            {
                if (std::fabs (inL) < quiet && std::fabs (inR) < quiet)
                    continue;
                delayIdle_ = false;
                delayQuiet_ = 0;
            }
            delayNow_ += (target - delayNow_) * glide;
            double read = static_cast<double> (delayAt_) - delayNow_;
            if (read < 0.0)
                read += static_cast<double> (size);
            const auto i0 = static_cast<std::size_t> (read) % size;
            const auto i1 = i0 + 1 == size ? 0 : i0 + 1;
            const auto frac = static_cast<float> (read - std::floor (read));
            float peak = 0.0f;
            for (int ch = 0; ch < 2; ++ch)
            {
                auto& line = delay_[static_cast<std::size_t> (ch)];
                const float wet = line[i0] + (line[i1] - line[i0]) * frac;
                auto& state = delayLp_[static_cast<std::size_t> (ch)];
                state += (wet - state) * lp;
                line[delayAt_] = (ch == 0 ? inL : inR) + state * feedback;
                out[ch][i] += wet * level;
                peak = std::max (peak, std::max (std::fabs (wet), std::fabs (line[delayAt_])));
            }
            delayAt_ = delayAt_ + 1 == size ? 0 : delayAt_ + 1;
            if (peak < quiet)
            {
                if (++delayQuiet_ > delayTail_)
                {
                    // Everything in the line is below -120 dB: clear it so a
                    // later hit starts from silence.
                    delayIdle_ = true;
                    clear (delay_[0]);
                    clear (delay_[1]);
                    delayLp_ = {};
                }
            }
            else
                delayQuiet_ = 0;
        }
    }
    //== reverb
    {
        const float size = std::clamp (s.reverbSize, 0.0f, 1.0f);
        const float feedback = 0.7f + 0.28f * size;
        const float damp = 0.05f + 0.6f * std::clamp (s.reverbDamp, 0.0f, 1.0f);
        const float level = std::clamp (s.reverbLevel, 0.0f, 1.0f);
        constexpr float inputGain = 0.015f, outputGain = 3.0f;
        const float* in[2] = { rl, rr };
        float* out[2] = { mainL, mainR };
        for (std::uint32_t i = 0; i < frames; ++i)
        {
            const float input = (in[0][i] + in[1][i]) * inputGain;
            if (reverbIdle_)
            {
                if (std::fabs (input) < quiet * inputGain)
                    continue;
                reverbIdle_ = false;
                reverbQuiet_ = 0;
            }
            float peak = 0.0f;
            for (int ch = 0; ch < 2; ++ch)
            {
                float sum = 0.0f;
                for (auto& c : combs_[static_cast<std::size_t> (ch)])
                {
                    const float y = c.buffer[c.at];
                    c.store = y * (1.0f - damp) + c.store * damp;
                    c.buffer[c.at] = input + c.store * feedback;
                    c.at = c.at + 1 == c.buffer.size() ? 0 : c.at + 1;
                    sum += y;
                }
                for (auto& a : allPasses_[static_cast<std::size_t> (ch)])
                {
                    const float buffered = a.buffer[a.at];
                    const float y = buffered - sum;
                    a.buffer[a.at] = sum + buffered * 0.5f;
                    a.at = a.at + 1 == a.buffer.size() ? 0 : a.at + 1;
                    sum = y;
                }
                out[ch][i] += sum * outputGain * level;
                peak = std::max (peak, std::fabs (sum) * outputGain);
            }
            if (peak < quiet)
            {
                if (++reverbQuiet_ > reverbTail_)
                {
                    reverbIdle_ = true;
                    for (auto& channel : combs_)
                        for (auto& c : channel)
                        {
                            clear (c.buffer);
                            c.store = 0.0f;
                        }
                    for (auto& channel : allPasses_)
                        for (auto& a : channel)
                            clear (a.buffer);
                }
            }
            else
                reverbQuiet_ = 0;
        }
    }
}

} // namespace vox::drums::kit
