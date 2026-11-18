#include "Processor.h"
#include "FxEditor.h"
#include <bit>
#include <cmath>
#include <limits>

namespace vox::fx {
namespace {
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
    append(2);
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

bool FxProcessor::storeComparison(std::size_t slot)
{
    if(slot>=comparisons_.size())return false;
    juce::MemoryBlock state;getStateInformation(state);
    if(state.isEmpty()||state.getSize()>16*1024*1024)return false;
    comparisons_[slot]=std::move(state);return true;
}
bool FxProcessor::recallComparison(std::size_t slot)
{
    if(slot>=comparisons_.size()||comparisons_[slot].isEmpty())return false;
    setStateInformation(comparisons_[slot].getData(),static_cast<int>(comparisons_[slot].getSize()));
    return lastStateError().isEmpty();
}

offline::Capture FxProcessor::captureProgramWav (offline::Settings settings) const
{
    const auto snapshot=programs_.snapshotState();
    settings.duration=duration_->get(); settings.gain=output_->get();
    settings.macroRoutes=snapshot.macros;
    for(std::size_t i=0;i<8;++i)settings.macros[i]=macros_[i]->get();
    for(std::size_t i=0;i<4;++i){
        settings.effects[i]=effects::unpack(static_cast<std::uint16_t>(snapshot.effects>>(i*16)));
        settings.effects[i].wet=effectWet_[i]->get(); settings.effects[i].amount=effectAmount_[i]->get();
    }
    offline::Capture capture;capture.program=snapshot.program;capture.settings=settings;
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

void FxProcessor::prepareToPlay (double rate, int blockSize)
{
    SuiteProcessor::prepareToPlay (rate, blockSize);
    engine_.prepare (rate);
    (void) effects_.prepare (std::isfinite(rate) && rate >= 8000 && rate <= 384000 ? rate : 44100);
    (void) macroRuntime_.prepare (std::isfinite(rate) && rate >= 8000 && rate <= 384000 ? rate : 44100);
    programSequence_ = 0; audioEffects_ = effects::defaults();
    outputSmoothing_.reset (std::isfinite (rate) && rate >= 8000.0 && rate <= 384000.0 ? rate : 44100.0, 0.005);
    outputSmoothing_.setCurrentAndTargetValue (output_->get());
    panicPending_.store (false, std::memory_order_relaxed);
    auditionPending_.store (false, std::memory_order_relaxed);
    peak_.store (0.0f, std::memory_order_relaxed);telemetry_.store(0,std::memory_order_relaxed);clipLatched_.store(false,std::memory_order_relaxed);
    transportKnown_ = wasPlaying_ = expectedSampleKnown_ = false;
}

void FxProcessor::releaseResources() { engine_.prepare (preparedSampleRate()); }
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

void FxProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    noteMidiSeen (midi);
    buffer.clear();
    // Stop, seek or loop discontinuity fades existing events. No stop is
    // inferred when a host does not provide a position. New notes in this
    // block may still start (including audition while transport is stopped).
    bool positionAvailable = false, patternHostReady=false;
    double hostPpq=0.0,hostBpm=0.0;
    if (auto* host = getPlayHead()) {
        if (const auto position = host->getPosition()) {
            positionAvailable = true;
            const bool playing = position->getIsPlaying();
            const auto ppq=position->getPpqPosition(), bpm=position->getBpm();
            const auto signature=position->getTimeSignature();
            const bool supportedSignature=!signature.hasValue() || (signature->numerator==4 && signature->denominator==4);
            if(playing && ppq.hasValue() && bpm.hasValue() && supportedSignature) {
                hostPpq=*ppq;hostBpm=*bpm;patternHostReady=true;
            }
            const auto sample = position->getTimeInSamples();
            const bool stopped = transportKnown_ && wasPlaying_ && !playing;
            const bool discontinuity = transportKnown_ && wasPlaying_ && playing && expectedSampleKnown_ &&
                                       sample.hasValue() && *sample != expectedSample_;
            if (stopped || discontinuity) engine_.allSoundOff (0);
            expectedSampleKnown_ = playing && sample.hasValue() &&
                                   *sample <= std::numeric_limits<std::int64_t>::max() - buffer.getNumSamples();
            if (expectedSampleKnown_) expectedSample_ = *sample + buffer.getNumSamples();
            wasPlaying_ = playing;
            transportKnown_ = true;
        }
    }
    if (!positionAvailable) transportKnown_ = wasPlaying_ = expectedSampleKnown_ = false;
    if (panicPending_.exchange (false, std::memory_order_acquire)) {
        engine_.allSoundOff (0);
        auditionPending_.store (false, std::memory_order_relaxed);
    }
    // One bounded attempt: if a concurrent writer is publishing, keep the
    // prior snapshot and retry on the next block. No locks or allocation.
    if (programs_.tryRead (audioProgram_, audioEffects_, audioPattern_, audioMacroConfig_, programSequence_)) {
        (void) engine_.setProgram (audioProgram_);
        (void) pattern::scheduleFixed(audioPattern_,audioPatternSchedule_);
        (void) macroRuntime_.setConfig(audioMacroConfig_);
    }
    const float requestedDuration = duration_->get();
    const double eventDuration = std::isfinite (requestedDuration)
        ? juce::jlimit (0.05, 32.0, static_cast<double> (requestedDuration)) : 4.0;
    if (auditionPending_.exchange (false, std::memory_order_acquire)) { const auto selection=samples_.selection();engine_.noteOn (1, 60, 0.8f, eventDuration,selection!=0,selection); }
    const int size = buffer.getNumSamples();
    if (buffer.getNumChannels() < 2 || size == 0) {
        midi.clear(); // This instrument has no MIDI output, including empty blocks.
        peak_.store (0.0f, std::memory_order_relaxed);publishTelemetry(false);
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
                {const auto selection=samples_.selection();engine_.noteOn(channel,bytes[1]&0x7f,static_cast<float>(bytes[2]&0x7f)/127.0f,eventDuration,selection!=0,selection);}
            } else if(type==0xb0 && (bytes[1]==120 || bytes[1]==123)) {
                renderTo(metadata.samplePosition);engine_.allSoundOff(channel);
            }
        }
    };
    if(patternEnabled_->get() && patternHostReady) {
        // MIDI at the same offset is processed first. No host MIDI output is generated.
        (void) pattern::forEachTrigger(audioPatternSchedule_,audioPattern_.bars,hostPpq,hostBpm,preparedSampleRate(),size,
            [&] (int offset,std::uint8_t note,std::uint8_t velocity) noexcept {
                processMidiThrough(offset);renderTo(offset);
                {const auto selection=samples_.selection();engine_.noteOn(1,note,static_cast<float>(velocity)/127.0f,eventDuration,selection!=0,selection);}
            });
    }
    processMidiThrough(size-1);
    midi.clear(); // Consume input: never echo host MIDI from this instrument.
    renderTo (size);
    const auto fxSnapshot=audioEffects_;
    for (std::size_t i=0;i<4;++i) {
        auto settings=effects::unpack(static_cast<std::uint16_t>(fxSnapshot>>(i*16)));
        const auto wet=effectWet_[i]->get(), amount=effectAmount_[i]->get();
        settings.wet=std::isfinite(wet)?juce::jlimit(0.0f,1.0f,wet):0.0f;
        settings.amount=std::isfinite(amount)?juce::jlimit(0.0f,1.0f,amount):0.0f;
        (void) effects_.set(i,settings);
    }
    effects_.process(left,right,static_cast<std::size_t>(size));
    const auto target = output_->get();
    outputSmoothing_.setTargetValue (std::isfinite (target) ? juce::jlimit (0.0f, 1.0f, target) : 0.0f);
    std::array<float,8> macroValues{};
    for(std::size_t i=0;i<8;++i)macroValues[i]=macros_[i]->get();
    float peak = 0.0f;
    for (int i = 0; i < size; ++i) {
        const auto gain = macroRuntime_.processStereo(outputSmoothing_.getNextValue(),macroValues);
        left[i] *= gain.left;
        right[i] *= gain.right;
        peak = std::max (peak, std::max (std::abs (left[i]), std::abs (right[i])));
    }
    peak_.store (peak, std::memory_order_relaxed);publishTelemetry(peak>=1.0f);
    if(peak>=1.0f)clipLatched_.store(true,std::memory_order_relaxed);
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
        } else if(section->data.size()==52 && wordAt(section->data,0)==2) {
            fx=0;
            for(std::size_t i=0;i<4;++i) {
                effects::Settings settings;
                const auto type=wordAt(section->data,4+i*12);
                if(type>=static_cast<std::uint32_t>(effects::Type::count)){error="Invalid FX effects type";return {};}
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
    auto held = std::make_shared<float> (0.0f);
    result.apply = [this, value, duration, program, held, fx, wet, amount, triggerPattern, patternEnabled, macroValues, macroRoutes, generatorState, preparedSample=std::move(preparedSample)] () mutable {
        (void) programs_.publishState (*program, fx, *triggerPattern, *macroRoutes, *generatorState);
        for(std::size_t i=0;i<8;++i)macros_[i]->setValueNotifyingHost(macroValues[i]);
        patternEnabled_->setValueNotifyingHost(patternEnabled?1.0f:0.0f);
        for(std::size_t i=0;i<4;++i){effectWet_[i]->setValueNotifyingHost(wet[i]);effectAmount_[i]->setValueNotifyingHost(amount[i]);}
        samples_.commitRestore(std::move(preparedSample));
        output_->setValueNotifyingHost (value);
        duration_->setValueNotifyingHost (duration);
        *held = static_cast<juce::RangedAudioParameter*> (duration_)->getValue();
    };
    result.settle = [this, duration, held] { loadedDuration_ = duration; heldDuration_ = *held; };
    return result;
}
} // namespace vox::fx

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new vox::fx::FxProcessor(); }
