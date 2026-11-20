#include "Processor.h"

#include "FactoryBank.h"
#include "FactoryContent.h"
#include "Parameters.h"
#include "SynthEditor.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

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

int previewSamples (double sampleRate, double seconds) noexcept
{
    const double rate = sampleRate > 1.0 ? sampleRate : 48000.0;
    return std::max (1, static_cast<int> (std::lround (rate * seconds)));
}

void pushPreview (engine::Event* events, int& count, std::uint32_t at, std::uint8_t status, int note, int velocity, std::uint8_t articulation) noexcept
{
    if (count >= previewEventCap || note < 0 || note > 127)
        return;
    engine::Event event;
    event.offset = at;
    event.status = status;
    event.data1 = static_cast<std::uint8_t> (note);
    event.data2 = static_cast<std::uint8_t> (std::clamp (velocity, 0, 127));
    event.articulation = articulation;
    events[count++] = event;
}

// Built on the message thread. Note-offs belong to this command, so a newer
// Hear cannot retarget them. Offsets stay inside one short gesture.
void fillRoleScore (FactoryRole role, int root, double sampleRate, engine::Event* events, int& count, int& tail) noexcept
{
    count = 0;
    root = std::clamp (root, 0, 127);
    const auto on = [&] (int at, int note, int velocity, bool slide, bool accent)
    {
        std::uint8_t articulation = 0;
        if (accent)
            articulation = static_cast<std::uint8_t> (articulation | engine::articulateAccent);
        if (slide)
            articulation = static_cast<std::uint8_t> (articulation | engine::articulateSlide);
        pushPreview (events, count, static_cast<std::uint32_t> (std::max (0, at)), 0x90, std::clamp (note, 0, 127), velocity, articulation);
    };
    const auto off = [&] (int at, int note)
    {
        pushPreview (events, count, static_cast<std::uint32_t> (std::max (0, at)), 0x80, std::clamp (note, 0, 127), 0, 0);
    };
    switch (role)
    {
        case FactoryRole::acid:
        {
            const int step = previewSamples (sampleRate, 0.125);
            const int gate = std::max (1, step * 3 / 4);
            const int line[] = { 0, 1, 3, 0, 5, -2 };
            int previous = -1;
            for (int i = 0; i < 6; ++i)
            {
                const int note = std::clamp (root + line[i], 0, 127);
                const int at = i * step;
                const bool accent = (i % 2) == 0;
                const bool slide = i > 0 && (i % 2) == 1;
                on (at, note, accent ? 118 : 84, slide, accent);
                if (previous >= 0)
                    off (at, previous);
                previous = note;
            }
            if (previous >= 0)
                off (5 * step + gate, previous);
            tail = previewSamples (sampleRate, 0.25);
            break;
        }
        case FactoryRole::arp:
        {
            const int step = previewSamples (sampleRate, 0.125);
            const int gate = std::max (1, step / 2);
            const int line[] = { 0, 3, 7, 12, 7, 3 };
            for (int i = 0; i < 6; ++i)
            {
                const int note = std::clamp (root + line[i], 0, 127);
                on (i * step, note, 100, false, i == 0);
                off (i * step + gate, note);
            }
            tail = previewSamples (sampleRate, 0.20);
            break;
        }
        case FactoryRole::padAtmos:
        {
            const int hold = previewSamples (sampleRate, 1.10);
            const int notes[] = { root, std::min (127, root + 7), std::min (127, root + 12) };
            for (int note : notes)
                on (0, note, 76, false, false);
            for (int note : notes)
                off (hold, note);
            tail = previewSamples (sampleRate, 0.80);
            break;
        }
        case FactoryRole::pluckKeys:
        case FactoryRole::stab:
        {
            const int hold = previewSamples (sampleRate, role == FactoryRole::stab ? 0.09 : 0.22);
            const int notes[] = { root, std::min (127, root + 3), std::min (127, root + 7) };
            for (int note : notes)
                on (0, note, role == FactoryRole::stab ? 112 : 96, false, role == FactoryRole::stab);
            for (int note : notes)
                off (hold, note);
            tail = previewSamples (sampleRate, 0.35);
            break;
        }
        case FactoryRole::lead:
        {
            const int step = previewSamples (sampleRate, 0.18);
            const int gate = previewSamples (sampleRate, 0.14);
            const int line[] = { 0, 3, 7, 3, 12 };
            for (int i = 0; i < 5; ++i)
            {
                const int note = std::clamp (root + line[i], 0, 127);
                on (i * step, note, 104, false, i == 0);
                off (i * step + gate, note);
            }
            tail = previewSamples (sampleRate, 0.25);
            break;
        }
        case FactoryRole::bass:
        default:
        {
            const int step = previewSamples (sampleRate, 0.14);
            const int gate = previewSamples (sampleRate, 0.10);
            const int notes[] = { root, std::max (0, root - 12), std::min (127, root + 7) };
            for (int i = 0; i < 3; ++i)
            {
                on (i * step, notes[i], 104, false, i == 0);
                off (i * step + gate, notes[i]);
            }
            tail = previewSamples (sampleRate, 0.20);
            break;
        }
    }
}

FactoryRole roleForCategory (int category) noexcept
{
    switch (category)
    {
        case static_cast<int> (ProgramCategory::acid): return FactoryRole::acid;
        case static_cast<int> (ProgramCategory::lead): return FactoryRole::lead;
        case static_cast<int> (ProgramCategory::pluck):
        case static_cast<int> (ProgramCategory::keys): return FactoryRole::pluckKeys;
        case static_cast<int> (ProgramCategory::pad):
        case static_cast<int> (ProgramCategory::atmos): return FactoryRole::padAtmos;
        case static_cast<int> (ProgramCategory::bass):
        default: return FactoryRole::bass;
    }
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

constexpr vstengine::effects::EffectType previewFxTypes[vstengine::effects::EffectsChain::effectCount] = {
    vstengine::effects::EffectType::distortion, vstengine::effects::EffectType::wavefolder,
    vstengine::effects::EffectType::phaser, vstengine::effects::EffectType::flanger,
    vstengine::effects::EffectType::chorus, vstengine::effects::EffectType::bitcrusher,
    vstengine::effects::EffectType::delay, vstengine::effects::EffectType::reverb
};

template <typename Load>
void fillFxSlots (vstengine::effects::EffectSettings* out, Load load) noexcept
{
    using vstengine::effects::EffectSettings;
    for (const auto& binding : fxBindings)
    {
        EffectSettings settings;
        settings.type = previewFxTypes[binding.slot];
        settings.enabled = load (binding.enable) >= 0.5f;
        settings.mix = std::clamp (load (binding.mix), 0.0f, 1.0f);
        settings.amount = std::clamp (load (binding.amount), 0.0f, 1.0f);
        settings.rate = std::clamp (load (binding.rate), 0.0f, 1.0f);
        settings.character = std::clamp (load (binding.character), 0.0f, 1.0f);
        out[binding.slot] = settings;
    }
}

void syncExposedFx (vstengine::effects::EffectsChain& fx, const std::array<std::atomic<float>*, parameterCatalog.size()>& raw) noexcept
{
    const auto load = [&raw] (int index) noexcept
    {
        const auto* value = raw[static_cast<std::size_t> (index)];
        return value != nullptr ? value->load (std::memory_order_relaxed) : 0.0f;
    };
    vstengine::effects::EffectSettings settings[vstengine::effects::EffectsChain::effectCount];
    fillFxSlots (settings, load);
    for (std::size_t slot = 0; slot < vstengine::effects::EffectsChain::effectCount; ++slot)
        fx.setEffect (slot, settings[slot]);
}

void appendUserProgram (std::vector<std::byte>& data, const UserProgram& program)
{
    const auto utf8 = program.name.toRawUTF8();
    const auto length = static_cast<std::uint32_t> (std::min<std::size_t> (std::strlen (utf8), 64));
    putU32 (data, length);
    for (std::uint32_t c = 0; c < length; ++c)
        data.push_back (static_cast<std::byte> (utf8[c]));
    putU32 (data, static_cast<std::uint32_t> (program.category));
    putU32 (data, static_cast<std::uint32_t> (program.values.size()));
    for (float value : program.values)
    {
        std::uint32_t bits = 0;
        std::memcpy (&bits, &value, sizeof bits);
        putU32 (data, bits);
    }
    putU32 (data, 2u);
    putU32 (data, static_cast<std::uint32_t> (extraRouteCount));
    for (int part = 0; part < 2; ++part)
        for (int route = 0; route < extraRouteCount; ++route)
        {
            putU32 (data, static_cast<std::uint32_t> (program.extra[part][route].source));
            putU32 (data, static_cast<std::uint32_t> (program.extra[part][route].dest));
            std::uint32_t bits = 0;
            std::memcpy (&bits, &program.extra[part][route].amount, sizeof bits);
            putU32 (data, bits);
        }
}

void appendUserBank (preset::Section& bank, const std::vector<UserProgram>& programs)
{
    bank.tag = userBankTag;
    bank.data.clear();
    putU32 (bank.data, userBankVersion);
    putU32 (bank.data, static_cast<std::uint32_t> (programs.size()));
    for (const auto& program : programs)
        appendUserProgram (bank.data, program);
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
    previewEngine_.prepare (sampleRate, maximumBlockSize);
    previewEngine_.reset();
    previewCapacity_ = std::max (1, maximumBlockSize);
    previewMixL_.assign (static_cast<std::size_t> (previewCapacity_), 0.0f);
    previewMixR_.assign (static_cast<std::size_t> (previewCapacity_), 0.0f);
    if (rtPreviewActive_)
        rtFinishedGen_ = rtPreviewGen_;
    rtPreviewActive_ = false;
    rtSoundingCount_ = 0;
    rtPreviewNote_ = -1;
    rtPreviewCursor_ = 0;
    rtPreviewPos_ = 0;
    rtPreviewTail_ = 0;
    rtPreviewEventCount_ = 0;
    rtPreviewGen_ = rtFinishedGen_;
    previewBusReady_ = false;
    sampleRate_ = sampleRate;
    const auto block = static_cast<std::uint32_t> (std::max (1, maximumBlockSize));
    fx_.prepare (sampleRate, block, 2u);
    previewFx_.prepare (sampleRate, block, 2u);
    prepared_ = sampleRate >= 8000.0 && sampleRate <= 384000.0 && maximumBlockSize > 0;
}

void SynthesisProcessor::releaseResources()
{
    engine_.reset();
    previewEngine_.reset();
    rtPreviewActive_ = false;
    rtSoundingCount_ = 0;
    previewBusReady_ = false;
    fx_.reset();
    previewFx_.reset();
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
    runEngine (buffer, count, block);
    servicePreview (buffer, frames);
}

engine::BlockParams SynthesisProcessor::latchBlock()
{
    float plain[parameterCatalog.size()];
    for (std::size_t i = 0; i < parameterCatalog.size(); ++i)
        plain[i] = raw_[i] != nullptr ? raw_[i]->load (std::memory_order_relaxed) : 0.0f;
    ExtraRoutes routes;
    readModSnapshot (routes);
    auto block = mapPlain (plain, routes);
    block.bpm = hostBpm_;
    block.bpmValid = hostBpmValid_;
    syncExposedFx (fx_, raw_);
    return block;
}

engine::BlockParams SynthesisProcessor::mapPlain (const float* plain, const ExtraRoutes& routes) const
{
    engine::BlockParams block;
    const auto load = [plain] (int index) noexcept
    {
        return plain[index];
    };
    for (int part = 0; part < engine::partCount; ++part)
    {
        const int base = part * 7;
        const int extra = part == 0 ? static_cast<int> (partAExtraIndex) : static_cast<int> (partBExtraIndex);
        auto& dst = block.part[part];
        dst.engine = kindFrom (load (base));
        dst.level = load (base + 1);
        dst.cutoff = load (base + 2);
        dst.resonance = load (base + 3);
        dst.slide = load (base + 4);
        dst.portamento = load (base + 5);
        dst.drive = load (base + 6);
        dst.sub = load (extra + static_cast<int> (PartExtra::sub));
        dst.pitchSeconds = static_cast<double> (load (extra + static_cast<int> (PartExtra::pitch))) * 0.1;
        dst.square = load (extra + static_cast<int> (PartExtra::wave));
        dst.env = load (extra + static_cast<int> (PartExtra::env));
        dst.detuneCents = static_cast<double> (load (extra + static_cast<int> (PartExtra::detune))) * 50.0;
        dst.attack = 0.001 + static_cast<double> (load (extra + static_cast<int> (PartExtra::attack))) * 0.2;
        dst.decay = 0.01 + static_cast<double> (load (extra + static_cast<int> (PartExtra::decay))) * 2.0;
        dst.sustain = static_cast<double> (load (extra + static_cast<int> (PartExtra::sustain)));
        dst.release = 0.02 + static_cast<double> (load (extra + static_cast<int> (PartExtra::release)));
        dst.noise = load (extra + static_cast<int> (PartExtra::noise));
        const int va = vaParamBase + part * vaParamCount;
        const auto knob = [&] (VaKnob which) noexcept
        {
            return load (va + static_cast<int> (which));
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
        dst.filterAttack = 0.001 + static_cast<double> (load (adsr)) * 0.2;
        dst.filterDecay = 0.01 + static_cast<double> (load (adsr + 1)) * 2.0;
        dst.filterSustain = static_cast<double> (load (adsr + 2));
        dst.filterRelease = 0.02 + static_cast<double> (load (adsr + 3));
        const int lfo = lfoBase + part * (lfoCount * 2);
        for (int index = 0; index < lfoCount; ++index)
        {
            const float rate = load (lfo + index * 2);
            dst.lfoRate[index] = 0.05f * std::pow (400.0f, std::clamp (rate, 0.0f, 1.0f));
            dst.lfoShape[index] = std::clamp (juce::roundToInt (load (lfo + index * 2 + 1)), 0, 6);
        }
        const int route = routeBase + part * (routeCount * 3);
        for (int index = 0; index < routeCount; ++index)
        {
            const int at = route + index * 3;
            dst.routeSource[index] = std::clamp (juce::roundToInt (load (at)), 0, 12);
            dst.routeDest[index] = std::clamp (juce::roundToInt (load (at + 1)), 0, 5);
            dst.routeAmount[index] = (load (at + 2) - 0.5f) * 2.0f;
        }
        const int feature = featureBase + part * featureCount;
        dst.unison = 1 + std::clamp (juce::roundToInt (load (feature)), 0, 3);
        dst.unisonDetune = load (feature + 1) * 40.0f;
        dst.unisonSpread = load (feature + 2);
        dst.wtLevel = load (feature + 3);
        dst.wtPos = load (feature + 4);
        for (int index = 0; index < lfoCount; ++index)
        {
            const int at = feature + 5 + index * 3;
            dst.lfoSync[index] = load (at) >= 0.5f;
            dst.lfoDiv[index] = std::clamp (juce::roundToInt (load (at + 1)), 0, 8);
            dst.lfoRetrig[index] = load (at + 2) >= 0.5f;
        }
        for (int index = 0; index < extraRouteCount; ++index)
        {
            dst.routeSource[routeCount + index] = routes.row[part][index].source;
            dst.routeDest[routeCount + index] = routes.row[part][index].dest;
            dst.routeAmount[routeCount + index] = routes.row[part][index].amount;
        }
        if (part == 0)
            for (int index = 0; index < macroCount; ++index)
                block.macro[index] = load (macroBase + index);
    }
    block.output = load (static_cast<int> (outputParameterIndex));
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

engine::BlockParams SynthesisProcessor::liveBlock (PreviewFx& fx) const
{
    float plain[parameterCatalog.size()];
    for (std::size_t i = 0; i < parameterCatalog.size(); ++i)
        plain[i] = raw_[i] != nullptr ? raw_[i]->load (std::memory_order_relaxed) : 0.0f;
    ExtraRoutes routes;
    {
        const juce::ScopedLock lock (modLock_);
        routes = modEdit_;
    }
    fx = fxFromPlain (plain);
    auto block = mapPlain (plain, routes);
    block.bpm = 120.0;
    block.bpmValid = false;
    return block;
}

SynthesisProcessor::PreviewFx SynthesisProcessor::fxFromPlain (const float* plain) const noexcept
{
    PreviewFx fx;
    fillFxSlots (fx.slot, [plain] (int index) noexcept { return plain[index]; });
    return fx;
}

SynthesisProcessor::PreviewFx SynthesisProcessor::bypassPreviewFx() noexcept
{
    PreviewFx fx;
    for (std::size_t slot = 0; slot < vstengine::effects::EffectsChain::effectCount; ++slot)
    {
        fx.slot[slot].type = previewFxTypes[slot];
        fx.slot[slot].enabled = false;
        fx.slot[slot].mix = 0.0f;
        fx.slot[slot].amount = 0.5f;
        fx.slot[slot].rate = 0.5f;
        fx.slot[slot].character = 0.5f;
    }
    return fx;
}

void SynthesisProcessor::publishPreview (const engine::BlockParams& params, const PreviewFx& fx, const engine::Event* events, int eventCount, int tailSamples) noexcept
{
    const juce::ScopedLock lock (previewLock_);
    const int generation = previewGeneration_ + 1;
    const int revision = previewRevision_ + 1;
    auto reclaim = [this] ()
    {
        const int published = previewPublished_.load (std::memory_order_acquire);
        for (int i = 0; i < 3; ++i)
        {
            if (i == published)
                continue;
            int expected = slotReady;
            previewSlots_[i].state.compare_exchange_strong (expected, slotFree, std::memory_order_acq_rel);
        }
    };
    int slot = -1;
    for (int attempt = 0; attempt < 8 && slot < 0; ++attempt)
    {
        reclaim();
        for (int i = 0; i < 3; ++i)
        {
            int expected = slotFree;
            if (previewSlots_[i].state.compare_exchange_strong (expected, slotWriting, std::memory_order_acq_rel))
            {
                slot = i;
                break;
            }
        }
    }
    if (slot < 0)
        return;
    previewGeneration_ = generation;
    previewRevision_ = revision;
    auto& dst = previewSlots_[slot];
    dst.stamp = revision;
    dst.stampEnd = 0;
    dst.params = params;
    dst.fx = fx;
    dst.eventCount = std::clamp (eventCount, 0, previewEventCap);
    for (int i = 0; i < dst.eventCount; ++i)
        dst.events[i] = events[i];
    dst.generation = generation;
    dst.tailSamples = std::max (0, tailSamples);
    dst.stampEnd = revision;
    dst.state.store (slotReady, std::memory_order_release);
    const int previous = previewPublished_.exchange (slot, std::memory_order_acq_rel);
    if (previous != slot)
    {
        int expected = slotReady;
        previewSlots_[previous].state.compare_exchange_strong (expected, slotFree, std::memory_order_acq_rel);
    }
    previewGenPublished_.store (generation, std::memory_order_release);
}

bool SynthesisProcessor::copyPreview (engine::BlockParams& params, PreviewFx& fx, engine::Event* events, int& eventCount, int& generation, int& tailSamples) noexcept
{
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        const int slot = previewPublished_.load (std::memory_order_acquire);
        if (slot < 0 || slot > 2)
            continue;
        int expected = slotReady;
        if (! previewSlots_[slot].state.compare_exchange_strong (expected, slotReading, std::memory_order_acq_rel))
            continue;
        const int stamp = previewSlots_[slot].stamp;
        params = previewSlots_[slot].params;
        fx = previewSlots_[slot].fx;
        eventCount = previewSlots_[slot].eventCount;
        for (int i = 0; i < eventCount && i < previewEventCap; ++i)
            events[i] = previewSlots_[slot].events[i];
        generation = previewSlots_[slot].generation;
        tailSamples = previewSlots_[slot].tailSamples;
        const int stampEnd = previewSlots_[slot].stampEnd;
        const int now = previewPublished_.load (std::memory_order_acquire);
        int owned = slotReading;
        previewSlots_[slot].state.compare_exchange_strong (owned, now == slot ? slotReady : slotFree, std::memory_order_acq_rel);
        if (stamp == stampEnd && stamp != 0 && eventCount >= 0 && eventCount <= previewEventCap)
            return true;
        previewClaimViolations_.fetch_add (1, std::memory_order_relaxed);
    }
    return false;
}

void SynthesisProcessor::adoptPreview (int generation, int tailSamples, const engine::Event* events, int eventCount, const engine::BlockParams& params, const PreviewFx& fx) noexcept
{
    rtPreviewGen_ = generation;
    rtPreviewEventCount_ = std::clamp (eventCount, 0, previewEventCap);
    for (int i = 0; i < rtPreviewEventCount_; ++i)
        rtPreviewEvents_[i] = events[i];
    rtPreviewParams_ = params;
    rtPreviewPos_ = 0;
    rtPreviewCursor_ = 0;
    rtPreviewTail_ = std::max (0, tailSamples);
    rtPreviewActive_ = rtPreviewEventCount_ > 0;
    rtSoundingCount_ = 0;
    rtPreviewNote_ = -1;
    if (previewFx_.isPrepared())
    {
        for (std::size_t slot = 0; slot < vstengine::effects::EffectsChain::effectCount; ++slot)
            previewFx_.setEffect (slot, fx.slot[slot]);
        previewFx_.reset();
    }
    if (! rtPreviewActive_)
        rtFinishedGen_ = generation;
}

void SynthesisProcessor::queuePreviewOffs (engine::Event* play, int& playCount, const int* notes, int noteCount) noexcept
{
    for (int i = 0; i < noteCount; ++i)
    {
        if (playCount >= previewEventCap + 8)
            break;
        engine::Event off;
        off.offset = 0;
        off.status = 0x80;
        off.data1 = static_cast<std::uint8_t> (std::clamp (notes[i], 0, 127));
        play[playCount++] = off;
        rtPreviewLastOff_ = notes[i];
    }
    rtSoundingCount_ = 0;
    rtPreviewNote_ = -1;
}

void SynthesisProcessor::queuePreviewScore (engine::Event* play, int& playCount, int frames) noexcept
{
    if (! rtPreviewActive_ || frames <= 0)
        return;
    const int start = rtPreviewPos_;
    const int end = start + frames;
    while (rtPreviewCursor_ < rtPreviewEventCount_ && playCount < previewEventCap + 8)
    {
        const auto& source = rtPreviewEvents_[rtPreviewCursor_];
        if (static_cast<int> (source.offset) >= end)
            break;
        engine::Event event = source;
        const int local = std::clamp (static_cast<int> (source.offset) - start, 0, frames - 1);
        event.offset = static_cast<std::uint32_t> (local);
        play[playCount++] = event;
        const auto type = static_cast<std::uint8_t> (event.status & 0xf0u);
        if (type == 0x90 && event.data2 > 0)
        {
            if (rtSoundingCount_ < 8)
                rtSounding_[rtSoundingCount_++] = event.data1;
            rtPreviewNote_ = event.data1;
        }
        else if (type == 0x80 || type == 0x90)
        {
            int write = 0;
            for (int i = 0; i < rtSoundingCount_; ++i)
                if (rtSounding_[i] != event.data1)
                    rtSounding_[write++] = rtSounding_[i];
            rtSoundingCount_ = write;
            rtPreviewNote_ = rtSoundingCount_ > 0 ? rtSounding_[rtSoundingCount_ - 1] : -1;
        }
        ++rtPreviewCursor_;
    }
    rtPreviewPos_ = end;
    if (rtPreviewCursor_ >= rtPreviewEventCount_)
        rtPreviewTail_ -= frames;
}

void SynthesisProcessor::renderPreview (juce::AudioBuffer<float>& buffer, int frames, const engine::Event* play, int playCount)
{
    if (frames <= 0 || frames > previewCapacity_ || previewMixL_.size() < static_cast<std::size_t> (frames))
        return;
    previewEngine_.process (previewMixL_.data(), previewMixR_.data(), frames, play, playCount, rtPreviewParams_);
    if (previewFx_.isPrepared())
    {
        float* audio[2] = { previewMixL_.data(), previewMixR_.data() };
        previewFx_.process (std::span<float*> (audio, 2), static_cast<std::uint32_t> (frames));
    }
    auto* left = buffer.getWritePointer (0);
    auto* right = buffer.getWritePointer (1);
    for (int i = 0; i < frames; ++i)
    {
        float mixedL = left[i] + previewMixL_[static_cast<std::size_t> (i)] * 0.30f;
        float mixedR = right[i] + previewMixR_[static_cast<std::size_t> (i)] * 0.30f;
        if (! std::isfinite (mixedL))
            mixedL = 0.0f;
        if (! std::isfinite (mixedR))
            mixedR = 0.0f;
        left[i] = std::clamp (mixedL, -1.0f, 1.0f);
        right[i] = std::clamp (mixedR, -1.0f, 1.0f);
    }
    previewBusReady_ = true;
}

void SynthesisProcessor::servicePreview (juce::AudioBuffer<float>& buffer, int frames)
{
    previewBusReady_ = false;
    if (! prepared_ || frames <= 0 || frames > previewCapacity_)
        return;
    engine::Event staged[previewEventCap] {};
    engine::BlockParams stagedParams;
    PreviewFx stagedFx;
    int stagedCount = 0;
    int stagedGen = 0;
    int stagedTail = 0;
    const auto takeNewer = [&] (int ownedGen) noexcept
    {
        const int genNow = previewGenPublished_.load (std::memory_order_acquire);
        if (genNow == 0 || genNow == ownedGen)
            return false;
        if (! copyPreview (stagedParams, stagedFx, staged, stagedCount, stagedGen, stagedTail) || stagedGen != genNow)
            return false;
        adoptPreview (stagedGen, stagedTail, staged, stagedCount, stagedParams, stagedFx);
        return true;
    };

    const int genBefore = previewGenPublished_.load (std::memory_order_acquire);
    const bool forced = previewForceRelease_.exchange (0, std::memory_order_acq_rel) == 1;
    const bool newer = rtPreviewActive_ && genBefore != 0 && genBefore != rtPreviewGen_;
    if (rtPreviewActive_ && (forced || newer))
    {
        int ownedNotes[8] {};
        const int ownedCount = rtSoundingCount_;
        for (int i = 0; i < ownedCount; ++i)
            ownedNotes[i] = rtSounding_[i];
        const int ownedGen = rtPreviewGen_;
        if (previewHandoffPark_.load (std::memory_order_acquire) == 1)
        {
            previewHandoffPark_.store (2, std::memory_order_release);
            while (previewHandoffPark_.load (std::memory_order_acquire) == 2)
            {
            }
        }
        engine::Event play[previewEventCap + 8] {};
        int playCount = 0;
        queuePreviewOffs (play, playCount, ownedNotes, ownedCount);
        rtPreviewActive_ = false;
        rtPreviewCursor_ = rtPreviewEventCount_;
        rtPreviewTail_ = 0;
        const bool adopted = takeNewer (ownedGen);
        if (! adopted && previewGenPublished_.load (std::memory_order_acquire) == ownedGen)
            rtFinishedGen_ = ownedGen;
        if (rtPreviewActive_)
            queuePreviewScore (play, playCount, frames);
        if (playCount > 0 || rtPreviewActive_)
            renderPreview (buffer, frames, play, playCount);
        return;
    }

    if (! rtPreviewActive_)
    {
        const int genNow = previewGenPublished_.load (std::memory_order_acquire);
        if (genNow != 0 && genNow != rtFinishedGen_)
            takeNewer (rtFinishedGen_);
    }

    engine::Event play[previewEventCap + 8] {};
    int playCount = 0;
    if (rtPreviewActive_)
        queuePreviewScore (play, playCount, frames);
    if (rtPreviewActive_ && rtPreviewCursor_ >= rtPreviewEventCount_ && rtPreviewTail_ <= 0)
    {
        int ownedNotes[8] {};
        const int ownedCount = rtSoundingCount_;
        for (int i = 0; i < ownedCount; ++i)
            ownedNotes[i] = rtSounding_[i];
        const int ownedGen = rtPreviewGen_;
        if (previewHandoffPark_.load (std::memory_order_acquire) == 1)
        {
            previewHandoffPark_.store (2, std::memory_order_release);
            while (previewHandoffPark_.load (std::memory_order_acquire) == 2)
            {
            }
        }
        if (ownedCount > 0)
            queuePreviewOffs (play, playCount, ownedNotes, ownedCount);
        rtPreviewActive_ = false;
        const bool adopted = takeNewer (ownedGen);
        if (! adopted && previewGenPublished_.load (std::memory_order_acquire) == ownedGen)
            rtFinishedGen_ = ownedGen;
        if (rtPreviewActive_)
            queuePreviewScore (play, playCount, frames);
    }
    if (playCount > 0 || rtPreviewActive_)
        renderPreview (buffer, frames, play, playCount);
}

void SynthesisProcessor::requestPreview (int note) noexcept
{
    note = std::clamp (note, 0, 127);
    engine::Event events[2] {};
    int count = 0;
    const int offAt = previewSamples (sampleRate_, 0.40);
    pushPreview (events, count, 0, 0x90, note, 96, 0);
    pushPreview (events, count, static_cast<std::uint32_t> (offAt), 0x80, note, 0, 0);
    PreviewFx fx;
    publishPreview (liveBlock (fx), fx, events, count, previewSamples (sampleRate_, 0.20));
}

void SynthesisProcessor::requestFactoryPreview (int index) noexcept
{
    if (index < 0 || index >= vox::syn::factoryProgramCount())
        return;
    float plain[parameterCatalog.size()];
    for (std::size_t i = 0; i < parameterCatalog.size(); ++i)
    {
        const float source = i < static_cast<std::size_t> (frozenParameterCount) ? factoryProgram (index).values[i] : parameterCatalog[i].initial;
        plain[i] = std::clamp (source, parameterCatalog[i].minimum, parameterCatalog[i].maximum);
    }
    const FactoryFeature feature = factoryFeature (index);
    plain[featureBase + 0] = feature.unison;
    plain[featureBase + 1] = feature.detune;
    plain[featureBase + 2] = feature.spread;
    plain[featureBase + 3] = feature.wtLevel;
    plain[featureBase + 4] = feature.wtPos;
    plain[vaParamBase + static_cast<int> (VaKnob::fm)] = feature.fm;
    plain[vaParamBase + static_cast<int> (VaKnob::ring)] = feature.ring;
    plain[vaParamBase + static_cast<int> (VaKnob::osc3Level)] = feature.osc3Level;
    plain[vaParamBase + static_cast<int> (VaKnob::osc3Semi)] = feature.osc3Semi;
    plain[routeBase + 0] = static_cast<float> (feature.modSource);
    plain[routeBase + 1] = static_cast<float> (feature.modDest);
    plain[routeBase + 2] = feature.modAmount;
    ExtraRoutes routes;
    if (feature.extraRoute >= routeCount && feature.extraRoute < engine::PartParams::routeSlots)
    {
        auto& row = routes.row[0][feature.extraRoute - routeCount];
        row.source = feature.extraSource;
        row.dest = feature.extraDest;
        row.amount = (feature.extraAmount - 0.5f) * 2.0f;
    }
    auto block = mapPlain (plain, routes);
    block.bpm = 120.0;
    block.bpmValid = false;
    engine::Event events[previewEventCap] {};
    int count = 0;
    int tail = 0;
    fillRoleScore (factoryProgram (index).role, factoryProgram (index).auditionNote, sampleRate_, events, count, tail);
    publishPreview (block, fxFromPlain (plain), events, count, tail);
}

bool SynthesisProcessor::requestUserPreview (int index)
{
    UserProgram program;
    {
        const juce::ScopedLock lock (bankLock_);
        if (index < 0 || index >= static_cast<int> (userPrograms_.size()))
            return false;
        program = userPrograms_[static_cast<std::size_t> (index)];
    }
    float plain[parameterCatalog.size()];
    for (std::size_t i = 0; i < parameterCatalog.size(); ++i)
    {
        const float source = i < program.values.size() ? program.values[i] : parameterCatalog[i].initial;
        plain[i] = std::clamp (source, parameterCatalog[i].minimum, parameterCatalog[i].maximum);
    }
    ExtraRoutes routes;
    for (int part = 0; part < 2; ++part)
        for (int route = 0; route < extraRouteCount; ++route)
        {
            const auto& saved = program.extra[part][route];
            auto& row = routes.row[part][route];
            row.source = std::clamp (saved.source, 0, 12);
            row.dest = std::clamp (saved.dest, 0, 5);
            row.amount = std::isfinite (saved.amount) ? std::clamp (saved.amount, -1.0f, 1.0f) : 0.0f;
        }
    auto block = mapPlain (plain, routes);
    block.bpm = 120.0;
    block.bpmValid = false;
    const int note = roleForCategory (program.category) == FactoryRole::bass || roleForCategory (program.category) == FactoryRole::acid ? 36 : 60;
    engine::Event events[previewEventCap] {};
    int count = 0;
    int tail = 0;
    fillRoleScore (roleForCategory (program.category), note, sampleRate_, events, count, tail);
    publishPreview (block, fxFromPlain (plain), events, count, tail);
    return true;
}

void SynthesisProcessor::cancelPreview() noexcept
{
    engine::BlockParams block;
    publishPreview (block, bypassPreviewFx(), nullptr, 0, 0);
}

std::uint32_t SynthesisProcessor::renderListeningHash (int index) const
{
    auto local = std::make_unique<engine::SynthEngine>();
    local->prepare (48000.0, 256);
    float plain[parameterCatalog.size()];
    for (std::size_t i = 0; i < parameterCatalog.size(); ++i)
    {
        const float source = i < static_cast<std::size_t> (frozenParameterCount) ? factoryProgram (index).values[i] : parameterCatalog[i].initial;
        plain[i] = std::clamp (source, parameterCatalog[i].minimum, parameterCatalog[i].maximum);
    }
    const FactoryFeature feature = factoryFeature (index);
    plain[featureBase + 0] = feature.unison;
    plain[featureBase + 1] = feature.detune;
    plain[featureBase + 2] = feature.spread;
    plain[featureBase + 3] = feature.wtLevel;
    plain[featureBase + 4] = feature.wtPos;
    plain[vaParamBase + static_cast<int> (VaKnob::fm)] = feature.fm;
    plain[vaParamBase + static_cast<int> (VaKnob::ring)] = feature.ring;
    plain[vaParamBase + static_cast<int> (VaKnob::osc3Level)] = feature.osc3Level;
    plain[vaParamBase + static_cast<int> (VaKnob::osc3Semi)] = feature.osc3Semi;
    plain[routeBase + 0] = static_cast<float> (feature.modSource);
    plain[routeBase + 1] = static_cast<float> (feature.modDest);
    plain[routeBase + 2] = feature.modAmount;
    ExtraRoutes routes;
    if (feature.extraRoute >= routeCount && feature.extraRoute < engine::PartParams::routeSlots)
    {
        auto& row = routes.row[0][feature.extraRoute - routeCount];
        row.source = feature.extraSource;
        row.dest = feature.extraDest;
        row.amount = (feature.extraAmount - 0.5f) * 2.0f;
    }
    auto block = mapPlain (plain, routes);
    block.bpm = 120.0;
    block.bpmValid = false;
    engine::Event events[previewEventCap] {};
    int count = 0;
    int tail = 0;
    const auto& program = factoryProgram (index);
    fillRoleScore (program.role, program.auditionNote, 48000.0, events, count, tail);
    std::uint32_t hash = 2166136261u;
    const auto mix = [&hash] (std::uint32_t bits)
    {
        hash ^= bits;
        hash *= 16777619u;
    };
    const auto* id = program.id != nullptr ? program.id : "";
    for (const char* cursor = id; *cursor != '\0'; ++cursor)
        mix (static_cast<std::uint32_t> (static_cast<unsigned char> (*cursor)));
    float left[256] {};
    float right[256] {};
    int cursor = 0;
    const int rendered = std::min (count > 0 ? static_cast<int> (events[count - 1].offset) + 1 : 256, 2048);
    while (cursor < rendered)
    {
        const int frames = std::min (256, rendered - cursor);
        engine::Event window[previewEventCap] {};
        int windowCount = 0;
        for (int i = 0; i < count && windowCount < previewEventCap; ++i)
        {
            const int at = static_cast<int> (events[i].offset);
            if (at < cursor || at >= cursor + frames)
                continue;
            window[windowCount] = events[i];
            window[windowCount].offset = static_cast<std::uint32_t> (at - cursor);
            ++windowCount;
        }
        local->process (left, right, frames, window, windowCount, block);
        for (int i = 0; i < frames; ++i)
        {
            std::uint32_t bits = 0;
            std::memcpy (&bits, &left[i], sizeof bits);
            mix (bits);
        }
        cursor += frames;
    }
    return hash == 0u ? 1u : hash;
}

void SynthesisProcessor::setListeningMark (int index, int status)
{
    if (index < 0 || index >= factoryProgramCount() || index >= factoryProgramCapacity)
        return;
    status = std::clamp (status, 0, 3);
    const std::uint32_t hash = status == 0 ? 0u : renderListeningHash (index);
    const juce::ScopedLock lock (listenLock_);
    listen_[index].status = static_cast<std::uint8_t> (status);
    listen_[index].hash = hash;
}

int SynthesisProcessor::listeningMark (int index) const
{
    if (index < 0 || index >= factoryProgramCapacity)
        return 0;
    const juce::ScopedLock lock (listenLock_);
    return listen_[index].status;
}

std::uint32_t SynthesisProcessor::listeningRenderHash (int index) const
{
    if (index < 0 || index >= factoryProgramCapacity)
        return 0;
    const juce::ScopedLock lock (listenLock_);
    return listen_[index].hash;
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
    appendUserBank (bank, userPrograms_);
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
    int listenCount = 0;
    ListenMark listenCopy[factoryProgramCapacity];
    {
        const juce::ScopedLock listenLock (listenLock_);
        for (int i = 0; i < factoryProgramCapacity; ++i)
        {
            listenCopy[i] = listen_[i];
            if (listen_[i].status != 0)
                ++listenCount;
        }
    }
    if (listenCount > 0)
    {
        preset::Section listen;
        listen.tag = listenSectionTag;
        putU32 (listen.data, 1u);
        putU32 (listen.data, static_cast<std::uint32_t> (listenCount));
        for (int i = 0; i < factoryProgramCapacity; ++i)
        {
            if (listenCopy[i].status == 0)
                continue;
            putU32 (listen.data, static_cast<std::uint32_t> (i));
            putU32 (listen.data, listenCopy[i].status);
            putU32 (listen.data, listenCopy[i].hash);
        }
        out.push_back (std::move (listen));
    }
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
        if (! takeU32 (section->data, at, version) || (version != 1u && version != userBankVersion)
            || ! takeU32 (section->data, at, count) || count > static_cast<std::uint32_t> (maxUserPrograms))
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
            if (version == userBankVersion)
            {
                std::uint32_t parts = 0, routes = 0;
                if (! takeU32 (section->data, at, parts) || ! takeU32 (section->data, at, routes)
                    || parts != 2u || routes != static_cast<std::uint32_t> (extraRouteCount))
                {
                    error = "malformed UBNK section";
                    return {};
                }
                for (std::uint32_t part = 0; part < parts; ++part)
                    for (std::uint32_t route = 0; route < routes; ++route)
                    {
                        std::uint32_t source = 0, dest = 0, bits = 0;
                        if (! takeU32 (section->data, at, source) || ! takeU32 (section->data, at, dest) || ! takeU32 (section->data, at, bits)
                            || source > 12u || dest > 5u)
                        {
                            error = "malformed UBNK section";
                            return {};
                        }
                        float amount = 0.0f;
                        std::memcpy (&amount, &bits, sizeof amount);
                        if (! std::isfinite (amount))
                        {
                            error = "non-finite UBNK value";
                            return {};
                        }
                        program.extra[part][route].source = static_cast<int> (source);
                        program.extra[part][route].dest = static_cast<int> (dest);
                        program.extra[part][route].amount = std::clamp (amount, -1.0f, 1.0f);
                    }
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
    appendUserBank (encoded, *bank);
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
    auto marks = std::make_shared<std::array<ListenMark, factoryProgramCapacity>>();
    if (const auto* section = document.find (listenSectionTag))
    {
        std::size_t at = 0;
        std::uint32_t version = 0, count = 0;
        if (! takeU32 (section->data, at, version) || version != 1u
            || ! takeU32 (section->data, at, count) || count > static_cast<std::uint32_t> (factoryProgramCapacity))
        {
            error = "malformed LIST section";
            return {};
        }
        for (std::uint32_t i = 0; i < count; ++i)
        {
            std::uint32_t index = 0, status = 0, hash = 0;
            if (! takeU32 (section->data, at, index) || ! takeU32 (section->data, at, status) || ! takeU32 (section->data, at, hash)
                || index >= static_cast<std::uint32_t> (factoryProgramCapacity) || status > 3u)
            {
                error = "malformed LIST section";
                return {};
            }
            (*marks)[index].status = static_cast<std::uint8_t> (status);
            (*marks)[index].hash = hash;
        }
        if (at != section->data.size())
        {
            error = "malformed LIST section";
            return {};
        }
    }
    int storedMarks = 0;
    for (const auto& mark : *marks)
        if (mark.status != 0)
            ++storedMarks;
    if (storedMarks > 0)
    {
        preset::Section listenSection;
        listenSection.tag = listenSectionTag;
        putU32 (listenSection.data, 1u);
        putU32 (listenSection.data, static_cast<std::uint32_t> (storedMarks));
        for (int i = 0; i < factoryProgramCapacity; ++i)
        {
            if ((*marks)[i].status == 0)
                continue;
            putU32 (listenSection.data, static_cast<std::uint32_t> (i));
            putU32 (listenSection.data, (*marks)[i].status);
            putU32 (listenSection.data, (*marks)[i].hash);
        }
        restore.sections.push_back (std::move (listenSection));
    }
    restore.apply = [this, values, bank, phrase, routes, marks] {
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
        const juce::ScopedLock listenLock (listenLock_);
        for (int i = 0; i < factoryProgramCapacity; ++i)
            listen_[i] = (*marks)[i];
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
    {
        const juce::ScopedLock lock (modLock_);
        for (int part = 0; part < 2; ++part)
            for (int route = 0; route < extraRouteCount; ++route)
            {
                program.extra[part][route].source = modEdit_.row[part][route].source;
                program.extra[part][route].dest = modEdit_.row[part][route].dest;
                program.extra[part][route].amount = modEdit_.row[part][route].amount;
            }
    }
    const juce::ScopedLock lock (bankLock_);
    if (static_cast<int> (userPrograms_.size()) >= maxUserPrograms)
        return false;
    userPrograms_.push_back (std::move (program));
    return true;
}

bool SynthesisProcessor::loadUserProgram (int index)
{
    UserProgram program;
    {
        const juce::ScopedLock lock (bankLock_);
        if (index < 0 || index >= static_cast<int> (userPrograms_.size()))
            return false;
        program = userPrograms_[static_cast<std::size_t> (index)];
    }
    for (std::size_t i = 0; i < parameterCatalog.size() && i < program.values.size(); ++i)
    {
        auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (parameters_.getParameter (parameterCatalog[i].id));
        if (parameter == nullptr)
            continue;
        const float clamped = std::clamp (program.values[i], parameterCatalog[i].minimum, parameterCatalog[i].maximum);
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (clamped));
    }
    applyUserRoutes (program);
    cancelPreview();
    return true;
}

void SynthesisProcessor::applyUserRoutes (const UserProgram& program) noexcept
{
    const juce::ScopedLock lock (modLock_);
    for (int part = 0; part < 2; ++part)
        for (int route = 0; route < extraRouteCount; ++route)
        {
            const auto& saved = program.extra[part][route];
            auto& row = modEdit_.row[part][route];
            row.source = std::clamp (saved.source, 0, 12);
            row.dest = std::clamp (saved.dest, 0, 5);
            row.amount = std::isfinite (saved.amount) ? std::clamp (saved.amount, -1.0f, 1.0f) : 0.0f;
        }
    publishModLocked();
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
    cancelPreview();
    return true;
}

int SynthesisProcessor::factoryProgramAudition (int index) const
{
    if (index < 0 || index >= vox::syn::factoryProgramCount())
        return 60;
    return factoryProgram (index).auditionNote;
}

float SynthesisProcessor::previewBusSample (int channel, int index) const noexcept
{
    const auto& bus = channel == 0 ? previewMixL_ : previewMixR_;
    if ((channel != 0 && channel != 1) || index < 0 || static_cast<std::size_t> (index) >= bus.size())
        return 0.0f;
    return bus[static_cast<std::size_t> (index)];
}

int SynthesisProcessor::factoryPreviewEventsForTest (int index, AuditionEvent* out, int capacity, int& tailSamples) const noexcept
{
    tailSamples = 0;
    if (out == nullptr || capacity <= 0 || index < 0 || index >= vox::syn::factoryProgramCount())
        return 0;
    engine::Event events[previewEventCap] {};
    int count = 0;
    int tail = 0;
    const auto& program = factoryProgram (index);
    fillRoleScore (program.role, program.auditionNote, sampleRate_, events, count, tail);
    int written = 0;
    for (int i = 0; i < count && written < capacity; ++i)
    {
        const auto type = static_cast<std::uint8_t> (events[i].status & 0xf0u);
        out[written].sample = static_cast<int> (events[i].offset);
        out[written].on = type == 0x90 && events[i].data2 > 0;
        out[written].note = events[i].data1;
        out[written].velocity = events[i].data2;
        out[written].slide = (events[i].articulation & engine::articulateSlide) != 0;
        out[written].accent = (events[i].articulation & engine::articulateAccent) != 0;
        ++written;
    }
    tailSamples = tail;
    return written;
}

} // namespace vox::syn

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new vox::syn::SynthesisProcessor();
}
