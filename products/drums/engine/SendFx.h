#pragma once

// VOX Drums shared send effects (DRM-06): one stereo delay (Send 1) and one
// stereo reverb (Send 2). The pads' post-fader sends feed them; both return
// to the Main output only. Realtime: prepare() allocates, process() and
// reset() do not.
//
// Delay: stereo, feedback through a one-pole low-pass (tone), time changes
// glide (fractional read). Reverb: 8 damped combs and 4 all-passes per
// channel (Schroeder/Moorer, lengths scaled to the sample rate).

#include <array>
#include <cstdint>
#include <vector>

namespace vox::drums::kit {

struct FxSettings {
    double delaySeconds = 0.3;    // 0.01 .. maxDelaySeconds
    float delayFeedback = 0.35f;  // 0 .. 0.85
    float delayTone = 0.5f;       // 0 dark .. 1 bright
    float delayLevel = 0.8f;      // return level 0 .. 1
    float reverbSize = 0.5f;      // 0 .. 1
    float reverbDamp = 0.5f;      // 0 .. 1
    float reverbLevel = 0.8f;     // return level 0 .. 1
};

class SendFx {
public:
    static constexpr double maxDelaySeconds = 2.0;

    void prepare (double sampleRate);
    void reset() noexcept;   // clears every tail

    // Adds the delay's and the reverb's returns to main. Inputs and main
    // hold `frames` samples; inputs are not modified.
    void process (const FxSettings& settings, const float* delayL, const float* delayR, const float* reverbL,
                  const float* reverbR, float* mainL, float* mainR, std::uint32_t frames) noexcept;

    // True when neither effect holds an audible tail (below -120 dB).
    bool idle() const noexcept { return delayIdle_ && reverbIdle_; }

private:
    struct Comb {
        std::vector<float> buffer;
        std::size_t at = 0;
        float store = 0.0f;
    };
    struct AllPass {
        std::vector<float> buffer;
        std::size_t at = 0;
    };
    double sampleRate_ = 48000.0;
    std::array<std::vector<float>, 2> delay_;
    std::size_t delayAt_ = 0;
    double delayNow_ = 0.0;                 // gliding delay length in samples
    std::array<float, 2> delayLp_ {};
    std::array<std::array<Comb, 8>, 2> combs_;
    std::array<std::array<AllPass, 4>, 2> allPasses_;
    bool delayIdle_ = true, reverbIdle_ = true;
    std::uint32_t delayQuiet_ = 0, reverbQuiet_ = 0;
    std::uint32_t delayTail_ = 0, reverbTail_ = 0;   // quiet samples before idle
};

} // namespace vox::drums::kit
