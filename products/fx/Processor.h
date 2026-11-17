#pragma once

#include "common/SuiteProcessor.h"
#include "engine/NoiseRiser.h"
#include "engine/ProgramStore.h"

namespace vox::fx {

inline constexpr preset::Tag fxParameterSectionTag = preset::makeTag ("FPRM");
inline constexpr preset::Tag fxFamilySectionTag = preset::makeTag ("FFAM");
inline constexpr preset::Tag fxProgramSectionTag = preset::makeTag ("FPRG");

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
    // Controller methods only: these may lock/allocate and are never called
    // from the audio callback. Existing events retain their captured program.
    bool setFamily (int family) {
        if (family < 0 || family > 9) return false;
        return programs_.setLayerFamily (0, static_cast<NoiseFamily> (family));
    }
    int family() const { return static_cast<int> (programs_.snapshot().layers[0].family); }
    bool setProgram (const NoiseProgram& program) { return programs_.publish (program); }
    NoiseProgram program() const { return programs_.snapshot(); }
    std::uint64_t programRestoreEpoch() const noexcept { return restoreEpoch_.load(std::memory_order_acquire); }
    template <typename Edit> bool editLayer (std::size_t index, Edit edit) { return programs_.editLayer (index, edit); }

protected:
    void writeProductState (std::vector<preset::Section>&) const override;
    ProductRestore readProductState (const preset::StateDocument&, juce::String&) override;
    bool isProductSection (preset::Tag tag) const override { return tag == fxParameterSectionTag || tag == fxFamilySectionTag || tag == fxProgramSectionTag; }

private:
    NoiseRiser engine_;
    ProgramStore programs_;
    std::atomic<std::uint64_t> restoreEpoch_ { 0 };
    NoiseProgram audioProgram_;
    std::uint64_t programSequence_ = 0; // audio thread only
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
