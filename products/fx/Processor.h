#pragma once

#include "common/SuiteProcessor.h"
#include "engine/NoiseRiser.h"
#include "engine/ProgramStore.h"
#include "engine/Generator.h"
#include "engine/Effects.h"
#include "engine/OfflineRender.h"
#include "engine/SampleController.h"
#include "engine/Transport.h"
#include "engine/ComparisonState.h"
#include "engine/PortablePreset.h"
#include "engine/Preview.h"

namespace vox::fx {

inline constexpr preset::Tag fxParameterSectionTag = preset::makeTag ("FPRM");
inline constexpr preset::Tag fxFamilySectionTag = preset::makeTag ("FFAM");
inline constexpr preset::Tag fxGeneratorSectionTag = preset::makeTag ("FGEN");
inline constexpr preset::Tag fxMacroSectionTag = preset::makeTag ("FMAC");
inline constexpr preset::Tag fxMacroRouteSectionTag = preset::makeTag ("FMOD");
inline constexpr preset::Tag fxPatternEnableSectionTag = preset::makeTag ("FPEN");
inline constexpr preset::Tag fxPatternSectionTag = preset::makeTag ("FPAT");
inline constexpr preset::Tag fxEffectsSectionTag = preset::makeTag ("FXSL");
inline constexpr preset::Tag fxSampleSectionTag = preset::makeTag ("FSMP");
inline constexpr preset::Tag fxProgramSectionTag = preset::makeTag ("FPRG");

// Work-in-progress standalone FX instrument. Structural edits are published
// off the audio thread; host parameters remain independent of generation.
class FxProcessor final : public vox::suite::SuiteProcessor {
public:
    FxProcessor();
    void setStateInformation(const void*,int) override;
    void prepareToPlay (double sampleRate, int maximumBlockSize) override;
    void releaseResources() override;
    void reset() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using vox::suite::SuiteProcessor::processBlock;
    double getTailLengthSeconds() const override { return 41.0; }
    juce::AudioProcessorEditor* createEditor() override;
    juce::AudioParameterFloat& macroParameter(std::size_t index) noexcept { return *macros_[index]; }
    bool setMacroGainRoute(std::size_t index,float depth,float smoothing) {
        return programs_.editMacroConfig([=](macro::Config& config){return config.setGainRoute(index,depth,smoothing);});
    }
    bool setMotionGainRoute(mod::Source source,float depth,float smoothing) {
        return programs_.editMacroConfig([=](macro::Config& config){return config.setSourceGainRoute(source,depth,smoothing);});
    }
    bool setMotionRoute(mod::Source source,mod::Destination destination,float depth,float smoothing) {
        return programs_.editMacroConfig([=](macro::Config& config){return config.setRoute(source,destination,depth,smoothing);});
    }
    ProgramStore::VersionedMotion versionedMotion() const {return programs_.versionedMotion();}
    bool replaceMotionIfCurrent(const ProgramStore::VersionedMotion& expected,const macro::Config& value) {
        return programs_.replaceMotionIfCurrent(expected,value);
    }
    macro::Config macroConfig() const { return programs_.snapshotState().macros; }
    juce::AudioParameterBool& patternEnabledParameter() noexcept { return *patternEnabled_; }
    bool musicalClockUnavailable() const noexcept { return musicalClockUnavailable_.load(std::memory_order_relaxed); }
    bool patternUnavailable() const noexcept { return patternUnavailable_.load(std::memory_order_relaxed); }
    juce::AudioParameterFloat& outputParameter() noexcept { return *output_; }
    juce::AudioParameterFloat& durationParameter() noexcept { return *duration_; }
    juce::AudioParameterFloat& effectWetParameter(std::size_t index) noexcept { return *effectWet_[index]; }
    juce::AudioParameterFloat& effectAmountParameter(std::size_t index) noexcept { return *effectAmount_[index]; }
    // Message-to-audio requests are bounded and coalesced, not a MIDI queue.
    void audition() noexcept { auditionPending_.store (true, std::memory_order_release); }
    void panic() noexcept { previewBus_.stop();panicPending_.store (true, std::memory_order_release); }
    float outputPeak() const noexcept { return peak_.load (std::memory_order_relaxed); }
    bool consumeClip() noexcept { return clipLatched_.exchange(false,std::memory_order_relaxed); }
    struct AudioTelemetry { unsigned events=0,grains=0;bool clipped=false;std::uint32_t dropped=0; };
    AudioTelemetry audioTelemetry() const noexcept {
        const auto value=telemetry_.load(std::memory_order_relaxed);
        return {value&15u,(value>>4)&127u,(value&(1u<<11))!=0,droppedEvents_.load(std::memory_order_relaxed)};
    }
    // Controller methods only: these may lock/allocate and are never called
    // from the audio callback. Existing events retain their captured program.
    bool setFamily (int family) {
        if (family < 0 || family > 11) return false;
        return programs_.setLayerFamily (0, static_cast<NoiseFamily> (family));
    }
    bool setEventBars (int bars) {
        if (bars != 0 && bars != 1 && bars != 2 && bars != 4 && bars != 8 && bars != 16) return false;
        return programs_.editProgram([bars](NoiseProgram& p) { p.bars=static_cast<std::uint8_t>(bars); return true; });
    }
    bool setPlayback(PlayMode mode,TriggerPolicy policy) {
        if(static_cast<unsigned>(mode)>static_cast<unsigned>(PlayMode::transition)||
           static_cast<unsigned>(policy)>static_cast<unsigned>(TriggerPolicy::replace))return false;
        return programs_.editProgram([=](NoiseProgram& p){p.mode=mode;p.triggerPolicy=policy;return true;});
    }
    int family() const { return static_cast<int> (programs_.snapshot().layers[0].family); }
    bool setProgram (const NoiseProgram& program) {
        return programs_.publish (program);
    }
    // Message/controller thread only. A/B snapshots are stored in the project and
    // deliberately recall all host parameter bases with host notification.
    bool storeComparison(std::size_t slot);
    bool recallComparison(std::size_t slot);
    portable::Sound portableSnapshot() const;
    bool encodePortablePreset(std::vector<std::byte>&) const;
    bool applyPortableSound(const portable::Sound&);
    bool restorePortablePreset(std::span<const std::byte>);
    bool loadReviewCandidate(std::size_t index);
    offline::Capture capturePresetPreview(const portable::Sound&) const;
    preview::Bus& previewBus() noexcept {return previewBus_;}
    generator::ControllerState generatorState() const { return programs_.snapshotState().generator; }
    template<typename Edit> bool editGeneratorState(Edit edit) { return programs_.editGeneratorState(edit); }
    bool generateProgram (const generator::Settings&, const generator::Locks&);
    bool mutateProgram (std::uint32_t seed, const generator::Locks&);
    bool generateSound(bool mutation);
    // These operations lock or perform file I/O; never call from audio.
    bool importSample(const juce::File& path) { return samples_.import(path); }
    bool relinkSample(const juce::File& path, const sample::Reference& expected) { return samples_.relink(path,expected); }
    bool useBuiltinSample() { return samples_.restore({}); }
    sample::Controller::Status sampleStatus() const { return samples_.status(); }
    sample::Controller::Snapshot sampleSnapshot() const { return samples_.snapshot(); }
    sample::Reference sampleReference() const { return samples_.reference(); }
    bool waitForSample(std::chrono::milliseconds timeout) { return samples_.waitUntilSettled(timeout); }
    // Worker/controller thread only: captures the structural snapshot and
    // current host base parameters; may allocate and render for many seconds.
    offline::Capture captureProgramWav (offline::Settings settings) const;
    bool renderProgramWav (offline::Settings settings, std::vector<std::uint8_t>& output,
                           const std::atomic<bool>* cancel=nullptr, std::atomic<float>* progress=nullptr) const;
    // Trigger pattern controller and offline MIDI export.
    bool setPattern(const pattern::Pattern& value) { return programs_.setPattern(value); }
    template<typename Edit> bool editPattern(Edit edit) { return programs_.editPattern(edit); }
    ProgramStore::VersionedPattern versionedPattern() const { return programs_.versionedPattern(); }
    bool replacePatternIfCurrent(const ProgramStore::VersionedPattern& expected,const pattern::Pattern& value) {
        return programs_.replacePatternIfCurrent(expected,value);
    }
    pattern::Pattern patternSnapshot() const { return programs_.snapshotState().pattern; }
    double observedTempo() const noexcept { return capturedTempo_.load(std::memory_order_relaxed); }
    bool exportPatternMidi(double bpm, std::vector<std::uint8_t>& bytes) const {
        return pattern::exportMidi(patternSnapshot(),bpm,bytes);
    }
    bool setEffect (std::size_t index, effects::Settings settings);
    effects::Settings effect (std::size_t index) const;
    std::uint64_t effectsSnapshot() const { return programs_.snapshotState().effects; }
    NoiseProgram program() const { return programs_.snapshot(); }
    std::uint64_t programRestoreEpoch() const noexcept { return programs_.programRevision(); }
    ProgramStore::VersionedProgram versionedProgram() const { return programs_.versionedProgram(); }
    bool editTonalityIfRevision(source::Tonality tonality,std::uint64_t revision) { return programs_.editTonalityIfRevision(tonality,revision); }
    bool editCurveIfRevision(CurveTarget target, std::uint64_t revision, const timeline::Curve& curve) {
        return programs_.editCurveIfRevision(target, revision, curve);
    }
    template <typename Edit> bool editLayerIfRevision(std::size_t index,std::uint64_t revision,Edit edit) {
        return programs_.editLayerIfRevision(index,revision,edit);
    }
    template <typename Edit> bool editLayer (std::size_t index, Edit edit) { return programs_.editLayer (index, edit); }

protected:
    void writeProductState (std::vector<preset::Section>&) const override;
    ProductRestore readProductState (const preset::StateDocument&, juce::String&) override;
    bool isProductSection (preset::Tag tag) const override { return tag == compare::sectionTag || tag == fxSampleSectionTag || tag == fxParameterSectionTag || tag == fxFamilySectionTag || tag == fxProgramSectionTag || tag == fxEffectsSectionTag || tag == fxPatternSectionTag || tag == fxPatternEnableSectionTag || tag == fxMacroSectionTag || tag == fxMacroRouteSectionTag || tag == fxGeneratorSectionTag; }

private:
    void publishTelemetry(bool clipped) noexcept {
        const auto value=engine_.activeEventCount()|(engine_.activeGrainCount()<<4)|(clipped?(1u<<11):0u);
        telemetry_.store(value,std::memory_order_relaxed);
        droppedEvents_.store(engine_.droppedEventCount(),std::memory_order_relaxed);
    }
    mutable sample::Controller samples_; // destroyed after engine and all sample leases
    NoiseRiser engine_;
    static_assert(std::atomic<double>::is_always_lock_free);
    std::atomic<double> capturedTempo_ {120.0}; // audio -> offline snapshot
    effects::Chain effects_;
    macro::Runtime macroRuntime_;
    preview::Bus previewBus_;
    macro::Config audioMacroConfig_;
    std::uint64_t audioEffects_ = effects::defaults(); // audio thread only
    ProgramStore programs_;
    mutable juce::CriticalSection controllerRestoreLock_; // serialize project restore, preset load and comparison commands
    mutable std::mutex comparisonMutex_; // never entered on the audio thread
    compare::Bank comparisons_;
    NoiseProgram audioProgram_;
    pattern::Pattern audioPattern_;
    pattern::FixedSchedule audioPatternSchedule_;
    struct CallbackBases {
        std::array<float,8> macros{};
        std::array<float,4> wet{},amount{};
        float duration=4,output=1;
        bool pattern=false;
    } audioBases_;
    CallbackBases readCallbackBases() const noexcept;
    // The structural store and immutable audio assets have separate owners.
    // This fence makes host restore/preset load an admission boundary. Only
    // the controller under controllerRestoreLock_ can change its parity.
    std::atomic<std::uint64_t> controllerEpoch_{0}, retireAtEpoch_{0};
    void retireOnPublication() noexcept;
    unsigned publicationDepth_=0;
    std::uint64_t audioEpoch_=0,retiredEpoch_=0;
    struct PendingGraph {
        NoiseProgram program;
        pattern::Pattern pattern;
        macro::Config macros;
        std::uint64_t effects=0;
    } pendingGraph_;
    std::uint64_t programSequence_ = 0; // audio thread only
    juce::AudioParameterFloat* output_ = nullptr; // owned by AudioProcessor
    std::atomic<bool> musicalClockUnavailable_ {true};
    juce::AudioParameterFloat* duration_ = nullptr;
    juce::AudioParameterBool* patternEnabled_ = nullptr;
    std::array<juce::AudioParameterFloat*,4> effectWet_{}, effectAmount_{};
    std::array<juce::AudioParameterFloat*,8> macros_{};
    // Guarded by SuiteProcessor state lock, not accessed by the audio thread.
    float loadedDuration_ = 0.0f, heldDuration_ = 0.0f;
    juce::SmoothedValue<float> outputSmoothing_;
    std::atomic<bool> panicPending_ { false }, auditionPending_ { false };
    std::atomic<float> peak_ { 0.0f };
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free && std::atomic<bool>::is_always_lock_free);
    std::atomic<std::uint32_t> telemetry_ { 0 }, droppedEvents_ { 0 };
    std::atomic<bool> clipLatched_ { false }, patternUnavailable_ { false };
    // Audio-thread-only transport history. Missing host position invalidates
    // continuity tracking; it never fabricates a seek.
    transport::Tracker transport_;
};

} // namespace vox::fx
