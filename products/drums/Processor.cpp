#include "Processor.h"

#include "DrumsEditor.h"

#include <cmath>
#include <cstring>
#include <map>

namespace vox::drums {
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

float plainValueOf (juce::AudioProcessorParameter& parameter)
{
    if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (&parameter))
        return f->get();
    if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (&parameter))
        return static_cast<float> (c->getIndex());
    if (auto* b = dynamic_cast<juce::AudioParameterBool*> (&parameter))
        return b->get() ? 1.0f : 0.0f;
    auto& ranged = dynamic_cast<juce::RangedAudioParameter&> (parameter);
    return ranged.convertFrom0to1 (ranged.getValue());
}

} // namespace

// Accent: Copper (05_ULTIMA_VOX_UI_JUCE_SKIN.md §3).
DrumsProcessor::DrumsProcessor()
    : vox::suite::SuiteProcessor ({ JucePlugin_Name,
                                    vox::preset::makeTag (VOX_SUITE_STATE_TAG),
                                    1,
                                    juce::Colour::fromString ("FFEF805D") }),
      parameters_ (*this, nullptr, "VOXDRUMS", createParameterLayout())
{
    const auto& engineParameters = DrumEngine::parameters();
    bindings_.resize (engineParameters.size());
    for (const auto& p : engineParameters)
        bindings_[p.index].fallback = p.defaultValue;
    for (const auto& entry : pluginParameters())
        if (entry.engineIndex >= 0)
            bindings_[static_cast<std::size_t> (entry.engineIndex)].value = parameters_.getRawParameterValue (entry.id);
    pattern_ = parameters_.getRawParameterValue (patternParameterId);
    sent_.assign (bindings_.size(), 0.0f);
    changes_.resize (bindings_.size());
    events_.resize (midiCapacity);
    for (const auto& entry : pluginParameters())
    {
        const float plain = plainValueOf (*parameters_.getParameter (entry.id));
        restored_.push_back ({ plain, plain });
    }

    padForNote_.fill (-1);
    const auto& notes = DrumEngine::padNotes();
    for (int pad = 0; pad < padCount; ++pad)
        padForNote_[notes[static_cast<std::size_t> (pad)] & 0x7f] = pad;
}

void DrumsProcessor::prepareToPlay (double sampleRate, int maximumBlockSize)
{
    vox::suite::SuiteProcessor::prepareToPlay (sampleRate, maximumBlockSize);
    if (! engine_.prepare (sampleRate, static_cast<std::uint32_t> (juce::jmax (1, maximumBlockSize))))
        return;
    for (std::size_t i = 0; i < bindings_.size(); ++i)
        sent_[i] = bindings_[i].value != nullptr ? bindings_[i].value->load (std::memory_order_relaxed)
                                                 : bindings_[i].fallback;
    engine_.setAllParameters (sent_.data(), static_cast<std::uint32_t> (sent_.size()));
}

void DrumsProcessor::releaseResources()
{
    engine_.reset();
}

void DrumsProcessor::reset()
{
    panicPending_.store (true, std::memory_order_release);
}

double DrumsProcessor::getTailLengthSeconds() const
{
    return 6.5;   // longest pad: cymbal decay 6 s, plus the -80 dB tail
}

bool DrumsProcessor::audition (int pad, std::uint8_t velocity)
{
    if (pad < 0 || pad >= padCount || velocity == 0)
        return false;
    const auto scope = auditionFifo_.write (1);
    if (scope.blockSize1 + scope.blockSize2 != 1)
        return false;
    auditions_[static_cast<std::size_t> (scope.blockSize1 == 1 ? scope.startIndex1 : scope.startIndex2)] =
        { static_cast<std::uint8_t> (pad), velocity };
    return true;
}

// Realtime: no allocation, lock or I/O. All buffers are sized in the
// constructor; the playhead query is the host's.
void DrumsProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    noteMidiSeen (midi);
    const int frames = buffer.getNumSamples();
    if (! engine_.isPrepared() || buffer.getNumChannels() < 2 || frames <= 0)
    {
        buffer.clear();
        return;
    }
    for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, frames);

    // Parameter changes since the previous block, applied at its start.
    std::uint32_t changeCount = 0;
    for (std::size_t i = 0; i < bindings_.size(); ++i)
    {
        if (bindings_[i].value == nullptr)
            continue;
        const float v = bindings_[i].value->load (std::memory_order_relaxed);
        if (! juce::exactlyEqual (v, sent_[i]))
        {
            sent_[i] = v;
            changes_[changeCount++] = { static_cast<std::uint32_t> (i), v };
        }
    }

    // Pad auditions from the editor first (offset 0), then host MIDI.
    std::uint32_t pads = 0;
    std::size_t eventCount = 0;
    {
        const auto scope = auditionFifo_.read (auditionFifo_.getNumReady());
        const auto push = [&] (int start, int count) {
            for (int i = 0; i < count && eventCount < events_.size(); ++i)
            {
                const auto& a = auditions_[static_cast<std::size_t> (start + i)];
                const auto note = DrumEngine::padNotes()[a.pad];
                events_[eventCount++] = { 0u, 3u, { 0x90u, note, a.velocity } };
                pads |= 1u << a.pad;
            }
        };
        push (scope.startIndex1, scope.blockSize1);
        push (scope.startIndex2, scope.blockSize2);
    }
    std::uint64_t dropped = 0;
    for (const auto metadata : midi)
    {
        const int size = metadata.numBytes;
        if (size < 1 || size > 3 || metadata.data[0] == 0xf0u)
            continue;
        if (eventCount == events_.size())
        {
            ++dropped;
            continue;
        }
        MidiEvent e;
        e.offset = static_cast<std::uint32_t> (juce::jlimit (0, frames - 1, metadata.samplePosition));
        e.size = static_cast<std::uint8_t> (size);
        std::memcpy (e.bytes, metadata.data, static_cast<std::size_t> (size));
        if ((e.bytes[0] & 0xf0u) == 0x90u && size == 3 && e.bytes[2] > 0 && padForNote_[e.bytes[1] & 0x7f] >= 0)
            pads |= 1u << padForNote_[e.bytes[1] & 0x7f];
        events_[eventCount++] = e;
    }
    if (dropped > 0)
        droppedMidi_.fetch_add (dropped, std::memory_order_relaxed);

    Transport transport;
    if (auto* host = getPlayHead())
    {
        if (const auto position = host->getPosition())
        {
            transport.playing = position->getIsPlaying();
            if (const auto bpm = position->getBpm())
            {
                transport.hasTempo = true;
                transport.bpm = *bpm;
            }
            if (const auto ppq = position->getPpqPosition())
            {
                transport.hasPpq = true;
                transport.ppq = *ppq;
            }
            if (const auto bar = position->getPpqPositionOfLastBarStart())
            {
                transport.hasBarStart = true;
                transport.barStartPpq = *bar;
            }
            if (const auto sig = position->getTimeSignature())
            {
                transport.hasTimeSignature = true;
                transport.numerator = static_cast<std::uint32_t> (juce::jmax (1, sig->numerator));
                transport.denominator = static_cast<std::uint32_t> (juce::jmax (1, sig->denominator));
            }
            if (const auto samples = position->getTimeInSamples())
            {
                transport.hasTime = true;
                transport.timeInSamples = *samples;
            }
        }
    }
    playing_.store (transport.playing, std::memory_order_relaxed);
    if (transport.hasPpq)
        lastPpq_.store (transport.ppq, std::memory_order_relaxed);

    const int pattern = pattern_ != nullptr ? static_cast<int> (std::lround (pattern_->load (std::memory_order_relaxed))) : 0;
    const bool panic = panicPending_.exchange (false, std::memory_order_acq_rel);
    engine_.process (buffer.getWritePointer (0), buffer.getWritePointer (1), static_cast<std::uint32_t> (frames),
                     changes_.data(), changeCount, events_.data(), static_cast<std::uint32_t> (eventCount),
                     transport, pattern, panic);
    pads |= engine_.sequencerHitMask();
    if (pads != 0)
        hitMask_.fetch_or (pads, std::memory_order_acq_rel);
}

//==============================================================================
// DPRM section: u32 count, then per parameter u32 id length, id bytes (UTF-8),
// f32 plain value (little endian bit pattern). Unknown IDs are ignored (a
// newer writer); missing ones take their defaults.
void DrumsProcessor::writeProductState (std::vector<preset::Section>& out) const
{
    preset::Section section;
    section.tag = parameterSectionTag;
    const auto& entries = pluginParameters();
    putU32 (section.data, static_cast<std::uint32_t> (entries.size()));
    for (const auto& entry : entries)
    {
        // A value restored from a state and not moved since is written back
        // as it was loaded: a skewed range cannot represent every float, so
        // the parameter may hold the nearest value instead.
        const auto i = static_cast<std::size_t> (&entry - entries.data());
        const float current = plainValueOf (*parameters_.getParameter (entry.id));
        const float plain = juce::exactlyEqual (current, restored_[i].held) ? restored_[i].loaded : current;
        putU32 (section.data, static_cast<std::uint32_t> (entry.id.size()));
        for (const char c : entry.id)
            section.data.push_back (static_cast<std::byte> (c));
        std::uint32_t bits = 0;
        std::memcpy (&bits, &plain, sizeof bits);
        putU32 (section.data, bits);
    }
    out.push_back (std::move (section));
}

std::function<void()> DrumsProcessor::readProductState (const preset::StateDocument& document, juce::String& error)
{
    std::map<std::string, float> values;
    if (const auto* section = document.find (parameterSectionTag))
    {
        const auto& data = section->data;
        std::size_t at = 0;
        std::uint32_t count = 0;
        if (! takeU32 (data, at, count) || count > 4096)
        {
            error = "malformed parameter section";
            return {};
        }
        for (std::uint32_t i = 0; i < count; ++i)
        {
            std::uint32_t length = 0, bits = 0;
            if (! takeU32 (data, at, length) || length == 0 || length > 64 || data.size() - at < length + 4u)
            {
                error = "malformed parameter section";
                return {};
            }
            std::string id (length, '\0');
            for (std::uint32_t c = 0; c < length; ++c)
                id[c] = static_cast<char> (data[at + c]);
            at += length;
            float plain = 0.0f;
            if (! takeU32 (data, at, bits))
            {
                error = "malformed parameter section";
                return {};
            }
            std::memcpy (&plain, &bits, sizeof plain);
            if (! std::isfinite (plain) || ! values.emplace (std::move (id), plain).second)
            {
                error = "invalid or duplicate parameter value";
                return {};
            }
        }
        if (at != data.size())
        {
            error = "malformed parameter section";
            return {};
        }
    }

    std::vector<std::pair<juce::RangedAudioParameter*, float>> next;
    for (const auto& entry : pluginParameters())
    {
        auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (parameters_.getParameter (entry.id));
        const auto found = values.find (entry.id);
        const float plain = parameter->getNormalisableRange().snapToLegalValue (
            found != values.end() ? found->second : parameter->convertFrom0to1 (parameter->getDefaultValue()));
        next.emplace_back (parameter, plain);
    }
    return [this, next = std::move (next)] {
        for (std::size_t i = 0; i < next.size(); ++i)
        {
            auto* parameter = next[i].first;
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (next[i].second));
            restored_[i] = { next[i].second, plainValueOf (*parameter) };
        }
    };
}

juce::AudioProcessorEditor* DrumsProcessor::createEditor()
{
    auto* editor = new vox::suite::SuiteEditor (*this);
    editor->setContent (std::make_unique<DrumsPanel> (*this));
    return editor;
}

} // namespace vox::drums

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new vox::drums::DrumsProcessor();
}
