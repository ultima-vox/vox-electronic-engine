#pragma once

#include "common/SuiteProcessor.h"
#include "effects/EffectsChain.h"
#include "engine/SynthEngine.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace vox::syn {

inline constexpr preset::Tag engineSectionTag = preset::makeTag ("ENG1");

// SYN-01: two parts (Psy Bass, Acid, VA), one 16-voice allocator, ENG1 state.
class SynthesisProcessor final : public vox::suite::SuiteProcessor {
public:
    SynthesisProcessor();

    void prepareToPlay (double sampleRate, int maximumBlockSize) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using vox::suite::SuiteProcessor::processBlock;
    double getTailLengthSeconds() const override;
    juce::AudioProcessorEditor* createEditor() override;

    juce::AudioProcessorValueTreeState& parameters() noexcept { return parameters_; }

    // Tests, same thread as processBlock, after it returns.
    [[nodiscard]] int activeVoices() const noexcept { return engine_.activeVoices(); }
    [[nodiscard]] int monoNote (int part) const noexcept { return engine_.monoNote (part); }
    [[nodiscard]] bool monoGate (int part) const noexcept { return engine_.monoGate (part); }
    [[nodiscard]] double monoPhase (int part) const noexcept { return engine_.monoPhase (part); }
    [[nodiscard]] int vaFadeLeft() const noexcept { return engine_.vaFadeLeft(); }
    [[nodiscard]] std::uint64_t droppedMidi() const noexcept { return droppedMidi_.load (std::memory_order_relaxed); }

protected:
    void writeProductState (std::vector<preset::Section>&) const override;
    ProductRestore readProductState (const preset::StateDocument&, juce::String&) override;
    bool isProductSection (preset::Tag tag) const override { return tag == engineSectionTag; }

private:
    juce::AudioProcessorValueTreeState parameters_;
    std::array<std::atomic<float>*, 15> raw_ {};
    engine::SynthEngine engine_;
    vstengine::effects::EffectsChain fx_;
    engine::Event events_[engine::maxEvents] {};
    bool prepared_ = false;
    std::atomic<std::uint64_t> droppedMidi_ { 0 };

    struct Restored { float loaded = 0.0f, held = 0.0f; };
    std::vector<Restored> restored_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SynthesisProcessor)
};

} // namespace vox::syn
