#include "Processor.h"

#include "FactoryBank.h"
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
    }
    block.output = load (raw_[outputParameterIndex]);
    syncExposedFx (fx_, raw_);

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
    restore.apply = [this, values, bank] {
        for (std::size_t i = 0; i < parameterCatalog.size(); ++i)
        {
            auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (parameters_.getParameter (parameterCatalog[i].id));
            if (parameter == nullptr)
                continue;
            parameter->setValueNotifyingHost (parameter->convertTo0to1 ((*values)[i]));
        }
        const juce::ScopedLock lock (bankLock_);
        userPrograms_ = *bank;
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

} // namespace vox::syn

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new vox::syn::SynthesisProcessor();
}
