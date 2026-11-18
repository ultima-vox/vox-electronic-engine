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
    output_ = new juce::AudioParameterFloat (juce::ParameterID { "fx.output.gain", 1 },
                                              "Output Gain", 0.0f, 1.0f, 1.0f);
    addParameter (output_);
    duration_ = new juce::AudioParameterFloat (juce::ParameterID { "fx.event.duration", 1 },
                                                "Event Duration", 0.05f, 32.0f, 4.0f);
    addParameter (duration_);
    loadedDuration_ = heldDuration_ = static_cast<juce::RangedAudioParameter*> (duration_)->getValue();
    for (std::size_t i=0;i<4;++i) {
        const auto number=juce::String(static_cast<int>(i+1));
        effectWet_[i]=new juce::AudioParameterFloat(juce::ParameterID{"fx.slot."+number+".wet",1},"FX " + number + " Wet",0.0f,1.0f,1.0f);
        effectAmount_[i]=new juce::AudioParameterFloat(juce::ParameterID{"fx.slot."+number+".amount",1},"FX " + number + " Amount",0.0f,1.0f,0.5f);
        addParameter(effectWet_[i]);addParameter(effectAmount_[i]);
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
    if (committed) restoreEpoch_.fetch_add (1, std::memory_order_release);
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
    if (committed) restoreEpoch_.fetch_add (1, std::memory_order_release);
    return committed;
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
    programSequence_ = 0; audioEffects_ = effects::defaults();
    outputSmoothing_.reset (std::isfinite (rate) && rate >= 8000.0 && rate <= 384000.0 ? rate : 44100.0, 0.005);
    outputSmoothing_.setCurrentAndTargetValue (output_->get());
    panicPending_.store (false, std::memory_order_relaxed);
    auditionPending_.store (false, std::memory_order_relaxed);
    peak_.store (0.0f, std::memory_order_relaxed);
    transportKnown_ = wasPlaying_ = expectedSampleKnown_ = false;
}

void FxProcessor::releaseResources() { engine_.prepare (preparedSampleRate()); }
void FxProcessor::reset() { panic(); }
juce::AudioProcessorEditor* FxProcessor::createEditor()
{
    auto editor = std::make_unique<vox::suite::SuiteEditor> (*this);
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
    bool positionAvailable = false;
    if (auto* host = getPlayHead()) {
        if (const auto position = host->getPosition()) {
            positionAvailable = true;
            const bool playing = position->getIsPlaying();
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
    if (programs_.tryRead (audioProgram_, audioEffects_, programSequence_))
        (void) engine_.setProgram (audioProgram_);
    const float requestedDuration = duration_->get();
    const double eventDuration = std::isfinite (requestedDuration)
        ? juce::jlimit (0.05, 32.0, static_cast<double> (requestedDuration)) : 4.0;
    if (auditionPending_.exchange (false, std::memory_order_acquire)) engine_.noteOn (1, 60, 0.8f, eventDuration);
    const int size = buffer.getNumSamples();
    if (buffer.getNumChannels() < 2 || size == 0) {
        peak_.store (0.0f, std::memory_order_relaxed);
        return;
    }
    auto* left = buffer.getWritePointer (0);
    auto* right = buffer.getWritePointer (1);
    int position = 0;
    auto renderTo = [&] (int end) {
        if (end > position) engine_.render (left + position, right + position, end - position);
        position = end;
    };
    for (const auto metadata : midi)
    {
        // Ignore malformed and out-of-block packets; never materialize a
        // MidiMessage (a long SysEx message could allocate on this thread).
        if (metadata.numBytes < 1 || metadata.samplePosition < 0 || metadata.samplePosition >= size) continue;
        const auto* bytes = metadata.data;
        const int status = bytes[0];
        const int type = status & 0xf0;
        if ((type != 0x90 && type != 0xb0) || metadata.numBytes < 3) continue;
        const int channel = (status & 0x0f) + 1;
        if (type == 0x90 && bytes[2] != 0) {
            renderTo (metadata.samplePosition);
            engine_.noteOn (channel, bytes[1] & 0x7f, static_cast<float> (bytes[2] & 0x7f) / 127.0f, eventDuration);
        } else if (type == 0xb0 && (bytes[1] == 120 || bytes[1] == 123)) {
            renderTo (metadata.samplePosition);
            engine_.allSoundOff (channel);
        }
        // NoteOff and zero-velocity NoteOn are deliberately ignored in this
        // interim one-shot mode. CC120/123 and reset use a 5 ms fade.
    }
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
    float peak = 0.0f;
    for (int i = 0; i < size; ++i) {
        const auto gain = outputSmoothing_.getNextValue();
        left[i] *= gain;
        right[i] *= gain;
        peak = std::max (peak, std::max (std::abs (left[i]), std::abs (right[i])));
    }
    peak_.store (peak, std::memory_order_relaxed);
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
}

FxProcessor::ProductRestore FxProcessor::readProductState (const preset::StateDocument& doc, juce::String& error)
{
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
            fx=static_cast<std::uint64_t>(wordAt(section->data,4)) |
               (static_cast<std::uint64_t>(wordAt(section->data,8))<<32);
            if (!effects::validPacked(fx)) {error="Invalid FX effects settings";return {};}
            for(std::size_t i=0;i<4;++i){const auto s=effects::unpack(static_cast<std::uint16_t>(fx>>(i*16)));wet[i]=s.wet;amount[i]=s.amount;}
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
    ProductRestore result;
    result.sections.push_back (encodeParameter (value, duration));
    result.sections.push_back (encodeFamily (static_cast<int> (program->layers[0].family)));
    result.sections.push_back (encodeProgram (*program));
    result.sections.push_back (encodeEffects (fx,wet,amount));
    auto held = std::make_shared<float> (0.0f);
    result.apply = [this, value, duration, program, held, fx, wet, amount] {
        (void) programs_.publishState (*program, fx);
        for(std::size_t i=0;i<4;++i){effectWet_[i]->setValueNotifyingHost(wet[i]);effectAmount_[i]->setValueNotifyingHost(amount[i]);}
        restoreEpoch_.fetch_add (1, std::memory_order_release);
        output_->setValueNotifyingHost (value);
        duration_->setValueNotifyingHost (duration);
        *held = static_cast<juce::RangedAudioParameter*> (duration_)->getValue();
    };
    result.settle = [this, duration, held] { loadedDuration_ = duration; heldDuration_ = *held; };
    return result;
}
} // namespace vox::fx

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new vox::fx::FxProcessor(); }
