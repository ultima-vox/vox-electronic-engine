#pragma once

#include "common/SuiteProcessor.h"
#include "effects/EffectsChain.h"
#include "engine/SynthEngine.h"
#include "Parameters.h"
#include "PhraseSchedule.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace vox::syn {

inline constexpr preset::Tag engineSectionTag = preset::makeTag ("ENG1");
inline constexpr preset::Tag userBankTag = preset::makeTag ("UBNK");
inline constexpr preset::Tag phraseSectionTag = preset::makeTag ("PHRS");
inline constexpr preset::Tag modSectionTag = preset::makeTag ("MODX");
inline constexpr int maxUserPrograms = 128;

struct UserProgram {
    juce::String name;
    int category = 0;
    std::vector<float> values;
};

// SYN-01: two parts (Psy Bass, Acid, VA), one 16-voice allocator, ENG1 state.
class SynthesisProcessor final : public vox::suite::SuiteProcessor {
public:
    SynthesisProcessor();

    void prepareToPlay (double sampleRate, int maximumBlockSize) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    // Native phrase audition. Not host MIDI: slide and accent ride on the event.
    void audition (juce::AudioBuffer<float>&, const AuditionEvent* events, int eventCount);
    using vox::suite::SuiteProcessor::processBlock;
    double getTailLengthSeconds() const override;
    juce::AudioProcessorEditor* createEditor() override;

    juce::AudioProcessorValueTreeState& parameters() noexcept { return parameters_; }

    // Tests, same thread as processBlock, after it returns.
    [[nodiscard]] int activeVoices() const noexcept { return engine_.activeVoices(); }
    [[nodiscard]] int monoNote (int part) const noexcept { return engine_.monoNote (part); }
    [[nodiscard]] bool monoGate (int part) const noexcept { return engine_.monoGate (part); }
    [[nodiscard]] double monoFrequency (int part) const noexcept { return engine_.monoFrequency (part); }
    [[nodiscard]] float monoAccent (int part) const noexcept { return engine_.monoAccent (part); }
    [[nodiscard]] int glideSource (int part) const noexcept { return engine_.glideSource (part); }
    [[nodiscard]] double monoPhase (int part) const noexcept { return engine_.monoPhase (part); }
    [[nodiscard]] int vaFadeLeft() const noexcept { return engine_.vaFadeLeft(); }
    [[nodiscard]] bool vaNoteHeld (int part, int note) const noexcept { return engine_.vaNoteHeld (part, note); }
    [[nodiscard]] std::uint64_t droppedMidi() const noexcept { return droppedMidi_.load (std::memory_order_relaxed); }

    [[nodiscard]] int userProgramCount() const;
    [[nodiscard]] juce::String userProgramName (int index) const;
    [[nodiscard]] int userProgramCategory (int index) const;
    bool saveUserProgram (const juce::String& name, int category);
    bool loadUserProgram (int index);
    [[nodiscard]] int factoryProgramCount() const noexcept;
    [[nodiscard]] juce::String factoryProgramName (int index) const;
    [[nodiscard]] int factoryProgramRole (int index) const;
    [[nodiscard]] juce::String factoryProgramSearch (int index) const;
    bool loadFactoryProgram (int index);

    void setPhraseIndex (int index);
    [[nodiscard]] int phraseIndex() const;
    [[nodiscard]] bool phrasePlaying() const noexcept { return phrasePlaying_.load (std::memory_order_acquire); }
    void setPhrasePlaying (bool playing);
    // Forced clock is for tests. Otherwise a playing host wins, else the internal 120 bpm clock.
    void setPhraseClock (bool forced, bool hostPlaying, double ppq, double bpm) noexcept;
    [[nodiscard]] int phraseNoteCount() const;
    [[nodiscard]] PhraseNote phraseNote (int index) const;
    void setPhraseNote (int index, PhraseNote note);
    [[nodiscard]] std::vector<std::byte> exportCurrentPhrase() const;

    // Routes 0-3 are the existing host ids. Routes 4-31 live in the MODX
    // section. amount01 is the host value: 0.5 is off. The audio thread adds
    // the route to the base and never writes the base back.
    void setModRoute (int part, int route, int source, int dest, float amount01);
    void getModRoute (int part, int route, int& source, int& dest, float& amount01);

    // Test gate. The audio thread loads the published slot, then waits until released,
    // and only then claims it. A writer can publish twice inside that gap.
    void armPhraseHandoffForTest() noexcept { phraseHandoffPark_.store (1, std::memory_order_release); }
    [[nodiscard]] int phraseHandoffPhase() const noexcept { return phraseHandoffPark_.load (std::memory_order_acquire); }
    void releasePhraseHandoffForTest() noexcept { phraseHandoffPark_.store (0, std::memory_order_release); }
    [[nodiscard]] int phraseClaimViolations() const noexcept { return phraseClaimViolations_.load (std::memory_order_acquire); }

    // Test gate for the MODX slot handoff. Same shape as the phrase gate.
    void armModHandoffForTest() noexcept { modHandoffPark_.store (1, std::memory_order_release); }
    [[nodiscard]] int modHandoffPhase() const noexcept { return modHandoffPark_.load (std::memory_order_acquire); }
    void releaseModHandoffForTest() noexcept { modHandoffPark_.store (0, std::memory_order_release); }
    [[nodiscard]] int modClaimViolations() const noexcept { return modClaimViolations_.load (std::memory_order_acquire); }

protected:
    void writeProductState (std::vector<preset::Section>&) const override;
    ProductRestore readProductState (const preset::StateDocument&, juce::String&) override;
    bool isProductSection (preset::Tag tag) const override
    {
        return tag == engineSectionTag || tag == userBankTag || tag == phraseSectionTag || tag == modSectionTag;
    }

private:
    struct ExtraRoutes {
        struct Row {
            int source = 0;
            int dest = 0;
            float amount = 0.0f;
        };
        Row row[2][extraRouteCount] {};
    };
    void readModSnapshot (ExtraRoutes& out) noexcept;
    void publishModLocked() noexcept;
    engine::BlockParams latchBlock();
    void runEngine (juce::AudioBuffer<float>& buffer, int count, const engine::BlockParams& block);
    bool insertEvent (const engine::Event& event, int& count);
    void publishLocked();
    void readSnapshot (PhraseSnapshot& out) noexcept;
    void collectPhrase (int frames, const PhraseSnapshot& snap, engine::Event* phraseEvents, int& phraseCount);
    juce::AudioProcessorValueTreeState parameters_;
    std::array<std::atomic<float>*, parameterCatalog.size()> raw_ {};
    engine::SynthEngine engine_;
    // Writer-side copy. Non-RT threads take modLock_, edit this, then publish
    // a slot. The audio thread never reads this object.
    mutable juce::CriticalSection modLock_;
    mutable ExtraRoutes modEdit_ {};
    struct ModSlot {
        ExtraRoutes routes {};
        int stamp = 0;
        int stampEnd = 0;
        std::atomic<int> state { 0 };
    };
    ModSlot modSlots_[3] {};
    std::atomic<int> modPublished_ { 0 };
    int modRevision_ = 0;
    ExtraRoutes modLast_ {};
    std::atomic<int> modClaimViolations_ { 0 };
    std::atomic<int> modHandoffPark_ { 0 };
    double hostBpm_ = 120.0;
    bool hostBpmValid_ = false;
    vstengine::effects::EffectsChain fx_;
    engine::Event events_[engine::maxEvents] {};
    bool prepared_ = false;
    double sampleRate_ = 48000.0;
    // A slot is Free, Ready, Reading or Writing. The snapshot bytes are written
    // only while the slot is Writing, and copied only while it is Reading.
    // Those states are exclusive, so a slot is never modified while the audio
    // thread may copy it. Failed claims retry. The last intact copy is kept.
    enum SlotState { slotFree = 0, slotReady = 1, slotReading = 2, slotWriting = 3 };
    juce::CriticalSection phraseLock_;
    int phraseIndex_ = 0;
    bool phraseEdited_ = false;
    int phraseLength_ = 3840;
    std::vector<PhraseNote> phraseEdit_;
    struct PhraseSlot {
        PhraseSnapshot snap {};
        std::atomic<int> state { slotFree };
    };
    PhraseSlot phraseSlots_[3] {};
    std::atomic<int> phrasePublished_ { 0 };
    int phraseRevision_ = 0;
    PhraseSnapshot rtLast_ {};
    std::atomic<int> phraseClaimViolations_ { 0 };
    std::atomic<int> phraseHandoffPark_ { 0 };
    std::atomic<bool> phrasePlaying_ { false };
    std::atomic<int> phraseStopEpoch_ { 0 };
    std::atomic<int> phraseClockEpoch_ { 0 };
    std::atomic<bool> clockForced_ { false };
    std::atomic<bool> clockHost_ { false };
    std::atomic<std::uint64_t> clockPpqBits_ { 0 };
    std::atomic<std::uint64_t> clockBpmBits_ { 0 };
    int rtStopEpoch_ = 0;
    int rtClockEpoch_ = 0;
    std::int64_t rtPlayed_ = 0;
    std::uint8_t phraseHeld_[128] {};
    std::atomic<std::uint64_t> droppedMidi_ { 0 };

    struct Restored { float loaded = 0.0f, held = 0.0f; };
    std::vector<Restored> restored_;
    std::vector<UserProgram> userPrograms_;
    juce::CriticalSection bankLock_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SynthesisProcessor)
};

} // namespace vox::syn
