// DRM-02: VOX Drums pad samples.
//
//  - decode: WAV 16/24-bit PCM and 32-bit float, mono and stereo, at 44.1,
//    48 and 96 kHz, read exactly; content hash; deterministic errors for a
//    missing, empty, foreign, truncated, too long or unsupported-rate file
//    and for the memory budget;
//  - sound: Sample, Synth and Layer sources; a pad set to Sample without a
//    usable sample plays its synthesis exactly; start/end/reverse/level;
//  - ownership: a replaced buffer stays alive while a voice holds it and
//    until the audio thread has reported a newer table, then is freed on the
//    message thread; no report frees nothing; a replace storm while voices
//    play leaves nothing behind (run under ASan in the PR evidence);
//  - realtime: processBlock with samples and replacements between blocks
//    allocates nothing;
//  - state: references round-trip; a missing or changed file keeps the
//    assignment, reports a deterministic diagnostic and saves back the same
//    reference; the library copy is found by content hash; malformed
//    sample sections are rejected without touching the live state.
#include "Processor.h"
#include "DrumsEditor.h"
#include "SampleLibrary.h"
#include "DrumsParameters.h"
#include "KitStore.h"

#ifdef VOX_TEST_INTERPOSE_MALLOC
#include "AllocationProbe.h"
#endif

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>
#include <functional>
#include <memory>
#include <vector>

namespace {

using vox::drums::DrumsProcessor;
using vox::drums::SampleLibrary;

int failures = 0;
juce::File scratch;

void check (bool condition, const juce::String& what)
{
    if (! condition)
    {
        std::printf ("FAIL [VOX Drums samples]: %s\n", what.toRawUTF8());
        ++failures;
    }
}

// A deterministic test signal: a decaying two-tone burst.
float signal (int channel, juce::int64 i, double rate)
{
    const double t = static_cast<double> (i) / rate;
    return static_cast<float> (0.8 * std::exp (-t * 6.0)
                               * std::sin (2.0 * juce::MathConstants<double>::pi * (channel == 0 ? 220.0 : 330.0) * t));
}

juce::File writeWav (const juce::String& name, double rate, int channels, int bits, double seconds)
{
    auto file = scratch.getChildFile (name);
    file.deleteFile();
    const auto frames = static_cast<juce::int64> (seconds * rate);
    juce::AudioBuffer<float> buffer (channels, static_cast<int> (frames));
    for (int ch = 0; ch < channels; ++ch)
        for (juce::int64 i = 0; i < frames; ++i)
            buffer.setSample (ch, static_cast<int> (i), signal (ch, i, rate));
    JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE ("-Wdeprecated-declarations")
    JUCE_BEGIN_IGNORE_WARNINGS_MSVC (4996)
    juce::WavAudioFormat wav;
    auto stream = file.createOutputStream();
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (
        stream.release(), rate, static_cast<unsigned int> (channels), bits, {}, 0));
    JUCE_END_IGNORE_WARNINGS_MSVC
    JUCE_END_IGNORE_WARNINGS_GCC_LIKE
    if (writer == nullptr)
    {
        std::printf ("cannot write %s\n", file.getFullPathName().toRawUTF8());
        std::exit (EXIT_FAILURE);
    }
    writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
    writer.reset();
    return file;
}

std::unique_ptr<DrumsProcessor> makeProcessor (double rate = 48000.0, int block = 256)
{
    auto p = std::make_unique<DrumsProcessor>();
    p->setRateAndBufferSizeDetails (rate, block);
    p->prepareToPlay (rate, block);
    return p;
}

// A host parameter by ID, or (DRM-03b) a structural setting of the kit
// store by its former parameter ID ("drm.pNN.source", ".sample_start",
// ".sample_end", ".sample_level", ".sample_loop", ".sample_reverse",
// ".sample_normalize": player A).
void setPlain (DrumsProcessor& p, const juce::String& id, float plain)
{
    if (auto* param = dynamic_cast<juce::RangedAudioParameter*> (p.state().getParameter (id)))
    {
        param->setValueNotifyingHost (param->convertTo0to1 (plain));
        return;
    }
    const int pad = id.fromFirstOccurrenceOf ("drm.p", false, false).upToFirstOccurrenceOf (".", false, false).getIntValue() - 1;
    const auto field = id.fromLastOccurrenceOf (".", false, false);
    if (! id.startsWith ("drm.p") || pad < 0 || pad >= vox::drums::padCount)
    {
        std::printf ("no parameter %s\n", id.toRawUTF8());
        std::exit (EXIT_FAILURE);
    }
    auto ps = p.kitStore().pad (pad);
    auto& a = ps.players[0];
    if (field == "source") ps.source = static_cast<vox::drums::kit::Source> (juce::roundToInt (plain));
    else if (field == "sample_start") a.start = plain;
    else if (field == "sample_end") a.end = plain;
    else if (field == "sample_level") a.level = plain;
    else if (field == "sample_loop") a.loop = plain >= 0.5f;
    else if (field == "sample_reverse") a.reverse = plain >= 0.5f;
    else if (field == "sample_normalize") a.normalize = plain >= 0.5f;
    else
    {
        std::printf ("no parameter or structural setting %s\n", id.toRawUTF8());
        std::exit (EXIT_FAILURE);
    }
    p.kitStore().setPad (pad, ps);
}

const std::array<std::uint8_t, vox::drums::padCount>& padNotes()
{
    static const auto notes = [] {
        std::array<std::uint8_t, vox::drums::padCount> n {};
        for (int pad = 0; pad < vox::drums::padCount; ++pad)
            n[static_cast<std::size_t> (pad)] = vox::drums::defaultKit().pads[static_cast<std::size_t> (pad)].note;
        return n;
    }();
    return notes;
}

std::shared_ptr<vox::drums::SampleBuffer> decode (const juce::File& file)
{
    auto result = vox::drums::decodeSampleFile (file);
    if (result.buffer == nullptr)
    {
        std::printf ("decode failed: %s\n", result.error.toRawUTF8());
        std::exit (EXIT_FAILURE);
    }
    return result.buffer;
}

// Renders `blocks` blocks, with a pad hit at the start of the first.
std::vector<float> render (DrumsProcessor& p, int blocks, int note = 36, int block = 256)
{
    std::vector<float> out;
    juce::AudioBuffer<float> buffer (2, block);
    for (int b = 0; b < blocks; ++b)
    {
        juce::MidiBuffer midi;
        if (b == 0 && note >= 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, note, static_cast<juce::uint8> (110)), 0);
        buffer.clear();
        p.processBlock (buffer, midi);
        for (int i = 0; i < block; ++i)
        {
            out.push_back (buffer.getSample (0, i));
            out.push_back (buffer.getSample (1, i));
        }
    }
    return out;
}

// Restored parameter values pass through the host's 0..1 normalisation and
// may differ from the original in the last float bit, so restored renders are
// compared with a tolerance far below audibility (-100 dBFS).
bool close (const std::vector<float>& a, const std::vector<float>& b)
{
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::abs (a[i] - b[i]) > 1.0e-5f)
            return false;
    return true;
}

double energy (const std::vector<float>& x)
{
    double e = 0.0;
    for (auto v : x)
        e += static_cast<double> (v) * v;
    return e;
}

//==============================================================================
void testDecode()
{
    for (double rate : { 44100.0, 48000.0, 96000.0 })
        for (int channels : { 1, 2 })
            for (int bits : { 16, 24, 32 })
            {
                const auto label = juce::String (rate) + " Hz " + juce::String (channels) + " ch " + juce::String (bits) + " bit";
                const auto file = writeWav ("t_" + juce::String (static_cast<int> (rate)) + "_" + juce::String (channels) + "_"
                                                + juce::String (bits) + ".wav",
                                            rate, channels, bits, 0.25);
                const auto result = vox::drums::decodeSampleFile (file);
                check (result.buffer != nullptr, label + ": decodes (" + result.error + ")");
                if (result.buffer == nullptr)
                    continue;
                const auto& b = *result.buffer;
                check (b.frames == static_cast<std::uint64_t> (0.25 * rate), label + ": frame count");
                check (juce::exactlyEqual (b.sampleRate, rate), label + ": native rate kept");
                check (static_cast<int> (b.data.size()) == channels && b.pointers.size() == b.data.size(), label + ": channels");
                // Values: exact for float, within the quantisation step otherwise.
                const double tolerance = bits == 32 ? 1.0e-7 : (bits == 24 ? 2.0e-7 : 4.0e-5);
                double worst = 0.0;
                for (int ch = 0; ch < channels; ++ch)
                    for (std::uint64_t i = 0; i < b.frames; i += 7)
                        worst = std::max (worst, std::abs (static_cast<double> (b.data[static_cast<std::size_t> (ch)][i])
                                                          - signal (ch, static_cast<juce::int64> (i), rate)));
                check (worst <= tolerance, label + ": samples read exactly (max error " + juce::String (worst) + ")");
                check (b.peak > 0.5f && b.peak <= 0.8001f, label + ": peak");
                juce::MemoryBlock bytes;
                file.loadFileAsData (bytes);
                const auto raw = juce::SHA256 (bytes).getRawData();
                check (std::memcmp (raw.getData(), b.sha256.data(), 32) == 0, label + ": SHA-256 of the file bytes");
                check (b.id != 0, label + ": buffer id");
            }

    const auto fails = [] (const juce::File& file, const juce::String& expect, const juce::String& what, std::size_t budget = vox::drums::SampleLimits::maxTotalBytes) {
        const auto r = vox::drums::decodeSampleFile (file, budget);
        check (r.buffer == nullptr && r.error.contains (expect), what + ": '" + r.error + "'");
    };
    fails (scratch.getChildFile ("does-not-exist.wav"), "file not found", "missing file");
    {
        auto f = scratch.getChildFile ("empty.wav");
        f.replaceWithText ("");
        fails (f, "empty file", "empty file");
    }
    {
        auto f = scratch.getChildFile ("text.wav");
        f.replaceWithText ("this is not audio at all, just text pretending to be a wave file");
        fails (f, "not a supported audio file", "foreign file");
    }
    {
        const auto good = writeWav ("full.wav", 48000.0, 2, 16, 0.5);
        juce::MemoryBlock bytes;
        good.loadFileAsData (bytes);
        auto f = scratch.getChildFile ("truncated.wav");
        f.replaceWithData (bytes.getData(), bytes.getSize() / 3);   // header claims more data
        const auto r = vox::drums::decodeSampleFile (f);
        // JUCE reads a truncated data chunk as shorter, or reports an error:
        // either way the result is a valid, finite buffer or a clear error.
        if (r.buffer != nullptr)
        {
            bool finite = true;
            for (const auto& ch : r.buffer->data)
                for (auto x : ch)
                    finite = finite && std::isfinite (x);
            check (finite && r.buffer->frames < 24000, "truncated file decodes to its readable part");
        }
        else
            check (r.error.isNotEmpty(), "truncated file reports an error");
    }
    fails (writeWav ("long.wav", 8000.0, 1, 16, 61.0), "longer than 60 s", "too long");
    fails (writeWav ("lowrate.wav", 4000.0, 1, 16, 0.2), "unsupported sample rate", "4 kHz file");
    fails (writeWav ("budget.wav", 48000.0, 2, 16, 1.0), "memory budget", "over budget", 1000);
}

//==============================================================================
void testSound()
{
    const auto file = writeWav ("kick-sample.wav", 48000.0, 2, 24, 0.4);

    auto synthOnly = makeProcessor();
    const auto synth = render (*synthOnly, 40);

    // Sample source without a sample: exactly the synthesis.
    {
        auto p = makeProcessor();
        setPlain (*p, "drm.p01.source", 1.0f);
        check (render (*p, 40) == synth, "Sample source without a sample plays the synthesis exactly");
    }
    // Synth source ignores an assigned sample.
    {
        auto p = makeProcessor();
        p->samples().assign (0, decode (file), file);
        check (render (*p, 40) == synth, "Synth source ignores the sample");
    }
    // Sample and Layer.
    std::vector<float> sampled, layered;
    {
        auto p = makeProcessor();
        p->samples().assign (0, decode (file), file);
        setPlain (*p, "drm.p01.source", 1.0f);
        sampled = render (*p, 40);
        check (sampled != synth && energy (sampled) > 1.0, "Sample source plays the sample");
        // The sample's first frames reach the output (gain 1, no synthesis).
        double correlation = 0.0, a = 0.0, b = 0.0;
        for (int i = 0; i < 2000; ++i)
        {
            const double x = sampled[static_cast<std::size_t> (2 * i)];
            const double y = signal (0, i, 48000.0);
            correlation += x * y;
            a += x * x;
            b += y * y;
        }
        check (correlation / std::sqrt (a * b + 1e-30) > 0.95, "output follows the sample waveform");
    }
    {
        auto p = makeProcessor();
        p->samples().assign (0, decode (file), file);
        setPlain (*p, "drm.p01.source", 2.0f);
        layered = render (*p, 40);
        check (layered != synth && layered != sampled, "Layer plays synthesis and sample together");
    }
    // Level 0 and a region shorter than two frames silence the sample part.
    {
        auto p = makeProcessor();
        p->samples().assign (0, decode (file), file);
        setPlain (*p, "drm.p01.source", 1.0f);
        setPlain (*p, "drm.p01.sample_level", 0.0f);
        check (energy (render (*p, 40)) < 1.0e-12, "sample level 0 is silent");
    }
    {
        auto p = makeProcessor();
        p->samples().assign (0, decode (file), file);
        setPlain (*p, "drm.p01.source", 1.0f);
        setPlain (*p, "drm.p01.sample_start", 0.5f);
        const auto late = render (*p, 40);
        check (late != sampled && energy (late) < energy (sampled), "start 50 % plays the quieter second half");
        setPlain (*p, "drm.p01.sample_reverse", 1.0f);
        check (render (*p, 40) != late, "reverse changes the playback");
    }
    // Every pad plays its own sample.
    {
        auto p = makeProcessor();
        const auto& notes = padNotes();
        for (int pad = 0; pad < vox::drums::padCount; ++pad)
        {
            p->samples().assign (vox::drums::slotOf (pad, 0), decode (file), file);
            setPlain (*p, juce::String (vox::drums::padPrefix (pad)) + ".source", 1.0f);
        }
        for (int pad = 0; pad < vox::drums::padCount; ++pad)
            check (energy (render (*p, 20, notes[static_cast<std::size_t> (pad)])) > 0.1,
                   "pad " + juce::String (pad + 1) + " plays its sample");
    }
}

//==============================================================================
void testOwnership()
{
    const auto a = writeWav ("own-a.wav", 48000.0, 1, 16, 2.0);
    const auto b = writeWav ("own-b.wav", 48000.0, 1, 16, 2.0);

    auto p = makeProcessor();
    auto& library = p->samples();
    setPlain (*p, "drm.p01.source", 1.0f);
    setPlain (*p, "drm.p01.sample_loop", 1.0f);
    setPlain (*p, "drm.p01.decay", 1.5f);

    // Before any block has run no voice can hold a buffer: a replaced one is
    // freed at once (review 6735498337). While a block runs, it is not.
    {
        library.assign (0, decode (a), a);
        std::weak_ptr<const vox::drums::SampleBuffer> first = library.pad (0).buffer;
        library.assign (0, decode (b), b);
        check (first.expired(), "before any block, a replaced buffer is freed at once");

        library.assign (0, decode (a), a);
        std::weak_ptr<const vox::drums::SampleBuffer> second = library.pad (0).buffer;
        library.acquire();                       // a block is running on the table with it
        library.assign (0, decode (b), b);
        library.collect();
        check (! second.expired(), "a replaced buffer is not freed while a block runs on its table");
        vox::drums::SampleUsage none;
        library.report (none);                   // the block ends, holding nothing
        library.collect();
        check (second.expired(), "the replaced buffer is freed after that block reports");
    }

    // A voice holds the buffer: it stays alive until the voice ends.
    {
        library.assign (0, decode (a), a);
        std::weak_ptr<const vox::drums::SampleBuffer> held = library.pad (0).buffer;
        render (*p, 4);                         // a looping voice starts on buffer A
        library.assign (0, decode (b), b);      // A leaves the table
        render (*p, 4, -1);                     // the voice keeps playing A
        library.collect();
        check (! held.expired(), "a buffer a voice still plays is not freed");
        // The voice ends (decay 1.5 s T60 to -80 dB): render 6 s.
        for (int i = 0; i < 1200 && ! held.expired(); ++i)
        {
            render (*p, 1, -1);
            library.collect();
        }
        check (held.expired(), "the buffer is freed once no voice holds it");
    }

    // Replace storm with voices on every pad; at the end nothing is left.
    {
        for (int slot = 0; slot < vox::drums::slotCount; ++slot)
            library.clear (slot);
        for (int i = 0; i < 1500 && library.retiredCount() > 0; ++i)
        {
            render (*p, 1, -1);
            library.collect();
        }
        const auto before = vox::drums::SampleBuffer::live.load();
        std::vector<std::shared_ptr<vox::drums::SampleBuffer>> pool;
        for (int i = 0; i < 4; ++i)
            pool.push_back (decode (i % 2 == 0 ? a : b));
        const auto& notes = padNotes();
        juce::AudioBuffer<float> buffer (2, 128);
        for (int round = 0; round < 400; ++round)
        {
            // Fresh buffers (each decode is a new id) so retirement is exercised.
            library.assign (round % vox::drums::slotCount, decode (round % 3 == 0 ? a : b), a);
            juce::MidiBuffer midi;
            for (int k = 0; k < 4; ++k)
                midi.addEvent (juce::MidiMessage::noteOn (1, notes[static_cast<std::size_t> ((round + k) % 8)],
                                                          static_cast<juce::uint8> (100)), k * 20);
            for (int pad = 0; pad < vox::drums::padCount; ++pad)
                setPlain (*p, juce::String (vox::drums::padPrefix (pad)) + ".source", 2.0f);
            p->processBlock (buffer, midi);
            if (round % 5 == 0)
                library.collect();
        }
        for (int slot = 0; slot < vox::drums::slotCount; ++slot)
            library.clear (slot);
        juce::MidiBuffer none;
        for (int i = 0; i < 1500; ++i)   // all voices end (~8 s at 128 frames)
        {
            buffer.clear();
            p->processBlock (buffer, none);
            library.collect();
        }
        pool.clear();
        check (library.retiredCount() == 0, "every retired table and buffer was freed ("
                                                + juce::String (static_cast<int> (library.retiredCount())) + " left)");
        check (vox::drums::SampleBuffer::live.load() == before - 0, "no sample buffer leaked ("
                                                + juce::String (static_cast<juce::int64> (vox::drums::SampleBuffer::live.load() - before)) + ")");
    }
}

//==============================================================================
void testRealtime()
{
#ifdef VOX_TEST_INTERPOSE_MALLOC
    const auto a = writeWav ("rt-a.wav", 44100.0, 2, 16, 0.5);
    auto p = makeProcessor (48000.0, 256);
    for (int pad = 0; pad < vox::drums::padCount; ++pad)
        setPlain (*p, juce::String (vox::drums::padPrefix (pad)) + ".source", 2.0f);
    const auto& notes = padNotes();
    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;
    midi.ensureSize (4096);
    unsigned long allocations = 0;
    for (int b = 0; b < 300; ++b)
    {
        if (b % 10 == 0)
            p->samples().assign (b % 8, decode (a), a);   // message-thread work between blocks
        if (b % 7 == 0)
            p->samples().collect();
        midi.clear();
        midi.addEvent (juce::MidiMessage::noteOn (1, notes[static_cast<std::size_t> (b % 8)], static_cast<juce::uint8> (100)), 0);
        voxProbeBegin();
        p->processBlock (buffer, midi);
        allocations += voxProbeEnd();
    }
    if (voxProbeAvailable())
        check (allocations == 0, "processBlock with samples allocates nothing (" + juce::String (static_cast<juce::int64> (allocations)) + ")");
#endif
}

//==============================================================================
juce::MemoryBlock stateOf (DrumsProcessor& p)
{
    juce::MemoryBlock block;
    p.getStateInformation (block);
    return block;
}

std::vector<std::byte> sampleSection (const juce::MemoryBlock& blob, const DrumsProcessor& p)
{
    const auto decoded = vox::preset::decode (std::span (static_cast<const std::byte*> (blob.getData()), blob.getSize()),
                                              p.product().stateTag, 1);
    const auto* s = decoded.ok() ? decoded.document.find (vox::drums::sampleSectionTag) : nullptr;
    return s != nullptr ? s->data : std::vector<std::byte> {};
}

void testState()
{
    const auto kick = writeWav ("state-kick.wav", 48000.0, 2, 24, 0.3);
    const auto snare = writeWav ("state-snare.wav", 44100.0, 1, 16, 0.2);

    auto source = makeProcessor();
    source->samples().assign (0, decode (kick), kick);
    source->samples().assign (1, decode (snare), snare);
    setPlain (*source, "drm.p01.source", 1.0f);
    const auto saved = stateOf (*source);

    // Round trip.
    {
        auto target = makeProcessor();
        target->setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
        check (target->lastStateError().isEmpty(), "state with samples accepted");
        const auto k = target->samples().pad (0), s = target->samples().pad (1);
        check (k.buffer != nullptr && k.buffer->sha256 == source->samples().pad (0).buffer->sha256, "kick sample restored by content");
        check (s.buffer != nullptr && s.buffer->data.size() == 1 && juce::exactlyEqual (s.buffer->sampleRate, 44100.0), "snare sample restored");
        check (! target->samples().pad (2).reference, "an unassigned pad stays empty");
        check (target->samples().diagnostics().isEmpty(), "no diagnostics");
        check (stateOf (*target) == saved, "restored state saves back byte-identically");
        auto ref = makeProcessor();
        ref->samples().assign (0, decode (kick), kick);
        setPlain (*ref, "drm.p01.source", 1.0f);
        check (close (render (*target, 30), render (*ref, 30)), "the restored kit sounds like the original");
    }

    // Player B (slot 1 = pad 1 B) is its own sample.
    {
        auto target = makeProcessor();
        target->setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
        check (target->samples().pad (vox::drums::slotOf (0, 1)).buffer != nullptr
                   && target->samples().pad (vox::drums::slotOf (0, 1)).buffer->sha256
                          == source->samples().pad (vox::drums::slotOf (0, 1)).buffer->sha256,
               "pad 1 player B restored as its own sample");
    }

    // A DRM-02 document (DSMP version 1: 8 pads, one sample each) maps pad n
    // onto player A of pad n.
    {
        const auto v2 = sampleSection (saved, *source);
        // Split the version 2 section into its 32 slot entries.
        std::vector<std::vector<std::byte>> entries;
        std::size_t at = 8;
        const auto u32At = [&] (std::size_t i) {
            std::uint32_t v = 0;
            for (int k = 0; k < 4; ++k)
                v |= static_cast<std::uint32_t> (std::to_integer<std::uint8_t> (v2[i + static_cast<std::size_t> (k)])) << (8 * k);
            return v;
        };
        for (int slot = 0; slot < vox::drums::slotCount; ++slot)
        {
            const auto begin = at;
            if (std::to_integer<int> (v2[at++]) == 1)
            {
                at += 32 + 8 + 4 + 8;
                for (int str = 0; str < 2; ++str)
                    at += 4 + u32At (at);
            }
            entries.emplace_back (v2.begin() + static_cast<std::ptrdiff_t> (begin), v2.begin() + static_cast<std::ptrdiff_t> (at));
        }
        check (at == v2.size(), "the version 2 sample section has 32 slot entries");
        std::vector<std::byte> v1 { std::byte { 1 }, std::byte {}, std::byte {}, std::byte {},
                                    std::byte { 8 }, std::byte {}, std::byte {}, std::byte {} };
        const auto empty = std::vector<std::byte> { std::byte { 0 } };
        for (int pad = 0; pad < 8; ++pad)
        {
            // v1 pad 3 holds the kick (slot 0's entry), pad 6 the snare (slot 1's).
            const auto& e = pad == 3 ? entries[0] : pad == 6 ? entries[1] : empty;
            v1.insert (v1.end(), e.begin(), e.end());
        }
        auto document = vox::preset::decode (std::span (static_cast<const std::byte*> (saved.getData()), saved.getSize()),
                                             source->product().stateTag, 1).document;
        for (auto& sec : document.sections)
            if (sec.tag == vox::drums::sampleSectionTag)
                sec.data = v1;
        const auto bytes = vox::preset::encode (document);
        auto target = makeProcessor();
        target->setStateInformation (bytes->data(), static_cast<int> (bytes->size()));
        check (target->lastStateError().isEmpty(), "a version 1 sample section is accepted");
        const auto& lib = target->samples();
        check (lib.pad (vox::drums::slotOf (3, 0)).buffer != nullptr
                   && lib.pad (vox::drums::slotOf (3, 0)).buffer->sha256 == source->samples().pad (0).buffer->sha256
                   && lib.pad (vox::drums::slotOf (6, 0)).buffer != nullptr
                   && lib.pad (vox::drums::slotOf (6, 0)).buffer->sha256 == source->samples().pad (1).buffer->sha256,
               "version 1 pads 4 and 7 land on player A of pads 4 and 7");
        int assigned = 0;
        for (int slot = 0; slot < vox::drums::slotCount; ++slot)
            assigned += lib.pad (slot).reference ? 1 : 0;
        check (assigned == 2, "nothing else is assigned from a version 1 section");
    }

    // Missing file: the assignment and the reference survive, with a
    // deterministic diagnostic; the pad plays its synthesis.
    {
        const auto moved = scratch.getChildFile ("state-kick.moved");
        moved.deleteFile();
        kick.moveFileTo (moved);
        auto target = makeProcessor();
        target->setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
        check (target->lastStateError().isEmpty(), "state with a missing sample is accepted");
        const auto info = target->samples().pad (0);
        check (info.reference.has_value() && info.buffer == nullptr && info.status == "not found", "missing sample keeps its assignment");
        const auto diag = target->samples().diagnostics();
        check (diag.contains ("Pad 1 A") && diag.contains ("state-kick.wav") && diag.contains ("not found"),
               "missing sample diagnostic names pad and file: " + diag);
        check (sampleSection (stateOf (*target), *target) == sampleSection (saved, *source), "the missing reference saves back unchanged");
        auto synthOnly = makeProcessor();
        setPlain (*synthOnly, "drm.p01.source", 1.0f);
        check (close (render (*target, 30), render (*synthOnly, 30)), "a pad with a missing sample plays its synthesis");

        // A different file at the same path is not used.
        writeWav ("state-kick.wav", 48000.0, 2, 24, 0.31);
        auto changed = makeProcessor();
        changed->setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
        check (changed->samples().pad (0).buffer == nullptr
                   && changed->samples().pad (0).status.contains ("content changed"),
               "a changed file is reported, not used: " + changed->samples().pad (0).status);
        kick.deleteFile();
        moved.moveFileTo (kick);
    }

    // Library copy: found by content hash after the original is gone.
    {
        auto p = makeProcessor();
        p->setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
        check (p->samples().copyIntoLibrary (0).wasOk(), "copy into the library");
        const auto withLibrary = stateOf (*p);
        const auto moved = scratch.getChildFile ("state-kick.away");
        moved.deleteFile();
        kick.moveFileTo (moved);
        auto target = makeProcessor();
        target->setStateInformation (withLibrary.getData(), static_cast<int> (withLibrary.getSize()));
        check (target->samples().pad (0).buffer != nullptr, "the library copy is used when the original is gone");
        // Also when the saved path still points at the original location.
        auto old = makeProcessor();
        old->setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
        check (old->samples().pad (0).buffer != nullptr, "an old path is resolved from the library by hash");
        moved.moveFileTo (kick);
    }

    // Malformed sample sections are rejected; the live state is kept.
    {
        auto target = makeProcessor();
        target->setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
        const auto live = stateOf (*target);
        const auto withSection = [&] (std::vector<std::byte> data) {
            auto document = vox::preset::decode (std::span (static_cast<const std::byte*> (saved.getData()), saved.getSize()),
                                                 target->product().stateTag, 1).document;
            for (auto& s : document.sections)
                if (s.tag == vox::drums::sampleSectionTag)
                    s.data = std::move (data);
            const auto bytes = vox::preset::encode (document);
            return juce::MemoryBlock (bytes->data(), bytes->size());
        };
        auto good = sampleSection (saved, *source);
        const auto rejected = [&] (std::vector<std::byte> data, const juce::String& what) {
            const auto blob = withSection (std::move (data));
            target->setStateInformation (blob.getData(), static_cast<int> (blob.getSize()));
            check (target->lastStateError().isNotEmpty(), what + ": rejected");
            check (stateOf (*target) == live, what + ": live state unchanged");
        };
        rejected ({}, "empty sample section");
        {
            auto d = good;
            d[0] = std::byte { 3 };
            rejected (d, "unknown sample section version");
        }
        {
            auto d = good;
            d[4] = std::byte { 8 };   // version 2 with 8 slots
            rejected (d, "version 2 with a version 1 slot count");
        }
        {
            auto d = good;
            d.resize (d.size() - 5);
            rejected (d, "truncated sample section");
        }
        {
            auto d = good;
            d.push_back (std::byte { 0 });
            rejected (d, "trailing bytes");
        }
        {
            auto d = good;
            d[8] = std::byte { 7 };   // pad 1 'assigned' flag
            rejected (d, "invalid assigned flag");
        }
    }
}

//==============================================================================
// Review 6734821926: a pending asynchronous load must never overwrite a later
// action. Completions are held (test dispatcher) until after Clear, a newer
// load, or a project restore; the later action wins.
void testStaleLoads()
{
    const auto a = writeWav ("async-a.wav", 48000.0, 1, 16, 0.3);
    const auto b = writeWav ("async-b.wav", 48000.0, 2, 16, 0.2);
    auto p = makeProcessor();
    auto& library = p->samples();
    std::vector<std::function<void()>> held;
    library.setCompletionDispatcherForTesting ([&] (std::function<void()> f) { held.push_back (std::move (f)); });
    std::vector<juce::String> results;
    const auto record = [&] (const juce::String& e) { results.push_back (e); };
    const auto shaOf = [] (const juce::File& f) { return decode (f)->sha256; };
    const auto runHeld = [&] (bool reverse) {
        library.waitForPendingDecodes();
        if (reverse)
            std::reverse (held.begin(), held.end());
        auto pending = std::move (held);
        held.clear();
        for (auto& f : pending)
            f();
    };

    // Normal load commits.
    library.loadAsync (0, a, record);
    runHeld (false);
    check (library.pad (0).buffer != nullptr && library.pad (0).buffer->sha256 == shaOf (a) && results.back().isEmpty(),
           "an asynchronous load commits when nothing intervened");

    // Clear after the load started: the pad stays empty.
    library.loadAsync (1, a, record);
    library.waitForPendingDecodes();   // the decode is finished, its completion is held
    library.clear (1);
    runHeld (false);
    check (! library.pad (1).reference && library.pad (1).buffer == nullptr, "Clear wins over a pending load");
    check (results.back().contains ("superseded"), "the stale load reports that it was superseded: " + results.back());

    // Two loads: the later request wins, whatever order the decodes finish in.
    for (const bool reverse : { false, true })
    {
        library.loadAsync (2, a, record);
        library.loadAsync (2, b, record);
        runHeld (reverse);
        check (library.pad (2).buffer != nullptr && library.pad (2).buffer->sha256 == shaOf (b),
               juce::String ("the newer load wins") + (reverse ? " (completions in reverse order)" : ""));
    }

    // A synchronous assign after the load started wins.
    library.loadAsync (3, a, record);
    library.assign (3, decode (b), b);
    runHeld (false);
    check (library.pad (3).buffer->sha256 == shaOf (b), "an assignment after the load started wins");

    // A project restore after the load started wins.
    juce::MemoryBlock saved;
    {
        auto source = makeProcessor();
        source->samples().assign (4, decode (b), b);
        source->getStateInformation (saved);
    }
    library.loadAsync (4, a, record);
    library.loadAsync (5, a, record);
    library.waitForPendingDecodes();
    p->setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    check (p->lastStateError().isEmpty(), "restore accepted");
    runHeld (false);
    check (library.pad (4).buffer != nullptr && library.pad (4).buffer->sha256 == shaOf (b), "a restore wins over a pending load (assigned pad)");
    check (! library.pad (5).reference, "a restore wins over a pending load (pad empty in the project)");

    // Superseded decodes are freed (nothing held by the library or voices).
    library.setCompletionDispatcherForTesting (nullptr);
    for (int slot = 0; slot < vox::drums::slotCount; ++slot)
        library.clear (slot);
    for (int i = 0; i < 10 && library.retiredCount() > 0; ++i)
    {
        render (*p, 1, -1);
        library.collect();
    }
    check (library.retiredCount() == 0, "superseded and cleared samples are released");
}

//==============================================================================
// Review 6735498337 P1: retired tables and buffers stay bounded when the
// host runs no audio callback (editor open, transport stopped), without ever
// freeing what a running block or a voice can still read.
void testNoCallbackLifetime()
{
    const auto a = writeWav ("idle-a.wav", 48000.0, 2, 16, 1.0);
    const auto b = writeWav ("idle-b.wav", 48000.0, 2, 16, 1.0);
    const auto c = writeWav ("idle-c.wav", 48000.0, 1, 16, 1.0);
    const auto files = std::array<juce::File, 3> { a, b, c };

    // 1. Prepared, no processBlock ever: 1000 replacements and clears.
    {
        const auto liveBefore = vox::drums::SampleBuffer::live.load();
        auto p = makeProcessor();
        auto& library = p->samples();
        std::size_t peakRetired = 0, peakBytes = 0;
        std::int64_t peakLive = 0;
        for (int i = 0; i < 1000; ++i)
        {
            const int pad = i % 3;
            if (i % 7 == 6)
                library.clear (pad);
            else
                library.assign (pad, decode (files[static_cast<std::size_t> (i % 3)]), files[static_cast<std::size_t> (i % 3)]);
            peakRetired = std::max (peakRetired, library.retiredCount());
            peakBytes = std::max (peakBytes, library.retiredBytes());
            peakLive = std::max (peakLive, vox::drums::SampleBuffer::live.load() - liveBefore);
        }
        check (peakRetired == 0, "no callback: nothing stays retired (peak " + juce::String (static_cast<int> (peakRetired)) + ")");
        check (peakBytes == 0, "no callback: no retired bytes held");
        check (peakLive <= 4, "no callback: at most the 3 assigned buffers + 1 decoding are alive (peak "
                                  + juce::String (static_cast<juce::int64> (peakLive)) + ")");
        p.reset();
        check (vox::drums::SampleBuffer::live.load() == liveBefore, "teardown without a callback frees every buffer");
    }

    // 2. A looping voice holds its buffer: idle reclamation keeps it until a
    //    later report no longer lists it.
    {
        auto p = makeProcessor();
        auto& library = p->samples();
        setPlain (*p, "drm.p01.source", 1.0f);
        setPlain (*p, "drm.p01.sample_loop", 1.0f);
        setPlain (*p, "drm.p01.decay", 1.0f);
        library.assign (0, decode (a), a);
        std::weak_ptr<const vox::drums::SampleBuffer> held = library.pad (0).buffer;
        render (*p, 4);                        // a looping voice plays A
        for (int i = 0; i < 50; ++i)           // the host stops calling; the user replaces 50 times
            library.assign (0, decode (i % 2 ? b : c), i % 2 ? b : c);
        library.collect();
        check (! held.expired(), "idle: the buffer a voice holds is kept");
        check (library.retiredCount() == 1, "idle: only the held buffer stays retired ("
                                                + juce::String (static_cast<int> (library.retiredCount())) + ")");
        for (int i = 0; i < 1000 && ! held.expired(); ++i)
            render (*p, 1, -1);                // the voice ends; reports stop listing A
        library.collect();
        check (held.expired() && library.retiredCount() == 0, "the held buffer is freed once no voice holds it");
    }

    // 3. A block in progress (acquire() without report()): nothing it can
    //    still read is freed, idle or not.
    {
        auto p = makeProcessor();
        auto& library = p->samples();
        library.assign (0, decode (a), a);
        std::weak_ptr<const vox::drums::SampleBuffer> current = library.pad (0).buffer;
        const auto* views = library.acquire();   // a block starts and takes the table
        library.assign (0, decode (b), b);       // A leaves the table
        library.collect();
        check (! current.expired() && views[0].channels != nullptr && juce::exactlyEqual (views[0].channels[0][0], current.lock()->data[0][0]),
               "a running block's table and buffer stay valid");
        vox::drums::SampleUsage usage;
        library.report (usage);                  // the block ends, holding nothing
        library.collect();
        check (current.expired() && library.retiredCount() == 0, "freed after the block ends");
    }

    // 4. An abandoned block (no usage) frees nothing until a complete report.
    {
        auto p = makeProcessor();
        auto& library = p->samples();
        library.assign (0, decode (a), a);
        render (*p, 1, -1);
        library.acquire();
        library.abandon();
        std::weak_ptr<const vox::drums::SampleBuffer> old = library.pad (0).buffer;
        library.assign (0, decode (b), b);
        check (! old.expired(), "an incomplete report frees nothing");
        render (*p, 1, -1);
        library.collect();
        check (old.expired(), "the next complete report frees it");
    }

    // 5. The budget counts retired buffers: while a voice holds a retired
    //    buffer, a load that fits only without it is refused.
    {
        auto p = makeProcessor();
        auto& library = p->samples();
        const auto size = decode (a)->bytes();
        library.setBudgetForTesting (size + size / 2);   // room for one buffer, not two
        setPlain (*p, "drm.p01.source", 1.0f);
        setPlain (*p, "drm.p01.sample_loop", 1.0f);
        setPlain (*p, "drm.p01.decay", 1.0f);
        library.assign (0, decode (a), a);
        render (*p, 4);                          // a looping voice holds A
        library.clear (0);                        // A is retired, still held
        check (library.retiredBytes() == size, "budget: the held buffer is counted as retired bytes");
        std::vector<juce::String> results;
        std::vector<std::function<void()>> held;
        library.setCompletionDispatcherForTesting ([&] (std::function<void()> f) { held.push_back (std::move (f)); });
        library.loadAsync (1, b, [&] (const juce::String& e) { results.push_back (e); });
        library.waitForPendingDecodes();
        for (auto& f : held)
            f();
        held.clear();
        check (results.size() == 1 && results[0].contains ("budget") && library.pad (1).buffer == nullptr,
               "budget: a load that needs the retired bytes is refused: " + (results.empty() ? juce::String() : results[0]));
        for (int i = 0; i < 1000 && library.retiredCount() > 0; ++i)
            render (*p, 1, -1);                  // the voice ends; A is reclaimed
        library.collect();
        library.loadAsync (1, b, [&] (const juce::String& e) { results.push_back (e); });
        library.waitForPendingDecodes();
        for (auto& f : held)
            f();
        check (results.size() == 2 && results[1].isEmpty() && library.pad (1).buffer != nullptr,
               "budget: after reclamation the load fits");
    }

    // 5b. Owner decision (#120): the 512 MiB budget covers both players of
    //     every pad; a load that does not fit is refused cleanly and the
    //     live assignment of its slot (buffer, reference, sound) is kept.
    {
        check (vox::drums::SampleLimits::maxTotalBytes == std::size_t (512) * 1024 * 1024, "the decoded budget is 512 MiB");
        auto p = makeProcessor();
        auto& library = p->samples();
        check (library.budget() == vox::drums::SampleLimits::maxTotalBytes, "a new instrument uses the 512 MiB budget");
        const auto size = decode (a)->bytes();
        library.setBudgetForTesting (size + size / 2);   // room for one buffer, not two
        library.assign (vox::drums::slotOf (0, 0), decode (a), a);
        setPlain (*p, "drm.p01.source", 1.0f);
        const auto liveBuffer = library.pad (vox::drums::slotOf (0, 0)).buffer;
        const auto liveRef = library.pad (vox::drums::slotOf (0, 0)).reference;
        p->reset();   // panic: each render starts from silence
        const auto before = render (*p, 20);
        std::vector<juce::String> results;
        std::vector<std::function<void()>> held;
        library.setCompletionDispatcherForTesting ([&] (std::function<void()> f) { held.push_back (std::move (f)); });
        const auto run = [&] {
            library.waitForPendingDecodes();
            for (auto& f : held)
                f();
            held.clear();
        };
        // Player B of the same pad counts against the same budget.
        library.loadAsync (vox::drums::slotOf (0, 1), b, [&] (const juce::String& e) { results.push_back (e); });
        run();
        check (results.size() == 1 && results[0].contains ("budget") && ! library.pad (vox::drums::slotOf (0, 1)).reference,
               "budget: player B does not fit next to player A and is refused");
        // A replacement of the occupied slot that does not fit (even with
        // the slot's current buffer released) keeps it.
        const auto big = writeWav ("own-big.wav", 48000.0, 1, 16, 4.0);   // twice A
        library.loadAsync (vox::drums::slotOf (0, 0), big, [&] (const juce::String& e) { results.push_back (e); });
        run();
        const auto after = library.pad (vox::drums::slotOf (0, 0));
        check (results.size() == 2 && results[1].contains ("budget"), "budget: an over-budget replacement is refused: "
                                                                          + (results.size() < 2 ? juce::String() : results[1]));
        check (after.buffer == liveBuffer && after.reference.has_value() && liveRef.has_value()
                   && after.reference->sha256 == liveRef->sha256,
               "budget: the refused replacement keeps the live buffer and reference");
        p->reset();
        check (render (*p, 20) == before, "budget: the pad still sounds exactly as before");
        library.setCompletionDispatcherForTesting (nullptr);
    }

    // 6. Concurrent stress: an audio thread renders with looping voices on
    //    every pad while the message thread replaces, clears and collects
    //    (ASan / TSan in the PR evidence).
    {
        auto p = makeProcessor (48000.0, 128);
        auto& library = p->samples();
        for (int pad = 0; pad < vox::drums::padCount; ++pad)
        {
            setPlain (*p, juce::String (vox::drums::padPrefix (pad)) + ".source", 2.0f);
            setPlain (*p, juce::String (vox::drums::padPrefix (pad)) + ".sample_loop", pad % 2 == 0 ? 1.0f : 0.0f);
        }
        std::atomic<bool> stop { false };
        std::thread audio ([&] {
            juce::AudioBuffer<float> buffer (2, 128);
            const auto& notes = padNotes();
            for (int k = 0; ! stop.load(); ++k)
            {
                juce::MidiBuffer midi;
                if (k % 3 == 0)
                    midi.addEvent (juce::MidiMessage::noteOn (1, notes[static_cast<std::size_t> (k % 8)], static_cast<juce::uint8> (100)), 5);
                p->processBlock (buffer, midi);
                if (k % 50 == 49)
                    std::this_thread::sleep_for (std::chrono::milliseconds (2));   // gaps: idle collections happen
            }
        });
        std::vector<std::shared_ptr<vox::drums::SampleBuffer>> pool;
        for (const auto& f : files)
            pool.push_back (decode (f));
        for (int i = 0; i < 1500; ++i)
        {
            const int pad = i % vox::drums::slotCount;
            if (i % 11 == 10)
                library.clear (pad);
            else
                library.assign (pad, decode (files[static_cast<std::size_t> (i % 3)]), files[static_cast<std::size_t> (i % 3)]);
            if (i % 5 == 0)
                library.collect();
        }
        stop = true;
        audio.join();
        for (int slot = 0; slot < vox::drums::slotCount; ++slot)
            library.clear (slot);
        for (int i = 0; i < 2000 && library.retiredCount() > 0; ++i)
        {
            render (*p, 1, -1);
            library.collect();
        }
        check (library.retiredCount() == 0, "stress: everything retired is reclaimed exactly once");
    }
}


//==============================================================================
// Review 6750424931 (P1): the decoded budget is a hard cap. Peak decoded
// memory (SampleBuffer::peakLiveBytes over a baseline) never exceeds it:
// same-size replacement with a held voice, a full project restore while
// voices hold the old generation (deferred samples load once it is
// released), concurrent loads, and a restore that is rejected later.
void testHardBudget()
{
    // Distinct contents (lengths) so that no restore can reuse one for another.
    const auto x = writeWav ("cap-x.wav", 48000.0, 1, 16, 2.00);
    const auto y = writeWav ("cap-y.wav", 48000.0, 1, 16, 2.01);
    const auto u = writeWav ("cap-u.wav", 48000.0, 1, 16, 2.02);
    const auto v = writeWav ("cap-v.wav", 48000.0, 1, 16, 2.03);
    const auto size = decode (x)->bytes();              // the smallest
    const auto largest = decode (v)->bytes();
    const auto shaU = decode (u)->sha256, shaV = decode (v)->sha256;   // before any peak is measured
    check (decode (x)->sha256 != decode (y)->sha256 && shaU != shaV, "cap: the test samples differ");
    std::vector<std::function<void()>> held;
    const auto runHeld = [&] (vox::drums::SampleLibrary& library) {
        library.waitForPendingDecodes();
        auto pending = std::move (held);
        held.clear();
        for (auto& f : pending)
            f();
    };
    const auto loopPad = [] (DrumsProcessor& p, int pad) {
        const auto prefix = juce::String (vox::drums::padPrefix (pad));
        setPlain (p, prefix + ".source", 1.0f);
        setPlain (p, prefix + ".sample_loop", 1.0f);
        setPlain (p, prefix + ".decay", 10.0f);
    };
    const auto overCap = [] (std::int64_t baseline, std::size_t cap) {
        return vox::drums::SampleBuffer::peakLiveBytes.load() - baseline > static_cast<std::int64_t> (cap);
    };

    // 1. Same-size replacement while a voice holds the current buffer.
    {
        auto p = makeProcessor();
        auto& library = p->samples();
        const auto cap = size + size / 2;
        library.setBudgetForTesting (cap);
        const auto baseline = vox::drums::SampleBuffer::liveBytes.load();
        vox::drums::SampleBuffer::resetPeakForTesting();
        library.setCompletionDispatcherForTesting ([&] (std::function<void()> f) { held.push_back (std::move (f)); });
        juce::String result;
        library.loadAsync (0, x, [&] (const juce::String& e) { result = e; });
        runHeld (library);
        loopPad (*p, 0);
        p->reset();
        const auto before = render (*p, 8);   // a looping voice now holds X
        auto live = library.pad (0).buffer;
        library.loadAsync (0, y, [&] (const juce::String& e) { result = e; });
        runHeld (library);
        check (result.contains ("budget") && library.pad (0).buffer == live, "cap: a same-size replacement that would hold two copies is refused");
        p->reset();
        check (render (*p, 8) == before, "cap: the refused replacement keeps the pad's sound");
        live.reset();   // the test's own reference would keep X alive
        check (! overCap (baseline, cap) && library.committedBytes() <= cap, "cap: peak decoded memory stays within the cap (replacement)");
        // Clear, then load: fits once X is reclaimed (no voice, idle).
        library.clear (0);
        for (int i = 0; i < 400 && library.retiredBytes() > 0; ++i)
        {
            p->reset();
            render (*p, 1, -1);
            library.collect();
        }
        library.loadAsync (0, y, [&] (const juce::String& e) { result = e; });
        runHeld (library);
        check (result.isEmpty() && library.pad (0).buffer != nullptr, "cap: after clear and reclamation the new sample loads");
        check (! overCap (baseline, cap), "cap: peak within the cap after the reload");
        library.setCompletionDispatcherForTesting (nullptr);
    }

    // 2. Project restore while voices hold the old generation.
    {
        juce::MemoryBlock projectXY, projectUV;
        {
            auto s = makeProcessor();
            s->samples().assign (vox::drums::slotOf (0, 0), decode (x), x);
            s->samples().assign (vox::drums::slotOf (1, 0), decode (y), y);
            projectXY = stateOf (*s);
            s->samples().assign (vox::drums::slotOf (0, 0), decode (u), u);
            s->samples().assign (vox::drums::slotOf (1, 0), decode (v), v);
            projectUV = stateOf (*s);
        }
        auto p = makeProcessor();
        auto& library = p->samples();
        const auto cap = 3 * largest;   // one generation and one more sample, not two generations
        library.setBudgetForTesting (cap);
        library.setCompletionDispatcherForTesting ([&] (std::function<void()> f) { held.push_back (std::move (f)); });
        const auto baseline = vox::drums::SampleBuffer::liveBytes.load();
        vox::drums::SampleBuffer::resetPeakForTesting();
        p->setStateInformation (projectXY.getData(), static_cast<int> (projectXY.getSize()));
        check (p->lastStateError().isEmpty() && library.pad (0).buffer != nullptr && library.pad (2).buffer != nullptr,
               "cap: project XY loads (2 samples)");
        loopPad (*p, 0);
        loopPad (*p, 1);
        render (*p, 4);                          // voices hold X
        render (*p, 4, 38);                      // and Y
        // Restore UV: one sample fits next to X and Y, the other waits.
        p->setStateInformation (projectUV.getData(), static_cast<int> (projectUV.getSize()));
        check (p->lastStateError().isEmpty(), "cap: restoring UV over a playing XY is accepted");
        const auto a = library.pad (0), b = library.pad (2);
        const int loaded = (a.buffer != nullptr ? 1 : 0) + (b.buffer != nullptr ? 1 : 0);
        const int deferred = (a.deferred ? 1 : 0) + (b.deferred ? 1 : 0);
        check (loaded == 1 && deferred == 1, "cap: one restored sample loads now, one waits for memory (" + juce::String (loaded) + " loaded, "
                                                 + juce::String (deferred) + " deferred)");
        check (library.diagnostics().contains ("waiting for memory") || library.diagnostics().contains ("waits"), "cap: the waiting sample is reported");
        check (stateOf (*p) == projectUV, "cap: the restored project saves back byte-identically while a sample waits");
        check (! overCap (baseline, cap) && library.committedBytes() <= cap, "cap: peak decoded memory stays within the cap (restore)");
        // The old voices end; the deferred sample then loads.
        for (int i = 0; i < 2000 && (library.pad (0).deferred || library.pad (2).deferred); ++i)
        {
            p->reset();                          // stop the voices holding X and Y
            render (*p, 1, -1);
            library.collect();
            runHeld (library);
        }
        check (library.pad (0).buffer != nullptr && library.pad (2).buffer != nullptr && ! library.pad (0).deferred
                   && ! library.pad (2).deferred && library.diagnostics().isEmpty(),
               "cap: once the old generation is released the deferred sample loads");
        check (library.pad (0).buffer->sha256 == shaU && library.pad (2).buffer->sha256 == shaV,
               "cap: the slots hold project UV's samples");
        check (! overCap (baseline, cap), "cap: peak within the cap through the deferred load");
        check (stateOf (*p) == projectUV, "cap: and the project still saves back byte-identically");

        // 3. Restoring the project already loaded reuses its buffers: no decode.
        const auto liveBuffers = vox::drums::SampleBuffer::live.load();
        const auto committed = library.committedBytes();
        p->setStateInformation (projectUV.getData(), static_cast<int> (projectUV.getSize()));
        check (vox::drums::SampleBuffer::live.load() == liveBuffers && library.committedBytes() == committed
                   && library.pad (0).buffer != nullptr && library.pad (2).buffer != nullptr,
               "cap: restoring the loaded project reuses its decoded samples");
        library.setCompletionDispatcherForTesting (nullptr);
    }

    // 4. Concurrent loads reserve before allocating: two that each fit alone
    //    but not together cannot both commit.
    {
        auto p = makeProcessor();
        auto& library = p->samples();
        const auto cap = size + size / 2;
        library.setBudgetForTesting (cap);
        library.setCompletionDispatcherForTesting ([&] (std::function<void()> f) { held.push_back (std::move (f)); });
        const auto baseline = vox::drums::SampleBuffer::liveBytes.load();
        vox::drums::SampleBuffer::resetPeakForTesting();
        std::vector<juce::String> results;
        for (int round = 0; round < 20; ++round)
        {
            library.clear (0);
            library.clear (2);
            library.collect();
            results.clear();
            library.loadAsync (0, x, [&] (const juce::String& e) { results.push_back (e); });
            library.loadAsync (2, y, [&] (const juce::String& e) { results.push_back (e); });
            runHeld (library);
            const int ok = (results.size() == 2 && results[0].isEmpty() ? 1 : 0) + (results.size() == 2 && results[1].isEmpty() ? 1 : 0);
            check (ok == 1, "cap: of two concurrent loads that only fit one at a time, exactly one commits (round " + juce::String (round) + ")");
        }
        check (! overCap (baseline, cap) && library.committedBytes() <= cap, "cap: peak within the cap with concurrent loads");
        library.setCompletionDispatcherForTesting (nullptr);
    }

    // 5. A restore rejected after its samples were read releases their
    //    reservation (nothing stays counted).
    {
        juce::MemoryBlock project;
        {
            auto s = makeProcessor();
            s->samples().assign (vox::drums::slotOf (0, 0), decode (x), x);
            project = stateOf (*s);
        }
        auto p = makeProcessor();
        auto& library = p->samples();
        // Same document with a malformed pad section (read after the samples).
        auto document = vox::preset::decode (std::span (static_cast<const std::byte*> (project.getData()), project.getSize()),
                                             p->product().stateTag, 1).document;
        for (auto& sec : document.sections)
            if (sec.tag == vox::drums::padSectionTag)
                sec.data.push_back (std::byte {});
        const auto bytes = vox::preset::encode (document);
        p->setStateInformation (bytes->data(), static_cast<int> (bytes->size()));
        check (p->lastStateError().isNotEmpty() && library.committedBytes() == 0 && ! library.pad (0).reference,
               "cap: a rejected restore releases its sample reservation and changes nothing");
    }
}

//==============================================================================
void testEditor()
{
    const auto file = writeWav ("editor-kick.wav", 48000.0, 2, 16, 0.3);
    auto p = makeProcessor();
    std::unique_ptr<juce::AudioProcessorEditor> editor (p->createEditorAndMakeActive());
    auto* frame = dynamic_cast<vox::suite::SuiteEditor*> (editor.get());
    auto* panel = frame != nullptr ? dynamic_cast<vox::drums::DrumsPanel*> (frame->content()) : nullptr;
    check (panel != nullptr, "Drums panel");
    if (panel == nullptr)
        return;
    check (panel->sampleStatus() == "No sample", "status without a sample: " + panel->sampleStatus());
    check (! panel->sampleButton ("clear")->isEnabled(), "Clear disabled without a sample");
    check (panel->isInterestedInFileDrag ({ file.getFullPathName() }), "accepts dropped WAV files");
    check (! panel->isInterestedInFileDrag ({ scratch.getChildFile ("notes.txt").getFullPathName() }), "ignores other files");

    p->samples().assign (0, decode (file), file);
    panel->selectPad (0);
    check (panel->sampleStatus().startsWith ("editor-kick.wav") && panel->sampleStatus().contains ("stereo"),
           "status shows the sample: " + panel->sampleStatus());
    check (panel->sampleButton ("clear")->isEnabled() && panel->sampleButton ("library")->isEnabled(), "Clear and Library enabled");
    panel->sampleButton ("clear")->onClick();
    check (! p->samples().pad (0).reference, "Clear removes the assignment");

    // Player B: the A/B buttons select the slot the panel loads, shows and
    // clears; the store edits of the B player leave A alone.
    {
        std::vector<std::function<void()>> held;
        p->samples().setCompletionDispatcherForTesting ([&] (std::function<void()> f) { held.push_back (std::move (f)); });
        panel->selectPad (9);
        panel->sampleButton ("B")->onClick();
        check (panel->selectedPlayer() == 1 && panel->sampleButton ("B")->getToggleState() && ! panel->sampleButton ("A")->getToggleState(),
               "B selects player B");
        panel->loadSample (9, 1, file);
        p->samples().waitForPendingDecodes();
        for (auto& f : held)
            f();
        held.clear();
        p->samples().setCompletionDispatcherForTesting (nullptr);
        check (p->samples().pad (vox::drums::slotOf (9, 1)).buffer != nullptr && ! p->samples().pad (vox::drums::slotOf (9, 0)).reference,
               "loading with B selected fills pad 10 player B only");
        check (panel->sampleStatus().startsWith ("editor-kick.wav"), "the status shows player B's sample: " + panel->sampleStatus());
        panel->structuralSlider ("start")->setValue (0.25, juce::sendNotificationSync);
        check (juce::exactlyEqual (p->kitStore().pad (9).players[1].start, 0.25f) && juce::exactlyEqual (p->kitStore().pad (9).players[0].start, 0.0f),
               "the start knob edits player B");
        panel->sampleButton ("A")->onClick();
        check (panel->sampleStatus() == "No sample" && ! panel->sampleButton ("clear")->isEnabled(), "A shows its own empty slot");
        check (juce::exactlyEqual (panel->structuralSlider ("start")->getValue(), 0.0), "the start knob shows player A");
        panel->sampleButton ("B")->onClick();
        panel->sampleButton ("clear")->onClick();
        check (! p->samples().pad (vox::drums::slotOf (9, 1)).reference, "Clear with B selected clears player B");
        panel->sampleButton ("A")->onClick();
    }

    juce::Image image (juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true);
    juce::Graphics g (image);
    p->samples().assign (0, decode (file), file);
    panel->selectPad (0);
    editor->paintEntireComponent (g, false);
    // VOX_SCREENSHOT_OUT=<dir>: the editor with a sample on the kick, for review.
    if (const char* out = std::getenv ("VOX_SCREENSHOT_OUT"))
    {
        setPlain (*p, "drm.p01.source", 2.0f);
        setPlain (*p, "drm.p01.sample_start", 0.05f);
        setPlain (*p, "drm.p01.sample_end", 0.8f);
        panel->selectPad (0);
        juce::Image shot (juce::Image::ARGB, editor->getWidth() * 2, editor->getHeight() * 2, true);
        juce::Graphics sg (shot);
        sg.addTransform (juce::AffineTransform::scale (2.0f));
        editor->paintEntireComponent (sg, false);
        auto target = juce::File (juce::String (out)).getChildFile ("vox-drums-editor-sample.png");
        target.deleteFile();
        juce::FileOutputStream stream (target);
        juce::PNGImageFormat().writeImageToStream (shot, stream);
    }
    p->editorBeingDeleted (editor.get());
}

} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juce;
    scratch = juce::File::getSpecialLocation (juce::File::tempDirectory)
                  .getChildFile ("vox-drums-sample-tests-" + juce::String (juce::Time::currentTimeMillis()));
    scratch.createDirectory();
    const auto library = scratch.getChildFile ("library");
#if JUCE_WINDOWS
    _putenv_s ("VOX_DRUMS_SAMPLE_LIBRARY", library.getFullPathName().toRawUTF8());
#else
    setenv ("VOX_DRUMS_SAMPLE_LIBRARY", library.getFullPathName().toRawUTF8(), 1);
#endif

    testDecode();
    testSound();
    testOwnership();
    testHardBudget();
    testRealtime();
    testState();
    testStaleLoads();
    testNoCallbackLifetime();
    testEditor();

    scratch.deleteRecursively();
    if (failures == 0)
        std::printf ("VOX Drums sample tests passed\n");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
