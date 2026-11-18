#pragma once

#include "common/SuiteProcessor.h"
#include "engine/NoiseRiser.h"
#include "engine/ProgramStore.h"
#include "engine/Generator.h"
#include "engine/Effects.h"
#include "engine/OfflineRender.h"

namespace vox::fx {

inline constexpr preset::Tag fxParameterSectionTag = preset::makeTag ("FPRM");
inline constexpr preset::Tag fxFamilySectionTag = preset::makeTag ("FFAM");
inline constexpr preset::Tag fxGeneratorSectionTag = preset::makeTag ("FGEN");
inline constexpr preset::Tag fxMacroSectionTag = preset::makeTag ("FMAC");
inline constexpr preset::Tag fxMacroRouteSectionTag = preset::makeTag ("FMOD");
inline constexpr preset::Tag fxPatternEnableSectionTag = preset::makeTag ("FPEN");
inline constexpr preset::Tag fxPatternSectionTag = preset::makeTag ("FPAT");
inline constexpr preset::Tag fxEffectsSectionTag = preset::makeTag ("FXSL");
inline constexpr preset::Tag fxProgramSectionTag = preset::makeTag ("FPRG");

// Work-in-progress standalone FX instrument. Structural edits are published
// off the audio thread; host parameters remain independent of generation.
class FxProcessor final : public vox::suite::SuiteProcessor {
public:
    FxProcessor();
    void prepareToPlay (double sampleRate, int maximumBlockSize) override;
    void releaseResources() override;
    void reset() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using vox::suite::SuiteProcessor::processBlock;
    double getTailLengthSeconds() const override { return 33.0; }
    juce::AudioProcessorEditor* createEditor() override;
    juce::AudioParameterFloat& macroParameter(std::size_t index) noexcept { return *macros_[index]; }
    bool setMacroGainRoute(std::size_t index,float depth,float smoothing) {
        return programs_.editMacroConfig([=](macro::Config& config){return config.setGainRoute(index,depth,smoothing);});
    }
    bool setMotionGainRoute(mod::Source source,float depth,float smoothing) {
        return programs_.editMacroConfig([=](macro::Config& config){return config.setSourceGainRoute(source,depth,smoothing);});
    }
    macro::Config macroConfig() const { return programs_.snapshotState().macros; }
    juce::AudioParameterBool& patternEnabledParameter() noexcept { return *patternEnabled_; }
    juce::AudioParameterFloat& outputParameter() noexcept { return *output_; }
    juce::AudioParameterFloat& durationParameter() noexcept { return *duration_; }
    juce::AudioParameterFloat& effectWetParameter(std::size_t index) noexcept { return *effectWet_[index]; }
    juce::AudioParameterFloat& effectAmountParameter(std::size_t index) noexcept { return *effectAmount_[index]; }
    // Message-to-audio requests are bounded and coalesced, not a MIDI queue.
    void audition() noexcept { auditionPending_.store (true, std::memory_order_release); }
    void panic() noexcept { panicPending_.store (true, std::memory_order_release); }
    float outputPeak() const noexcept { return peak_.load (std::memory_order_relaxed); }
    // Controller methods only: these may lock/allocate and are never called
    // from the audio callback. Existing events retain their captured program.
    bool setFamily (int family) {
        if (family < 0 || family > 11) return false;
        return programs_.setLayerFamily (0, static_cast<NoiseFamily> (family));
    }
    int family() const { return static_cast<int> (programs_.snapshot().layers[0].family); }
    bool setProgram (const NoiseProgram& program) {
        if(!programs_.publish (program))return false;
        restoreEpoch_.fetch_add(1,std::memory_order_release);return true;
    }
    generator::ControllerState generatorState() const { return programs_.snapshotState().generator; }
    template<typename Edit> bool editGeneratorState(Edit edit) { return programs_.editGeneratorState(edit); }
    bool generateProgram (const generator::Settings&, const generator::Locks&);
    bool mutateProgram (std::uint32_t seed, const generator::Locks&);
    // Worker/controller thread only: captures the structural snapshot and
    // current host base parameters; may allocate and render for many seconds.
    bool renderProgramWav (offline::Settings settings, std::vector<std::uint8_t>& output,
                           const std::atomic<bool>* cancel=nullptr, std::atomic<float>* progress=nullptr) const;
    // Offline trigger pattern controller/export; not a running sequencer yet.
    bool setPattern(const pattern::Pattern& value) { return programs_.setPattern(value); }
    template<typename Edit> bool editPattern(Edit edit) { return programs_.editPattern(edit); }
    pattern::Pattern patternSnapshot() const { return programs_.snapshotState().pattern; }
    bool exportPatternMidi(double bpm, std::vector<std::uint8_t>& bytes) const {
        return pattern::exportMidi(patternSnapshot(),bpm,bytes);
    }
    bool setEffect (std::size_t index, effects::Settings settings);
    effects::Settings effect (std::size_t index) const;
    std::uint64_t effectsSnapshot() const { return programs_.snapshotState().effects; }
    NoiseProgram program() const { return programs_.snapshot(); }
    std::uint64_t programRestoreEpoch() const noexcept { return restoreEpoch_.load(std::memory_order_acquire); }
    template <typename Edit> bool editLayer (std::size_t index, Edit edit) { return programs_.editLayer (index, edit); }

protected:
    void writeProductState (std::vector<preset::Section>&) const override;
    ProductRestore readProductState (const preset::StateDocument&, juce::String&) override;
    bool isProductSection (preset::Tag tag) const override { return tag == fxParameterSectionTag || tag == fxFamilySectionTag || tag == fxProgramSectionTag || tag == fxEffectsSectionTag || tag == fxPatternSectionTag || tag == fxPatternEnableSectionTag || tag == fxMacroSectionTag || tag == fxMacroRouteSectionTag || tag == fxGeneratorSectionTag; }

private:
    NoiseRiser engine_;
    effects::Chain effects_;
    macro::Runtime macroRuntime_;
    macro::Config audioMacroConfig_;
    std::uint64_t audioEffects_ = effects::defaults(); // audio thread only
    ProgramStore programs_;
    std::atomic<std::uint64_t> restoreEpoch_ { 0 };
    NoiseProgram audioProgram_;
    pattern::Pattern audioPattern_;
    pattern::FixedSchedule audioPatternSchedule_;
    std::uint64_t programSequence_ = 0; // audio thread only
    juce::AudioParameterFloat* output_ = nullptr; // owned by AudioProcessor
    juce::AudioParameterFloat* duration_ = nullptr;
    juce::AudioParameterBool* patternEnabled_ = nullptr;
    std::array<juce::AudioParameterFloat*,4> effectWet_{}, effectAmount_{};
    std::array<juce::AudioParameterFloat*,8> macros_{};
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
