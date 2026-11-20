#include "Processor.h"
#include "FxEditor.h"
#include "engine/SoundGenerator.h"
#include "content/FactoryCandidates.h"
#include <bit>
#include <cmath>
#include <limits>

namespace vox::fx {
namespace {
class ControllerPublication {
public:
    ControllerPublication(std::atomic<std::uint64_t>& epoch,unsigned& depth):epoch_(epoch),depth_(depth){
        if(depth_++==0)epoch_.fetch_add(1,std::memory_order_acq_rel);
    }
    ~ControllerPublication(){if(--depth_==0)epoch_.fetch_add(1,std::memory_order_release);}
    ControllerPublication(const ControllerPublication&)=delete;
private:
    std::atomic<std::uint64_t>& epoch_;unsigned& depth_;
};
preset::Section encodeParameter (float value, float durationNormalized)
{
    preset::Section section;
    section.tag = fxParameterSectionTag;
    // FPRM v2: version, normalized output and duration, IEEE-754 little endian.
    for (const auto word : { std::uint32_t { 2 }, std::bit_cast<std::uint32_t> (value), std::bit_cast<std::uint32_t> (durationNormalized) })
        for (int shift = 0; shift < 32; shift += 8)
            section.data.push_back (static_cast<std::byte> ((word >> shift) & 0xffu));
    return section;
}
preset::Section encodeFamily (int family)
{
    preset::Section section;
    section.tag = fxFamilySectionTag;
    for (const auto word : { std::uint32_t { 1 }, static_cast<std::uint32_t> (family) })
        for (int shift = 0; shift < 32; shift += 8)
            section.data.push_back (static_cast<std::byte> ((word >> shift) & 0xffu));
    return section;
}
preset::Section encodeProgram (const NoiseProgram& program)
{
    preset::Section section;
    section.tag = fxProgramSectionTag;
    (void) programcodec::encode (program, section.data); // validated on publication
    return section;
}
preset::Section encodeEffects (std::uint64_t packed, const std::array<float,4>& wet, const std::array<float,4>& amount)
{
    preset::Section section; section.tag=fxEffectsSectionTag;
    const auto append=[&] (std::uint32_t word) {
        for (int shift=0; shift<32; shift+=8) section.data.push_back(static_cast<std::byte>((word>>shift)&0xffu));
    };
    append(effects::includesTempoDelay(packed)?3:2);
    for (std::size_t i=0;i<4;++i) {
        append(static_cast<std::uint32_t>(effects::unpack(static_cast<std::uint16_t>(packed>>(i*16))).type));
        append(std::bit_cast<std::uint32_t>(wet[i]));append(std::bit_cast<std::uint32_t>(amount[i]));
    }
    return section;
}
preset::Section encodeGenerator(const generator::ControllerState& state)
{
    preset::Section section;section.tag=fxGeneratorSectionTag;
    (void)generator::statecodec::encode(state,section.data);return section;
}
preset::Section encodeMacros(const std::array<float,8>& values)
{
    preset::Section section;section.tag=fxMacroSectionTag;
    auto put=[&](std::uint32_t word){for(int shift=0;shift<32;shift+=8)section.data.push_back(static_cast<std::byte>((word>>shift)&255u));};
    put(1);for(float value:values)put(std::bit_cast<std::uint32_t>(value));return section;
}
preset::Section encodeMacroRoutes(const macro::Config& config)
{
    preset::Section section;section.tag=fxMacroRouteSectionTag;
    (void)macro::codec::encode(config,section.data);return section;
}
preset::Section encodePatternEnable (bool enabled)
{
    preset::Section section; section.tag=fxPatternEnableSectionTag;
    for(const auto word:{std::uint32_t{1},enabled?std::uint32_t{1}:std::uint32_t{0}})
        for(int shift=0;shift<32;shift+=8)section.data.push_back(static_cast<std::byte>((word>>shift)&255u));
    return section;
}
preset::Section encodePattern (const pattern::Pattern& pattern)
{
    preset::Section section; section.tag=fxPatternSectionTag;
    (void) pattern::codec::encode(pattern,section.data); // validated at publication
    return section;
}
std::uint32_t wordAt (const std::vector<std::byte>& data, std::size_t offset)
{
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i)
        value |= std::to_integer<std::uint32_t> (data[offset + i]) << (8 * i);
    return value;
}
} // namespace

FxProcessor::FxProcessor()
    : vox::suite::SuiteProcessor ({ JucePlugin_Name,
                                    vox::preset::makeTag (VOX_SUITE_STATE_TAG),
                                    1,
                                    juce::Colour::fromString ("FF16CDBA") })
{
    (void) engine_.attachSampleStore(&samples_.store());
    output_ = new juce::AudioParameterFloat (juce::ParameterID { "fx.output.gain", 1 },
                                              "Output Gain", 0.0f, 1.0f, 1.0f);
    addParameter (output_);
    duration_ = new juce::AudioParameterFloat (juce::ParameterID { "fx.event.duration", 1 },
                                                "Event Duration", 0.05f, 32.0f, 4.0f);
    addParameter (duration_);
    loadedDuration_ = heldDuration_ = static_cast<juce::RangedAudioParameter*> (duration_)->getValue();
    patternEnabled_=new juce::AudioParameterBool(juce::ParameterID{"fx.pattern.enabled",1},"Pattern Enabled",false);
    addParameter(patternEnabled_);
    for (std::size_t i=0;i<4;++i) {
        const auto number=juce::String(static_cast<int>(i+1));
        effectWet_[i]=new juce::AudioParameterFloat(juce::ParameterID{"fx.slot."+number+".wet",1},"FX " + number + " Wet",0.0f,1.0f,1.0f);
        effectAmount_[i]=new juce::AudioParameterFloat(juce::ParameterID{"fx.slot."+number+".amount",1},"FX " + number + " Amount",0.0f,1.0f,0.5f);
        addParameter(effectWet_[i]);addParameter(effectAmount_[i]);
    }
    for(std::size_t i=0;i<8;++i){const auto number=juce::String(static_cast<int>(i+1));
        macros_[i]=new juce::AudioParameterFloat(juce::ParameterID{"fx.macro."+number,1},"Macro "+number,0.0f,1.0f,.5f);
        addParameter(macros_[i]);
    }
}

bool FxProcessor::generateProgram (const generator::Settings& settings, const generator::Locks& locks)
{
    // Generator duration is intentionally not applied to the independent host
    // automation lane. The currently automated duration wins for new events.
    const bool committed = programs_.editProgram ([&] (NoiseProgram& program) {
        generator::Result previous; previous.program = program;
        generator::Result result;
        if (!generator::generate (settings, previous, locks, result)) return false;
        program = result.program; return true;
    });
    return committed;
}

bool FxProcessor::mutateProgram (std::uint32_t seed, const generator::Locks& locks)
{
    const bool committed = programs_.editProgram ([&] (NoiseProgram& program) {
        generator::Result previous; previous.program = program;
        generator::Result result;
        if (!generator::mutate (previous, locks, seed, result)) return false;
        program = result.program; return true;
    });
    return committed;
}

bool FxProcessor::generateSound(bool mutation){
    const juce::ScopedLock transaction(controllerRestoreLock_);
    sample::Controller::PreparedRestore builtin;
    if(!sample::Controller::prepareRestore({},builtin))return false;
    const ControllerPublication publication(controllerEpoch_,publicationDepth_);
    bool replaceSample=false;
    const bool graphChanged=programs_.editSoundGraph([&](auto& state){return generator::makeSound(state,mutation,state,replaceSample);});
    const bool assetChanged=replaceSample&&samples_.reference().kind!=sample::Reference::Kind::builtin;
    if(!graphChanged&&!assetChanged)return false;
    if(assetChanged)samples_.commitRestore(std::move(builtin));
    previewBus_.stop();return true;
}

void FxProcessor::setStateInformation(const void* data,int size){
    const juce::ScopedLock transaction(controllerRestoreLock_);
    vox::suite::SuiteProcessor::setStateInformation(data,size);
}

bool FxProcessor::storeComparison(std::size_t slot)
{
    const juce::ScopedLock transaction(controllerRestoreLock_);
    if(slot>=comparisons_.size())return false;
    juce::MemoryBlock state;getStateInformation(state);
    auto decoded=preset::decode({static_cast<const std::byte*>(state.getData()),state.getSize()},compare::productTag,1);
    if(!decoded.ok())return false;
    auto& sections=decoded.document.sections;
    std::erase_if(sections,[](const auto& section){return section.tag==compare::sectionTag;});
    auto snapshot=preset::encode(decoded.document);
    if(!snapshot||snapshot->size()>compare::maxSlotBytes)return false;
    const std::lock_guard lock(comparisonMutex_);
    comparisons_[slot]=std::move(*snapshot);return true;
}
bool FxProcessor::recallComparison(std::size_t slot)
{
    const juce::ScopedLock transaction(controllerRestoreLock_);
    if(slot>=comparisons_.size())return false;
    compare::Bank bank;
    {const std::lock_guard lock(comparisonMutex_);bank=comparisons_;}
    preset::StateDocument snapshot;
    if(!compare::slotDocument(bank[slot],snapshot))return false;
    snapshot.sections.push_back(compare::encode(bank)); // preserve both slots across recall
    const auto encoded=preset::encode(snapshot);
    if(!encoded)return false;
    setStateInformation(encoded->data(),static_cast<int>(encoded->size()));
    return lastStateError().isEmpty();
}

portable::Sound FxProcessor::portableSnapshot() const {
    const juce::ScopedLock transaction(controllerRestoreLock_);
    return {programs_.snapshotState(),samples_.reference()};
}
bool FxProcessor::encodePortablePreset(std::vector<std::byte>& out) const {
    return portable::encode(portableSnapshot(),out);
}
bool FxProcessor::applyPortableSound(const portable::Sound& sound) {
    const juce::ScopedLock transaction(controllerRestoreLock_);
    sample::Controller::PreparedRestore prepared;
    if(!sample::Controller::prepareRestore(sound.sample,prepared))return false;
    const ControllerPublication publication(controllerEpoch_,publicationDepth_);
    const auto& s=sound.state;
    if(!programs_.publishState(s.program,s.effects,s.pattern,s.macros,s.generator))return false;
    samples_.commitRestore(std::move(prepared));
    retireOnPublication();
    return true; // All 19 host parameter values, layout and the A/B bank are left alone.
}
bool FxProcessor::restorePortablePreset(std::span<const std::byte> bytes) {
    portable::Sound sound;
    return portable::decode(bytes,sound)&&applyPortableSound(sound);
}
bool FxProcessor::loadReviewCandidate(std::size_t index) {
    if(index>=std::size(content::reviewEntries))return false;
    const auto preview=content::makePreview(content::reviewEntries[index],48000);
    portable::Sound sound;
    sound.state.program=preview.program;sound.state.pattern=preview.pattern;sound.state.macros=preview.settings.macroRoutes;
    sound.state.effects=0;
    for(unsigned i=0;i<4;++i)sound.state.effects|=static_cast<std::uint64_t>(effects::pack(preview.settings.effects[i]))<<(16*i);
    return applyPortableSound(sound);
}

offline::Capture FxProcessor::capturePresetPreview(const portable::Sound& sound) const {
    offline::Capture capture;
    capture.program=sound.state.program;capture.pattern=sound.state.pattern;
    auto& settings=capture.settings;
    settings.duration=duration_->get();settings.gain=output_->get();settings.tempo=capturedTempo_.load(std::memory_order_relaxed);
    settings.macroRoutes=sound.state.macros;
    for(std::size_t i=0;i<8;++i)settings.macros[i]=macros_[i]->get();
    for(std::size_t i=0;i<4;++i){
        settings.effects[i]=effects::unpack(static_cast<std::uint16_t>(sound.state.effects>>(i*16)));
        settings.effects[i].wet=effectWet_[i]->get();settings.effects[i].amount=effectAmount_[i]->get();
    }
    return capture;
}

offline::Capture FxProcessor::captureProgramWav (offline::Settings settings) const
{
    const juce::ScopedLock transaction(controllerRestoreLock_);
    const auto snapshot=programs_.snapshotState();
    settings.duration=duration_->get(); settings.gain=output_->get();
    settings.tempo=capturedTempo_.load(std::memory_order_relaxed);
    settings.macroRoutes=snapshot.macros;
    for(std::size_t i=0;i<8;++i)settings.macros[i]=macros_[i]->get();
    for(std::size_t i=0;i<4;++i){
        settings.effects[i]=effects::unpack(static_cast<std::uint16_t>(snapshot.effects>>(i*16)));
        settings.effects[i].wet=effectWet_[i]->get(); settings.effects[i].amount=effectAmount_[i]->get();
    }
    offline::Capture capture;capture.program=snapshot.program;capture.settings=settings;capture.pattern=snapshot.pattern;
    capture.patternEnabled=patternEnabled_->get();
    const auto generation=samples_.selection();
    const bool needsSample=std::any_of(capture.program.layers.begin(),capture.program.layers.end(),
        [](const auto& layer){return layer.enabled && layer.family==NoiseFamily::granular;});
    if(generation!=0 && needsSample) {
        capture.useUserSample=true;
        sample::Store::Lease lease;
        if(samples_.store().acquire(lease,generation)) {
            try { capture.sampleRate=lease.rate();capture.sample.assign(lease.data(),lease.data()+lease.frames()); }
            catch(...) { capture.sample.clear();capture.sampleRate=0; }
            samples_.store().release(lease);
        }
    }
    return capture;
}

bool FxProcessor::renderProgramWav (offline::Settings settings, std::vector<std::uint8_t>& output,
                                    const std::atomic<bool>* cancel, std::atomic<float>* progress) const
{
    const auto capture=captureProgramWav(settings);
    return offline::renderWav(capture,output,cancel,progress);
}

namespace {
bool onFileControllerThread() noexcept {
    const auto* manager=juce::MessageManager::getInstanceWithoutCreating();
    return manager&&manager->isThisTheMessageThread();
}
}

bool FxProcessor::startWavExport(offline::Capture capture,juce::File target) {
    if(!onFileControllerThread()||fileExportJob_||dragInProgress()||!target.hasFileExtension("wav"))return false;
    try { fileExportJob_=std::make_unique<fileexport::Job>(std::move(capture),std::move(target)); }
    catch(...) { return false; }
    return true;
}

bool FxProcessor::startMidiExport(pattern::Pattern snapshot,double tempo,juce::File target,
                                  fileexport::EncodeObserver observe,void* observerContext) {
    if(!onFileControllerThread()||fileExportJob_||dragInProgress()||!target.hasFileExtension("mid")||
       !snapshot.valid()||!std::isfinite(tempo)||tempo<10||tempo>1000)return false;
    try { fileExportJob_=std::make_unique<fileexport::Job>(std::move(snapshot),tempo,std::move(target),nullptr,observerContext,observe); }
    catch(...) { return false; }
    return true;
}

bool FxProcessor::prepareWavDrag(offline::Capture capture) {
    if(!onFileControllerThread()||fileExportJob_||dragInProgress())return false;
    try {
        fileExportJob_=std::make_unique<fileexport::Job>(std::move(capture),juce::File{},nullptr,nullptr,
                                                         fileexport::Purpose::drag,std::move(preparedDrag_));
    } catch(...) { return false; }
    return true;
}

bool FxProcessor::prepareMidiDrag(pattern::Pattern snapshot,double tempo) {
    if(!onFileControllerThread()||fileExportJob_||dragInProgress()||!snapshot.valid()||
       !std::isfinite(tempo)||tempo<10||tempo>1000)return false;
    try {
        fileExportJob_=std::make_unique<fileexport::Job>(std::move(snapshot),tempo,juce::File{},nullptr,nullptr,nullptr,
                                                         fileexport::Purpose::drag,std::move(preparedDrag_));
    } catch(...) { return false; }
    return true;
}

std::optional<fileexport::Kind> FxProcessor::preparedDragKind() const noexcept {
    if(!onFileControllerThread()||!preparedDrag_||preparedDrag_->offered)return {};
    return preparedDrag_->kind;
}

bool FxProcessor::dragInProgress() const noexcept {
    return onFileControllerThread()&&preparedDrag_&&preparedDrag_->active.load(std::memory_order_acquire);
}

bool FxProcessor::offerPreparedDrag(const DragLauncher& launch) {
    if(!onFileControllerThread()||fileExportJob_||!preparedDragKind()||!launch)return false;
    const auto payload=preparedDrag_;
    payload->offered=true; // before launch: nested dispatch cannot offer it twice
    payload->active.store(true,std::memory_order_release);
    bool started=false;
    try { started=launch(payload->file,[payload]{payload->active.store(false,std::memory_order_release);}); }
    catch(...) {}
    // No callback owns the processor/editor. A successful native start does
    // not tell us whether the drop was accepted or the file was copied.
    if(!started){payload->offered=false;payload->active.store(false,std::memory_order_release);}
    return started;
}

std::optional<fileexport::Status> FxProcessor::fileExportStatus() const noexcept {
    if(!onFileControllerThread()||!fileExportJob_)return {};
    return fileExportJob_->status();
}

bool FxProcessor::cancelFileExport(fileexport::Kind kind) noexcept {
    return onFileControllerThread()&&fileExportJob_&&fileExportJob_->kind()==kind&&fileExportJob_->cancel();
}

std::optional<fileexport::Completion> FxProcessor::takeFileExportResult() {
    if(!onFileControllerThread()||!fileExportJob_)return {};
    const auto result=fileExportJob_->result();
    if(result==fileexport::Result::running)return {};
    const auto kind=fileExportJob_->kind();
    const auto purpose=fileExportJob_->purpose();
    if(purpose==fileexport::Purpose::drag&&result==fileexport::Result::success)
        preparedDrag_=fileExportJob_->takeDragFile();
    fileExportJob_.reset();
    return fileexport::Completion{kind,result,purpose};
}

bool FxProcessor::setEffect (std::size_t index, effects::Settings settings)
{
    return programs_.setEffect(index,settings);
}
effects::Settings FxProcessor::effect (std::size_t index) const
{
    if (index>=4) return {};
    auto settings=effects::unpack(static_cast<std::uint16_t>(effectsSnapshot()>>(index*16)));
    settings.wet=effectWet_[index]->get();settings.amount=effectAmount_[index]->get();
    return settings;
}

std::optional<float> FxProcessor::curvePlayhead(std::uint64_t publication,std::optional<std::size_t> layer) const noexcept {
    if(layer&&*layer>=4)return {};
    const auto before=cursorSequence_.load();
    if(before&1u)return {};
    const auto graph=cursorGraph_.load();
    const auto phases=cursorPhases_.load();
    if(before!=cursorSequence_.load()||graph!=publication)return {};
    const auto encoded=(phases>>(layer?12*(*layer+1):0))&4095u;
    if(encoded==0)return {};
    return static_cast<float>(encoded-1)/4094.f;
}

void FxProcessor::publishPlayhead(bool rendered) noexcept {
    std::uint64_t phases=0;
    if(rendered){
        const auto positions=engine_.playhead(programSequence_);
        for(std::size_t i=0;i<positions.size();++i)if(positions[i]>=0){
            const auto encoded=1+static_cast<std::uint64_t>(std::clamp(positions[i],0.f,1.f)*4094.f+.5f);
            phases|=encoded<<(12*i);
        }
    }
    cursorSequence_.fetch_add(1);
    cursorGraph_.store(programSequence_);cursorPhases_.store(phases);
    cursorSequence_.fetch_add(1);
}

void FxProcessor::prepareToPlay (double rate, int blockSize)
{
    SuiteProcessor::prepareToPlay (rate, blockSize);
    previewBus_.prepare(rate);
    engine_.prepare (rate);capturedTempo_.store(120.0,std::memory_order_relaxed);
    (void) effects_.prepare (std::isfinite(rate) && rate >= 8000 && rate <= 384000 ? rate : 44100);
    (void) macroRuntime_.prepare (std::isfinite(rate) && rate >= 8000 && rate <= 384000 ? rate : 44100);
    programSequence_ = 0;publishPlayhead(false);audioEffects_ = effects::defaults();
    audioBases_=readCallbackBases();audioEpoch_=controllerEpoch_.load(std::memory_order_acquire);
    retiredEpoch_=retireAtEpoch_.load(std::memory_order_acquire);
    outputSmoothing_.reset (std::isfinite (rate) && rate >= 8000.0 && rate <= 384000.0 ? rate : 44100.0, 0.005);
    outputSmoothing_.setCurrentAndTargetValue (output_->get());
    panicPending_.store (false, std::memory_order_relaxed);
    auditionRequest_.store(AuditionRequest::none,std::memory_order_relaxed);
    peaks_.store(0,std::memory_order_relaxed);telemetry_.store(0,std::memory_order_relaxed);droppedEvents_.store(0,std::memory_order_relaxed);clipLatched_.store(false,std::memory_order_relaxed);
    patternClock_.store(PatternClock::inactive,std::memory_order_relaxed);
    transport_.unavailable();
}

void FxProcessor::releaseResources() {
    previewBus_.prepare(preparedSampleRate());
    patternClock_.store(PatternClock::inactive,std::memory_order_relaxed);
    engine_.prepare (preparedSampleRate());
    telemetry_.store(0,std::memory_order_relaxed);droppedEvents_.store(0,std::memory_order_relaxed);
    peaks_.store(0,std::memory_order_relaxed);clipLatched_.store(false,std::memory_order_relaxed);publishPlayhead(false);
}
void FxProcessor::reset() { panic(); }
juce::AudioProcessorEditor* FxProcessor::createEditor()
{
    auto editor = std::make_unique<vox::suite::SuiteEditor> (*this);
    // The interim layer cards and pattern row need vertical space. Old saved
    // sizes below this product-specific minimum are enlarged when opened.
    editor->setResizeLimits (1040, 760, 1500, 920);
    if(editor->getHeight()<760)editor->setSize(editor->getWidth(),760);
    editor->setContent (std::make_unique<FxPanel> (*this));
    return editor.release();
}

void FxProcessor::retireOnPublication() noexcept {
    // Attach retirement to adoption of the complete new state, not to a
    // generic Panic exchange which the preceding callback could consume.
    layerSoloMask_.store(0,std::memory_order_relaxed);
    retireAtEpoch_.store(controllerEpoch_.load(std::memory_order_relaxed)+1,std::memory_order_release);
    previewBus_.stop();
}

FxProcessor::CallbackBases FxProcessor::readCallbackBases() const noexcept {
    CallbackBases values;
    values.duration=duration_->get();values.output=output_->get();values.pattern=patternEnabled_->get();
    for(std::size_t i=0;i<8;++i)values.macros[i]=macros_[i]->get();
    for(std::size_t i=0;i<4;++i){values.wet[i]=effectWet_[i]->get();values.amount[i]=effectAmount_[i]->get();}
    return values;
}

void FxProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    noteMidiSeen (midi);
    buffer.clear();
    // Stop, seek or loop discontinuity fades existing events. No stop is
    // inferred when a host does not provide a position. New notes in this
    // block may still start (including audition while transport is stopped).
    bool positionAvailable = false, hostPlaying=false, musicalClockReady=false;
    auto patternClock=PatternClock::noPlayhead;
    double hostPpq=0.0,hostBpm=0.0,renderTempo=120.0;
    timeline::BarGrid barGrid;
    if (auto* host = getPlayHead()) {
        if (const auto position = host->getPosition()) {
            positionAvailable = true;
            const bool playing = position->getIsPlaying();hostPlaying=playing;
            const auto ppq=position->getPpqPosition(), bpm=position->getBpm();
            if(bpm.hasValue()&&std::isfinite(*bpm)&&*bpm>=1.0&&*bpm<=1000.0)renderTempo=*bpm;
            const auto signature=position->getTimeSignature();
            const bool supportedSignature=!signature.hasValue() || (signature->numerator==4 && signature->denominator==4);
            musicalClockReady=playing && ppq.hasValue() && std::isfinite(*ppq) && bpm.hasValue() &&
                std::isfinite(*bpm) && *bpm>=1.0 && *bpm<=1000.0 && signature.hasValue() &&
                signature->numerator==4 && signature->denominator==4;
            if (musicalClockReady) {
                const auto lastBar = position->getPpqPositionOfLastBarStart();
                barGrid = timeline::BarGrid::from(*ppq, *bpm, preparedSampleRate(), lastBar.orFallback(0.0));
            }
            if(!playing)patternClock=PatternClock::stopped;
            else if(!supportedSignature)patternClock=PatternClock::unsupportedMeter;
            else if(!ppq.hasValue()||!std::isfinite(*ppq)||std::abs(*ppq)>1.0e9)patternClock=PatternClock::invalidPpq;
            else if(!bpm.hasValue()||!std::isfinite(*bpm)||*bpm<10.0||*bpm>1000.0)patternClock=PatternClock::invalidTempo;
            else {
                hostPpq=*ppq;hostBpm=*bpm;
                patternClock=signature.hasValue()?PatternClock::ready44:PatternClock::readyAssumed44;
            }
            const auto sample=position->getTimeInSamples();
            transport::Position current;current.playing=playing;
            current.hasSample=sample.hasValue();if(current.hasSample)current.sample=*sample;
            current.hasPpq=ppq.hasValue();if(current.hasPpq)current.ppq=*ppq;
            current.hasTempo=bpm.hasValue();if(current.hasTempo)current.tempo=*bpm;
            if(transport_.update(current,preparedSampleRate(),buffer.getNumSamples()))engine_.allSoundOff(0);
            else if(transport_.musicalDiscontinuity())engine_.releaseAlignedTransitions();
        }
    }
    if (!positionAvailable) transport_.unavailable();
    (void)engine_.setTempo(renderTempo);
    engine_.setMusicalClockAvailable(musicalClockReady,renderTempo);
    engine_.setBarGrid(barGrid);
    capturedTempo_.store(renderTempo,std::memory_order_relaxed);
    // Take one bounded read. A save/restore listener may notify several
    // host parameters synchronously; do not admit notes from a half-applied
    // graph/sample pair or use those parameter values in the meantime.
    const auto epoch=controllerEpoch_.load(std::memory_order_acquire);
    bool admit=false;
    std::uint64_t selection=0;
    if((epoch&1u)==0){
        auto nextSequence=epoch==audioEpoch_?programSequence_:0;
        const bool changed=programs_.tryRead(pendingGraph_.program,pendingGraph_.effects,
            pendingGraph_.pattern,pendingGraph_.macros,nextSequence);
        const auto bases=readCallbackBases();selection=samples_.selection();
        if(epoch==controllerEpoch_.load(std::memory_order_acquire)){
            // A new epoch always re-reads the complete graph, including
            // no-op controller publications with an unchanged sequence.
            if(changed){
                audioProgram_=pendingGraph_.program;audioEffects_=pendingGraph_.effects;
                if(programSequence_==0||audioPattern_!=pendingGraph_.pattern){
                    audioPattern_=pendingGraph_.pattern;(void)pattern::scheduleFixed(audioPattern_,audioPatternSchedule_);
                }
                audioMacroConfig_=pendingGraph_.macros;programSequence_=nextSequence;audioEpoch_=epoch;
                (void)engine_.setProgram(audioProgram_,programSequence_);
                (void)macroRuntime_.setConfig(audioMacroConfig_);(void)engine_.setVoiceMotion(audioMacroConfig_);
            }
            admit=audioEpoch_==epoch;
            if(admit)audioBases_=bases;
        }
    }
    const auto retirement=retireAtEpoch_.load(std::memory_order_acquire);
    const bool retire=admit&&retirement>retiredEpoch_&&retirement<=audioEpoch_;
    const bool panic=admit&&panicPending_.exchange(false,std::memory_order_acquire);
    if(retire)retiredEpoch_=retirement;
    if(retire||panic){
        engine_.allSoundOff(0);previewBus_.stop();
        auditionRequest_.store(AuditionRequest::none,std::memory_order_relaxed);
    }
    musicalClockUnavailable_.store(!musicalClockReady || (audioProgram_.mode == PlayMode::transition && !barGrid.valid()),
                                   std::memory_order_relaxed);
    const float requestedDuration = audioBases_.duration;
    const double eventDuration = std::isfinite (requestedDuration)
        ? juce::jlimit (0.05, 32.0, static_cast<double> (requestedDuration)) : 4.0;
    const auto& macroValues=audioBases_.macros;
    engine_.setMacroBases(macroValues);
    macroRuntime_.setTempo(renderTempo);
    effects_.setTempo(renderTempo);
    const auto audition=admit?auditionRequest_.exchange(AuditionRequest::none,std::memory_order_acquire):AuditionRequest::none;
    const bool offlineBounce=isNonRealtime();
    if(offlineBounce){
        engine_.discardPreview();
        previewBus_.discardOnAudioThread();
    }else{
        if(audition==AuditionRequest::stop)engine_.stopPreview();
        if(audition==AuditionRequest::start)engine_.noteOn(1,60,.8f,eventDuration,selection!=0,selection,TriggerSource::preview);
    }
    engine_.setLayerSoloMask(offlineBounce?0:layerSoloMask(),offlineBounce);
    const int size = buffer.getNumSamples();
    const bool wantsPattern=audioBases_.pattern||audioProgram_.mode==PlayMode::sequence;
    const bool patternHostReady=patternClock==PatternClock::ready44||patternClock==PatternClock::readyAssumed44;
    if(!wantsPattern)patternClock=PatternClock::inactive;
    else if(!admit&&patternHostReady)patternClock=PatternClock::waitingForState;
    if (buffer.getNumChannels() < 2 || size == 0) {
        midi.clear(); // This instrument has no MIDI output, including empty blocks.
        patternClock_.store(patternClock,std::memory_order_relaxed);
        peaks_.store(0,std::memory_order_relaxed);publishTelemetry(false);publishPlayhead(false);
        return;
    }
    auto* left = buffer.getWritePointer (0);
    auto* right = buffer.getWritePointer (1);
    int position = 0;
    auto renderTo = [&] (int end) {
        if (end > position) engine_.render (left + position, right + position, end - position);
        position = end;
    };
    auto midiIt=midi.cbegin(); const auto midiEnd=midi.cend();
    auto processMidiThrough = [&] (int through) {
        while(midiIt!=midiEnd) {
            const auto metadata=*midiIt;
            if(metadata.samplePosition>through)break;
            ++midiIt;
            // Do not materialize a MidiMessage: SysEx may allocate.
            if(metadata.numBytes<3 || metadata.samplePosition<0 || metadata.samplePosition>=size)continue;
            const auto* bytes=metadata.data;const int type=bytes[0]&0xf0;
            const int channel=(bytes[0]&0x0f)+1;
            if(type==0x90 && bytes[2]!=0) {
                renderTo(metadata.samplePosition);
                if(admit)engine_.noteOn(channel,bytes[1]&0x7f,static_cast<float>(bytes[2]&0x7f)/127.0f,eventDuration,selection!=0,selection);
            } else if(type==0x80 || (type==0x90 && bytes[2]==0)) {
                renderTo(metadata.samplePosition);engine_.noteOff(channel,bytes[1]&0x7f);
            } else if(type==0xb0 && (bytes[1]==120 || bytes[1]==123)) {
                renderTo(metadata.samplePosition);engine_.allSoundOff(channel);
            }
        }
    };
    if(admit && wantsPattern && patternHostReady) {
        // MIDI at the same offset is processed first. No host MIDI output is generated.
        const bool scheduled=pattern::forEachTrigger(audioPatternSchedule_,audioPattern_.bars,hostPpq,hostBpm,preparedSampleRate(),size,
            [&] (int offset,std::uint8_t note,std::uint8_t velocity) noexcept {
                processMidiThrough(offset);renderTo(offset);
                engine_.noteOn(1,note,static_cast<float>(velocity)/127.0f,eventDuration,selection!=0,selection,TriggerSource::pattern);
            });
        if(!scheduled)patternClock=PatternClock::blockRejected;
    }
    patternClock_.store(patternClock,std::memory_order_relaxed);
    processMidiThrough(size-1);
    midi.clear(); // Consume input: never echo host MIDI from this instrument.
    renderTo (size);
    const auto fxSnapshot=audioEffects_;
    for (std::size_t i=0;i<4;++i) {
        auto settings=effects::unpack(static_cast<std::uint16_t>(fxSnapshot>>(i*16)));
        const auto wet=audioBases_.wet[i], amount=audioBases_.amount[i];
        settings.wet=std::isfinite(wet)?juce::jlimit(0.0f,1.0f,wet):0.0f;
        settings.amount=std::isfinite(amount)?juce::jlimit(0.0f,1.0f,amount):0.0f;
        (void) effects_.set(i,settings);
    }
    const auto target = audioBases_.output;
    outputSmoothing_.setTargetValue (std::isfinite (target) ? juce::jlimit (0.0f, 1.0f, target) : 0.0f);
    macro::renderPost(effects_,macroRuntime_,left,right,static_cast<std::size_t>(size),macroValues,[&]{return outputSmoothing_.getNextValue();});
    if(!offlineBounce)previewBus_.mix(left,right,size);
    float peakL=0,peakR=0;
    for(int i=0;i<size;++i){peakL=std::max(peakL,std::abs(left[i]));peakR=std::max(peakR,std::abs(right[i]));}
    peaks_.store(std::bit_cast<std::uint32_t>(peakL)|(std::uint64_t{std::bit_cast<std::uint32_t>(peakR)}<<32),std::memory_order_relaxed);
    const bool clip=peakL>=1.f||peakR>=1.f;
    publishTelemetry(clip);publishPlayhead(!offlineBounce);
    if(clip)clipLatched_.store(true,std::memory_order_relaxed);
}

void FxProcessor::writeProductState (std::vector<preset::Section>& out) const
{
    const auto current = static_cast<juce::RangedAudioParameter*> (duration_)->getValue();
    out.push_back (encodeParameter (output_->get(), juce::exactlyEqual (current, heldDuration_) ? loadedDuration_ : current));
    const auto snapshot = programs_.snapshotState();
    out.push_back (encodeFamily (static_cast<int> (snapshot.program.layers[0].family)));
    out.push_back (encodeProgram (snapshot.program));
    std::array<float,4> wet{},amount{};
    for(std::size_t i=0;i<4;++i){wet[i]=effectWet_[i]->get();amount[i]=effectAmount_[i]->get();}
    out.push_back (encodeEffects (snapshot.effects,wet,amount));
    out.push_back (encodePattern (snapshot.pattern));
    out.push_back (encodePatternEnable (patternEnabled_->get()));
    std::array<float,8> macroValues{};for(std::size_t i=0;i<8;++i)macroValues[i]=macros_[i]->get();
    out.push_back (encodeMacros(macroValues));out.push_back (encodeMacroRoutes(snapshot.macros));
    out.push_back (encodeGenerator(snapshot.generator));
    preset::Section sampleSection;sampleSection.tag=fxSampleSectionTag;
    (void)sample::referencecodec::encode(samples_.reference(),sampleSection.data);
    out.push_back(std::move(sampleSection));
    const std::lock_guard lock(comparisonMutex_);
    if(!compare::empty(comparisons_))out.push_back(compare::encode(comparisons_));
}

FxProcessor::ProductRestore FxProcessor::readProductState (const preset::StateDocument& doc, juce::String& error)
{
    sample::Reference sampleReference;
    if(const auto* section=doc.find(fxSampleSectionTag)) {
        if(!sample::referencecodec::decode(section->data,sampleReference)) { error="Malformed sample reference";return {}; }
    }
    sample::Controller::PreparedRestore preparedSample;
    if(!sample::Controller::prepareRestore(sampleReference,preparedSample)) { error="Cannot prepare sample reference";return {}; }
    float value = 1.0f; // FOUND-01 shell states have no FPRM section.
    float duration = duration_->convertTo0to1 (4.0f);
    if (const auto* section = doc.find (fxParameterSectionTag)) {
        const auto size = section->data.size();
        if (size != 8 && size != 12) {
            error = "Malformed FX parameter section";
            return {};
        }
        const auto version = wordAt (section->data, 0);
        if ((version != 1 || size != 8) && (version != 2 || size != 12)) {
            error = "Unsupported FX parameter section";
            return {};
        }
        value = std::bit_cast<float> (wordAt (section->data, 4));
        if (version == 2) duration = std::bit_cast<float> (wordAt (section->data, 8));
        if (! std::isfinite (value) || value < 0.0f || value > 1.0f ||
            ! std::isfinite (duration) || duration < 0.0f || duration > 1.0f) {
            error = "Invalid FX parameter value";
            return {};
        }
    }
    int family = 0;
    if (const auto* section = doc.find (fxFamilySectionTag)) {
        if (section->data.size() != 8 || wordAt (section->data, 0) != 1 || wordAt (section->data, 4) > 11) {
            error = "Unsupported or malformed FX family section";
            return {};
        }
        family = static_cast<int> (wordAt (section->data, 4));
    }
    auto program = std::make_shared<NoiseProgram>();
    program->layers[0].family = static_cast<NoiseFamily> (family);
    if (const auto* section = doc.find (fxProgramSectionTag)) {
        if (!programcodec::decode (section->data, *program)) {
            error = "Unsupported or malformed FX Program section";
            return {};
        }
        if (doc.find (fxFamilySectionTag) != nullptr && static_cast<int> (program->layers[0].family) != family) {
            error = "Inconsistent FX family and Program sections";
            return {};
        }
    }
    auto fx=effects::defaults();
    std::array<float,4> wet{1,1,1,1},amount{.5f,.5f,.5f,.5f};
    if (const auto* section=doc.find(fxEffectsSectionTag)) {
        if (section->data.size()==12 && wordAt(section->data,0)==1) {
            const auto legacy=static_cast<std::uint64_t>(wordAt(section->data,4)) |
               (static_cast<std::uint64_t>(wordAt(section->data,8))<<32);
            fx=0;
            for(std::size_t i=0;i<4;++i){const auto s=effects::unpackLegacy(static_cast<std::uint16_t>(legacy>>(i*16)));
                if(!s.valid()){error="Invalid FX effects settings";return {};}
                wet[i]=s.wet;amount[i]=s.amount;fx|=static_cast<std::uint64_t>(effects::pack(s))<<(i*16);}
        } else if(section->data.size()==52 && (wordAt(section->data,0)==2||wordAt(section->data,0)==3)) {
            const auto typeLimit=wordAt(section->data,0)==2?effects::Type::tempoDelay:effects::Type::count;
            fx=0;
            for(std::size_t i=0;i<4;++i) {
                effects::Settings settings;
                const auto type=wordAt(section->data,4+i*12);
                if(type>=static_cast<std::uint32_t>(typeLimit)){error="Invalid FX effects type";return {};}
                settings.type=static_cast<effects::Type>(type);
                wet[i]=settings.wet=std::bit_cast<float>(wordAt(section->data,8+i*12));
                amount[i]=settings.amount=std::bit_cast<float>(wordAt(section->data,12+i*12));
                if(!settings.valid()){error="Invalid FX effects settings";return {};}
                fx|=static_cast<std::uint64_t>(effects::pack(settings))<<(i*16);
            }
        } else {error="Unsupported or malformed FX effects section";return {};}
    }
    auto triggerPattern=std::make_shared<pattern::Pattern>();
    if(const auto* section=doc.find(fxPatternSectionTag)) {
        if(!pattern::codec::decode(section->data,*triggerPattern)) {
            error="Unsupported or malformed FX pattern section";return {};
        }
    }
    bool patternEnabled=false;
    if(const auto* section=doc.find(fxPatternEnableSectionTag)) {
        if(section->data.size()!=8 || wordAt(section->data,0)!=1 || wordAt(section->data,4)>1) {
            error="Unsupported or malformed FX pattern enable section";return {};
        }
        patternEnabled=wordAt(section->data,4)!=0;
    }
    std::array<float,8> macroValues{.5f,.5f,.5f,.5f,.5f,.5f,.5f,.5f};
    if(const auto* section=doc.find(fxMacroSectionTag)) {
        if(section->data.size()!=36 || wordAt(section->data,0)!=1){error="Unsupported or malformed FX macro section";return {};}
        for(std::size_t i=0;i<8;++i){macroValues[i]=std::bit_cast<float>(wordAt(section->data,4+i*4));
            if(!std::isfinite(macroValues[i])||macroValues[i]<0||macroValues[i]>1){error="Invalid FX macro value";return {};}}
    }
    auto macroRoutes=std::make_shared<macro::Config>();
    if(const auto* section=doc.find(fxMacroRouteSectionTag))if(!macro::codec::decode(section->data,*macroRoutes)){
        error="Unsupported or malformed FX macro routes";return {};}
    auto generatorState=std::make_shared<generator::ControllerState>();
    if(const auto* section=doc.find(fxGeneratorSectionTag))if(!generator::statecodec::decode(section->data,*generatorState)){
        error="Unsupported or malformed FX generator settings";return {};}
    auto bank=std::make_shared<compare::Bank>();
    if(const auto* section=doc.find(compare::sectionTag)){
        if(!compare::decode(section->data,*bank)){error="Malformed FX comparison bank";return {};}
        for(const auto& slot:*bank){
            if(slot.empty())continue;
            preset::StateDocument inner;
            if(!compare::slotDocument(slot,inner)){error="Invalid FX comparison snapshot";return {};}
            const auto* ui=inner.find(compare::uiTag);
            if(ui->data.size()!=8 || !vox::suite::EditorLimits::valid(wordAt(ui->data,0),wordAt(ui->data,4))){
                error="Invalid FX comparison editor size";return {};
            }
            juce::String validationError;
            if(!readProductState(inner,validationError).apply){error="Invalid FX comparison: "+validationError;return {};}
        }
    }
    ProductRestore result;
    result.sections.push_back (encodeParameter (value, duration));
    result.sections.push_back (encodeFamily (static_cast<int> (program->layers[0].family)));
    result.sections.push_back (encodeProgram (*program));
    result.sections.push_back (encodeEffects (fx,wet,amount));
    result.sections.push_back (encodePattern (*triggerPattern));
    result.sections.push_back (encodePatternEnable (patternEnabled));
    result.sections.push_back (encodeMacros (macroValues));
    result.sections.push_back (encodeMacroRoutes (*macroRoutes));
    result.sections.push_back (encodeGenerator (*generatorState));
    preset::Section sampleSection;sampleSection.tag=fxSampleSectionTag;
    if(!sample::referencecodec::encode(sampleReference,sampleSection.data)) { error="Cannot encode sample reference";return {}; }
    result.sections.push_back(std::move(sampleSection));
    if(!compare::empty(*bank))result.sections.push_back(compare::encode(*bank));
    auto held = std::make_shared<float> (0.0f);
    result.apply = [this, value, duration, program, held, fx, wet, amount, triggerPattern, patternEnabled, macroValues, macroRoutes, generatorState, bank, preparedSample=std::move(preparedSample)] () mutable {
        const ControllerPublication publication(controllerEpoch_,publicationDepth_);
        {const std::lock_guard lock(comparisonMutex_);comparisons_=std::move(*bank);}
        (void) programs_.publishState (*program, fx, *triggerPattern, *macroRoutes, *generatorState);
        for(std::size_t i=0;i<8;++i)macros_[i]->setValueNotifyingHost(macroValues[i]);
        patternEnabled_->setValueNotifyingHost(patternEnabled?1.0f:0.0f);
        for(std::size_t i=0;i<4;++i){effectWet_[i]->setValueNotifyingHost(wet[i]);effectAmount_[i]->setValueNotifyingHost(amount[i]);}
        samples_.commitRestore(std::move(preparedSample));
        output_->setValueNotifyingHost (value);
        duration_->setValueNotifyingHost (duration);
        *held = static_cast<juce::RangedAudioParameter*> (duration_)->getValue();
        retireOnPublication();
    };
    result.settle = [this, duration, held] { loadedDuration_ = duration; heldDuration_ = *held; };
    return result;
}
} // namespace vox::fx

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new vox::fx::FxProcessor(); }
