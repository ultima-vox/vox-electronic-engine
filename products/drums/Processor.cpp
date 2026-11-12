#include "Processor.h"
#include "PatternExport.h"

#include "DrumsEditor.h"
#include "Presets.h"

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

bool decodeParameterValues (const preset::Section& section, std::map<std::string, float>& values, juce::String& error)
{
    values.clear();
    const auto& data = section.data;
    std::size_t at = 0;
    std::uint32_t count = 0;
    if (! takeU32 (data, at, count) || count > 4096)
    {
        error = "malformed parameter section";
        return false;
    }
    for (std::uint32_t i = 0; i < count; ++i)
    {
        std::uint32_t length = 0, bits = 0;
        if (! takeU32 (data, at, length) || length == 0 || length > 64 || data.size() - at < length + 4u)
        {
            error = "malformed parameter section";
            return false;
        }
        std::string id (length, '\0');
        for (std::uint32_t c = 0; c < length; ++c)
            id[c] = static_cast<char> (data[at + c]);
        at += length;
        float plain = 0.0f;
        if (! takeU32 (data, at, bits))
        {
            error = "malformed parameter section";
            return false;
        }
        std::memcpy (&plain, &bits, sizeof plain);
        if (! std::isfinite (plain) || ! values.emplace (std::move (id), plain).second)
        {
            error = "invalid or duplicate parameter value";
            return false;
        }
    }
    if (at != data.size())
    {
        error = "malformed parameter section";
        return false;
    }
    return true;
}

preset::Section encodeParameterValues (const std::map<std::string, float>& values)
{
    preset::Section section;
    section.tag = parameterSectionTag;
    putU32 (section.data, static_cast<std::uint32_t> (values.size()));
    for (const auto& [id, plain] : values)
    {
        putU32 (section.data, static_cast<std::uint32_t> (id.size()));
        for (const char c : id)
            section.data.push_back (static_cast<std::byte> (c));
        std::uint32_t bits = 0;
        std::memcpy (&bits, &plain, sizeof bits);
        putU32 (section.data, bits);
    }
    return section;
}

// DUIS: u32 version (1), u8 page, pad, song bar, pattern bar, export scope.
preset::Section encodeUiState (const UiState& ui)
{
    preset::Section section;
    section.tag = uiStateSectionTag;
    putU32 (section.data, 1);
    for (auto v : { ui.page, ui.pad, ui.songBar, ui.patternBar, ui.exportScope })
        section.data.push_back (static_cast<std::byte> (v));
    return section;
}

bool decodeUiState (const preset::Section& section, UiState& out, juce::String& error)
{
    const auto& d = section.data;
    std::size_t at = 0;
    std::uint32_t version = 0;
    if (! takeU32 (d, at, version) || version != 1 || d.size() != 9)
    {
        error = "unsupported editor state section";
        return false;
    }
    UiState ui;
    ui.page = std::to_integer<std::uint8_t> (d[4]);
    ui.pad = std::to_integer<std::uint8_t> (d[5]);
    ui.songBar = std::to_integer<std::uint8_t> (d[6]);
    ui.patternBar = std::to_integer<std::uint8_t> (d[7]);
    ui.exportScope = std::to_integer<std::uint8_t> (d[8]);
    if (ui.page > 7 || ui.pad >= padCount || ui.songBar >= kit::maxSongBars || ui.patternBar >= kit::maxPatternBars
        || ui.exportScope > 1)
    {
        error = "invalid editor state section";
        return false;
    }
    out = ui;
    return true;
}

GeneratorSettings DrumsProcessor::generator() const
{
    const juce::SpinLock::ScopedLockType sl (settingsLock_);
    return generator_;
}

void DrumsProcessor::setGenerator (const GeneratorSettings& g)
{
    const juce::SpinLock::ScopedLockType sl (settingsLock_);
    generator_ = g;
}

UiState DrumsProcessor::uiState() const
{
    const juce::SpinLock::ScopedLockType sl (settingsLock_);
    return ui_;
}

void DrumsProcessor::setUiState (const UiState& ui)
{
    const juce::SpinLock::ScopedLockType sl (settingsLock_);
    ui_ = ui;
}

int DrumsProcessor::currentSlot() const
{
    return std::clamp (static_cast<int> (std::lround (parameters_.getRawParameterValue (patternParameterId)->load())), 0,
                       PatternStore::slotCount - 1);
}

bool DrumsProcessor::editSong (const std::function<void (SongData&)>& change)
{
    auto e = std::make_unique<PatternHistory::Entry>();
    e->slot = -1;
    e->hasSong = true;
    e->songBefore = patternStore_.song();
    e->songAfter = e->songBefore;
    change (e->songAfter);
    e->songAfter.normalise();   // what the store will hold
    if (e->songAfter == e->songBefore)
        return false;
    patternStore_.setSong (e->songAfter);
    patternHistory().push (std::move (e));
    return true;
}

bool DrumsProcessor::generate (Generate what, std::uint32_t laneMask, bool newSeed, int bar)
{
    auto g = generator();
    if (newSeed)
    {
        g.seed = g.seed * 1664525u + 1013904223u;
        setGenerator (g);
    }
    const int slot = currentSlot();
    auto e = std::make_unique<PatternHistory::Entry>();   // heap: patterns are ~24 KB
    if (what == Generate::song)
    {
        e->slot = -1;
        e->allBefore = std::make_unique<PatternStore::Everything>();
        patternStore_.copyEverything (*e->allBefore);
        e->allAfter = std::make_unique<PatternStore::Everything> (*e->allBefore);
        generateSong (g, *e->allAfter);
        if (e->allAfter->slots == e->allBefore->slots && e->allAfter->alternates == e->allBefore->alternates
            && e->allAfter->song == e->allBefore->song)
            return false;
        patternStore_.setEverything (*e->allAfter);
    }
    else if (what == Generate::variation)
    {
        e->slot = slot;
        e->alternate = true;
        patternStore_.copySlot (slot, e->before);
        e->after = e->before;
        patternStore_.copyAlternate (slot, e->beforeB);
        e->afterB = e->beforeB;
        generateVariation (g, e->before, e->afterB);
        if (e->afterB == e->beforeB)
            return false;
        patternStore_.setAlternate (slot, e->afterB);
    }
    else
    {
        e->slot = slot;
        patternStore_.copySlot (slot, e->before);
        e->after = e->before;
        if (what == Generate::pattern)
            generatePattern (g, e->after);
        else if (what == Generate::mutate)
            mutateLanes (g, laneMask, e->after);
        else
            generateFill (g, e->after, bar);
        if (e->after == e->before)
            return false;
        patternStore_.setSlot (slot, e->after);
    }
    patternHistory().push (std::move (e));
    return true;
}

// Accent: Copper (05_ULTIMA_VOX_UI_JUCE_SKIN.md §3).
DrumsProcessor::DrumsProcessor()
    : vox::suite::SuiteProcessor ({ JucePlugin_Name,
                                    vox::preset::makeTag (VOX_SUITE_STATE_TAG),
                                    1,
                                    juce::Colour::fromString ("FFEF805D") },
                                  [] {
                                      auto buses = BusesProperties().withOutput ("Main", juce::AudioChannelSet::stereo(), true);
                                      for (int out = 2; out <= kit::hostOutputBuses; ++out)
                                          buses = buses.withOutput ("Out " + juce::String (out), juce::AudioChannelSet::stereo(), false);
                                      return buses;
                                  }()),
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
        r.send1 = parameters_.getRawParameterValue (prefix + ".send1");
        r.send2 = parameters_.getRawParameterValue (prefix + ".send2");
        r.solo = parameters_.getRawParameterValue (prefix + ".solo");
    }
    volume_ = parameters_.getRawParameterValue ("drm.kit.volume");
    velocity_ = parameters_.getRawParameterValue ("drm.kit.velocity");
    choke_ = parameters_.getRawParameterValue ("drm.kit.hat_choke");
    mode_ = parameters_.getRawParameterValue ("drm.seq.mode");
    swing_ = parameters_.getRawParameterValue ("drm.seq.swing");
    accent_ = parameters_.getRawParameterValue ("drm.seq.accent");
    pattern_ = parameters_.getRawParameterValue (patternParameterId);
    delayTime_ = parameters_.getRawParameterValue ("drm.fx.delay.time");
    delayFeedback_ = parameters_.getRawParameterValue ("drm.fx.delay.feedback");
    delayTone_ = parameters_.getRawParameterValue ("drm.fx.delay.tone");
    delayLevel_ = parameters_.getRawParameterValue ("drm.fx.delay.level");
    reverbSize_ = parameters_.getRawParameterValue ("drm.fx.reverb.size");
    reverbDamp_ = parameters_.getRawParameterValue ("drm.fx.reverb.damp");
    reverbLevel_ = parameters_.getRawParameterValue ("drm.fx.reverb.level");
    presets_ = std::make_unique<Presets> (*this);
    events_.resize (midiCapacity);
    live_ = defaultKit();
    for (const auto& entry : pluginParameters())
    {
        const float plain = plainValueOf (*parameters_.getParameter (entry.id));
        restored_.push_back ({ plain, plain });
    }
}

DrumsProcessor::~DrumsProcessor() = default;

void DrumsProcessor::prepareToPlay (double sampleRate, int maximumBlockSize)
{
    vox::suite::SuiteProcessor::prepareToPlay (sampleRate, maximumBlockSize);
    samples_.collectAllStopped();   // the audio thread is not running here
    engine_.prepare (sampleRate);
    voiceLimitApplied_ = voiceLimitOverride_ > 0 ? voiceLimitOverride_ : static_cast<int> (store_.snapshot().voiceLimit);
    engine_.setMaxVoices (voiceLimitApplied_);
    fx_.prepare (sampleRate);
    // The engine's internal buses. A host may not exceed maximumBlockSize;
    // a block that does anyway is rendered in chunks of scratchFrames_.
    scratchFrames_ = std::max (maximumBlockSize, 1024);
    scratch_.assign (static_cast<std::size_t> (scratchFrames_) * kit::auxBuses * 2, 0.0f);
    for (std::size_t i = 0; i < scratchPointers_.size(); ++i)
        scratchPointers_[i] = scratch_.data() + i * static_cast<std::size_t> (scratchFrames_);
    prepared_ = true;
}

void DrumsProcessor::releaseResources()
{
    engine_.reset();
    fx_.reset();
}

// Main stereo, then Out 2..9: each stereo or disabled, nothing else.
bool DrumsProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (! layouts.inputBuses.isEmpty() || layouts.outputBuses.size() < 1 || layouts.outputBuses.size() > kit::hostOutputBuses
        || layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    for (int bus = 1; bus < layouts.outputBuses.size(); ++bus)
        if (! layouts.outputBuses[bus].isDisabled() && layouts.outputBuses[bus] != juce::AudioChannelSet::stereo())
            return false;
    return true;
}

void DrumsProcessor::reset()
{
    panicPending_.store (true, std::memory_order_release);
}

double DrumsProcessor::getTailLengthSeconds() const
{
    // The longest pad decay (10 s plus its -80 dB tail) and then the
    // returns down to -60 dB at the current settings: the delay needs
    // ln(0.001) / ln(feedback) repeats of its time (at most 2 s, at the last
    // tempo seen); the reverb's longest comb (36.7 ms) decays by its
    // feedback 0.7 + 0.28 * size.
    const auto load = [] (const std::atomic<float>* a) { return static_cast<double> (a->load (std::memory_order_relaxed)); };
    const double feedback = std::clamp (load (delayFeedback_), 0.0, 0.85);
    const double delaySeconds = std::clamp (delayTimeQuarters (static_cast<int> (std::lround (load (delayTime_)))) * 60.0
                                                / std::clamp (lastBpm_.load (std::memory_order_relaxed), 20.0, 999.0),
                                            0.01, kit::SendFx::maxDelaySeconds);
    const double repeats = feedback > 0.001 ? std::log (0.001) / std::log (feedback) : 1.0;
    const double delayTail = delaySeconds * (repeats + 1.0);
    const double reverbFeedback = 0.7 + 0.28 * std::clamp (load (reverbSize_), 0.0, 1.0);
    const double reverbTail = -3.0 * 0.0367 / std::log10 (reverbFeedback) + 0.1;
    return 10.5 + std::max (delayTail, reverbTail);
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
    buffer.clear();

    // Settings: structure from the store, host parameters on top.
    store_.pull (live_);
    const auto load = [] (std::atomic<float>* a) { return a->load (std::memory_order_relaxed); };
    bool anySolo = false;
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
        p.send1 = load (r.send1);
        p.send2 = load (r.send2);
        anySolo = anySolo || load (r.solo) >= 0.5f;
    }
    // Solo: while any pad is soloed, the others are muted.
    if (anySolo)
        for (int pad = 0; pad < padCount; ++pad)
            if (load (padRefs_[static_cast<std::size_t> (pad)].solo) < 0.5f)
                live_.pads[static_cast<std::size_t> (pad)].mute = true;
    {
        const int want = voiceLimitOverride_ > 0 ? voiceLimitOverride_ : static_cast<int> (live_.voiceLimit);
        if (want != voiceLimitApplied_)
        {
            engine_.setMaxVoices (want);   // stops the voices; no allocation
            voiceLimitApplied_ = want;
        }
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
                // An audition triggers the pad itself (review 6743099574):
                // not through the note map, where a shared note goes to the
                // lowest pad, and also in Internal mode.
                kit::Event e;
                e.offset = 0;
                e.bytes[2] = a.velocity;
                e.pad = static_cast<std::int8_t> (a.pad);
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
                lastBpm_.store (*bpm, std::memory_order_relaxed);
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

    const int slot = std::clamp (static_cast<int> (std::lround (load (pattern_))), 0, PatternStore::slotCount - 1);
    const auto& pattern = patternStore_.pull (slot, livePattern_);
    const kit::Song* song = patternStore_.pullSong (liveSong_);
    songBar_.store (song != nullptr && transport.hasPpq
                        ? static_cast<int> (kit::songBarAt (*song, static_cast<std::int64_t> (std::floor (transport.ppq / 4.0))))
                        : -1,
                    std::memory_order_relaxed);
    const bool panic = panicPending_.exchange (false, std::memory_order_acq_rel);
    const auto* sampleViews = samples_.acquire();

    // Host buses: Main, then Out 2..9 where the host enabled them. A bus is
    // usable when it has both of its channels in this buffer.
    std::array<float*, kit::hostOutputBuses * 2> host {};
    std::uint32_t active = 0;
    for (int bus = 0; bus < std::min (getBusCount (false), kit::hostOutputBuses); ++bus)
    {
        if (getChannelCountOfBus (false, bus) != 2)
            continue;
        const int first = getChannelIndexInProcessBlockBuffer (false, bus, 0);
        if (first < 0 || first + 1 >= buffer.getNumChannels())
            continue;
        host[static_cast<std::size_t> (bus * 2)] = buffer.getWritePointer (first);
        host[static_cast<std::size_t> (bus * 2 + 1)] = buffer.getWritePointer (first + 1);
        active |= 1u << bus;
    }
    if ((active & 1u) == 0)   // no stereo Main: nothing to render into
    {
        samples_.report (engine_.usage());
        return;
    }
    activeOutputs_.store (active, std::memory_order_relaxed);

    kit::FxSettings fx;
    fx.delaySeconds = delayTimeQuarters (static_cast<int> (std::lround (load (delayTime_)))) * 60.0
                    / std::clamp (transport.hasTempo ? transport.bpm : 120.0, 20.0, 999.0);
    fx.delayFeedback = load (delayFeedback_);
    fx.delayTone = load (delayTone_);
    fx.delayLevel = load (delayLevel_);
    fx.reverbSize = load (reverbSize_);
    fx.reverbDamp = load (reverbDamp_);
    fx.reverbLevel = load (reverbLevel_);

    // Rendered in chunks no longer than the internal buses (one chunk for
    // any block within the prepared maximum).
    const double ppqPerFrame = transport.hasTempo && getSampleRate() > 0.0 ? transport.bpm / (60.0 * getSampleRate()) : 0.0;
    std::array<float, padCount> padPeak {};
    float mainPeak[2] = { 0.0f, 0.0f };
    std::size_t nextEvent = 0;
    for (int start = 0; start < frames;)
    {
        const int n = std::min (frames - start, scratchFrames_);
        std::size_t lastEvent = nextEvent;
        while (lastEvent < eventCount && static_cast<int> (events_[lastEvent].offset) < start + n)
        {
            events_[lastEvent].offset -= static_cast<std::uint32_t> (start);
            ++lastEvent;
        }
        kit::Transport chunk = transport;
        if (chunk.hasPpq)
            chunk.ppq += start * ppqPerFrame;
        float* const mainL = host[0] + start;
        float* const mainR = host[1] + start;
        const kit::Outputs out { mainL, mainR, &scratchPointers_ };
        engine_.process (live_, out, static_cast<std::uint32_t> (n), events_.data() + nextEvent,
                         static_cast<std::uint32_t> (lastEvent - nextEvent), chunk, &pattern, sampleViews, panic && start == 0,
                         kit::KitEngine::Release::none, song);
        nextEvent = lastEvent;
        pads |= engine_.sequencerHits();
        for (int pad = 0; pad < padCount; ++pad)
            padPeak[static_cast<std::size_t> (pad)] = std::max (padPeak[static_cast<std::size_t> (pad)], engine_.padPeak (pad));

        // Out 2..9: to the host bus, or to Main when the host disabled it.
        for (int bus = 1; bus <= kit::auxOutputs; ++bus)
        {
            const float* fromL = scratchPointers_[static_cast<std::size_t> ((kit::auxSends + bus - 1) * 2)];
            const float* fromR = scratchPointers_[static_cast<std::size_t> ((kit::auxSends + bus - 1) * 2 + 1)];
            if ((active & (1u << bus)) != 0)
            {
                std::memcpy (host[static_cast<std::size_t> (bus * 2)] + start, fromL, sizeof (float) * static_cast<std::size_t> (n));
                std::memcpy (host[static_cast<std::size_t> (bus * 2 + 1)] + start, fromR, sizeof (float) * static_cast<std::size_t> (n));
            }
            else
                for (int i = 0; i < n; ++i)
                {
                    mainL[i] += fromL[i];
                    mainR[i] += fromR[i];
                }
        }
        // Send 1 -> delay, Send 2 -> reverb; both return to Main only.
        // All Sound Off / panic also ends the returns' tails, at its frame:
        // what was sent before it is played into the old tail and cleared
        // with it, so nothing sounded before the message echoes after it.
        const int cut = std::clamp (engine_.soundOffFrame(), -1, n);
        if (cut > 0)
            fx_.process (fx, scratchPointers_[0], scratchPointers_[1], scratchPointers_[2], scratchPointers_[3], mainL, mainR,
                         static_cast<std::uint32_t> (cut));
        if (cut >= 0)
            fx_.reset();
        const int rest = std::max (cut, 0);
        fx_.process (fx, scratchPointers_[0] + rest, scratchPointers_[1] + rest, scratchPointers_[2] + rest, scratchPointers_[3] + rest,
                     mainL + rest, mainR + rest, static_cast<std::uint32_t> (n - rest));
        for (int i = 0; i < n; ++i)
        {
            mainPeak[0] = std::max (mainPeak[0], std::fabs (mainL[i]));
            mainPeak[1] = std::max (mainPeak[1], std::fabs (mainR[i]));
        }
        start += n;
    }
    samples_.report (engine_.usage());
    activeVoices_.store (engine_.activeVoices(), std::memory_order_relaxed);
    const auto raise = [] (std::atomic<float>& meter, float peak) {
        if (peak > meter.load (std::memory_order_relaxed))
            meter.store (peak, std::memory_order_relaxed);
    };
    for (int pad = 0; pad < padCount; ++pad)
        raise (padMeter_[static_cast<std::size_t> (pad)], padPeak[static_cast<std::size_t> (pad)]);
    raise (mainMeter_[0], mainPeak[0]);
    raise (mainMeter_[1], mainPeak[1]);
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
    out.push_back (patternStore_.encodeCurrent());
    out.push_back (encodeSong (patternStore_.song()));
    out.push_back (encodeGenerator (generator()));
    out.push_back (encodeUiState (uiState()));
}

std::shared_ptr<const ExportSnapshot> DrumsProcessor::captureExport (ExportSnapshot::Scope scope, int passes)
{
    auto snapshot = std::make_shared<ExportSnapshot>();   // heap: a pattern is ~24 KB
    snapshot->scope = scope;
    snapshot->passes = std::max (1, passes);
    snapshot->bpm = lastBpm();
    snapshot->slot = std::clamp (static_cast<int> (std::lround (parameters_.getRawParameterValue (patternParameterId)->load())), 0,
                                 PatternStore::slotCount - 1);
    snapshot->context.swing = parameters_.getRawParameterValue ("drm.seq.swing")->load();
    snapshot->context.accent = parameters_.getRawParameterValue ("drm.seq.accent")->load();
    snapshot->context.laneMask = 0;
    const auto kit = store_.snapshot();
    for (int pad = 0; pad < padCount; ++pad)
    {
        if (parameters_.getRawParameterValue ("drm.seq.lane." + padPrefix (pad).substr (4))->load() >= 0.5f)
            snapshot->context.laneMask |= 1u << pad;
        snapshot->notes[static_cast<std::size_t> (pad)] = kit.pads[static_cast<std::size_t> (pad)].note;
    }
    if (scope == ExportSnapshot::Scope::pattern)
    {
        patternStore_.copySlot (snapshot->slot, snapshot->pattern);
        snapshot->trackName = "VOX Drums Pattern " + juce::String (snapshot->slot + 1);
    }
    else
    {
        const auto all = std::make_unique<PatternStore::Everything>();
        patternStore_.copyEverything (*all);
        snapshot->songLength = all->song.length;
        for (int bar = 0; bar < all->song.length; ++bar)
            snapshot->bars[static_cast<std::size_t> (bar)] =
                resolveBar (all->song.bars[static_cast<std::size_t> (bar)], all->slots, all->alternates);
        snapshot->trackName = "VOX Drums Song";
    }
    return snapshot;
}

juce::MemoryBlock DrumsProcessor::renderExport (const ExportSnapshot& snapshot)
{
    ExportOptions options;
    options.passes = snapshot.passes;
    options.bpm = snapshot.bpm;
    if (snapshot.scope == ExportSnapshot::Scope::pattern)
        return exportMidi (snapshot.pattern.view(), snapshot.context, snapshot.notes, options, snapshot.trackName);
    std::array<kit::SongBarView, kit::maxSongBars> views {};
    for (int bar = 0; bar < snapshot.songLength; ++bar)
        if (const auto& cells = snapshot.bars[static_cast<std::size_t> (bar)])
            views[static_cast<std::size_t> (bar)] = { cells->cells.data(), BarCells::stride, cells->steps, cells->seed };
    kit::Song song;
    song.bars = static_cast<std::uint32_t> (snapshot.songLength);
    song.views = views.data();
    return exportSongMidi (song, snapshot.context, snapshot.notes, options, snapshot.trackName);
}

juce::MemoryBlock DrumsProcessor::exportPatternMidi (int slot, int passes, double bpm)
{
    auto snapshot = std::make_shared<ExportSnapshot> (*captureExport (ExportSnapshot::Scope::pattern, passes));
    snapshot->slot = std::clamp (slot, 0, PatternStore::slotCount - 1);
    patternStore_.copySlot (snapshot->slot, snapshot->pattern);
    snapshot->trackName = "VOX Drums Pattern " + juce::String (snapshot->slot + 1);
    snapshot->bpm = bpm;
    return renderExport (*snapshot);
}

DrumsProcessor::ProductRestore DrumsProcessor::readProductState (const preset::StateDocument& document, juce::String& error)
{
    // Samples first: validation and decoding, before anything changes.
    // Hard cap (owner decision, #120): the restored samples reserve their
    // memory next to everything held now; what does not fit is deferred and
    // loads once the samples it replaces are reclaimed. A restore that is
    // rejected later releases its reservation with sampleRestore.
    auto sampleRestore = std::make_shared<SampleLibrary::Restore>();
    if (! samples_.readState (document.find (sampleSectionTag), *sampleRestore, error))
        return {};

    std::map<std::string, float> values;
    if (const auto* section = document.find (parameterSectionTag))
    {
        if (! decodeParameterValues (*section, values, error))
            return {};
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

    // Patterns: the DSEQ section; factory patterns otherwise (DRM-01..03).
    auto patternRestore = std::make_shared<std::array<StoredPattern, PatternStore::slotCount>>();
    PatternStore::factorySlots (*patternRestore);
    auto alternateRestore = std::make_shared<std::array<StoredPattern, PatternStore::slotCount>> (*patternRestore);
    if (const auto* seq = document.find (sequenceSectionTag))
    {
        if (! PatternStore::decode (*seq, *patternRestore, *alternateRestore, error))
            return {};
    }

    // Song: the DSNG section; the initial song otherwise (projects before DRM-05).
    auto songRestore = std::make_shared<SongData> (SongData::initial());
    if (const auto* songSection = document.find (songSectionTag))
    {
        if (! decodeSong (*songSection, *songRestore, error))
            return {};
    }

    // Generator settings and editor state: defaults in older projects.
    auto generatorRestore = std::make_shared<GeneratorSettings>();
    if (const auto* section = document.find (generatorSectionTag))
    {
        if (! decodeGenerator (*section, *generatorRestore, error))
            return {};
    }
    auto uiRestore = std::make_shared<UiState>();
    if (const auto* section = document.find (uiStateSectionTag))
    {
        if (! decodeUiState (*section, *uiRestore, error))
            return {};
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
        restore.sections.push_back (PatternStore::encode (*patternRestore, *alternateRestore));
        restore.sections.push_back (encodeSong (*songRestore));
        restore.sections.push_back (encodeGenerator (*generatorRestore));
        restore.sections.push_back (encodeUiState (*uiRestore));
    }
    restore.apply = [this, shared, held, sampleRestore, kitRestore, patternRestore, alternateRestore, songRestore, generatorRestore,
                     uiRestore] {
        setGenerator (*generatorRestore);
        setUiState (*uiRestore);
        store_.setAll (*kitRestore);
        patternStore_.setAll (*patternRestore);
        patternStore_.setAlternates (*alternateRestore);
        patternStore_.setSong (*songRestore);
        patternEpoch_.fetch_add (1);   // the editor's undo history ends here
        samples_.apply (*sampleRestore);
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
