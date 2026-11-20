#include "Processor.h"

#include "FactoryBank.h"
#include "FactoryContent.h"
#include "Parameters.h"
#include "SynthEditor.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace vox::syn {
namespace {

void putU32 (std::vector<std::byte>& out, std::uint32_t value)
{
    for (int shift = 0; shift < 32; shift += 8)
        out.push_back (static_cast<std::byte> ((value >> shift) & 0xffu));
}

bool takeU32 (const std::vector<std::byte>& in, std::size_t& at, std::uint32_t& value)
{
    if (at > in.size() || in.size() - at < 4)
        return false;
    value = 0;
    for (std::size_t i = 0; i < 4; ++i)
        value |= std::to_integer<std::uint32_t> (in[at + i]) << (8 * i);
    at += 4;
    return true;
}

float plainValue (juce::AudioProcessorParameter& parameter)
{
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (&parameter))
        return static_cast<float> (choice->getIndex());
    if (auto* number = dynamic_cast<juce::AudioParameterFloat*> (&parameter))
        return number->get();
    auto& ranged = dynamic_cast<juce::RangedAudioParameter&> (parameter);
    return ranged.convertFrom0to1 (ranged.getValue());
}

engine::EngineKind kindFrom (float raw) noexcept
{
    const int index = juce::roundToInt (raw);
    if (index <= 0)
        return engine::EngineKind::psyBass;
    if (index == 1)
        return engine::EngineKind::acid;
    return engine::EngineKind::va;
}

bool isPanic (const juce::MidiMessageMetadata& metadata) noexcept
{
    if (metadata.numBytes < 2 || (metadata.data[0] & 0xf0u) != 0xb0u)
        return false;
    const auto control = metadata.data[1];
    return control == 120 || control == 123;
}

// Note-off, note-on velocity 0, and sustain release. Dropping these is what
// leaves a gate up after a full event queue.
bool isRelease (const juce::MidiMessageMetadata& metadata) noexcept
{
    if (metadata.numBytes < 2)
        return false;
    const auto type = static_cast<std::uint8_t> (metadata.data[0] & 0xf0u);
    const auto data2 = metadata.numBytes > 2 ? metadata.data[2] : std::uint8_t { 0 };
    if (type == 0x80)
        return true;
    if (type == 0x90 && data2 == 0)
        return true;
    return type == 0xb0 && metadata.data[1] == 64 && data2 < 64;
}

void syncExposedFx (vstengine::effects::EffectsChain& fx, const std::array<std::atomic<float>*, parameterCatalog.size()>& raw) noexcept
{
    using vstengine::effects::EffectSettings;
    using vstengine::effects::EffectType;
    constexpr EffectType types[8] = {
        EffectType::distortion, EffectType::wavefolder, EffectType::phaser, EffectType::flanger,
        EffectType::chorus, EffectType::bitcrusher, EffectType::delay, EffectType::reverb
    };
    const auto load = [] (const std::atomic<float>* value) noexcept
    {
        return value != nullptr ? value->load (std::memory_order_relaxed) : 0.0f;
    };
    for (const auto& binding : fxBindings)
    {
        EffectSettings settings;
        settings.type = types[binding.slot];
        settings.enabled = load (raw[static_cast<std::size_t> (binding.enable)]) >= 0.5f;
        settings.mix = std::clamp (load (raw[static_cast<std::size_t> (binding.mix)]), 0.0f, 1.0f);
        settings.amount = std::clamp (load (raw[static_cast<std::size_t> (binding.amount)]), 0.0f, 1.0f);
        settings.rate = std::clamp (load (raw[static_cast<std::size_t> (binding.rate)]), 0.0f, 1.0f);
        settings.character = std::clamp (load (raw[static_cast<std::size_t> (binding.character)]), 0.0f, 1.0f);
        fx.setEffect (static_cast<std::size_t> (binding.slot), settings);
    }
}

preset::Section encodeParameters (const float* plain, int count)
{
    preset::Section section;
    section.tag = engineSectionTag;
    putU32 (section.data, 1u);
    putU32 (section.data, static_cast<std::uint32_t> (count));
    for (int i = 0; i < count; ++i)
    {
        const auto id = parameterCatalog[static_cast<std::size_t> (i)].id;
        const auto length = static_cast<std::uint32_t> (std::strlen (id));
        putU32 (section.data, length);
        for (std::uint32_t c = 0; c < length; ++c)
            section.data.push_back (static_cast<std::byte> (id[c]));
        std::uint32_t bits = 0;
        std::memcpy (&bits, &plain[i], sizeof bits);
        putU32 (section.data, bits);
    }
    return section;
}

} // namespace

SynthesisProcessor::SynthesisProcessor()
    : vox::suite::SuiteProcessor ({ JucePlugin_Name,
                                    vox::preset::makeTag (VOX_SUITE_STATE_TAG),
                                    1,
                                    juce::Colour::fromString ("FF9668F2") }),
      parameters_ (*this, nullptr, "VOXSYNTH", createParameterLayout())
{
    for (std::size_t i = 0; i < parameterCatalog.size(); ++i)
        raw_[i] = parameters_.getRawParameterValue (parameterCatalog[i].id);
    restored_.resize (parameterCatalog.size());
    for (std::size_t i = 0; i < parameterCatalog.size(); ++i)
    {
        const float plain = plainValue (*parameters_.getParameter (parameterCatalog[i].id));
        restored_[i] = { plain, plain };
    }
    const double bpm = phraseTempoBpm;
    std::uint64_t bits = 0;
    std::memcpy (&bits, &bpm, sizeof bits);
    clockBpmBits_.store (bits, std::memory_order_relaxed);
    publishLocked();
}

void SynthesisProcessor::prepareToPlay (double sampleRate, int maximumBlockSize)
{
    vox::suite::SuiteProcessor::prepareToPlay (sampleRate, maximumBlockSize);
    engine_.prepare (sampleRate, maximumBlockSize);
    sampleRate_ = sampleRate;
    const auto block = static_cast<std::uint32_t> (std::max (1, maximumBlockSize));
    fx_.prepare (sampleRate, block, 2u);
    prepared_ = sampleRate >= 8000.0 && sampleRate <= 384000.0 && maximumBlockSize > 0;
}

void SynthesisProcessor::releaseResources()
{
    engine_.reset();
    fx_.reset();
    prepared_ = false;
}

double SynthesisProcessor::getTailLengthSeconds() const
{
    // Voice release is voiceTailSeconds(). A VA release longer than that
    // extends the report. Delay is 6 s and reverb is 8 s when that slot is
    // enabled and its mix is above zero, matching EffectsChain::tailSamples.
    const auto load = [] (const std::atomic<float>* value) noexcept
    {
        return value != nullptr ? value->load (std::memory_order_relaxed) : 0.0f;
    };
    double tail = engine::voiceTailSeconds();
    for (int part = 0; part < engine::partCount; ++part)
    {
        const int base = part * 7;
        const int extra = part == 0 ? static_cast<int> (partAExtraIndex) : static_cast<int> (partBExtraIndex);
        if (kindFrom (load (raw_[static_cast<std::size_t> (base)])) != engine::EngineKind::va)
            continue;
        const float release = 0.02f + load (raw_[static_cast<std::size_t> (extra + static_cast<int> (PartExtra::release))]);
        tail = std::max (tail, static_cast<double> (release));
    }
    const bool reverb = load (raw_[reverbParameterIndex]) >= 0.5f && load (raw_[reverbMixIndex]) > 0.0f;
    const bool delay = load (raw_[delayParameterIndex]) >= 0.5f && load (raw_[delayMixIndex]) > 0.0f;
    if (reverb)
        tail = std::max (tail, 8.0);
    else if (delay)
        tail = std::max (tail, 6.0);
    return tail;
}

juce::AudioProcessorEditor* SynthesisProcessor::createEditor()
{
    auto* editor = new vox::suite::SuiteEditor (*this);
    editor->setContent (std::make_unique<SynthPanel> (*this));
    return editor;
}

void SynthesisProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    noteMidiSeen (midi);
    const int frames = buffer.getNumSamples();
    buffer.clear();
    if (! prepared_ || frames <= 0 || buffer.getNumChannels() < 2)
        return;

    hostBpm_ = 120.0;
    hostBpmValid_ = false;
    if (auto* head = getPlayHead())
        if (const auto position = head->getPosition())
            if (const auto hostBpm = position->getBpm())
                if (*hostBpm > 1.0)
                {
                    hostBpm_ = *hostBpm;
                    hostBpmValid_ = true;
                }

    const auto block = latchBlock();

    PhraseSnapshot snap;
    readSnapshot (snap);
    engine::Event phraseEvents[maxPhraseEvents];
    int phraseCount = 0;
    int phraseOffs = 0;
    collectPhrase (frames, snap, phraseEvents, phraseCount);
    for (int i = 0; i < phraseCount; ++i)
        if ((phraseEvents[i].status & 0xf0u) == 0x80u)
            ++phraseOffs;

    int count = 0;
    std::uint64_t dropped = 0;
    const int hostCap = std::max (0, engine::maxEvents - phraseOffs);
    const auto admit = [&] (const juce::MidiMessageMetadata& metadata)
    {
        if (count >= hostCap)
        {
            ++dropped;
            return;
        }
        engine::Event event;
        event.offset = static_cast<std::uint32_t> (juce::jlimit (0, frames - 1, metadata.samplePosition));
        event.status = metadata.data[0];
        event.data1 = metadata.numBytes > 1 ? metadata.data[1] : 0;
        event.data2 = metadata.numBytes > 2 ? metadata.data[2] : 0;
        event.articulation = 0;
        if (! insertEvent (event, count))
            ++dropped;
    };

    int total = 0;
    int panic = 0;
    int release = 0;
    for (const auto metadata : midi)
    {
        if (metadata.numBytes < 1 || metadata.data[0] == 0xf0)
            continue;
        ++total;
        if (isPanic (metadata))
            ++panic;
        else if (isRelease (metadata))
            ++release;
    }

    if (total <= hostCap)
    {
        for (const auto metadata : midi)
        {
            if (metadata.numBytes < 1 || metadata.data[0] == 0xf0)
                continue;
            admit (metadata);
        }
    }
    else
    {
        // Panic and releases keep the newest events, so a note-off or CC120
        // at the end of a saturated block is admitted. Other events keep
        // the earliest ones that still fit.
        const int keepPanic = std::min (panic, hostCap);
        const int keepRelease = std::min (release, hostCap - keepPanic);
        const int keepOther = hostCap - keepPanic - keepRelease;
        int skipPanic = panic - keepPanic;
        int skipRelease = release - keepRelease;
        int takenOther = 0;
        for (const auto metadata : midi)
        {
            if (metadata.numBytes < 1 || metadata.data[0] == 0xf0)
                continue;
            if (isPanic (metadata))
            {
                if (skipPanic > 0)
                {
                    --skipPanic;
                    ++dropped;
                    continue;
                }
            }
            else if (isRelease (metadata))
            {
                if (skipRelease > 0)
                {
                    --skipRelease;
                    ++dropped;
                    continue;
                }
            }
            else if (takenOther == keepOther)
            {
                ++dropped;
                continue;
            }
            else
            {
                ++takenOther;
            }
            admit (metadata);
        }
    }
    if (dropped != 0)
        droppedMidi_.fetch_add (dropped, std::memory_order_relaxed);

    for (int i = 0; i < phraseCount; ++i)
    {
        const bool off = (phraseEvents[i].status & 0xf0u) == 0x80u;
        if (! off && count >= engine::maxEvents)
        {
            droppedMidi_.fetch_add (1, std::memory_order_relaxed);
            continue;
        }
        if (! insertEvent (phraseEvents[i], count))
            continue;
        const int note = phraseEvents[i].data1;
        if (note > 127)
            continue;
        if (off)
        {
            if (phraseHeld_[note] > 0)
                --phraseHeld_[note];
        }
        else if (phraseHeld_[note] < 255)
            ++phraseHeld_[note];
    }
    takePreview (frames, count);
    runEngine (buffer, count, block);
}

engine::BlockParams SynthesisProcessor::latchBlock()
{
    ExtraRoutes routes;
    readModSnapshot (routes);
    engine::BlockParams block;
    const auto load = [] (std::atomic<float>* value) noexcept
    {
        return value != nullptr ? value->load (std::memory_order_relaxed) : 0.0f;
    };
    for (int part = 0; part < engine::partCount; ++part)
    {
        const int base = part * 7;
        const int extra = part == 0 ? static_cast<int> (partAExtraIndex) : static_cast<int> (partBExtraIndex);
        auto& dst = block.part[part];
        dst.engine = kindFrom (load (raw_[static_cast<std::size_t> (base)]));
        dst.level = load (raw_[static_cast<std::size_t> (base + 1)]);
        dst.cutoff = load (raw_[static_cast<std::size_t> (base + 2)]);
        dst.resonance = load (raw_[static_cast<std::size_t> (base + 3)]);
        dst.slide = load (raw_[static_cast<std::size_t> (base + 4)]);
        dst.portamento = load (raw_[static_cast<std::size_t> (base + 5)]);
        dst.drive = load (raw_[static_cast<std::size_t> (base + 6)]);
        dst.sub = load (raw_[static_cast<std::size_t> (extra + static_cast<int> (PartExtra::sub))]);
        dst.pitchSeconds = static_cast<double> (load (raw_[static_cast<std::size_t> (extra + static_cast<int> (PartExtra::pitch))])) * 0.1;
        dst.square = load (raw_[static_cast<std::size_t> (extra + static_cast<int> (PartExtra::wave))]);
        dst.env = load (raw_[static_cast<std::size_t> (extra + static_cast<int> (PartExtra::env))]);
        dst.detuneCents = static_cast<double> (load (raw_[static_cast<std::size_t> (extra + static_cast<int> (PartExtra::detune))])) * 50.0;
        dst.attack = 0.001 + static_cast<double> (load (raw_[static_cast<std::size_t> (extra + static_cast<int> (PartExtra::attack))])) * 0.2;
        dst.decay = 0.01 + static_cast<double> (load (raw_[static_cast<std::size_t> (extra + static_cast<int> (PartExtra::decay))])) * 2.0;
        dst.sustain = static_cast<double> (load (raw_[static_cast<std::size_t> (extra + static_cast<int> (PartExtra::sustain))]));
        dst.release = 0.02 + static_cast<double> (load (raw_[static_cast<std::size_t> (extra + static_cast<int> (PartExtra::release))]));
        dst.noise = load (raw_[static_cast<std::size_t> (extra + static_cast<int> (PartExtra::noise))]);
        const int va = vaParamBase + part * vaParamCount;
        const auto knob = [&] (VaKnob which) noexcept
        {
            return load (raw_[static_cast<std::size_t> (va + static_cast<int> (which))]);
        };
        dst.oscWave[0] = std::clamp (juce::roundToInt (knob (VaKnob::osc1Wave)), 0, 4);
        dst.oscLevel[0] = knob (VaKnob::osc1Level);
        dst.oscPulse[0] = knob (VaKnob::osc1Pw);
        dst.oscWave[1] = std::clamp (juce::roundToInt (knob (VaKnob::osc2Wave)), 0, 4);
        dst.oscLevel[1] = knob (VaKnob::osc2Level);
        dst.oscPulse[1] = knob (VaKnob::osc2Pw);
        dst.oscWave[2] = std::clamp (juce::roundToInt (knob (VaKnob::osc3Wave)), 0, 4);
        dst.oscLevel[2] = knob (VaKnob::osc3Level);
        dst.oscPulse[2] = 0.5f;
        dst.osc3Semi = (knob (VaKnob::osc3Semi) - 0.5f) * 48.0f;
        dst.vaSub = knob (VaKnob::sub);
        dst.ring = knob (VaKnob::ring);
        dst.fm = knob (VaKnob::fm);
        dst.filterType = std::clamp (juce::roundToInt (knob (VaKnob::filterType)), 0, 5);
        dst.filter2Type = std::clamp (juce::roundToInt (knob (VaKnob::filter2Type)), 0, 5);
        dst.filter2Cutoff = knob (VaKnob::filter2Cutoff);
        dst.filter2Resonance = knob (VaKnob::filter2Resonance);
        dst.filter2Mix = knob (VaKnob::filter2Mix);
        dst.filterRoute = std::clamp (juce::roundToInt (knob (VaKnob::filterRoute)), 0, 1);
        dst.filterEnv = knob (VaKnob::filterEnv);
        dst.keyTrack = knob (VaKnob::keyTrack);
        const int adsr = filterAdsrBase + part * 4;
        dst.filterAttack = 0.001 + static_cast<double> (load (raw_[static_cast<std::size_t> (adsr)])) * 0.2;
        dst.filterDecay = 0.01 + static_cast<double> (load (raw_[static_cast<std::size_t> (adsr + 1)])) * 2.0;
        dst.filterSustain = static_cast<double> (load (raw_[static_cast<std::size_t> (adsr + 2)]));
        dst.filterRelease = 0.02 + static_cast<double> (load (raw_[static_cast<std::size_t> (adsr + 3)]));
        const int lfo = lfoBase + part * (lfoCount * 2);
        for (int index = 0; index < lfoCount; ++index)
        {
            const float rate = load (raw_[static_cast<std::size_t> (lfo + index * 2)]);
            dst.lfoRate[index] = 0.05f * std::pow (400.0f, std::clamp (rate, 0.0f, 1.0f));
            dst.lfoShape[index] = std::clamp (juce::roundToInt (load (raw_[static_cast<std::size_t> (lfo + index * 2 + 1)])), 0, 6);
        }
        const int route = routeBase + part * (routeCount * 3);
        for (int index = 0; index < routeCount; ++index)
        {
            const int at = route + index * 3;
            dst.routeSource[index] = std::clamp (juce::roundToInt (load (raw_[static_cast<std::size_t> (at)])), 0, 12);
            dst.routeDest[index] = std::clamp (juce::roundToInt (load (raw_[static_cast<std::size_t> (at + 1)])), 0, 5);
            dst.routeAmount[index] = (load (raw_[static_cast<std::size_t> (at + 2)]) - 0.5f) * 2.0f;
        }
        const int feature = featureBase + part * featureCount;
        dst.unison = 1 + std::clamp (juce::roundToInt (load (raw_[static_cast<std::size_t> (feature)])), 0, 3);
        dst.unisonDetune = load (raw_[static_cast<std::size_t> (feature + 1)]) * 40.0f;
        dst.unisonSpread = load (raw_[static_cast<std::size_t> (feature + 2)]);
        dst.wtLevel = load (raw_[static_cast<std::size_t> (feature + 3)]);
        dst.wtPos = load (raw_[static_cast<std::size_t> (feature + 4)]);
        for (int index = 0; index < lfoCount; ++index)
        {
            const int at = feature + 5 + index * 3;
            dst.lfoSync[index] = load (raw_[static_cast<std::size_t> (at)]) >= 0.5f;
            dst.lfoDiv[index] = std::clamp (juce::roundToInt (load (raw_[static_cast<std::size_t> (at + 1)])), 0, 8);
            dst.lfoRetrig[index] = load (raw_[static_cast<std::size_t> (at + 2)]) >= 0.5f;
        }
        for (int index = 0; index < extraRouteCount; ++index)
        {
            dst.routeSource[routeCount + index] = routes.row[part][index].source;
            dst.routeDest[routeCount + index] = routes.row[part][index].dest;
            dst.routeAmount[routeCount + index] = routes.row[part][index].amount;
        }
        if (part == 0)
            for (int index = 0; index < macroCount; ++index)
                block.macro[index] = load (raw_[static_cast<std::size_t> (macroBase + index)]);
    }
    block.output = load (raw_[outputParameterIndex]);
    block.bpm = hostBpm_;
    block.bpmValid = hostBpmValid_;
    syncExposedFx (fx_, raw_);
    return block;
}

void SynthesisProcessor::runEngine (juce::AudioBuffer<float>& buffer, int count, const engine::BlockParams& block)
{
    const int frames = buffer.getNumSamples();
    engine_.process (buffer.getWritePointer (0), buffer.getWritePointer (1), frames, events_, count, block);
    if (fx_.isPrepared())
    {
        float* audio[2] = { buffer.getWritePointer (0), buffer.getWritePointer (1) };
        fx_.process (std::span<float*> (audio, 2), static_cast<std::uint32_t> (frames));
    }
}

void SynthesisProcessor::audition (juce::AudioBuffer<float>& buffer, const AuditionEvent* events, int eventCount)
{
    juce::ScopedNoDenormals noDenormals;
    const int frames = buffer.getNumSamples();
    buffer.clear();
    if (! prepared_ || frames <= 0 || buffer.getNumChannels() < 2)
        return;
    const auto block = latchBlock();
    const int count = std::min (eventCount, engine::maxEvents);
    for (int i = 0; i < count; ++i)
    {
        engine::Event event;
        event.offset = static_cast<std::uint32_t> (juce::jlimit (0, frames - 1, events[i].sample));
        event.status = static_cast<std::uint8_t> (events[i].on ? 0x90 : 0x80);
        event.data1 = static_cast<std::uint8_t> (events[i].note);
        event.data2 = static_cast<std::uint8_t> (events[i].on ? std::clamp (events[i].velocity, 1, 127) : 0);
        event.articulation = 0;
        if (events[i].accent)
            event.articulation = static_cast<std::uint8_t> (event.articulation | engine::articulateAccent);
        if (events[i].slide)
            event.articulation = static_cast<std::uint8_t> (event.articulation | engine::articulateSlide);
        int index = i;
        while (index > 0 && events_[index - 1].offset > event.offset)
        {
            events_[index] = events_[index - 1];
            --index;
        }
        events_[index] = event;
    }
    runEngine (buffer, count, block);
}

bool SynthesisProcessor::insertEvent (const engine::Event& event, int& count)
{
    if (count >= engine::maxEvents)
        return false;
    int index = count;
    while (index > 0 && events_[index - 1].offset > event.offset)
    {
        events_[index] = events_[index - 1];
        --index;
    }
    events_[index] = event;
    ++count;
    return true;
}

bool SynthesisProcessor::insertKeepingRelease (const engine::Event& event, int& count)
{
    if (insertEvent (event, count))
        return true;
    const auto type = static_cast<std::uint8_t> (event.status & 0xf0u);
    const bool release = type == 0x80u || (type == 0x90u && event.data2 == 0);
    if (! release)
    {
        droppedMidi_.fetch_add (1, std::memory_order_relaxed);
        return false;
    }
    int victim = -1;
    for (int i = count - 1; i >= 0; --i)
    {
        const auto held = static_cast<std::uint8_t> (events_[i].status & 0xf0u);
        const bool heldRelease = held == 0x80u || (held == 0x90u && events_[i].data2 == 0)
                              || (held == 0xb0u && (events_[i].data1 == 120 || events_[i].data1 == 123));
        if (! heldRelease)
        {
            victim = i;
            break;
        }
    }
    if (victim < 0)
    {
        droppedMidi_.fetch_add (1, std::memory_order_relaxed);
        return false;
    }
    for (int i = victim; i < count - 1; ++i)
        events_[i] = events_[i + 1];
    --count;
    droppedMidi_.fetch_add (1, std::memory_order_relaxed);
    return insertEvent (event, count);
}

void SynthesisProcessor::takePreview (int frames, int& count)
{
    const int stage = previewStage_.load (std::memory_order_acquire);
    if (stage == 0 || frames <= 0)
        return;
    if (stage == 1)
    {
        const int replace = previewReplace_.exchange (-1, std::memory_order_relaxed);
        if (replace >= 0 && replace < 128)
        {
            engine::Event off;
            off.offset = 0;
            off.status = 0x80;
            off.data1 = static_cast<std::uint8_t> (replace);
            insertKeepingRelease (off, count);
        }
        engine::Event on;
        on.offset = 0;
        on.status = 0x90;
        on.data1 = static_cast<std::uint8_t> (std::clamp (previewNote_.load (std::memory_order_relaxed), 0, 127));
        on.data2 = 96;
        if (! insertEvent (on, count))
        {
            droppedMidi_.fetch_add (1, std::memory_order_relaxed);
            previewStage_.store (0, std::memory_order_release);
            return;
        }
        previewStage_.store (2, std::memory_order_release);
        return;
    }
    const int left = previewLeft_.load (std::memory_order_relaxed) - frames;
    previewLeft_.store (left, std::memory_order_relaxed);
    if (left > 0)
        return;
    engine::Event off;
    off.offset = 0;
    off.status = 0x80;
    off.data1 = static_cast<std::uint8_t> (std::clamp (previewNote_.load (std::memory_order_relaxed), 0, 127));
    insertKeepingRelease (off, count);
    previewStage_.store (0, std::memory_order_release);
}

void SynthesisProcessor::requestPreview (int note) noexcept
{
    note = std::clamp (note, 0, 127);
    const int stage = previewStage_.exchange (0, std::memory_order_acq_rel);
    if (stage == 2)
        previewReplace_.store (previewNote_.load (std::memory_order_relaxed), std::memory_order_relaxed);
    else
        previewReplace_.store (-1, std::memory_order_relaxed);
    previewNote_.store (note, std::memory_order_relaxed);
    const double rate = sampleRate_ > 1.0 ? sampleRate_ : 48000.0;
    previewLeft_.store (std::max (1, static_cast<int> (rate * 0.40)), std::memory_order_relaxed);
    previewStage_.store (1, std::memory_order_release);
}

void SynthesisProcessor::publishLocked()
{
    const PhraseNote* notes = nullptr;
    int count = 0;
    int length = std::max (1, phraseLength_);
    if (phraseEdited_)
    {
        notes = phraseEdit_.data();
        count = static_cast<int> (phraseEdit_.size());
    }
    else
    {
        const auto& phrase = factoryPhrase (phraseIndex_);
        notes = phrase.notes;
        count = phrase.noteCount;
        length = std::max (1, phrase.lengthTicks);
        phraseLength_ = length;
    }
    PhraseSnapshot snap;
    if (! fillPerformanceSnapshot (notes, count, length, snap))
        return;
    const int revision = ++phraseRevision_;
    snap.stamp = revision;
    snap.stampEnd = revision;

    auto reclaim = [this] ()
    {
        const int published = phrasePublished_.load (std::memory_order_acquire);
        for (int i = 0; i < 3; ++i)
        {
            if (i == published)
                continue;
            int expected = slotReady;
            phraseSlots_[i].state.compare_exchange_strong (expected, slotFree, std::memory_order_acq_rel);
        }
    };

    int slot = -1;
    for (int attempt = 0; attempt < 8 && slot < 0; ++attempt)
    {
        reclaim();
        for (int i = 0; i < 3; ++i)
        {
            int expected = slotFree;
            if (phraseSlots_[i].state.compare_exchange_strong (expected, slotWriting, std::memory_order_acq_rel))
            {
                slot = i;
                break;
            }
        }
    }
    if (slot < 0)
        return;
    phraseSlots_[slot].snap = snap;
    phraseSlots_[slot].state.store (slotReady, std::memory_order_release);
    const int previous = phrasePublished_.exchange (slot, std::memory_order_acq_rel);
    if (previous != slot)
    {
        int expected = slotReady;
        phraseSlots_[previous].state.compare_exchange_strong (expected, slotFree, std::memory_order_acq_rel);
    }
}

void SynthesisProcessor::readSnapshot (PhraseSnapshot& out) noexcept
{
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        const int slot = phrasePublished_.load (std::memory_order_acquire);
        if (phraseHandoffPark_.load (std::memory_order_acquire) == 1)
        {
            phraseHandoffPark_.store (2, std::memory_order_release);
            while (phraseHandoffPark_.load (std::memory_order_acquire) == 2)
            {
            }
        }
        if (slot < 0 || slot > 2)
            continue;
        int expected = slotReady;
        if (! phraseSlots_[slot].state.compare_exchange_strong (expected, slotReading, std::memory_order_acq_rel))
            continue;
        const PhraseSnapshot local = phraseSlots_[slot].snap;
        const int now = phrasePublished_.load (std::memory_order_acquire);
        int owned = slotReading;
        phraseSlots_[slot].state.compare_exchange_strong (owned, now == slot ? slotReady : slotFree, std::memory_order_acq_rel);
        if (local.stamp == local.stampEnd && local.stamp != 0)
        {
            rtLast_ = local;
            out = local;
            return;
        }
        phraseClaimViolations_.fetch_add (1, std::memory_order_relaxed);
    }
    out = rtLast_;
}

namespace {
double loadBits (const std::atomic<std::uint64_t>& bits) noexcept
{
    const auto raw = bits.load (std::memory_order_relaxed);
    double value = 0.0;
    std::memcpy (&value, &raw, sizeof value);
    return value;
}

void storeBits (std::atomic<std::uint64_t>& bits, double value) noexcept
{
    std::uint64_t raw = 0;
    std::memcpy (&raw, &value, sizeof raw);
    bits.store (raw, std::memory_order_relaxed);
}
}

void SynthesisProcessor::setPhraseIndex (int index)
{
    const juce::ScopedLock lock (phraseLock_);
    if (index < 0 || index >= factoryPhraseCount())
        return;
    phraseIndex_ = index;
    phraseEdited_ = false;
    phraseEdit_.clear();
    publishLocked();
    phraseClockEpoch_.fetch_add (1, std::memory_order_release);
    if (phrasePlaying_.load (std::memory_order_relaxed))
        phraseStopEpoch_.fetch_add (1, std::memory_order_release);
}

int SynthesisProcessor::phraseIndex() const
{
    const juce::ScopedLock lock (phraseLock_);
    return phraseIndex_;
}

void SynthesisProcessor::setPhrasePlaying (bool playing)
{
    const juce::ScopedLock lock (phraseLock_);
    const bool was = phrasePlaying_.load (std::memory_order_relaxed);
    if (was && ! playing)
        phraseStopEpoch_.fetch_add (1, std::memory_order_release);
    if (playing && ! was)
        phraseClockEpoch_.fetch_add (1, std::memory_order_release);
    phrasePlaying_.store (playing, std::memory_order_release);
}

void SynthesisProcessor::setPhraseClock (bool forced, bool hostPlaying, double ppq, double bpm) noexcept
{
    clockForced_.store (forced, std::memory_order_relaxed);
    clockHost_.store (hostPlaying, std::memory_order_relaxed);
    storeBits (clockPpqBits_, ppq);
    storeBits (clockBpmBits_, bpm > 1.0 ? bpm : phraseTempoBpm);
}

int SynthesisProcessor::phraseNoteCount() const
{
    const juce::ScopedLock lock (phraseLock_);
    if (phraseEdited_)
        return static_cast<int> (phraseEdit_.size());
    return factoryPhrase (phraseIndex_).noteCount;
}

PhraseNote SynthesisProcessor::phraseNote (int index) const
{
    const juce::ScopedLock lock (phraseLock_);
    if (phraseEdited_)
    {
        if (index < 0 || index >= static_cast<int> (phraseEdit_.size()))
            return {};
        return phraseEdit_[static_cast<std::size_t> (index)];
    }
    const auto& phrase = factoryPhrase (phraseIndex_);
    if (index < 0 || index >= phrase.noteCount || phrase.notes == nullptr)
        return {};
    return phrase.notes[index];
}

void SynthesisProcessor::setPhraseNote (int index, PhraseNote note)
{
    const juce::ScopedLock lock (phraseLock_);
    if (! phraseEdited_)
    {
        const auto& phrase = factoryPhrase (phraseIndex_);
        phraseEdit_.assign (phrase.notes, phrase.notes + phrase.noteCount);
        phraseLength_ = phrase.lengthTicks;
        phraseEdited_ = true;
    }
    if (index < 0 || index >= static_cast<int> (phraseEdit_.size()))
        return;
    note.note = std::clamp (note.note, 0, 127);
    note.velocity = std::clamp (note.velocity, 1, 127);
    note.duration = std::max (1, note.duration);
    note.tick = std::max (0, note.tick);
    phraseEdit_[static_cast<std::size_t> (index)] = note;
    publishLocked();
}

std::vector<std::byte> SynthesisProcessor::exportCurrentPhrase() const
{
    const juce::ScopedLock lock (phraseLock_);
    if (phraseEdited_)
        return exportPhraseSmf (phraseEdit_.data(), static_cast<int> (phraseEdit_.size()));
    const auto& phrase = factoryPhrase (phraseIndex_);
    return exportPhraseSmf (phrase.notes, phrase.noteCount);
}

void SynthesisProcessor::collectPhrase (int frames, const PhraseSnapshot& snap, engine::Event* phraseEvents, int& phraseCount)
{
    phraseCount = 0;
    if (frames <= 0)
        return;
    const int stopEpoch = phraseStopEpoch_.load (std::memory_order_acquire);
    if (stopEpoch != rtStopEpoch_)
    {
        rtStopEpoch_ = stopEpoch;
        rtPlayed_ = 0;
        for (int note = 0; note < 128; ++note)
        {
            while (phraseHeld_[note] > 0 && phraseCount < maxPhraseEvents)
            {
                engine::Event event;
                event.offset = 0;
                event.status = 0x80;
                event.data1 = static_cast<std::uint8_t> (note);
                phraseEvents[phraseCount++] = event;
                --phraseHeld_[note];
            }
        }
        return;
    }
    if (! phrasePlaying_.load (std::memory_order_acquire))
        return;

    const int clockEpoch = phraseClockEpoch_.load (std::memory_order_acquire);
    if (clockEpoch != rtClockEpoch_)
    {
        rtClockEpoch_ = clockEpoch;
        rtPlayed_ = 0;
    }

    bool host = false;
    double ppq = 0.0;
    double bpm = phraseTempoBpm;
    if (clockForced_.load (std::memory_order_relaxed))
    {
        host = clockHost_.load (std::memory_order_relaxed);
        ppq = loadBits (clockPpqBits_);
        bpm = loadBits (clockBpmBits_);
    }
    else if (auto* head = getPlayHead())
    {
        if (const auto position = head->getPosition())
        {
            if (position->getIsPlaying())
            {
                host = true;
                if (const auto hostPpq = position->getPpqPosition())
                    ppq = *hostPpq;
                if (const auto hostBpm = position->getBpm())
                    if (*hostBpm > 1.0)
                        bpm = *hostBpm;
            }
        }
    }
    if (bpm <= 1.0)
        bpm = phraseTempoBpm;

    const auto push = [&] (const ScheduledEvent& event, int offset)
    {
        if (offset < 0 || offset >= frames || phraseCount >= maxPhraseEvents)
            return;
        engine::Event midi;
        midi.offset = static_cast<std::uint32_t> (offset);
        midi.status = static_cast<std::uint8_t> (event.on ? 0x90 : 0x80);
        midi.data1 = static_cast<std::uint8_t> (event.note & 0x7f);
        midi.data2 = static_cast<std::uint8_t> (event.on ? std::clamp (event.velocity, 1, 127) : 0);
        if (event.accent)
            midi.articulation = static_cast<std::uint8_t> (midi.articulation | engine::articulateAccent);
        if (event.slide)
            midi.articulation = static_cast<std::uint8_t> (midi.articulation | engine::articulateSlide);
        phraseEvents[phraseCount++] = midi;
    };

    const int loopTicks = std::max (1, snap.loopTicks);
    if (host)
    {
        const double ticksPerSample = (bpm / 60.0) * phrasePulsesPerQuarter / sampleRate_;
        if (ticksPerSample <= 0.0)
            return;
        const double startTick = ppq * phrasePulsesPerQuarter;
        const double endTick = startTick + static_cast<double> (frames) * ticksPerSample;
        const double cycle = std::floor (startTick / static_cast<double> (loopTicks)) * loopTicks;
        for (int i = 0; i < snap.eventCount; ++i)
        {
            const auto& event = snap.events[i];
            for (int k = -1; k <= 1; ++k)
            {
                const double at = cycle + static_cast<double> (k * loopTicks + event.tick);
                if (at < startTick || at >= endTick)
                    continue;
                push (event, static_cast<int> (std::llround ((at - startTick) / ticksPerSample)));
            }
        }
        return;
    }

    const int loopSamples = std::max (1, phraseSample (loopTicks, sampleRate_, bpm));
    for (int i = 0; i < snap.eventCount; ++i)
    {
        const auto& event = snap.events[i];
        std::int64_t at = phraseSample (event.tick, sampleRate_, bpm);
        if (at < rtPlayed_)
        {
            const auto behind = rtPlayed_ - at;
            const auto steps = (behind + loopSamples - 1) / loopSamples;
            at += steps * static_cast<std::int64_t> (loopSamples);
        }
        if (at >= rtPlayed_ && at < rtPlayed_ + frames)
            push (event, static_cast<int> (at - rtPlayed_));
    }
    rtPlayed_ += frames;
}

void SynthesisProcessor::writeProductState (std::vector<preset::Section>& out) const
{
    float plain[parameterCatalog.size()];
    for (std::size_t i = 0; i < parameterCatalog.size(); ++i)
    {
        const float current = plainValue (*parameters_.getParameter (parameterCatalog[i].id));
        plain[i] = juce::exactlyEqual (current, restored_[i].held) ? restored_[i].loaded : current;
    }
    out.push_back (encodeParameters (plain, static_cast<int> (parameterCatalog.size())));
    const juce::ScopedLock lock (bankLock_);
    preset::Section bank;
    bank.tag = userBankTag;
    putU32 (bank.data, 1u);
    putU32 (bank.data, static_cast<std::uint32_t> (userPrograms_.size()));
    for (const auto& program : userPrograms_)
    {
        const auto utf8 = program.name.toRawUTF8();
        const auto length = static_cast<std::uint32_t> (std::min<std::size_t> (std::strlen (utf8), 64));
        putU32 (bank.data, length);
        for (std::uint32_t c = 0; c < length; ++c)
            bank.data.push_back (static_cast<std::byte> (utf8[c]));
        putU32 (bank.data, static_cast<std::uint32_t> (program.category));
        putU32 (bank.data, static_cast<std::uint32_t> (program.values.size()));
        for (float value : program.values)
        {
            std::uint32_t bits = 0;
            std::memcpy (&bits, &value, sizeof bits);
            putU32 (bank.data, bits);
        }
    }
    out.push_back (std::move (bank));
    int phraseIndex = 0;
    bool phraseEdited = false;
    int phraseLength = 1;
    std::vector<PhraseNote> phraseNotes;
    {
        const juce::ScopedLock phraseLock (phraseLock_);
        phraseIndex = phraseIndex_;
        phraseEdited = phraseEdited_;
        phraseLength = phraseLength_;
        if (phraseEdited_)
            phraseNotes = phraseEdit_;
    }
    preset::Section phrase;
    phrase.tag = phraseSectionTag;
    putU32 (phrase.data, 1u);
    putU32 (phrase.data, static_cast<std::uint32_t> (std::max (0, phraseIndex)));
    putU32 (phrase.data, phraseEdited ? 1u : 0u);
    putU32 (phrase.data, static_cast<std::uint32_t> (std::max (1, phraseLength)));
    putU32 (phrase.data, static_cast<std::uint32_t> (phraseNotes.size()));
    for (const auto& note : phraseNotes)
    {
        putU32 (phrase.data, static_cast<std::uint32_t> (note.tick));
        putU32 (phrase.data, static_cast<std::uint32_t> (note.note));
        putU32 (phrase.data, static_cast<std::uint32_t> (note.velocity));
        putU32 (phrase.data, static_cast<std::uint32_t> (note.duration));
        putU32 (phrase.data, note.slide ? 1u : 0u);
        putU32 (phrase.data, note.accent ? 1u : 0u);
    }
    out.push_back (std::move (phrase));
    ExtraRoutes routes;
    {
        const juce::ScopedLock lock (modLock_);
        routes = modEdit_;
    }
    preset::Section mod;
    mod.tag = modSectionTag;
    putU32 (mod.data, 1u);
    for (int part = 0; part < 2; ++part)
        for (int index = 0; index < extraRouteCount; ++index)
        {
            putU32 (mod.data, static_cast<std::uint32_t> (routes.row[part][index].source));
            putU32 (mod.data, static_cast<std::uint32_t> (routes.row[part][index].dest));
            std::uint32_t bits = 0;
            std::memcpy (&bits, &routes.row[part][index].amount, sizeof bits);
            putU32 (mod.data, bits);
        }
    out.push_back (std::move (mod));
}

SynthesisProcessor::ProductRestore SynthesisProcessor::readProductState (const preset::StateDocument& document,
                                                                        juce::String& error)
{
    float plain[parameterCatalog.size()];
    for (std::size_t i = 0; i < parameterCatalog.size(); ++i)
        plain[i] = parameterCatalog[i].initial;

    if (const auto* section = document.find (engineSectionTag))
    {
        std::size_t at = 0;
        std::uint32_t version = 0, count = 0;
        if (! takeU32 (section->data, at, version) || version != 1u || ! takeU32 (section->data, at, count) || count > 256)
        {
            error = "malformed ENG1 section";
            return {};
        }
        for (std::uint32_t i = 0; i < count; ++i)
        {
            std::uint32_t length = 0, bits = 0;
            if (! takeU32 (section->data, at, length) || length == 0 || length > 64
                || section->data.size() - at < static_cast<std::size_t> (length) + 4u)
            {
                error = "malformed ENG1 section";
                return {};
            }
            std::string id (length, '\0');
            for (std::uint32_t c = 0; c < length; ++c)
                id[c] = static_cast<char> (section->data[at + c]);
            at += length;
            if (! takeU32 (section->data, at, bits))
            {
                error = "malformed ENG1 section";
                return {};
            }
            float value = 0.0f;
            std::memcpy (&value, &bits, sizeof value);
            if (! std::isfinite (value))
            {
                error = "non-finite ENG1 value";
                return {};
            }
            for (std::size_t p = 0; p < parameterCatalog.size(); ++p)
                if (id == parameterCatalog[p].id)
                    plain[p] = std::clamp (value, parameterCatalog[p].minimum, parameterCatalog[p].maximum);
        }
        if (at != section->data.size())
        {
            error = "malformed ENG1 section";
            return {};
        }
    }

    auto bank = std::make_shared<std::vector<UserProgram>>();
    if (const auto* section = document.find (userBankTag))
    {
        std::size_t at = 0;
        std::uint32_t version = 0, count = 0;
        if (! takeU32 (section->data, at, version) || version != 1u || ! takeU32 (section->data, at, count) || count > static_cast<std::uint32_t> (maxUserPrograms))
        {
            error = "malformed UBNK section";
            return {};
        }
        bank->reserve (count);
        for (std::uint32_t i = 0; i < count; ++i)
        {
            std::uint32_t length = 0, category = 0, values = 0;
            if (! takeU32 (section->data, at, length) || length > 64
                || section->data.size() - at < static_cast<std::size_t> (length))
            {
                error = "malformed UBNK section";
                return {};
            }
            std::string name (length, '\0');
            for (std::uint32_t c = 0; c < length; ++c)
                name[c] = static_cast<char> (section->data[at + c]);
            at += length;
            if (! takeU32 (section->data, at, category) || ! takeU32 (section->data, at, values)
                || category >= static_cast<std::uint32_t> (ProgramCategory::count)
                || values > parameterCatalog.size())
            {
                error = "malformed UBNK section";
                return {};
            }
            UserProgram program;
            program.name = name;
            program.category = static_cast<int> (category);
            program.values.resize (parameterCatalog.size());
            for (std::size_t p = 0; p < parameterCatalog.size(); ++p)
                program.values[p] = parameterCatalog[p].initial;
            for (std::uint32_t v = 0; v < values; ++v)
            {
                std::uint32_t bits = 0;
                if (! takeU32 (section->data, at, bits))
                {
                    error = "malformed UBNK section";
                    return {};
                }
                float value = 0.0f;
                std::memcpy (&value, &bits, sizeof value);
                if (! std::isfinite (value))
                {
                    error = "non-finite UBNK value";
                    return {};
                }
                program.values[v] = std::clamp (value, parameterCatalog[v].minimum, parameterCatalog[v].maximum);
            }
            bank->push_back (std::move (program));
        }
        if (at != section->data.size())
        {
            error = "malformed UBNK section";
            return {};
        }
    }

    struct PhraseState {
        int index = 0;
        bool edited = false;
        int length = 3840;
        std::vector<PhraseNote> notes;
    };
    auto phrase = std::make_shared<PhraseState>();
    if (const auto* section = document.find (phraseSectionTag))
    {
        std::size_t at = 0;
        std::uint32_t version = 0, index = 0, edited = 0, length = 0, count = 0;
        if (! takeU32 (section->data, at, version) || version != 1u
            || ! takeU32 (section->data, at, index) || ! takeU32 (section->data, at, edited)
            || ! takeU32 (section->data, at, length) || ! takeU32 (section->data, at, count)
            || index >= static_cast<std::uint32_t> (factoryPhraseCount())
            || length == 0 || length > 1000000u
            || count > static_cast<std::uint32_t> (maxPhraseNotes)
            || (edited > 1u))
        {
            error = "malformed PHRS section";
            return {};
        }
        phrase->index = static_cast<int> (index);
        phrase->edited = edited == 1u;
        phrase->length = static_cast<int> (length);
        if (phrase->edited)
        {
            phrase->notes.resize (count);
            for (std::uint32_t i = 0; i < count; ++i)
            {
                std::uint32_t tick = 0, note = 0, velocity = 0, duration = 0, slide = 0, accent = 0;
                if (! takeU32 (section->data, at, tick) || ! takeU32 (section->data, at, note)
                    || ! takeU32 (section->data, at, velocity) || ! takeU32 (section->data, at, duration)
                    || ! takeU32 (section->data, at, slide) || ! takeU32 (section->data, at, accent)
                    || note > 127u || velocity < 1u || velocity > 127u || duration < 1u || slide > 1u || accent > 1u)
                {
                    error = "malformed PHRS section";
                    return {};
                }
                phrase->notes[i] = { static_cast<int> (tick), static_cast<int> (note), static_cast<int> (velocity),
                                     static_cast<int> (duration), slide == 1u, accent == 1u };
            }
        }
        else if (count != 0u)
        {
            error = "malformed PHRS section";
            return {};
        }
        if (at != section->data.size())
        {
            error = "malformed PHRS section";
            return {};
        }
    }

    auto values = std::make_shared<std::array<float, parameterCatalog.size()>>();
    for (std::size_t i = 0; i < values->size(); ++i)
        (*values)[i] = plain[i];
    ProductRestore restore;
    restore.sections.push_back (encodeParameters (values->data(), static_cast<int> (values->size())));
    preset::Section encoded;
    encoded.tag = userBankTag;
    putU32 (encoded.data, 1u);
    putU32 (encoded.data, static_cast<std::uint32_t> (bank->size()));
    for (const auto& program : *bank)
    {
        const auto utf8 = program.name.toRawUTF8();
        const auto length = static_cast<std::uint32_t> (std::min<std::size_t> (std::strlen (utf8), 64));
        putU32 (encoded.data, length);
        for (std::uint32_t c = 0; c < length; ++c)
            encoded.data.push_back (static_cast<std::byte> (utf8[c]));
        putU32 (encoded.data, static_cast<std::uint32_t> (program.category));
        putU32 (encoded.data, static_cast<std::uint32_t> (program.values.size()));
        for (float value : program.values)
        {
            std::uint32_t bits = 0;
            std::memcpy (&bits, &value, sizeof bits);
            putU32 (encoded.data, bits);
        }
    }
    restore.sections.push_back (std::move (encoded));
    preset::Section phraseSection;
    phraseSection.tag = phraseSectionTag;
    putU32 (phraseSection.data, 1u);
    putU32 (phraseSection.data, static_cast<std::uint32_t> (phrase->index));
    putU32 (phraseSection.data, phrase->edited ? 1u : 0u);
    putU32 (phraseSection.data, static_cast<std::uint32_t> (phrase->length));
    putU32 (phraseSection.data, static_cast<std::uint32_t> (phrase->notes.size()));
    for (const auto& note : phrase->notes)
    {
        putU32 (phraseSection.data, static_cast<std::uint32_t> (note.tick));
        putU32 (phraseSection.data, static_cast<std::uint32_t> (note.note));
        putU32 (phraseSection.data, static_cast<std::uint32_t> (note.velocity));
        putU32 (phraseSection.data, static_cast<std::uint32_t> (note.duration));
        putU32 (phraseSection.data, note.slide ? 1u : 0u);
        putU32 (phraseSection.data, note.accent ? 1u : 0u);
    }
    restore.sections.push_back (std::move (phraseSection));
    auto routes = std::make_shared<ExtraRoutes>();
    if (const auto* section = document.find (modSectionTag))
    {
        std::size_t at = 0;
        std::uint32_t version = 0;
        if (! takeU32 (section->data, at, version) || version != 1u)
        {
            error = "malformed MODX section";
            return {};
        }
        for (int part = 0; part < 2; ++part)
            for (int index = 0; index < extraRouteCount; ++index)
            {
                std::uint32_t source = 0, dest = 0, bits = 0;
                if (! takeU32 (section->data, at, source) || ! takeU32 (section->data, at, dest) || ! takeU32 (section->data, at, bits)
                    || source > 12u || dest > 5u)
                {
                    error = "malformed MODX section";
                    return {};
                }
                float amount = 0.0f;
                std::memcpy (&amount, &bits, sizeof amount);
                if (! std::isfinite (amount))
                {
                    error = "non-finite MODX value";
                    return {};
                }
                routes->row[part][index].source = static_cast<int> (source);
                routes->row[part][index].dest = static_cast<int> (dest);
                routes->row[part][index].amount = std::clamp (amount, -1.0f, 1.0f);
            }
        if (at != section->data.size())
        {
            error = "malformed MODX section";
            return {};
        }
    }
    preset::Section modSection;
    modSection.tag = modSectionTag;
    putU32 (modSection.data, 1u);
    for (int part = 0; part < 2; ++part)
        for (int index = 0; index < extraRouteCount; ++index)
        {
            putU32 (modSection.data, static_cast<std::uint32_t> (routes->row[part][index].source));
            putU32 (modSection.data, static_cast<std::uint32_t> (routes->row[part][index].dest));
            std::uint32_t bits = 0;
            std::memcpy (&bits, &routes->row[part][index].amount, sizeof bits);
            putU32 (modSection.data, bits);
        }
    restore.sections.push_back (std::move (modSection));
    restore.apply = [this, values, bank, phrase, routes] {
        for (std::size_t i = 0; i < parameterCatalog.size(); ++i)
        {
            auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (parameters_.getParameter (parameterCatalog[i].id));
            if (parameter == nullptr)
                continue;
            parameter->setValueNotifyingHost (parameter->convertTo0to1 ((*values)[i]));
        }
        const juce::ScopedLock lock (bankLock_);
        userPrograms_ = *bank;
        const juce::ScopedLock phraseLock (phraseLock_);
        phraseIndex_ = phrase->index;
        phraseEdited_ = phrase->edited;
        phraseLength_ = phrase->length;
        phraseEdit_ = phrase->notes;
        publishLocked();
        phraseClockEpoch_.fetch_add (1, std::memory_order_release);
        if (phrasePlaying_.load (std::memory_order_relaxed))
            phraseStopEpoch_.fetch_add (1, std::memory_order_release);
        const juce::ScopedLock modLock (modLock_);
        modEdit_ = *routes;
        publishModLocked();
    };
    restore.settle = [this, values] {
        for (std::size_t i = 0; i < parameterCatalog.size(); ++i)
        {
            const float held = plainValue (*parameters_.getParameter (parameterCatalog[i].id));
            restored_[i] = { (*values)[i], held };
        }
    };
    return restore;
}

int SynthesisProcessor::userProgramCount() const
{
    const juce::ScopedLock lock (bankLock_);
    return static_cast<int> (userPrograms_.size());
}

juce::String SynthesisProcessor::userProgramName (int index) const
{
    const juce::ScopedLock lock (bankLock_);
    if (index < 0 || index >= static_cast<int> (userPrograms_.size()))
        return {};
    return userPrograms_[static_cast<std::size_t> (index)].name;
}

int SynthesisProcessor::userProgramCategory (int index) const
{
    const juce::ScopedLock lock (bankLock_);
    if (index < 0 || index >= static_cast<int> (userPrograms_.size()))
        return 0;
    return userPrograms_[static_cast<std::size_t> (index)].category;
}

bool SynthesisProcessor::saveUserProgram (const juce::String& name, int category)
{
    if (name.isEmpty() || category < 0 || category >= static_cast<int> (ProgramCategory::count))
        return false;
    UserProgram program;
    program.name = name.substring (0, 64);
    program.category = category;
    program.values.resize (parameterCatalog.size());
    for (std::size_t i = 0; i < parameterCatalog.size(); ++i)
        program.values[i] = plainValue (*parameters_.getParameter (parameterCatalog[i].id));
    const juce::ScopedLock lock (bankLock_);
    if (static_cast<int> (userPrograms_.size()) >= maxUserPrograms)
        return false;
    userPrograms_.push_back (std::move (program));
    return true;
}

bool SynthesisProcessor::loadUserProgram (int index)
{
    std::vector<float> values;
    {
        const juce::ScopedLock lock (bankLock_);
        if (index < 0 || index >= static_cast<int> (userPrograms_.size()))
            return false;
        values = userPrograms_[static_cast<std::size_t> (index)].values;
    }
    for (std::size_t i = 0; i < parameterCatalog.size() && i < values.size(); ++i)
    {
        auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (parameters_.getParameter (parameterCatalog[i].id));
        if (parameter == nullptr)
            continue;
        const float clamped = std::clamp (values[i], parameterCatalog[i].minimum, parameterCatalog[i].maximum);
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (clamped));
    }
    return true;
}

int SynthesisProcessor::factoryProgramCount() const noexcept
{
    return vox::syn::factoryProgramCount();
}

juce::String SynthesisProcessor::factoryProgramName (int index) const
{
    if (index < 0 || index >= vox::syn::factoryProgramCount())
        return {};
    const auto& program = factoryProgram (index);
    return program.name;
}

int SynthesisProcessor::factoryProgramRole (int index) const
{
    if (index < 0 || index >= vox::syn::factoryProgramCount())
        return 0;
    return static_cast<int> (factoryProgram (index).role);
}

juce::String SynthesisProcessor::factoryProgramSearch (int index) const
{
    if (index < 0 || index >= vox::syn::factoryProgramCount())
        return {};
    const auto& program = factoryProgram (index);
    return juce::String (program.id) + " " + program.name + " " + program.style + " "
         + program.variant + " " + program.mood + " " + program.purpose;
}

void SynthesisProcessor::readModSnapshot (ExtraRoutes& out) noexcept
{
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        const int slot = modPublished_.load (std::memory_order_acquire);
        if (modHandoffPark_.load (std::memory_order_acquire) == 1)
        {
            modHandoffPark_.store (2, std::memory_order_release);
            while (modHandoffPark_.load (std::memory_order_acquire) == 2)
            {
            }
        }
        if (slot < 0 || slot > 2)
            continue;
        int expected = slotReady;
        if (! modSlots_[slot].state.compare_exchange_strong (expected, slotReading, std::memory_order_acq_rel))
            continue;
        const int stamp = modSlots_[slot].stamp;
        const ExtraRoutes local = modSlots_[slot].routes;
        const int stampEnd = modSlots_[slot].stampEnd;
        const int now = modPublished_.load (std::memory_order_acquire);
        int owned = slotReading;
        modSlots_[slot].state.compare_exchange_strong (owned, now == slot ? slotReady : slotFree, std::memory_order_acq_rel);
        if (stamp == stampEnd && stamp != 0)
        {
            modLast_ = local;
            out = local;
            return;
        }
        modClaimViolations_.fetch_add (1, std::memory_order_relaxed);
    }
    out = modLast_;
}

void SynthesisProcessor::publishModLocked() noexcept
{
    const int revision = ++modRevision_;
    auto reclaim = [this] ()
    {
        const int published = modPublished_.load (std::memory_order_acquire);
        for (int i = 0; i < 3; ++i)
        {
            if (i == published)
                continue;
            int expected = slotReady;
            modSlots_[i].state.compare_exchange_strong (expected, slotFree, std::memory_order_acq_rel);
        }
    };
    int slot = -1;
    for (int attempt = 0; attempt < 8 && slot < 0; ++attempt)
    {
        reclaim();
        for (int i = 0; i < 3; ++i)
        {
            int expected = slotFree;
            if (modSlots_[i].state.compare_exchange_strong (expected, slotWriting, std::memory_order_acq_rel))
            {
                slot = i;
                break;
            }
        }
    }
    if (slot < 0)
        return;
    modSlots_[slot].stamp = revision;
    modSlots_[slot].stampEnd = 0;
    modSlots_[slot].routes = modEdit_;
    modSlots_[slot].stampEnd = revision;
    modSlots_[slot].state.store (slotReady, std::memory_order_release);
    const int previous = modPublished_.exchange (slot, std::memory_order_acq_rel);
    if (previous != slot)
    {
        int expected = slotReady;
        modSlots_[previous].state.compare_exchange_strong (expected, slotFree, std::memory_order_acq_rel);
    }
}

void SynthesisProcessor::setModRoute (int part, int route, int source, int dest, float amount01)
{
    if (part < 0 || part > 1 || route < 0 || route >= engine::PartParams::routeSlots)
        return;
    source = std::clamp (source, 0, 12);
    dest = std::clamp (dest, 0, 5);
    amount01 = std::clamp (amount01, 0.0f, 1.0f);
    if (route < routeCount)
    {
        const juce::String prefix = juce::String (part == 0 ? "syn.a.mod" : "syn.b.mod") + juce::String (route + 1);
        const auto write = [&] (const juce::String& suffix, float value)
        {
            auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (parameters_.getParameter (prefix + suffix));
            if (parameter != nullptr)
                parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
        };
        write (".src", static_cast<float> (source));
        write (".dst", static_cast<float> (dest));
        write (".amt", amount01);
        return;
    }
    const juce::ScopedLock lock (modLock_);
    auto& row = modEdit_.row[part][route - routeCount];
    row.source = source;
    row.dest = dest;
    row.amount = (amount01 - 0.5f) * 2.0f;
    publishModLocked();
}

void SynthesisProcessor::getModRoute (int part, int route, int& source, int& dest, float& amount01)
{
    source = 0;
    dest = 0;
    amount01 = 0.5f;
    if (part < 0 || part > 1 || route < 0 || route >= engine::PartParams::routeSlots)
        return;
    if (route < routeCount)
    {
        const juce::String prefix = juce::String (part == 0 ? "syn.a.mod" : "syn.b.mod") + juce::String (route + 1);
        const auto read = [&] (const juce::String& suffix)
        {
            auto* parameter = parameters_.getParameter (prefix + suffix);
            return parameter == nullptr ? 0.0f : plainValue (*parameter);
        };
        source = juce::roundToInt (read (".src"));
        dest = juce::roundToInt (read (".dst"));
        amount01 = read (".amt");
        return;
    }
    const juce::ScopedLock lock (modLock_);
    const auto& row = modEdit_.row[part][route - routeCount];
    source = row.source;
    dest = row.dest;
    amount01 = std::clamp (row.amount * 0.5f + 0.5f, 0.0f, 1.0f);
}

bool SynthesisProcessor::loadFactoryProgram (int index)
{
    if (index < 0 || index >= vox::syn::factoryProgramCount())
        return false;
    const float* values = factoryProgram (index).values;
    const FactoryFeature feature = factoryFeature (index);
    for (std::size_t i = 0; i < parameterCatalog.size(); ++i)
    {
        auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (parameters_.getParameter (parameterCatalog[i].id));
        if (parameter == nullptr)
            continue;
        const float source = i < static_cast<std::size_t> (frozenParameterCount) ? values[i] : parameterCatalog[i].initial;
        const float clamped = std::clamp (source, parameterCatalog[i].minimum, parameterCatalog[i].maximum);
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (clamped));
    }
    const auto write = [&] (const char* id, float value)
    {
        auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (parameters_.getParameter (id));
        if (parameter == nullptr)
            return;
        const auto& range = parameter->getNormalisableRange();
        const float clamped = std::clamp (value, range.start, range.end);
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (clamped));
    };
    write ("syn.a.unison", feature.unison);
    write ("syn.a.uni.detune", feature.detune);
    write ("syn.a.uni.spread", feature.spread);
    write ("syn.a.wt.level", feature.wtLevel);
    write ("syn.a.wt.pos", feature.wtPos);
    write ("syn.a.fm", feature.fm);
    write ("syn.a.ring", feature.ring);
    write ("syn.a.osc3.level", feature.osc3Level);
    write ("syn.a.osc3.semi", feature.osc3Semi);
    for (int part = 0; part < 2; ++part)
        for (int route = routeCount; route < engine::PartParams::routeSlots; ++route)
            setModRoute (part, route, 0, 0, 0.5f);
    setModRoute (0, 0, feature.modSource, feature.modDest, feature.modAmount);
    if (feature.extraRoute >= routeCount)
        setModRoute (0, feature.extraRoute, feature.extraSource, feature.extraDest, feature.extraAmount);
    return true;
}

int SynthesisProcessor::factoryProgramAudition (int index) const
{
    if (index < 0 || index >= vox::syn::factoryProgramCount())
        return 60;
    return factoryProgram (index).auditionNote;
}

} // namespace vox::syn

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new vox::syn::SynthesisProcessor();
}
