#pragma once

#include "common/SuiteProcessor.h"
#include "engine/NoiseRiser.h"

namespace vox::fx {

inline constexpr preset::Tag fxParameterSectionTag = preset::makeTag ("FPRM");
inline constexpr preset::Tag fxFamilySectionTag = preset::makeTag ("FFAM");

// Interim FX-01 instrument: a four-second one-shot noise riser. The timeline,
// structural Program, layers and remaining families are not implemented yet.
class FxProcessor final : public vox::suite::SuiteProcessor {
public:
    FxProcessor();
    void prepareToPlay (double sampleRate, int maximumBlockSize) override;
    void releaseResources() override;
    void reset() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using vox::suite::SuiteProcessor::processBlock;
    double getTailLengthSeconds() const override { return 32.0; }
    juce::AudioProcessorEditor* createEditor() override;
    juce::AudioParameterFloat& outputParameter() noexcept { return *output_; }
    juce::AudioParameterFloat& durationParameter() noexcept { return *duration_; }
    // Message-to-audio requests are bounded and coalesced, not a MIDI queue.
    void audition() noexcept { auditionPending_.store (true, std::memory_order_release); }
    void panic() noexcept { panicPending_.store (true, std::memory_order_release); }
    float outputPeak() const noexcept { return peak_.load (std::memory_order_relaxed); }
    // Structural selector: snapshots into newly triggered events, not a host
    // automation parameter. Family selection is a single lock-free scalar.
    bool setFamily (int family) noexcept {
        if (family < 0 || family > 6) return false;
        family_.store (family, std::memory_order_release); return true;
    }
    int family() const noexcept { return family_.load (std::memory_order_acquire); }

protected:
    void writeProductState (std::vector<preset::Section>&) const override;
    ProductRestore readProductState (const preset::StateDocument&, juce::String&) override;
    bool isProductSection (preset::Tag tag) const override { return tag == fxParameterSectionTag || tag == fxFamilySectionTag; }

private:
    NoiseRiser engine_;
    static_assert (std::atomic<int>::is_always_lock_free);
    std::atomic<int> family_ { 0 };
    int appliedFamily_ = -1; // audio thread only
    juce::AudioParameterFloat* output_ = nullptr; // owned by AudioProcessor
    juce::AudioParameterFloat* duration_ = nullptr;
    // Guarded by SuiteProcessor state lock, not accessed by the audio thread.
    float loadedDuration_ = 0.0f, heldDuration_ = 0.0f;
    juce::SmoothedValue<float> outputSmoothing_;
    std::atomic<bool> panicPending_ { false }, auditionPending_ { false };
    std::atomic<float> peak_ { 0.0f };
    // Audio-thread-only transport history. Missing host position invalidates
    // continuity tracking; it never fabricates a seek.
    bool transportKnown_ = false, wasPlaying_ = false, expectedSampleKnown_ = false;
    std::int64_t expectedSample_ = 0;
};

} // namespace vox::fx
