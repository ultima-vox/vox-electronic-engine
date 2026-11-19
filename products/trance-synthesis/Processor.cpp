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
    // Voice release is the documented bound in voiceTailSeconds. An enabled
    // EffectsChain slot adds its own finite tail (reverb 8 s, delay 6 s,
    // phaser 0.5 s, chorus/flanger 0.1 s, bitcrusher 0.01 s). Slots ship off.
    double tail = engine::voiceTailSeconds();
    const double rate = getSampleRate();
    if (fx_.isPrepared() && rate > 0.0)
        tail = std::max (tail, static_cast<double> (fx_.tailSamples()) / rate);
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
    block.output = load (raw_[14]);

    int count = 0;
    std::uint64_t dropped = 0;
    for (const auto metadata : midi)
    {
        if (metadata.numBytes < 1 || metadata.data[0] == 0xf0)
            continue;
        if (count == engine::maxEvents)
        {
            ++dropped;
            continue;
        }
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
    float plain[15];
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
    float plain[15];
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

    auto values = std::make_shared<std::array<float, 15>>();
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
