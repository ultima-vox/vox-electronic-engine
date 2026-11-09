#pragma once

#include "common/SuiteProcessor.h"

#include "DrumsParameters.h"
#include "KitStore.h"
#include "PatternStore.h"
#include "SampleLibrary.h"
#include "engine/KitEngine.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace vox::drums {

// Product state section of schema 1: the plugin parameter values by ID.
inline constexpr preset::Tag parameterSectionTag = preset::makeTag ("DPRM");

// VOX Drums (DRM-03b): 16 pads, each any of 8 models with two sample
// players, played by host MIDI on assignable notes and/or the internal
// sequencer with the factory patterns; stereo main output.
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
    SampleLibrary& samples() noexcept { return samples_; }
    KitStore& kitStore() noexcept { return store_; }
    PatternStore& patterns() noexcept { return patternStore_; }
    // Tests: the engine, to read what the last processBlock triggered
    // (same thread as processBlock, after it returns).
    const kit::KitEngine& engineForTesting() const noexcept { return engine_; }

    // Message thread: pattern slot `slot` as a Standard MIDI File with the
    // current swing, accent, lanes and pad notes (what the sequencer plays
    // from song position 0), `passes` cycles at `bpm`.
    juce::MemoryBlock exportPatternMidi (int slot, int passes, double bpm);

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

    // Tests: the voice pool size applied at the next prepareToPlay (the
    // product uses kit::maxVoices; the DRM-00 reference used 16).
    void setVoiceLimitForTesting (int voices) noexcept { voiceLimit_ = voices; }

protected:
    void writeProductState (std::vector<preset::Section>&) const override;
    ProductRestore readProductState (const preset::StateDocument&, juce::String&) override;
    bool isProductSection (preset::Tag tag) const override
    {
        return tag == parameterSectionTag || tag == sampleSectionTag || tag == padSectionTag || tag == sequenceSectionTag;
    }

private:
    juce::AudioProcessorValueTreeState parameters_;
    kit::KitEngine engine_;
    KitStore store_;
    SampleLibrary samples_;
    bool prepared_ = false;
    int voiceLimit_ = kit::maxVoices;

    // Host parameters, read once per block into live_.
    struct PadRefs { std::atomic<float>* level; std::atomic<float>* tune; std::atomic<float>* decay;
                     std::atomic<float>* pan; std::atomic<float>* cutoff; std::atomic<float>* mute;
                     std::atomic<float>* lane; };
    std::array<PadRefs, padCount> padRefs_ {};
    std::atomic<float>* volume_ = nullptr;
    std::atomic<float>* velocity_ = nullptr;
    std::atomic<float>* choke_ = nullptr;
    std::atomic<float>* mode_ = nullptr;
    std::atomic<float>* swing_ = nullptr;
    std::atomic<float>* accent_ = nullptr;
    std::atomic<float>* pattern_ = nullptr;
    kit::KitSettings live_;                    // audio thread
    std::vector<kit::Event> events_;
    PatternStore patternStore_;
    PatternStore::Live livePattern_;   // audio thread

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
