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
inline constexpr preset::Tag listenSectionTag = preset::makeTag ("LIST");
inline constexpr int maxUserPrograms = 128;
inline constexpr int previewEventCap = 32;
// UBNK v1 is name, category, and parameter floats. v2 appends the extra
// MODX routes (indices 4-31) so a user program is a whole patch. v1 still loads.
inline constexpr std::uint32_t userBankVersion = 2u;

struct UserProgram {
    struct Route {
        int source = 0;
        int dest = 0;
        float amount = 0.0f; // bipolar, same as the MODX section. 0 is off.
    };
    juce::String name;
    int category = 0;
    std::vector<float> values;
    Route extra[2][extraRouteCount] {};
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
    [[nodiscard]] int factoryProgramAudition (int index) const;
    // Message thread. Publishes one immutable preview command. The audio
    // thread owns the sounding notes and the countdown. This does not write
    // the host parameters. `requestPreview` auditions the live patch on a
    // side engine. Factory and user preview audition that candidate instead.
    void requestPreview (int note) noexcept;
    // Same as requestPreview, but the side-chain delay and reverb are the
    // candidate's, not a fresh write of the host parameters. Measurement only.
    void requestPreviewWithFx (int note, float delayRate, float reverbAmount) noexcept;
    void requestFactoryPreview (int index) noexcept;
    bool requestUserPreview (int index);
    void cancelPreview() noexcept;

    // Local listening ledger. Status 1 reviewed, 2 rejected, 3 owner mark.
    // This never changes factoryProgramsHumanApproved.
    void setListeningMark (int index, int status);
    [[nodiscard]] int listeningMark (int index) const;
    [[nodiscard]] std::uint32_t listeningRenderHash (int index) const;

    // Test gate for the preview handoff. The audio thread snapshots the notes
    // it actually started, then waits, and only then sends their NoteOff.
    void armPreviewHandoffForTest() noexcept { previewHandoffPark_.store (1, std::memory_order_release); }
    [[nodiscard]] int previewHandoffPhase() const noexcept { return previewHandoffPark_.load (std::memory_order_acquire); }
    void releasePreviewHandoffForTest() noexcept { previewHandoffPark_.store (0, std::memory_order_release); }
    void forcePreviewReleaseForTest() noexcept { previewForceRelease_.store (1, std::memory_order_release); }
    [[nodiscard]] int previewClaimViolations() const noexcept { return previewClaimViolations_.load (std::memory_order_acquire); }
    [[nodiscard]] int previewHeldNote() const noexcept { return rtPreviewNote_; }
    [[nodiscard]] int previewSoundingCount() const noexcept { return rtSoundingCount_; }
    [[nodiscard]] int previewLastOff() const noexcept { return rtPreviewLastOff_; }
    [[nodiscard]] bool previewActive() const noexcept { return rtPreviewActive_; }
    [[nodiscard]] bool previewMonoGate (int part) const noexcept { return previewEngine_.monoGate (part); }
    [[nodiscard]] int previewActiveVoices() const noexcept { return previewEngine_.activeVoices(); }
    [[nodiscard]] int previewMonoNote (int part) const noexcept { return previewEngine_.monoNote (part); }
    [[nodiscard]] float previewMonoAccent (int part) const noexcept { return previewEngine_.monoAccent (part); }
    [[nodiscard]] int previewGlideSource (int part) const noexcept { return previewEngine_.glideSource (part); }
    [[nodiscard]] bool previewVaHeld (int part, int note) const noexcept { return previewEngine_.vaNoteHeld (part, note); }
    // Post-FX preview bus from the last callback, before the 0.30 trim.
    [[nodiscard]] float previewBusSample (int channel, int index) const noexcept;
    [[nodiscard]] bool previewBusValid() const noexcept { return previewBusReady_; }
    [[nodiscard]] float previewRouteAmount (int part, int index) const noexcept { return previewEngine_.routeAmount (part, index); }
    // Absolute sample offsets of the factory Hear score. Same events Hear plays.
    int factoryPreviewEventsForTest (int index, AuditionEvent* out, int capacity, int& tailSamples) const noexcept;

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
        return tag == engineSectionTag || tag == userBankTag || tag == phraseSectionTag
            || tag == modSectionTag || tag == listenSectionTag;
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
    struct PreviewFx {
        vstengine::effects::EffectSettings slot[vstengine::effects::EffectsChain::effectCount] {};
    };
    void readModSnapshot (ExtraRoutes& out) noexcept;
    void publishModLocked() noexcept;
    engine::BlockParams latchBlock();
    void runEngine (juce::AudioBuffer<float>& buffer, int count, const engine::BlockParams& block);
    bool insertEvent (const engine::Event& event, int& count);
    bool insertKeepingRelease (const engine::Event& event, int& count);
    engine::BlockParams mapPlain (const float* plain, const ExtraRoutes& routes) const;
    engine::BlockParams liveBlock (PreviewFx& fx) const;
    void publishPreview (const engine::BlockParams& params, const PreviewFx& fx, const engine::Event* events, int eventCount, int tailSamples) noexcept;
    bool copyPreview (engine::BlockParams& params, PreviewFx& fx, engine::Event* events, int& eventCount, int& generation, int& tailSamples) noexcept;
    void servicePreview (juce::AudioBuffer<float>& buffer, int frames);
    void adoptPreview (int generation, int tailSamples, const engine::Event* events, int eventCount, const engine::BlockParams& params, const PreviewFx& fx) noexcept;
    void queuePreviewOffs (engine::Event* play, int& playCount, const int* notes, int noteCount) noexcept;
    void queuePreviewScore (engine::Event* play, int& playCount, int frames) noexcept;
    void renderPreview (juce::AudioBuffer<float>& buffer, int frames, const engine::Event* play, int playCount);
    void applyUserRoutes (const UserProgram& program) noexcept;
    PreviewFx fxFromPlain (const float* plain) const noexcept;
    static PreviewFx bypassPreviewFx() noexcept;
    std::uint32_t renderListeningHash (int index) const;
    void publishLocked();
    void readSnapshot (PhraseSnapshot& out) noexcept;
    void collectPhrase (int frames, const PhraseSnapshot& snap, engine::Event* phraseEvents, int& phraseCount);
    juce::AudioProcessorValueTreeState parameters_;
    std::array<std::atomic<float>*, parameterCatalog.size()> raw_ {};
    engine::SynthEngine engine_;
    engine::SynthEngine previewEngine_;
    vstengine::effects::EffectsChain fx_;
    vstengine::effects::EffectsChain previewFx_;
    std::vector<float> previewMixL_;
    std::vector<float> previewMixR_;
    int previewCapacity_ = 0;
    bool previewBusReady_ = false;
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
    // Preview commands are immutable slots. The audio thread copies one while
    // it is Reading, then owns the notes it started. A later publish cannot
    // change that NoteOff, and finishing a command never clears a newer one.
    struct PreviewSlot {
        engine::BlockParams params {};
        PreviewFx fx {};
        engine::Event events[previewEventCap] {};
        int eventCount = 0;
        int generation = 0;
        int tailSamples = 0;
        int stamp = 0;
        int stampEnd = 0;
        std::atomic<int> state { slotFree };
    };
    mutable juce::CriticalSection previewLock_;
    PreviewSlot previewSlots_[3] {};
    std::atomic<int> previewPublished_ { 0 };
    std::atomic<int> previewGenPublished_ { 0 };
    int previewGeneration_ = 0;
    int previewRevision_ = 0;
    std::atomic<int> previewClaimViolations_ { 0 };
    std::atomic<int> previewHandoffPark_ { 0 };
    std::atomic<int> previewForceRelease_ { 0 };
    int rtPreviewGen_ = 0;
    int rtFinishedGen_ = 0;
    int rtPreviewNote_ = -1;
    int rtPreviewLastOff_ = -1;
    int rtSounding_[8] {};
    int rtSoundingCount_ = 0;
    bool rtPreviewActive_ = false;
    int rtPreviewPos_ = 0;
    int rtPreviewCursor_ = 0;
    int rtPreviewTail_ = 0;
    int rtPreviewEventCount_ = 0;
    engine::Event rtPreviewEvents_[previewEventCap] {};
    engine::BlockParams rtPreviewParams_ {};
    struct ListenMark {
        std::uint8_t status = 0;
        std::uint32_t hash = 0;
    };
    mutable juce::CriticalSection listenLock_;
    ListenMark listen_[factoryProgramCapacity] {};

    struct Restored { float loaded = 0.0f, held = 0.0f; };
    std::vector<Restored> restored_;
    std::vector<UserProgram> userPrograms_;
    juce::CriticalSection bankLock_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SynthesisProcessor)
};

} // namespace vox::syn
