#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace vox::fx {

// FX-01 work in progress: deterministic, allocation-free one-shot noise risers.
// Each event is four seconds long. Note-off is intentionally ignored (one-shot);
// all-sound-off uses a 5 ms release. This is not yet the full Program engine.
class ReferenceNoiseRiser {
public:
    void prepare(double sampleRate) noexcept {
        rate_ = std::isfinite(sampleRate) && sampleRate >= 8000.0 ? sampleRate : 44100.0;
        voices_ = {};
        retiring_ = {};
        serial_ = 0;
    }

    // A ninth event fades the oldest voice in a dedicated, bounded retirement
    // slot. If that slot is still fading, the new event is dropped instead of
    // truncating an audible voice or allocating memory.
    bool noteOn(int channel, int note, float velocity) noexcept {
        if (!std::isfinite(velocity) || velocity <= 0.0f) return false;
        Voice* slot = nullptr;
        for (auto& voice : voices_)
            if (!voice.active) { slot = &voice; break; }
        if (!slot) {
            if (retiring_.active) return false;
            slot = &*std::min_element(voices_.begin(), voices_.end(),
                [](const Voice& a, const Voice& b) { return a.serial < b.serial; });
            retiring_ = *slot;
            beginRelease(retiring_);
        }
        *slot = {};
        slot->active = true;
        slot->channel = channel;
        slot->note = note;
        slot->velocity = std::clamp(velocity, 0.0f, 1.0f);
        slot->serial = ++serial_;
        slot->rng = 0x9e3779b9u ^ static_cast<std::uint32_t>(serial_ * 0x85ebca6bu);
        if (slot->rng == 0) slot->rng = 1;
        return true;
    }

    void allSoundOff(int channel) noexcept {
        for (auto& v : voices_) if (v.active && (channel == 0 || v.channel == channel)) beginRelease(v);
        if (retiring_.active && (channel == 0 || retiring_.channel == channel)) beginRelease(retiring_);
    }

    void render(float* left, float* right, int count) noexcept {
        for (int i = 0; i < count; ++i) {
            double sum = 0;
            for (auto& v : voices_) sum += tick(v);
            sum += tick(retiring_);
            // Conservative fixed gain for 8 active voices plus one retirement.
            const auto sample = static_cast<float>(sum * 0.055);
            left[i] = sample;
            right[i] = sample;
        }
    }

private:
    struct Voice {
        bool active = false;
        int channel = 0, note = 0;
        float velocity = 0;
        std::uint64_t serial = 0, age = 0;
        std::uint32_t rng = 1;
        double lowpass = 0, dcInput = 0, dcOutput = 0;
        int releaseRemaining = 0, releaseLength = 0;
    };
    void beginRelease(Voice& v) noexcept {
        if (v.releaseRemaining != 0) return;
        v.releaseLength = std::max(1, static_cast<int>(rate_ * 0.005));
        v.releaseRemaining = v.releaseLength;
    }
    double tick(Voice& v) noexcept {
        if (!v.active) return 0;
        constexpr double duration = 4.0;
        const double t = static_cast<double>(v.age) / (duration * rate_);
        if (t >= 1.0) { v.active = false; return 0; }
        ++v.age;
        v.rng ^= v.rng << 13; v.rng ^= v.rng >> 17; v.rng ^= v.rng << 5;
        const double noise = (static_cast<double>(v.rng) / 2147483648.0) - 1.0;
        // Exponential cutoff sweep, clamped below Nyquist.
        const double cutoff = std::min(180.0 * std::exp(t * std::log(10000.0 / 180.0)), rate_ * 0.45);
        const double coefficient = 1.0 - std::exp(-6.283185307179586 * cutoff / rate_);
        v.lowpass += coefficient * (noise - v.lowpass);
        // DC blocker prevents very-low-frequency energy building up in a sum.
        const double filtered = v.lowpass - v.dcInput + 0.995 * v.dcOutput;
        v.dcInput = v.lowpass; v.dcOutput = filtered;
        const double fadeIn = std::min(1.0, t * duration / 0.01);
        const double fadeOut = std::min(1.0, (1.0 - t) * duration / 0.02);
        double release = 1.0;
        if (v.releaseRemaining > 0) {
            release = static_cast<double>(v.releaseRemaining) / v.releaseLength;
            if (--v.releaseRemaining == 0) v.active = false;
        }
        return filtered * v.velocity * (0.15 + 0.85 * t) * fadeIn * fadeOut * release;
    }
    double rate_ = 44100.0;
    std::uint64_t serial_ = 0;
    std::array<Voice, 8> voices_ {};
    Voice retiring_ {};
};
} // namespace vox::fx
