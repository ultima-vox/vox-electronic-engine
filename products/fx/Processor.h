#pragma once

#include "common/SuiteProcessor.h"
#include "engine/NoiseRiser.h"

namespace vox::fx {

inline constexpr preset::Tag fxParameterSectionTag = preset::makeTag ("FPRM");

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
    double getTailLengthSeconds() const override { return 4.0; }

protected:
    void writeProductState (std::vector<preset::Section>&) const override;
    ProductRestore readProductState (const preset::StateDocument&, juce::String&) override;
    bool isProductSection (preset::Tag tag) const override { return tag == fxParameterSectionTag; }

private:
    NoiseRiser engine_;
    juce::AudioParameterFloat* output_ = nullptr; // owned by AudioProcessor
    juce::SmoothedValue<float> outputSmoothing_;
    std::atomic<bool> panicPending_ { false };
};

} // namespace vox::fx
