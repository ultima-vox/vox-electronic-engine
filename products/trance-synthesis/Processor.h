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
    [[nodiscard]] int phraseIndex() const noexcept { return phraseIndex_; }
    [[nodiscard]] bool phrasePlaying() const noexcept { return phrasePlaying_; }
    void setPhrasePlaying (bool playing);
    // Forced clock is for tests. Otherwise a playing host wins, else the internal 120 bpm clock.
    void setPhraseClock (bool forced, bool hostPlaying, double ppq, double bpm) noexcept;
    [[nodiscard]] int phraseNoteCount() const;
    [[nodiscard]] PhraseNote phraseNote (int index) const;
    void setPhraseNote (int index, PhraseNote note);
    [[nodiscard]] std::vector<std::byte> exportCurrentPhrase() const;

protected:
    void writeProductState (std::vector<preset::Section>&) const override;
    ProductRestore readProductState (const preset::StateDocument&, juce::String&) override;
    bool isProductSection (preset::Tag tag) const override
    {
        return tag == engineSectionTag || tag == userBankTag;
    }

private:
    engine::BlockParams latchBlock();
    void runEngine (juce::AudioBuffer<float>& buffer, int count, const engine::BlockParams& block);
    void insertEvent (const engine::Event& event, int& count);
    void addPhraseEvents (int frames, int& count);
    void phraseNotes (const PhraseNote*& notes, int& count, int& lengthTicks) const;
    juce::AudioProcessorValueTreeState parameters_;
    std::array<std::atomic<float>*, parameterCatalog.size()> raw_ {};
    engine::SynthEngine engine_;
    vstengine::effects::EffectsChain fx_;
    engine::Event events_[engine::maxEvents] {};
    bool prepared_ = false;
    double sampleRate_ = 48000.0;
    int phraseIndex_ = 0;
    bool phrasePlaying_ = false;
    bool phraseStop_ = false;
    bool phraseEdited_ = false;
    bool clockForced_ = false;
    bool clockHost_ = false;
    double clockPpq_ = 0.0;
    double clockBpm_ = 120.0;
    std::int64_t phrasePlayed_ = 0;
    int phraseLength_ = 0;
    std::vector<PhraseNote> phraseEdit_;
    std::atomic<std::uint64_t> droppedMidi_ { 0 };

    struct Restored { float loaded = 0.0f, held = 0.0f; };
    std::vector<Restored> restored_;
    std::vector<UserProgram> userPrograms_;
    juce::CriticalSection bankLock_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SynthesisProcessor)
};

} // namespace vox::syn
