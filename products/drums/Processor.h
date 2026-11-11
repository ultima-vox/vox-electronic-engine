#pragma once

#include "common/SuiteProcessor.h"

#include "DrumsParameters.h"
#include "Generator.h"
#include "KitStore.h"
#include "PatternStore.h"
#include "SampleLibrary.h"
#include "engine/KitEngine.h"
#include "engine/SendFx.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace vox::drums {

// Product state section of schema 1: the plugin parameter values by ID.
inline constexpr preset::Tag parameterSectionTag = preset::makeTag ("DPRM");

// DPRM: plain parameter values by host ID. decode is strict about the
// layout and rejects NaN and duplicates; unknown IDs are kept in the map
// (the caller ignores them).
bool decodeParameterValues (const preset::Section& section, std::map<std::string, float>& values, juce::String& error);
preset::Section encodeParameterValues (const std::map<std::string, float>& values);

// Editor state saved with the project (section DUIS): where the user was.
inline constexpr preset::Tag uiStateSectionTag = preset::makeTag ("DUIS");
struct UiState {
    std::uint8_t page = 0;         // 0..7: PERFORMANCE .. ADVANCED
    std::uint8_t pad = 0;          // selected pad 0..15
    std::uint8_t songBar = 0;      // selected song bar 0..63
    std::uint8_t patternBar = 0;   // shown pattern bar 0..3
    std::uint8_t exportScope = 0;  // 0 Pattern, 1 Entire Song
    bool operator== (const UiState& o) const noexcept
    {
        return page == o.page && pad == o.pad && songBar == o.songBar && patternBar == o.patternBar && exportScope == o.exportScope;
    }
};
preset::Section encodeUiState (const UiState&);
bool decodeUiState (const preset::Section&, UiState& out, juce::String& error);

// What a MIDI export writes, frozen on the message thread at the click or
// at the start of the drag (DRM-05): both paths render the same snapshot
// with the same code, so their files are byte-identical.
struct ExportSnapshot {
    enum class Scope { pattern, song };
    Scope scope = Scope::pattern;
    int slot = 0, passes = 1;
    double bpm = 120.0;
    kit::StepContext context;
    std::array<std::uint8_t, kit::padCount> notes {};
    StoredPattern pattern;                                                // scope pattern
    std::array<std::shared_ptr<const BarCells>, kit::maxSongBars> bars;   // scope song: resolved bars (null = empty)
    int songLength = 16;
    juce::String trackName;
};

// VOX Drums: 16 pads, each any of 8 models with two sample players, played
// by host MIDI on assignable notes and/or the internal sequencer (Pattern or
// Song mode).
//
// Output contract (DRM-06): 9 host-visible stereo buses, Main and Out 2..9
// (18 channels). Out 2..9 are disabled by default; the host enables them.
// Tap points: a pad's signal, post level/pan/mute, goes to exactly one dry
// destination (Main or its output bus) and, post-fader, to Send 1 (delay)
// and Send 2 (reverb). Both returns go to Main only. A pad routed to a bus
// the host has disabled plays on Main. The two send pairs are internal
// buffers; they are never host buses.
class DrumsProcessor final : public vox::suite::SuiteProcessor {
public:
    DrumsProcessor();

    void prepareToPlay (double sampleRate, int maximumBlockSize) override;
    void releaseResources() override;
    void reset() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using vox::suite::SuiteProcessor::processBlock;
    double getTailLengthSeconds() const override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    juce::AudioProcessorEditor* createEditor() override;

    juce::AudioProcessorValueTreeState& state() noexcept { return parameters_; }
    SampleLibrary& samples() noexcept { return samples_; }
    KitStore& kitStore() noexcept { return store_; }
    PatternStore& patterns() noexcept { return patternStore_; }
    // Message thread: undo/redo of pattern edits. patternEpoch() changes
    // with every project restore; the editor clears the history then.
    PatternHistory& patternHistory() noexcept { return patternHistory_; }
    std::uint64_t patternEpoch() const noexcept { return patternEpoch_.load(); }
    // The host tempo of the last block (120 before any), for MIDI export.
    double lastBpm() const noexcept { return lastBpm_.load (std::memory_order_relaxed); }
    // Tests: the engine, to read what the last processBlock triggered
    // (same thread as processBlock, after it returns).
    const kit::KitEngine& engineForTesting() const noexcept { return engine_; }

    // Message thread: pattern slot `slot` as a Standard MIDI File with the
    // current swing, accent, lanes and pad notes (what the sequencer plays
    // from song position 0), `passes` cycles at `bpm`.
    juce::MemoryBlock exportPatternMidi (int slot, int passes, double bpm);
    // Message thread: freezes what an export of `scope` writes now (pattern:
    // the slot the sequencer plays, `passes` cycles; song: the entire song,
    // one pass), and renders a frozen snapshot. Deterministic: equal
    // snapshots give equal bytes.
    std::shared_ptr<const ExportSnapshot> captureExport (ExportSnapshot::Scope scope, int passes = 1);
    static juce::MemoryBlock renderExport (const ExportSnapshot&);

    //== generator (DRM-05), message thread
    GeneratorSettings generator() const;
    void setGenerator (const GeneratorSettings&);
    UiState uiState() const;
    void setUiState (const UiState&);
    // Runs the generator on the slot the sequencer plays (pattern, mutate
    // `laneMask`, fill in `bar`, variation: B from A) or on everything
    // (song). With `newSeed` the seed advances first, so every press gives
    // new data; without it the stored seed reproduces its result. One undo
    // step in patternHistory(). Returns false when nothing changed.
    enum class Generate { pattern, song, mutate, fill, variation };
    bool generate (Generate what, std::uint32_t laneMask = 0xffffu, bool newSeed = true, int bar = -1);
    int currentSlot() const;

    //== mixer (DRM-06), any thread
    // Peak since the last call: pad post-fader, Main after the returns.
    float takePadMeter (int pad) noexcept { return padMeter_[static_cast<std::size_t> (pad & 15)].exchange (0.0f, std::memory_order_relaxed); }
    float takeMainMeter (int channel) noexcept { return mainMeter_[static_cast<std::size_t> (channel & 1)].exchange (0.0f, std::memory_order_relaxed); }
    // Bit n: output bus n (0 Main, 1..8 = Out 2..9) was enabled in the last block.
    std::uint32_t activeOutputs() const noexcept { return activeOutputs_.load (std::memory_order_relaxed); }
    int activeVoices() const noexcept { return activeVoices_.load (std::memory_order_relaxed); }
    // The song bar (0-based) playing in the last block, -1 outside Song mode.
    int songBarPlaying() const noexcept { return songBar_.load (std::memory_order_relaxed); }

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
    void setVoiceLimitForTesting (int voices) noexcept { voiceLimitOverride_ = voices; }

protected:
    void writeProductState (std::vector<preset::Section>&) const override;
    ProductRestore readProductState (const preset::StateDocument&, juce::String&) override;
    bool isProductSection (preset::Tag tag) const override
    {
        return tag == parameterSectionTag || tag == sampleSectionTag || tag == padSectionTag || tag == sequenceSectionTag
            || tag == songSectionTag || tag == generatorSectionTag || tag == uiStateSectionTag;
    }

private:
    juce::AudioProcessorValueTreeState parameters_;
    kit::KitEngine engine_;
    KitStore store_;
    SampleLibrary samples_;
    bool prepared_ = false;
    int voiceLimitOverride_ = 0;        // tests; 0 = the kit's setting
    int voiceLimitApplied_ = 0;         // audio thread (and prepareToPlay)
    kit::SendFx fx_;
    // Internal engine buses: kit::auxBuses stereo pairs of scratchFrames_
    // samples each (sends, then Out 2..9), sized in prepareToPlay.
    std::vector<float> scratch_;
    std::array<float*, kit::auxBuses * 2> scratchPointers_ {};
    int scratchFrames_ = 0;
    std::array<std::atomic<float>, padCount> padMeter_ {};
    std::array<std::atomic<float>, 2> mainMeter_ {};
    std::atomic<std::uint32_t> activeOutputs_ { 1 };
    std::atomic<int> activeVoices_ { 0 };
    std::atomic<int> songBar_ { -1 };

    // Host parameters, read once per block into live_.
    struct PadRefs { std::atomic<float>* level; std::atomic<float>* tune; std::atomic<float>* decay;
                     std::atomic<float>* pan; std::atomic<float>* cutoff; std::atomic<float>* mute;
                     std::atomic<float>* lane; std::atomic<float>* send1; std::atomic<float>* send2;
                     std::atomic<float>* solo; };
    std::array<PadRefs, padCount> padRefs_ {};
    std::atomic<float>* volume_ = nullptr;
    std::atomic<float>* velocity_ = nullptr;
    std::atomic<float>* choke_ = nullptr;
    std::atomic<float>* mode_ = nullptr;
    std::atomic<float>* swing_ = nullptr;
    std::atomic<float>* accent_ = nullptr;
    std::atomic<float>* pattern_ = nullptr;
    std::atomic<float>* delayTime_ = nullptr;
    std::atomic<float>* delayFeedback_ = nullptr;
    std::atomic<float>* delayTone_ = nullptr;
    std::atomic<float>* delayLevel_ = nullptr;
    std::atomic<float>* reverbSize_ = nullptr;
    std::atomic<float>* reverbDamp_ = nullptr;
    std::atomic<float>* reverbLevel_ = nullptr;
    kit::KitSettings live_;                    // audio thread
    std::vector<kit::Event> events_;
    PatternStore patternStore_;
    PatternHistory patternHistory_;
    std::atomic<std::uint64_t> patternEpoch_ { 0 };
    std::atomic<double> lastBpm_ { 120.0 };
    PatternStore::Live livePattern_;   // audio thread
    PatternStore::SongLive liveSong_;  // audio thread
    mutable juce::SpinLock settingsLock_;   // generator_, ui_
    GeneratorSettings generator_;
    UiState ui_;

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
