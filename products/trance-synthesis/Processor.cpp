#include "Processor.h"

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

void syncExposedFx (vstengine::effects::EffectsChain& fx, float delay, float reverb) noexcept
{
    using vstengine::effects::EffectSettings;
    using vstengine::effects::EffectType;
    const auto slot = [] (EffectType type, bool on) noexcept
    {
        EffectSettings settings;
        settings.type = type;
        settings.enabled = on;
        settings.mix = on ? 0.35f : 0.0f;
        settings.amount = 0.5f;
        settings.rate = 0.4f;
        settings.character = 0.5f;
        return settings;
    };
    fx.setEffect (6, slot (EffectType::delay, delay >= 0.5f));
    fx.setEffect (7, slot (EffectType::reverb, reverb >= 0.5f));
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
}

void SynthesisProcessor::prepareToPlay (double sampleRate, int maximumBlockSize)
{
    vox::suite::SuiteProcessor::prepareToPlay (sampleRate, maximumBlockSize);
    engine_.prepare (sampleRate, maximumBlockSize);
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
    // Voice release is voiceTailSeconds(). syn.fx.delay and syn.fx.reverb
    // are the host controls. 6 s and 8 s match EffectsChain::tailSamples
    // for delay slot 6 and reverb slot 7. The report follows the controls
    // so a host read between callbacks is not a stale chain.
    double tail = engine::voiceTailSeconds();
    const auto on = [] (const std::atomic<float>* value) noexcept
    {
        return value != nullptr && value->load (std::memory_order_relaxed) >= 0.5f;
    };
    if (on (raw_[reverbParameterIndex]))
        tail = std::max (tail, 8.0);
    else if (on (raw_[delayParameterIndex]))
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

    engine::BlockParams block;
    const auto load = [] (std::atomic<float>* value) noexcept
    {
        return value != nullptr ? value->load (std::memory_order_relaxed) : 0.0f;
    };
    for (int part = 0; part < engine::partCount; ++part)
    {
        const int base = part * 7;
        block.part[part].engine = kindFrom (load (raw_[static_cast<std::size_t> (base)]));
        block.part[part].level = load (raw_[static_cast<std::size_t> (base + 1)]);
        block.part[part].cutoff = load (raw_[static_cast<std::size_t> (base + 2)]);
        block.part[part].resonance = load (raw_[static_cast<std::size_t> (base + 3)]);
        block.part[part].slide = load (raw_[static_cast<std::size_t> (base + 4)]);
        block.part[part].portamento = load (raw_[static_cast<std::size_t> (base + 5)]);
        block.part[part].drive = load (raw_[static_cast<std::size_t> (base + 6)]);
    }
    block.output = load (raw_[outputParameterIndex]);
    syncExposedFx (fx_, load (raw_[delayParameterIndex]), load (raw_[reverbParameterIndex]));

    int count = 0;
    std::uint64_t dropped = 0;
    const auto admit = [&] (const juce::MidiMessageMetadata& metadata)
    {
        engine::Event event;
        event.offset = static_cast<std::uint32_t> (juce::jlimit (0, frames - 1, metadata.samplePosition));
        event.status = metadata.data[0];
        event.data1 = metadata.numBytes > 1 ? metadata.data[1] : 0;
        event.data2 = metadata.numBytes > 2 ? metadata.data[2] : 0;
        int index = count;
        while (index > 0 && events_[index - 1].offset > event.offset)
        {
            events_[index] = events_[index - 1];
            --index;
        }
        events_[index] = event;
        ++count;
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

    if (total <= engine::maxEvents)
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
        const int keepPanic = std::min (panic, engine::maxEvents);
        const int keepRelease = std::min (release, engine::maxEvents - keepPanic);
        const int keepOther = engine::maxEvents - keepPanic - keepRelease;
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

    engine_.process (buffer.getWritePointer (0), buffer.getWritePointer (1), frames, events_, count, block);
    if (fx_.isPrepared())
    {
        float* audio[2] = { buffer.getWritePointer (0), buffer.getWritePointer (1) };
        fx_.process (std::span<float*> (audio, 2), static_cast<std::uint32_t> (frames));
    }
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
        if (! takeU32 (section->data, at, version) || version != 1u || ! takeU32 (section->data, at, count) || count > 64)
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

    auto values = std::make_shared<std::array<float, parameterCatalog.size()>>();
    for (std::size_t i = 0; i < values->size(); ++i)
        (*values)[i] = plain[i];
    ProductRestore restore;
    restore.sections.push_back (encodeParameters (values->data(), static_cast<int> (values->size())));
    restore.apply = [this, values] {
        for (std::size_t i = 0; i < parameterCatalog.size(); ++i)
        {
            auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (parameters_.getParameter (parameterCatalog[i].id));
            if (parameter == nullptr)
                continue;
            parameter->setValueNotifyingHost (parameter->convertTo0to1 ((*values)[i]));
        }
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

} // namespace vox::syn

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new vox::syn::SynthesisProcessor();
}
