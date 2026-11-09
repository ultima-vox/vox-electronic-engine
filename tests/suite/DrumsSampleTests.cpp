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

#ifdef VOX_TEST_INTERPOSE_MALLOC
#include "AllocationProbe.h"
#endif

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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

void setPlain (DrumsProcessor& p, const juce::String& id, float plain)
{
    auto* param = dynamic_cast<juce::RangedAudioParameter*> (p.state().getParameter (id));
    if (param == nullptr)
    {
        std::printf ("no parameter %s\n", id.toRawUTF8());
        std::exit (EXIT_FAILURE);
    }
    param->setValueNotifyingHost (param->convertTo0to1 (plain));
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
        const auto& notes = vox::drums::DrumEngine::padNotes();
        for (int pad = 0; pad < vox::drums::padCount; ++pad)
        {
            p->samples().assign (pad, decode (file), file);
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

    // No report yet: nothing is freed.
    {
        library.assign (0, decode (a), a);
        std::weak_ptr<const vox::drums::SampleBuffer> first = library.pad (0).buffer;
        library.assign (0, decode (b), b);
        library.collect();
        check (! first.expired(), "a replaced buffer is not freed before the audio thread reports");
        render (*p, 1, -1);   // one block on the new table, no voice holds the old buffer
        library.collect();
        check (first.expired(), "the replaced buffer is freed after a report on the newer table");
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
        for (int pad = 0; pad < vox::drums::padCount; ++pad)
            library.clear (pad);
        for (int i = 0; i < 1500 && library.retiredCount() > 0; ++i)
        {
            render (*p, 1, -1);
            library.collect();
        }
        const auto before = vox::drums::SampleBuffer::live.load();
        std::vector<std::shared_ptr<vox::drums::SampleBuffer>> pool;
        for (int i = 0; i < 4; ++i)
            pool.push_back (decode (i % 2 == 0 ? a : b));
        const auto& notes = vox::drums::DrumEngine::padNotes();
        juce::AudioBuffer<float> buffer (2, 128);
        for (int round = 0; round < 400; ++round)
        {
            // Fresh buffers (each decode is a new id) so retirement is exercised.
            library.assign (round % vox::drums::padCount, decode (round % 3 == 0 ? a : b), a);
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
        for (int pad = 0; pad < vox::drums::padCount; ++pad)
            library.clear (pad);
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
    const auto& notes = vox::drums::DrumEngine::padNotes();
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
        check (diag.contains ("Pad 1 (Kick)") && diag.contains ("state-kick.wav") && diag.contains ("not found"),
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
            d[0] = std::byte { 2 };
            rejected (d, "unknown sample section version");
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
    check (panel->sampleButton ("clear")->isEnabled() && panel->sampleButton ("library")->isEnabled(), "Clear and To library enabled");
    panel->sampleButton ("clear")->onClick();
    check (! p->samples().pad (0).reference, "Clear removes the assignment");

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
    testRealtime();
    testState();
    testEditor();

    scratch.deleteRecursively();
    if (failures == 0)
        std::printf ("VOX Drums sample tests passed\n");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
