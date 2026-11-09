// DRM-01 parity: the VOX Drums plugin renders bit-identically to the frozen
// DRM-00 reference module (tests/suite/legacy-reference/drums-f9c8bc6,
// driven directly through its C ABI the way the Rack did), for host MIDI on
// every pad, automation of every exposed parameter, the internal sequencer
// with every factory pattern in Internal and Both mode, a loop back jump,
// swing and accent, at 44.1/48/96 kHz and 64/333/512-frame blocks.
//
// This proves the plugin's parameter mapping, MIDI and transport conversion
// and pattern hand-off add nothing to and lose nothing from the reviewed
// engine. Main stereo output only: this build has no aux buses or samples
// (DRM-02 / DRM-06).
#include "instrument/abi/VoxModuleAbiV2.h"

#include "Processor.h"
#include "DrumsParameters.h"
#include "SampleLibrary.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

extern "C" VoxResult VOX_CALL voxModuleEntryV2 (uint32_t hostAbiVersion, const VoxHostServicesV2* host,
                                               const VoxModuleV2** module);

namespace {

using vox::drums::DrumsProcessor;

constexpr const char* drumsId = "com.ultimavox.fullon-drums";
int failures = 0;

void check (bool ok, const std::string& what)
{
    if (! ok)
    {
        std::printf ("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

class TestPlayHead final : public juce::AudioPlayHead {
public:
    juce::Optional<PositionInfo> getPosition() const override { return info; }
    PositionInfo info;
};

// The reference: the DRM-00 module, one instance, driven per host block.
class Reference {
public:
    Reference (double rate, std::uint32_t block)
    {
        static const VoxHostServicesV2 services = { sizeof (VoxHostServicesV2), VOX_MODULE_ABI_V2, nullptr, nullptr };
        if (voxModuleEntryV2 (VOX_MODULE_ABI_V2, &services, &module_) != VOX_OK || module_ == nullptr)
            std::exit (EXIT_FAILURE);
        module_->getInstrument (0, &descriptor_);
        const VoxCreateParamsV2 cp = { sizeof cp, VOX_MODULE_ABI_V2, 0, 0 };
        module_->create (drumsId, &cp, &instance_);
        const VoxPrepareSpecV2 spec = { sizeof spec, VOX_MODULE_ABI_V2, rate, block, 2u,
                                        descriptor_->parameterCount, 512u };
        check (module_->prepare (instance_, &spec) == VOX_OK, "reference prepares");
        values_.resize (descriptor_->parameterCount);
        for (std::uint32_t i = 0; i < descriptor_->parameterCount; ++i)
            values_[i] = parameter (i).defaultValue;

        for (std::uint32_t index = 0; index < 5; ++index)
        {
            auto& cells = cells_[index];
            VoxPatternV2 pattern = { sizeof pattern, VOX_MODULE_ABI_V2, 0, 0, 0, 0,
                                     sizeof (VoxPatternStepV2), static_cast<std::uint32_t> (cells.size()), cells.data() };
            const char* name = nullptr;
            check (module_->content->getFactoryPattern (drumsId, index, &name, &pattern) == VOX_OK, "factory pattern");
            patterns_[index] = pattern;
        }
    }

    ~Reference() { module_->destroy (instance_); }

    const VoxParameterDescriptorV2& parameter (std::uint32_t i) const
    {
        return *reinterpret_cast<const VoxParameterDescriptorV2*> (
            reinterpret_cast<const std::uint8_t*> (descriptor_->parameters) + std::size_t (i) * descriptor_->parameterStride);
    }

    void setAll() { module_->setAllParameters (instance_, values_.data(), static_cast<std::uint32_t> (values_.size())); }

    std::vector<float>& values() { return values_; }

    void setSamples (const std::array<std::shared_ptr<vox::drums::SampleBuffer>, 8>& buffers)
    {
        for (std::size_t pad = 0; pad < 8; ++pad)
        {
            auto& s = samples_[pad];
            s = {};
            s.structSize = sizeof s;
            s.version = VOX_MODULE_ABI_V2;
            if (const auto& b = buffers[pad])
            {
                s.id = b->id;
                s.channels = b->pointers.data();
                s.channelCount = static_cast<std::uint32_t> (b->pointers.size());
                s.frames = b->frames;
                s.sampleRate = b->sampleRate;
                s.peak = b->peak;
            }
        }
    }

    void process (float* l, float* r, std::uint32_t frames, const std::vector<VoxParamChangeV2>& changes,
                  const std::vector<VoxMidiEventV2>& midi, const juce::AudioPlayHead::PositionInfo& pos, int pattern)
    {
        VoxTransportV2 tr {};
        tr.structSize = sizeof tr;
        tr.version = VOX_MODULE_ABI_V2;
        tr.flags = (pos.getIsPlaying() ? VOX_TRANSPORT_PLAYING : 0u) | VOX_TRANSPORT_HAS_TEMPO | VOX_TRANSPORT_HAS_PPQ
                 | VOX_TRANSPORT_HAS_BAR_START | VOX_TRANSPORT_HAS_TIME_SIGNATURE;
        const double ppq = *pos.getPpqPosition();
        if (ppq < lastPpq_)
            tr.flags |= VOX_TRANSPORT_DISCONTINUITY;
        lastPpq_ = ppq;
        tr.timeSigNumerator = 4;
        tr.timeSigDenominator = 4;
        tr.tempoBpm = *pos.getBpm();
        tr.ppqPosition = ppq;
        tr.barStartPpq = *pos.getPpqPositionOfLastBarStart();

        float* outputs[2] = { l, r };
        VoxProcessContextV2 c {};
        c.structSize = sizeof c;
        c.version = VOX_MODULE_ABI_V2;
        c.numSamples = frames;
        c.outputChannelCount = 2;
        c.outputs = outputs;
        c.paramChanges = changes.data();
        c.paramChangeCount = static_cast<std::uint32_t> (changes.size());
        c.paramChangeStride = sizeof (VoxParamChangeV2);
        c.midiEvents = midi.data();
        c.midiEventCount = static_cast<std::uint32_t> (midi.size());
        c.midiEventStride = sizeof (VoxMidiEventV2);
        c.transport = &tr;
        c.pattern = &patterns_[static_cast<std::size_t> (pattern)];
        c.patternGeneration = static_cast<std::uint64_t> (pattern + 1);
        // The plugin always hands its engine the full sample table (empty
        // slots have id 0); the reference gets the same.
        for (auto& s : samples_)
            if (s.structSize == 0)
            {
                s.structSize = sizeof s;
                s.version = VOX_MODULE_ABI_V2;
            }
        usage_.structSize = sizeof usage_;
        usage_.version = VOX_MODULE_ABI_V2;
        usage_.capacity = static_cast<std::uint32_t> (usageIds_.size());
        usage_.ids = usageIds_.data();
        c.samples = samples_.data();
        c.sampleSlotCount = 8;
        c.sampleUsage = &usage_;
        check (module_->process (instance_, &c) == VOX_OK, "reference processes");
    }

private:
    const VoxModuleV2* module_ = nullptr;
    const VoxInstrumentDescriptorV2* descriptor_ = nullptr;
    VoxInstance instance_ = nullptr;
    std::vector<float> values_;
    std::array<std::array<VoxPatternStepV2, 8 * 4 * 16>, 5> cells_ {};
    std::array<VoxPatternV2, 5> patterns_ {};
    double lastPpq_ = -1.0e300;
    std::array<VoxSampleV2, 8> samples_ {};
    std::array<std::uint64_t, 32> usageIds_ {};
    VoxSampleUsageV2 usage_ {};
};

struct Scenario {
    const char* name;
    int mode;            // 0 Host, 1 Internal, 2 Both
    int pattern;
    bool hostNotes;
    bool automation;
    bool loopBack;
    bool samples = false;   // every pad: even Sample, odd Layer
};

juce::File sampleFile;

void run (const Scenario& sc, double rate, int block)
{
    const std::string label = std::string (sc.name) + " @ " + std::to_string (static_cast<int> (rate)) + " Hz / "
                            + std::to_string (block);
    DrumsProcessor plugin;
    TestPlayHead head;
    plugin.setPlayHead (&head);
    plugin.setRateAndBufferSizeDetails (rate, block);

    const auto setPlain = [&] (const std::string& id, float plain) {
        auto* p = dynamic_cast<juce::RangedAudioParameter*> (plugin.state().getParameter (id));
        p->setValueNotifyingHost (p->convertTo0to1 (plain));
    };
    setPlain ("drm.seq.mode", static_cast<float> (sc.mode));
    setPlain (vox::drums::patternParameterId.toStdString(), static_cast<float> (sc.pattern));
    if (sc.mode != 0)
    {
        setPlain ("drm.seq.swing", 0.18f);
        setPlain ("drm.seq.accent", 0.8f);
    }
    std::array<std::shared_ptr<vox::drums::SampleBuffer>, 8> buffers {};
    if (sc.samples)
    {
        for (int pad = 0; pad < 8; ++pad)
        {
            auto decoded = vox::drums::decodeSampleFile (sampleFile);
            check (decoded.buffer != nullptr, "decode the parity sample: " + decoded.error.toStdString());
            buffers[static_cast<std::size_t> (pad)] = decoded.buffer;
            plugin.samples().assign (pad, decoded.buffer, sampleFile);
            setPlain (vox::drums::padPrefix (pad) + ".source", pad % 2 == 0 ? 1.0f : 2.0f);
            setPlain (vox::drums::padPrefix (pad) + ".sample_loop", pad == 3 ? 1.0f : 0.0f);
            setPlain (vox::drums::padPrefix (pad) + ".sample_reverse", pad == 5 ? 1.0f : 0.0f);
            setPlain (vox::drums::padPrefix (pad) + ".sample_normalize", pad == 6 ? 1.0f : 0.0f);
        }
    }
    plugin.prepareToPlay (rate, block);

    Reference reference (rate, static_cast<std::uint32_t> (block));
    reference.setSamples (buffers);
    // The reference starts from exactly what the plugin's engine holds.
    const auto engineValue = [&] (std::uint32_t index) -> float {
        for (const auto& entry : vox::drums::pluginParameters())
            if (entry.engineIndex == static_cast<int> (index))
                return plugin.state().getRawParameterValue (entry.id)->load();
        return reference.parameter (index).defaultValue;
    };
    for (std::uint32_t i = 0; i < reference.values().size(); ++i)
        reference.values()[i] = engineValue (i);
    reference.setAll();

    std::mt19937 random (static_cast<unsigned> (block * 7 + static_cast<int> (rate) + sc.mode * 31 + sc.pattern));
    std::uniform_real_distribution<float> unit (0.0f, 1.0f);
    const auto& notes = vox::drums::DrumEngine::padNotes();

    const double bpm = 145.0;
    const int total = static_cast<int> (rate * 6.0);   // > 3 bars at 145 bpm
    juce::AudioBuffer<float> buffer (2, block);
    std::vector<float> refL (static_cast<std::size_t> (block)), refR (static_cast<std::size_t> (block));
    double ppq = 0.0;
    bool jumped = false;
    std::size_t mismatches = 0, firstMismatch = 0;
    double peak = 0.0;
    for (int done = 0, b = 0; done < total; done += block, ++b)
    {
        if (sc.loopBack && ! jumped && ppq >= 6.0)
        {
            ppq = 2.0;   // loop back two bars' worth of beats
            jumped = true;
        }
        head.info = {};
        head.info.setIsPlaying (true);
        head.info.setBpm (bpm);
        head.info.setPpqPosition (ppq);
        head.info.setPpqPositionOfLastBarStart (std::floor (ppq / 4.0) * 4.0);
        head.info.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });

        // Automation: every few blocks move a random exposed parameter.
        if (sc.automation && b % 3 == 1)
        {
            const auto& entries = vox::drums::pluginParameters();
            for (int k = 0; k < 3; ++k)
            {
                const auto& entry = entries[static_cast<std::size_t> (random() % entries.size())];
                if (entry.engineIndex < 0 || entry.id == "drm.seq.mode")
                    continue;
                auto* p = plugin.state().getParameter (entry.id);
                p->setValueNotifyingHost (unit (random));
            }
        }
        std::vector<VoxParamChangeV2> changes;
        for (std::uint32_t i = 0; i < reference.values().size(); ++i)
        {
            const float v = engineValue (i);
            if (! juce::exactlyEqual (v, reference.values()[i]))
            {
                reference.values()[i] = v;
                changes.push_back ({ i, 0u, v, 0u });
            }
        }

        juce::MidiBuffer midi;
        std::vector<VoxMidiEventV2> refMidi;
        if (sc.hostNotes && b % 2 == 0)
        {
            for (int k = 0; k < 3; ++k)
            {
                const int offset = static_cast<int> (random() % static_cast<unsigned> (block));
                const auto note = notes[random() % notes.size()];
                const auto velocity = static_cast<juce::uint8> (1 + random() % 127);
                midi.addEvent (juce::MidiMessage::noteOn (1, note, velocity), offset);
            }
            if (b % 10 == 4)
                midi.addEvent (juce::MidiMessage::allNotesOff (1), block / 2);
        }
        for (const auto metadata : midi)
        {
            VoxMidiEventV2 e {};
            e.sampleOffset = static_cast<std::uint32_t> (metadata.samplePosition);
            e.size = static_cast<std::uint32_t> (metadata.numBytes);
            std::memcpy (e.bytes, metadata.data, static_cast<std::size_t> (metadata.numBytes));
            refMidi.push_back (e);
        }

        buffer.clear();
        plugin.processBlock (buffer, midi);
        reference.process (refL.data(), refR.data(), static_cast<std::uint32_t> (block), changes, refMidi, head.info,
                           sc.pattern);
        for (int i = 0; i < block; ++i)
        {
            const float pl = buffer.getSample (0, i), pr = buffer.getSample (1, i);
            peak = std::max (peak, static_cast<double> (std::abs (pl)));
            if (std::memcmp (&pl, &refL[static_cast<std::size_t> (i)], 4) != 0
                || std::memcmp (&pr, &refR[static_cast<std::size_t> (i)], 4) != 0)
            {
                if (mismatches++ == 0)
                    firstMismatch = static_cast<std::size_t> (done + i);
            }
        }
        ppq += block / rate * bpm / 60.0;
    }
    plugin.setPlayHead (nullptr);
    check (peak > 1.0e-3, label + ": scenario makes sound");
    check (mismatches == 0, label + ": bit-identical to the DRM-00 reference (" + std::to_string (mismatches)
                                + " samples differ, first at " + std::to_string (firstMismatch) + ")");
}

// Review 6734386412: bursts of more than 512 events on one sample. The
// engine resolves a sample's events as a set (loudest NoteOn per pad; CC120;
// CC123), so a burst made of repeated copies of a base set must sound exactly
// like the reference module given the base set once. A dense block that only
// exceeds 512 events across samples must match the reference given the same
// events in two calls split at a sample boundary (the module is block-size
// invariant, DRM-00).
void testMidiBursts()
{
    using vox::drums::DrumEngine;
    using vox::drums::MidiEvent;
    const double rate = 48000.0;
    const std::uint32_t block = 256;
    const auto& notes = DrumEngine::padNotes();
    const auto midi3 = [] (std::uint32_t offset, std::uint8_t b0, std::uint8_t b1, std::uint8_t b2) {
        MidiEvent e;
        e.offset = offset;
        e.size = 3;
        e.bytes[0] = b0;
        e.bytes[1] = b1;
        e.bytes[2] = b2;
        return e;
    };
    const auto toRef = [] (const std::vector<MidiEvent>& events, std::uint32_t base) {
        std::vector<VoxMidiEventV2> out;
        for (const auto& e : events)
        {
            VoxMidiEventV2 v {};
            v.sampleOffset = e.offset - base;
            v.size = 3;
            std::memcpy (v.bytes, e.bytes, 3);
            out.push_back (v);
        }
        return out;
    };
    juce::AudioPlayHead::PositionInfo stopped;
    stopped.setIsPlaying (false);
    stopped.setBpm (120.0);
    stopped.setPpqPosition (0.0);
    stopped.setPpqPositionOfLastBarStart (0.0);
    stopped.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
    vox::drums::Transport transport;
    transport.hasTempo = transport.hasPpq = transport.hasBarStart = transport.hasTimeSignature = true;
    transport.bpm = 120.0;

    struct Case {
        const char* name;
        std::vector<MidiEvent> burst;      // what the plugin's engine gets
        std::vector<MidiEvent> base;       // what the reference gets (one call)
    };
    std::vector<Case> cases;
    for (const std::uint32_t at : { 0u, 100u })
    {
        // Base set: one NoteOn per pad, a CC123 (once), and a CC120 at the
        // first burst only.
        std::vector<MidiEvent> base;
        if (at == 100)
            base.push_back (midi3 (at, 0xB0, 120, 0));
        for (int pad = 0; pad < 8; ++pad)
            base.push_back (midi3 (at, 0x99, notes[static_cast<std::size_t> (pad)], static_cast<std::uint8_t> (40 + pad * 10)));
        base.push_back (midi3 (at, 0xB0, 123, 0));
        // Burst: 600 events; copies of the base NoteOns at lower velocities,
        // NoteOffs and unrelated CCs first, the base set last (beyond the
        // 512th event, where a chunker that drops or misplaces the rest
        // changes the sound).
        std::vector<MidiEvent> burst;
        int k = 0;
        while (burst.size() + base.size() < 600)
        {
            const int pad = k % 8;
            switch ((k / 8) % 3)
            {
                case 0: burst.push_back (midi3 (at, 0x99, notes[static_cast<std::size_t> (pad)], static_cast<std::uint8_t> (1 + (k % (40 + pad * 10))))); break;
                case 1: burst.push_back (midi3 (at, 0x89, notes[static_cast<std::size_t> (pad)], 0)); break;
                default: burst.push_back (midi3 (at, 0xB0, 7, static_cast<std::uint8_t> (k % 128))); break;
            }
            ++k;
        }
        burst.insert (burst.end(), base.begin(), base.end());
        cases.push_back ({ at == 0 ? "600 events at sample 0" : "600 events at sample 100 (with CC120 and CC123)",
                           burst, base });
    }

    for (const auto& c : cases)
    {
        DrumEngine engine;
        engine.prepare (rate, block);
        std::vector<float> values;
        for (const auto& p : DrumEngine::parameters())
            values.push_back (p.defaultValue);
        engine.setAllParameters (values.data(), static_cast<std::uint32_t> (values.size()));
        Reference reference (rate, block);
        reference.setAll();

        std::vector<float> l (block), r (block), rl (block), rr (block);
        bool identical = true;
        double peak = 0.0;
        for (int b = 0; b < 40; ++b)
        {
            const auto& events = b == 0 ? c.burst : std::vector<MidiEvent> {};
            engine.process (l.data(), r.data(), block, nullptr, 0, events.data(), static_cast<std::uint32_t> (events.size()),
                            transport, -1, false);
            if (b == 0)
            {
                const auto& st = engine.lastStats();
                check (st.offsetsValid, std::string (c.name) + ": every delivered offset lies inside its engine call");
                check (st.rejected == 0, std::string (c.name) + ": no engine call rejected");
                check (st.maxEventsInCall <= 512, std::string (c.name) + ": at most 512 events per engine call");
                check (st.eventsIn == 600 && st.delivered + st.folded == st.eventsIn,
                       std::string (c.name) + ": every event delivered or folded (" + std::to_string (st.delivered) + " + "
                           + std::to_string (st.folded) + ")");
            }
            reference.process (rl.data(), rr.data(), block, {}, b == 0 ? toRef (c.base, 0) : std::vector<VoxMidiEventV2> {},
                               stopped, 0);
            for (std::uint32_t i = 0; i < block; ++i)
            {
                identical = identical && std::memcmp (&l[i], &rl[i], 4) == 0 && std::memcmp (&r[i], &rr[i], 4) == 0;
                peak = std::max (peak, static_cast<double> (std::abs (l[i])));
            }
        }
        check (peak > 1.0e-3, std::string (c.name) + ": the burst sounds");
        check (identical, std::string (c.name) + ": bit-identical to the reference given the base set once");
    }

    // 600 events across two samples (300 at 0, 300 at 10): split at sample
    // 10, nothing folded, identical to the reference in two calls.
    {
        std::vector<MidiEvent> events;
        for (int i = 0; i < 600; ++i)
            events.push_back (midi3 (i < 300 ? 0u : 10u, 0x99, notes[static_cast<std::size_t> (i % 8)], static_cast<std::uint8_t> (1 + i % 127)));
        DrumEngine engine;
        engine.prepare (rate, block);
        std::vector<float> values;
        for (const auto& p : DrumEngine::parameters())
            values.push_back (p.defaultValue);
        engine.setAllParameters (values.data(), static_cast<std::uint32_t> (values.size()));
        Reference reference (rate, block);
        reference.setAll();
        std::vector<float> l (block), r (block), rl (block), rr (block);
        engine.process (l.data(), r.data(), block, nullptr, 0, events.data(), 600, transport, -1, false);
        const auto st = engine.lastStats();
        check (st.folded == 0 && st.delivered == 600 && st.calls == 2 && st.offsetsValid && st.rejected == 0,
               "300 + 300 events: two engine calls, nothing folded");
        const std::vector<MidiEvent> first (events.begin(), events.begin() + 300), second (events.begin() + 300, events.end());
        reference.process (rl.data(), rr.data(), 10, {}, toRef (first, 0), stopped, 0);
        reference.process (rl.data() + 10, rr.data() + 10, block - 10, {}, toRef (second, 10), stopped, 0);
        bool identical = true;
        for (std::uint32_t i = 0; i < block; ++i)
            identical = identical && std::memcmp (&l[i], &rl[i], 4) == 0 && std::memcmp (&r[i], &rr[i], 4) == 0;
        check (identical, "300 + 300 events: bit-identical to the reference split at the same sample");
    }

    // Through the plugin: a 600-event burst at sample 0 is not dropped and sounds.
    {
        DrumsProcessor plugin;
        plugin.setRateAndBufferSizeDetails (rate, static_cast<int> (block));
        plugin.prepareToPlay (rate, static_cast<int> (block));
        juce::AudioBuffer<float> buffer (2, static_cast<int> (block));
        juce::MidiBuffer midi;
        for (const auto& e : cases[0].burst)
            midi.addEvent (e.bytes, 3, 0);
        plugin.processBlock (buffer, midi);
        check (plugin.droppedMidiEvents() == 0 && buffer.getMagnitude (0, 0, static_cast<int> (block)) > 1.0e-3f,
               "plugin: a 600-event burst at sample 0 sounds and nothing is dropped");
    }
}

} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juce;
    // A stereo 44.1 kHz sample (resampled by the engine at every host rate).
    sampleFile = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("vox-drums-parity-sample.wav");
    {
        const int frames = 22050;
        juce::AudioBuffer<float> b (2, frames);
        for (int i = 0; i < frames; ++i)
        {
            const double t = i / 44100.0;
            b.setSample (0, i, static_cast<float> (0.7 * std::exp (-t * 9.0) * std::sin (2.0 * juce::MathConstants<double>::pi * 180.0 * t)));
            b.setSample (1, i, static_cast<float> (0.5 * std::exp (-t * 4.0) * std::sin (2.0 * juce::MathConstants<double>::pi * 1210.0 * t)));
        }
        sampleFile.deleteFile();
        JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE ("-Wdeprecated-declarations")
        JUCE_BEGIN_IGNORE_WARNINGS_MSVC (4996)
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (sampleFile.createOutputStream().release(), 44100.0, 2, 24, {}, 0));
        JUCE_END_IGNORE_WARNINGS_MSVC
        JUCE_END_IGNORE_WARNINGS_GCC_LIKE
        writer->writeFromAudioSampleBuffer (b, 0, frames);
    }
    const Scenario scenarios[] = {
        { "host MIDI, defaults", 0, 0, true, false, false },
        { "host MIDI, automation", 0, 0, true, true, false },
        { "internal, Full-on Basic", 1, 0, false, false, false },
        { "internal, Full-on Driving", 1, 1, false, true, false },
        { "internal, Rolling Offbeat, loop back", 1, 2, false, false, true },
        { "both, Breakdown", 2, 3, true, false, false },
        { "both, Kick & Offbeat, automation, loop back", 2, 4, true, true, true },
        { "samples, host MIDI, automation", 0, 0, true, true, false, true },
        { "samples, both, Full-on Driving", 2, 1, true, false, true, true },
    };
    testMidiBursts();
    int runs = 0;
    for (const auto& sc : scenarios)
        for (double rate : { 44100.0, 48000.0, 96000.0 })
            for (int block : { 64, 333, 512 })
            {
                run (sc, rate, block);
                ++runs;
            }
    sampleFile.deleteFile();
    if (failures == 0)
        std::printf ("VOX Drums parity: %d renders bit-identical to the DRM-00 reference\n", runs);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
