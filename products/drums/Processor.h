#pragma once

#include "common/SuiteProcessor.h"

#include "DrumsParameters.h"
#include "engine/DrumEngine.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace vox::drums {

// Product state section of schema 1: the plugin parameter values by ID.
inline constexpr preset::Tag parameterSectionTag = preset::makeTag ("DPRM");

// VOX Drums (DRM-01): eight synthesized pads played by host MIDI (pad notes
// 36 38 39 42 46 45 37 49) and/or the internal sequencer with the factory
// patterns, stereo main output.
class DrumsProcessor final : public vox::suite::SuiteProcessor {
public:
    DrumsProcessor();

    void prepareToPlay (double sampleRate, int maximumBlockSize) override;
    void releaseResources() override;
    void reset() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using vox::suite::SuiteProcessor::processBlock;
    double getTailLengthSeconds() const override;
    juce::AudioProcessorEditor* createEditor() override;

    juce::AudioProcessorValueTreeState& state() noexcept { return parameters_; }

    // Message thread: plays pad `pad` at the start of the next block.
    // Returns false when the queue is full.
    bool audition (int pad, std::uint8_t velocity);

    // Pads that sounded since the last call (bit n = pad n), host MIDI and
    // sequencer hits; for the editor's pad lights.
    std::uint32_t takeHitMask() noexcept { return hitMask_.exchange (0, std::memory_order_acq_rel); }
    double lastPpq() const noexcept { return lastPpq_.load (std::memory_order_relaxed); }
    bool transportPlaying() const noexcept { return playing_.load (std::memory_order_relaxed); }
    std::uint64_t droppedMidiEvents() const noexcept { return droppedMidi_.load (std::memory_order_relaxed); }

    static constexpr std::size_t midiCapacity = 4096;   // events per host block

protected:
    void writeProductState (std::vector<preset::Section>&) const override;
    ProductRestore readProductState (const preset::StateDocument&, juce::String&) override;
    bool isProductSection (preset::Tag tag) const override { return tag == parameterSectionTag; }

private:
    juce::AudioProcessorValueTreeState parameters_;
    DrumEngine engine_;

    struct Binding {
        std::atomic<float>* value = nullptr;   // plugin parameter, or null (engine default)
        float fallback = 0.0f;
    };
    std::vector<Binding> bindings_;            // per engine parameter index
    std::vector<float> sent_;                  // values the engine has
    std::vector<ParameterChange> changes_;
    std::vector<MidiEvent> events_;
    std::atomic<float>* pattern_ = nullptr;

    struct Audition { std::uint8_t pad = 0, velocity = 0; };
    juce::AbstractFifo auditionFifo_ { 64 };
    std::array<Audition, 64> auditions_ {};

    std::atomic<bool> panicPending_ { false };
    std::atomic<std::uint32_t> hitMask_ { 0 };
    std::atomic<double> lastPpq_ { 0.0 };
    std::atomic<bool> playing_ { false };
    std::atomic<std::uint64_t> droppedMidi_ { 0 };
    std::array<int, 128> padForNote_ {};

    // Per plugin parameter: the plain value last loaded from a state and the
    // value the parameter then held (state lock).
    struct Restored { float loaded = 0.0f, held = 0.0f; };
    std::vector<Restored> restored_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumsProcessor)
};

} // namespace vox::drums
