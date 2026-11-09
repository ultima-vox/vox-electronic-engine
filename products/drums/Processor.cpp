#include "Processor.h"

#include "DrumsEditor.h"

#include <cmath>
#include <cstring>
#include <map>
#include <memory>

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
    for (int pad = 0; pad < padCount; ++pad)
    {
        const auto prefix = padPrefix (pad);
        auto& r = padRefs_[static_cast<std::size_t> (pad)];
        r.level = parameters_.getRawParameterValue (prefix + ".level");
        r.tune = parameters_.getRawParameterValue (prefix + ".tune");
        r.decay = parameters_.getRawParameterValue (prefix + ".decay");
        r.pan = parameters_.getRawParameterValue (prefix + ".pan");
        r.cutoff = parameters_.getRawParameterValue (prefix + ".cutoff");
        r.mute = parameters_.getRawParameterValue (prefix + ".mute");
        r.lane = parameters_.getRawParameterValue ("drm.seq.lane." + prefix.substr (4));
    }
    volume_ = parameters_.getRawParameterValue ("drm.kit.volume");
    velocity_ = parameters_.getRawParameterValue ("drm.kit.velocity");
    choke_ = parameters_.getRawParameterValue ("drm.kit.hat_choke");
    mode_ = parameters_.getRawParameterValue ("drm.seq.mode");
    swing_ = parameters_.getRawParameterValue ("drm.seq.swing");
    accent_ = parameters_.getRawParameterValue ("drm.seq.accent");
    pattern_ = parameters_.getRawParameterValue (patternParameterId);
    for (int i = 0; i < kit::factoryPatternCount; ++i)
        patterns_[static_cast<std::size_t> (i)] = kit::factoryPattern (i, patternCells_[static_cast<std::size_t> (i)]);
    events_.resize (midiCapacity);
    live_ = defaultKit();
    for (const auto& entry : pluginParameters())
    {
        const float plain = plainValueOf (*parameters_.getParameter (entry.id));
        restored_.push_back ({ plain, plain });
    }
}

void DrumsProcessor::prepareToPlay (double sampleRate, int maximumBlockSize)
{
    vox::suite::SuiteProcessor::prepareToPlay (sampleRate, maximumBlockSize);
    samples_.collectAllStopped();   // the audio thread is not running here
    engine_.prepare (sampleRate);
    engine_.setMaxVoices (voiceLimit_);
    prepared_ = true;
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
    return 10.5;   // longest decay (10 s) plus the -80 dB tail
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
// constructor; the structural settings come wait-free from the store; the
// playhead query is the host's.
void DrumsProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    noteMidiSeen (midi);
    const int frames = buffer.getNumSamples();
    if (! prepared_ || buffer.getNumChannels() < 2 || frames <= 0)
    {
        buffer.clear();
        return;
    }
    for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, frames);

    // Settings: structure from the store, host parameters on top.
    store_.pull (live_);
    const auto load = [] (std::atomic<float>* a) { return a->load (std::memory_order_relaxed); };
    for (int pad = 0; pad < padCount; ++pad)
    {
        auto& p = live_.pads[static_cast<std::size_t> (pad)];
        const auto& r = padRefs_[static_cast<std::size_t> (pad)];
        p.level = load (r.level);
        p.tune = load (r.tune);
        p.decay = load (r.decay);
        p.pan = load (r.pan);
        p.cutoff = load (r.cutoff);
        p.mute = load (r.mute) >= 0.5f;
        p.laneOn = load (r.lane) >= 0.5f;
    }
    live_.volume = load (volume_);
    live_.velocitySensitivity = load (velocity_);
    live_.chokeEnabled = load (choke_) >= 0.5f;
    live_.sequencerMode = static_cast<std::uint8_t> (std::lround (load (mode_)));
    live_.swing = load (swing_);
    live_.accent = load (accent_);

    // Pad auditions from the editor first (offset 0), then host MIDI.
    std::array<int, 128> padForNote {};
    padForNote.fill (-1);
    for (int pad = padCount - 1; pad >= 0; --pad)
        padForNote[live_.pads[static_cast<std::size_t> (pad)].note & 0x7f] = pad;
    std::uint32_t pads = 0;
    std::size_t eventCount = 0;
    {
        const auto scope = auditionFifo_.read (auditionFifo_.getNumReady());
        const auto push = [&] (int start, int count) {
            for (int i = 0; i < count && eventCount < events_.size(); ++i)
            {
                const auto& a = auditions_[static_cast<std::size_t> (start + i)];
                const auto note = live_.pads[a.pad].note;
                // An audition plays the pad even in Internal mode and on a
                // shared note: it goes to the pad's own note.
                kit::Event e;
                e.offset = 0;
                e.size = 3;
                e.bytes[0] = 0x90u;
                e.bytes[1] = note;
                e.bytes[2] = a.velocity;
                events_[eventCount++] = e;
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
        kit::Event e;
        e.offset = static_cast<std::uint32_t> (juce::jlimit (0, frames - 1, metadata.samplePosition));
        e.size = static_cast<std::uint8_t> (size);
        std::memcpy (e.bytes, metadata.data, static_cast<std::size_t> (size));
        if ((e.bytes[0] & 0xf0u) == 0x90u && size == 3 && e.bytes[2] > 0 && padForNote[e.bytes[1] & 0x7f] >= 0
            && live_.sequencerMode != 1)
            pads |= 1u << padForNote[e.bytes[1] & 0x7f];
        events_[eventCount++] = e;
    }
    if (dropped > 0)
        droppedMidi_.fetch_add (dropped, std::memory_order_relaxed);

    kit::Transport transport;
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
        }
    }
    playing_.store (transport.playing, std::memory_order_relaxed);
    if (transport.hasPpq)
        lastPpq_.store (transport.ppq, std::memory_order_relaxed);

    const int pattern = std::clamp (static_cast<int> (std::lround (load (pattern_))), 0, kit::factoryPatternCount - 1);
    const bool panic = panicPending_.exchange (false, std::memory_order_acq_rel);
    const auto* sampleViews = samples_.acquire();
    const kit::Outputs out { buffer.getWritePointer (0), buffer.getWritePointer (1), nullptr };
    engine_.process (live_, out, static_cast<std::uint32_t> (frames), events_.data(), static_cast<std::uint32_t> (eventCount),
                     transport, &patterns_[static_cast<std::size_t> (pattern)], sampleViews, panic);
    samples_.report (engine_.usage());
    pads |= engine_.sequencerHits();
    for (int pad = 0; pad < padCount; ++pad)   // a muted pad does not play, so it does not light
        if (live_.pads[static_cast<std::size_t> (pad)].mute)
            pads &= ~(1u << pad);
    if (pads != 0)
        hitMask_.fetch_or (pads, std::memory_order_acq_rel);
}

//==============================================================================
// DPRM section: u32 count, then per parameter u32 id length, id bytes (UTF-8),
// f32 plain value (little endian bit pattern). Unknown IDs are ignored (a
// newer writer); missing ones take their defaults.
namespace {

// One DPRM section from plain values in pluginParameters() order.
preset::Section encodeParameters (const std::vector<float>& plain)
{
    preset::Section section;
    section.tag = parameterSectionTag;
    const auto& entries = pluginParameters();
    putU32 (section.data, static_cast<std::uint32_t> (entries.size()));
    for (std::size_t i = 0; i < entries.size(); ++i)
    {
        putU32 (section.data, static_cast<std::uint32_t> (entries[i].id.size()));
        for (const char c : entries[i].id)
            section.data.push_back (static_cast<std::byte> (c));
        std::uint32_t bits = 0;
        std::memcpy (&bits, &plain[i], sizeof bits);
        putU32 (section.data, bits);
    }
    return section;
}

} // namespace

void DrumsProcessor::writeProductState (std::vector<preset::Section>& out) const
{
    const auto& entries = pluginParameters();
    std::vector<float> plain (entries.size());
    for (std::size_t i = 0; i < entries.size(); ++i)
    {
        // A value restored from a state and not moved since is written back
        // as it was loaded: a skewed range cannot represent every float, so
        // the parameter may hold the nearest value instead.
        const float current = plainValueOf (*parameters_.getParameter (entries[i].id));
        plain[i] = juce::exactlyEqual (current, restored_[i].held) ? restored_[i].loaded : current;
    }
    out.push_back (encodeParameters (plain));
    out.push_back (KitStore::encode (store_.snapshot()));
    out.push_back (samples_.writeState());
}

DrumsProcessor::ProductRestore DrumsProcessor::readProductState (const preset::StateDocument& document, juce::String& error)
{
    // Samples first: validation and decoding, before anything changes.
    auto sampleRestore = std::make_shared<SampleLibrary::Restore>();
    // The restored samples may use what the retired ones leave of the budget;
    // the pads they replace are retired afterwards (transiently at most
    // twice the budget until they are reclaimed).
    samples_.collect();
    const auto retired = samples_.retiredBytes();
    const auto limit = samples_.budget();
    const auto budget = retired < limit ? limit - retired : 0;
    if (! SampleLibrary::readState (document.find (sampleSectionTag), *sampleRestore, error, budget))
        return {};

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

    // Pad structure: the DPAD section, or (DRM-01/02 projects) the former
    // parameters migrated onto pads 1-8; defaults otherwise.
    auto kitRestore = std::make_shared<kit::KitSettings> (defaultKit());
    if (const auto* pads = document.find (padSectionTag))
    {
        if (! KitStore::decode (*pads, *kitRestore, error))
            return {};
    }
    else
        KitStore::migrateLegacyParameters (values, *kitRestore);

    std::vector<std::pair<juce::RangedAudioParameter*, float>> next;
    for (const auto& entry : pluginParameters())
    {
        auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (parameters_.getParameter (entry.id));
        const auto found = values.find (entry.id);
        const float plain = parameter->getNormalisableRange().snapToLegalValue (
            found != values.end() ? found->second : parameter->convertFrom0to1 (parameter->getDefaultValue()));
        next.emplace_back (parameter, plain);
    }
    // Apply notifies the host (no state lock held); settle records, under
    // the state lock, what each parameter now holds so an unmoved value saves
    // back exactly as loaded.
    auto shared = std::make_shared<decltype (next)> (std::move (next));
    auto held = std::make_shared<std::vector<float>> (shared->size(), 0.0f);
    ProductRestore restore;
    {
        std::vector<float> plain;
        for (const auto& entry : *shared)
            plain.push_back (entry.second);
        restore.sections.push_back (encodeParameters (plain));   // = writeProductState after settle
        restore.sections.push_back (KitStore::encode (*kitRestore));
        restore.sections.push_back (SampleLibrary::encodeState (sampleRestore->pads));
    }
    restore.apply = [this, shared, held, sampleRestore, kitRestore] {
        store_.setAll (*kitRestore);
        samples_.apply (std::move (*sampleRestore));
        for (std::size_t i = 0; i < shared->size(); ++i)
        {
            auto* parameter = (*shared)[i].first;
            parameter->setValueNotifyingHost (parameter->convertTo0to1 ((*shared)[i].second));
            (*held)[i] = plainValueOf (*parameter);
        }
    };
    restore.settle = [this, shared, held] {
        for (std::size_t i = 0; i < shared->size(); ++i)
            restored_[i] = { (*shared)[i].second, (*held)[i] };
    };
    return restore;
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
