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
#include <map>
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
    std::uint32_t count() const { return descriptor_->parameterCount; }

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
    bool samples = false;   // every pad 1-8: even Sample, odd Layer
};

juce::File sampleFile;
constexpr const char* padKeys[8] = { "kick", "snare", "clap", "chat", "ohat", "tom", "rim", "cymbal" };

float raw (DrumsProcessor& p, const std::string& id)
{
    return p.state().getRawParameterValue (id)->load();
}

// What the plugin's engine holds, as legacy parameter values by legacy index:
// host parameters (raw values) and structural settings (the kit store).
std::vector<float> legacyValues (DrumsProcessor& plugin, const Reference& reference)
{
    std::map<std::string, float> v;
    const auto kit = plugin.kitStore().snapshot();
    for (int pad = 0; pad < 8; ++pad)
    {
        const std::string key = padKeys[pad];
        const auto prefix = vox::drums::padPrefix (pad);
        const auto& ps = kit.pads[static_cast<std::size_t> (pad)];
        v[key + ".level"] = raw (plugin, prefix + ".level");
        v[key + ".tune"] = raw (plugin, prefix + ".tune");
        v[key + ".decay"] = raw (plugin, prefix + ".decay");
        v[key + ".pan"] = raw (plugin, prefix + ".pan");
        v[key + ".cutoff"] = raw (plugin, prefix + ".cutoff");
        v[key + ".character"] = ps.character;
        v[key + ".fine"] = ps.fine;
        v[key + ".resonance"] = ps.resonance;
        v[key + ".filter"] = static_cast<float> (ps.filter);
        v[key + ".transient_attack"] = ps.attack;
        v[key + ".transient_sustain"] = ps.sustain;
        v[key + ".send1"] = ps.send1;
        v[key + ".send2"] = ps.send2;
        v[key + ".output"] = static_cast<float> (ps.output);
        v[key + ".source"] = static_cast<float> (ps.source);
        v[key + ".sample_start"] = ps.players[0].start;
        v[key + ".sample_end"] = ps.players[0].end;
        v[key + ".sample_loop"] = ps.players[0].loop ? 1.0f : 0.0f;
        v[key + ".sample_reverse"] = ps.players[0].reverse ? 1.0f : 0.0f;
        v[key + ".sample_normalize"] = ps.players[0].normalize ? 1.0f : 0.0f;
        v[key + ".sample_level"] = ps.players[0].level;
        v["seq.lane." + key] = raw (plugin, "drm.seq.lane." + prefix.substr (4));
    }
    v["kick.pitch_amount"] = kit.pads[0].pitchAmount;
    v["kick.pitch_decay"] = kit.pads[0].pitchDecay;
    v["kick.drive"] = kit.pads[0].drive;
    v["kick.sub"] = kit.pads[0].sub;
    v["kick.tail"] = kit.pads[0].tail;
    v["kit.volume"] = raw (plugin, "drm.kit.volume");
    v["kit.velocity"] = raw (plugin, "drm.kit.velocity");
    v["kit.hat_choke"] = raw (plugin, "drm.kit.hat_choke");
    v["seq.mode"] = raw (plugin, "drm.seq.mode");
    v["seq.swing"] = raw (plugin, "drm.seq.swing");
    v["seq.accent"] = raw (plugin, "drm.seq.accent");
    std::vector<float> out (reference.count());
    for (std::uint32_t i = 0; i < reference.count(); ++i)
    {
        const auto it = v.find (reference.parameter (i).id);
        if (it == v.end())
        {
            std::printf ("FAIL: legacy parameter %s has no plugin counterpart\n", reference.parameter (i).id);
            std::exit (EXIT_FAILURE);
        }
        out[i] = it->second;
    }
    return out;
}

void setPlain (DrumsProcessor& plugin, const std::string& id, float plain)
{
    auto* p = dynamic_cast<juce::RangedAudioParameter*> (plugin.state().getParameter (id));
    p->setValueNotifyingHost (p->convertTo0to1 (plain));
}

void run (const Scenario& sc, double rate, int block)
{
    const std::string label = std::string (sc.name) + " @ " + std::to_string (static_cast<int> (rate)) + " Hz / "
                            + std::to_string (block);
    DrumsProcessor plugin;
    TestPlayHead head;
    plugin.setPlayHead (&head);
    plugin.setRateAndBufferSizeDetails (rate, block);
    setPlain (plugin, "drm.seq.mode", static_cast<float> (sc.mode));
    setPlain (plugin, vox::drums::patternParameterId.toStdString(), static_cast<float> (sc.pattern));
    if (sc.mode != 0)
    {
        setPlain (plugin, "drm.seq.swing", 0.18f);
        setPlain (plugin, "drm.seq.accent", 0.8f);
    }

    std::array<std::shared_ptr<vox::drums::SampleBuffer>, 8> buffers {};
    if (sc.samples)
    {
        auto kit = plugin.kitStore().snapshot();
        for (int pad = 0; pad < 8; ++pad)
        {
            auto decoded = vox::drums::decodeSampleFile (sampleFile);
            check (decoded.buffer != nullptr, "decode the parity sample: " + decoded.error.toStdString());
            buffers[static_cast<std::size_t> (pad)] = decoded.buffer;
            plugin.samples().assign (vox::drums::slotOf (pad, 0), decoded.buffer, sampleFile);
            auto& p = kit.pads[static_cast<std::size_t> (pad)];
            p.source = pad % 2 == 0 ? vox::drums::kit::Source::sample : vox::drums::kit::Source::layer;
            p.players[0].loop = pad == 3;
            p.players[0].reverse = pad == 5;
            p.players[0].normalize = pad == 6;
        }
        plugin.kitStore().setAll (kit);
    }
    plugin.setVoiceLimitForTesting (16);   // the reference's voice pool
    plugin.prepareToPlay (rate, block);

    Reference reference (rate, static_cast<std::uint32_t> (block));
    reference.setSamples (buffers);
    reference.values() = legacyValues (plugin, reference);
    reference.setAll();

    std::mt19937 random (static_cast<unsigned> (block * 7 + static_cast<int> (rate) + sc.mode * 31 + sc.pattern));
    std::uniform_real_distribution<float> unit (0.0f, 1.0f);
    // Host parameters that exist in both, with the legacy range to draw from.
    struct Automatable { std::string id; float lo, hi; };
    std::vector<Automatable> automatable;
    for (int pad = 0; pad < 8; ++pad)
    {
        const auto prefix = vox::drums::padPrefix (pad);
        const std::string key = padKeys[pad];
        for (const auto* name : { "level", "tune", "decay", "pan", "cutoff" })
        {
            for (std::uint32_t i = 0; i < reference.count(); ++i)
                if (key + "." + name == reference.parameter (i).id)
                {
                    const auto& d = reference.parameter (i);
                    const float margin = 0.01f * (d.maximum - d.minimum);
                    automatable.push_back ({ prefix + "." + name, d.minimum + margin, d.maximum - margin });
                }
        }
    }
    automatable.push_back ({ "drm.kit.volume", 0.05f, 0.95f });
    automatable.push_back ({ "drm.kit.velocity", 0.05f, 0.95f });
    automatable.push_back ({ "drm.seq.accent", 0.05f, 0.95f });

    const auto& notes = std::array<std::uint8_t, 8> { 36, 38, 39, 42, 46, 45, 37, 49 };
    const double bpm = 145.0;
    const int total = static_cast<int> (rate * 6.0);
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
            ppq = 2.0;
            jumped = true;
        }
        head.info = {};
        head.info.setIsPlaying (true);
        head.info.setBpm (bpm);
        head.info.setPpqPosition (ppq);
        head.info.setPpqPositionOfLastBarStart (std::floor (ppq / 4.0) * 4.0);
        head.info.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });

        if (sc.automation && b % 3 == 1)
            for (int k = 0; k < 3; ++k)
            {
                const auto& a = automatable[random() % automatable.size()];
                setPlain (plugin, a.id, a.lo + unit (random) * (a.hi - a.lo));
            }
        if (sc.automation && b % 17 == 5)
            setPlain (plugin, "drm.kit.hat_choke", random() % 2 ? 1.0f : 0.0f);
        const auto now = legacyValues (plugin, reference);
        std::vector<VoxParamChangeV2> changes;
        for (std::uint32_t i = 0; i < reference.count(); ++i)
            if (! juce::exactlyEqual (now[i], reference.values()[i]))
            {
                reference.values()[i] = now[i];
                changes.push_back ({ i, 0u, now[i], 0u });
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
        reference.process (refL.data(), refR.data(), static_cast<std::uint32_t> (block), changes, refMidi, head.info, sc.pattern);
        for (int i = 0; i < block; ++i)
        {
            const float pl = buffer.getSample (0, i), pr = buffer.getSample (1, i);
            peak = std::max (peak, static_cast<double> (std::abs (pl)));
            if ((std::memcmp (&pl, &refL[static_cast<std::size_t> (i)], 4) != 0
                 || std::memcmp (&pr, &refR[static_cast<std::size_t> (i)], 4) != 0)
                && mismatches++ == 0)
                firstMismatch = static_cast<std::size_t> (done + i);
        }
        ppq += block / rate * bpm / 60.0;
    }
    plugin.setPlayHead (nullptr);
    check (peak > 1.0e-3, label + ": scenario makes sound");
    check (mismatches == 0, label + ": bit-identical to the DRM-00 reference (" + std::to_string (mismatches)
                                + " samples differ, first at " + std::to_string (firstMismatch) + ")");
}

// Factory patterns ported into the kit engine equal the reference module's.
void testFactoryPatterns()
{
    static const VoxHostServicesV2 services = { sizeof (VoxHostServicesV2), VOX_MODULE_ABI_V2, nullptr, nullptr };
    const VoxModuleV2* module = nullptr;
    voxModuleEntryV2 (VOX_MODULE_ABI_V2, &services, &module);
    for (int index = 0; index < vox::drums::kit::factoryPatternCount; ++index)
    {
        std::array<VoxPatternStepV2, 8 * 4 * 16> refCells {};
        VoxPatternV2 pattern = { sizeof pattern, VOX_MODULE_ABI_V2, 0, 0, 0, 0, sizeof (VoxPatternStepV2),
                                 static_cast<std::uint32_t> (refCells.size()), refCells.data() };
        const char* name = nullptr;
        module->content->getFactoryPattern (drumsId, static_cast<std::uint32_t> (index), &name, &pattern);
        std::array<vox::drums::kit::PatternStep, 8 * 4 * 16> cells {};
        const auto kitPattern = vox::drums::kit::factoryPattern (index, cells);
        bool equal = std::string (name) == vox::drums::kit::factoryPatternName (index) && kitPattern.lanes == pattern.laneCount
                  && kitPattern.steps == pattern.stepsPerBar && kitPattern.bars == pattern.barCount && kitPattern.timing == pattern.timing;
        for (std::size_t i = 0; i < cells.size(); ++i)
            equal = equal && cells[i].on == ((refCells[i].flags & VOX_STEP_ON) != 0)
                 && (! cells[i].on || (cells[i].accent == ((refCells[i].flags & VOX_STEP_ACCENT) != 0) && cells[i].velocity == refCells[i].velocity));
        check (equal, std::string ("factory pattern ") + std::to_string (index + 1) + " equals the reference");
    }
}

// A burst of 600 events on one sample through the plugin sounds exactly like
// the reference given the burst's base set (the engine resolves a sample's
// events as a set: loudest NoteOn per pad, CC120, CC123).
void testMidiBurst()
{
    const double rate = 48000.0;
    const int block = 256;
    const auto& notes = std::array<std::uint8_t, 8> { 36, 38, 39, 42, 46, 45, 37, 49 };
    for (const int at : { 0, 100 })
    {
        DrumsProcessor plugin;
        TestPlayHead head;
        head.info.setIsPlaying (false);
        head.info.setBpm (120.0);
        head.info.setPpqPosition (0.0);
        head.info.setPpqPositionOfLastBarStart (0.0);
        plugin.setPlayHead (&head);
        plugin.setRateAndBufferSizeDetails (rate, block);
        plugin.setVoiceLimitForTesting (16);
        plugin.prepareToPlay (rate, block);
        Reference reference (rate, block);
        reference.values() = legacyValues (plugin, reference);
        reference.setAll();

        std::vector<std::array<std::uint8_t, 3>> base;
        if (at == 100)
            base.push_back ({ 0xB0, 120, 0 });
        for (int pad = 0; pad < 8; ++pad)
            base.push_back ({ 0x99, notes[static_cast<std::size_t> (pad)], static_cast<std::uint8_t> (40 + pad * 10) });
        base.push_back ({ 0xB0, 123, 0 });
        juce::MidiBuffer burst;
        int k = 0;
        for (; k + static_cast<int> (base.size()) < 600; ++k)
        {
            const int pad = k % 8;
            const std::uint8_t ev[3] = { static_cast<std::uint8_t> ((k / 8) % 2 ? 0x89 : 0x99), notes[static_cast<std::size_t> (pad)],
                                         static_cast<std::uint8_t> ((k / 8) % 2 ? 0 : 1 + k % (40 + pad * 10)) };
            burst.addEvent (ev, 3, at);
        }
        for (const auto& e : base)
            burst.addEvent (e.data(), 3, at);
        std::vector<VoxMidiEventV2> refMidi;
        for (const auto& e : base)
        {
            VoxMidiEventV2 v {};
            v.sampleOffset = static_cast<std::uint32_t> (at);
            v.size = 3;
            std::memcpy (v.bytes, e.data(), 3);
            refMidi.push_back (v);
        }
        juce::AudioBuffer<float> buffer (2, block);
        std::vector<float> rl (block), rr (block);
        bool identical = true;
        double peak = 0.0;
        for (int b = 0; b < 40; ++b)
        {
            juce::MidiBuffer midi;
            if (b == 0)
                midi = burst;
            buffer.clear();
            plugin.processBlock (buffer, midi);
            reference.process (rl.data(), rr.data(), block, {}, b == 0 ? refMidi : std::vector<VoxMidiEventV2> {}, head.info, 0);
            for (int i = 0; i < block; ++i)
            {
                const float l = buffer.getSample (0, i), r = buffer.getSample (1, i);
                identical = identical && std::memcmp (&l, &rl[static_cast<std::size_t> (i)], 4) == 0
                         && std::memcmp (&r, &rr[static_cast<std::size_t> (i)], 4) == 0;
                peak = std::max (peak, static_cast<double> (std::abs (l)));
            }
        }
        plugin.setPlayHead (nullptr);
        check (plugin.droppedMidiEvents() == 0 && peak > 1.0e-3, "burst at " + std::to_string (at) + ": sounds, nothing dropped");
        check (identical, "burst of 600 events at sample " + std::to_string (at) + ": bit-identical to the reference given its base set");
    }
}

} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juce;
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
    testFactoryPatterns();
    testMidiBurst();
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
        std::printf ("VOX Drums parity: %d plugin renders bit-identical to the DRM-00 reference\n", runs);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
