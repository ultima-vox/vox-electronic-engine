#include "PluginProcessor.h"
#include "bass/BassGenerator.h"
#include "bass/PsyBassVoice.h"
#include "instrument/HostParameterSchema.h"
#include "midi/GeneratedNoteScheduler.h"
#include "midi/MidiExport.h"
#include "modules/BuiltInProvider.h"
#include "sequence/PatternAdapter.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct HostPlayHead final : public juce::AudioPlayHead {
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm(bpm);
        info.setPpqPosition(ppq);
        info.setTimeInSamples(samples);
        info.setIsPlaying(playing);
        return info;
    }
    void advance(int numSamples, double sampleRate)
    {
        samples += numSamples;
        ppq += (static_cast<double>(numSamples) / sampleRate) * (bpm / 60.0);
    }
    void reset(double newBpm = 145.0, double newPpq = 0.0)
    {
        bpm = newBpm;
        ppq = newPpq;
        samples = 0;
        playing = true;
    }
    double bpm { 145.0 };
    double ppq { 0.0 };
    std::int64_t samples { 0 };
    bool playing { true };
};

struct AudioStats {
    double peak { 0.0 };
    double rms { 0.0 };
    std::size_t nonFiniteCount { 0 };
    std::size_t totalSamples { 0 };
    std::size_t activeSamples { 0 };
};

AudioStats computeStats(const juce::AudioBuffer<float>& buffer)
{
    AudioStats stats;
    stats.totalSamples = static_cast<std::size_t>(buffer.getNumSamples());
    double sumSquares = 0.0;
    const float threshold = 1.0e-5f;

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
        const float* read = buffer.getReadPointer(ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i) {
            const float sample = read[i];
            if (!std::isfinite(sample)) {
                ++stats.nonFiniteCount;
                continue;
            }
            const double absVal = std::abs(static_cast<double>(sample));
            if (absVal > stats.peak) stats.peak = absVal;
            sumSquares += absVal * absVal;
            if (absVal > threshold) ++stats.activeSamples;
        }
    }
    const std::size_t totalValues = stats.totalSamples * static_cast<std::size_t>(buffer.getNumChannels());
    if (totalValues > 0)
        stats.rms = std::sqrt(sumSquares / static_cast<double>(totalValues));
    return stats;
}

void writeWavFile(const juce::File& file, const juce::AudioBuffer<float>& buffer, double sampleRate)
{
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    if (stream) {
        juce::WavAudioFormat wavFormat;
        const auto options = juce::AudioFormatWriterOptions()
            .withSampleRate(sampleRate).withNumChannels(buffer.getNumChannels())
            .withBitsPerSample(32)
            .withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
        if (auto writer = wavFormat.createWriterFor(stream, options)) {
            if (!writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples()))
                throw std::runtime_error("WAV write failed: " + file.getFullPathName().toStdString());
            return;
        }
    }
    throw std::runtime_error("WAV writer failed: " + file.getFullPathName().toStdString());
}

void writeMidiFile(const juce::File& file, const juce::MidiBuffer& buffer, double sampleRate, double bpm)
{
    file.deleteFile();
    juce::MidiFile midiFile;
    midiFile.setTicksPerQuarterNote(960);
    juce::MidiMessageSequence seq;
    const double ticksPerSample = (960.0 * bpm) / (60.0 * sampleRate);
    for (const auto meta : buffer) {
        auto msg = meta.getMessage();
        msg.setTimeStamp(static_cast<double>(meta.samplePosition) * ticksPerSample);
        seq.addEvent(msg);
    }
    seq.addEvent(juce::MidiMessage::tempoMetaEvent(
        static_cast<int>(std::llround(60000000.0 / bpm))), 0.0);
    seq.updateMatchedPairs();
    midiFile.addTrack(seq);
    if (auto stream = file.createOutputStream())
        if (midiFile.writeTo(*stream)) return;
    throw std::runtime_error("MIDI write failed");
}

// A fresh Rack starts empty (owner Cubase acceptance). These diagnostics
// render Bass on CH1 next to Acid on CH2, loaded explicitly as a user would.
std::unique_ptr<VstEngineAudioProcessor> makeStarterProcessor()
{
    auto proc = std::make_unique<VstEngineAudioProcessor>();
    juce::String diagnostic;
    if (!proc->loadSlotInstrument(0, vstengine::modules::bassInstrumentId, diagnostic)
        || !proc->loadSlotInstrument(1, vstengine::modules::acidInstrumentId, diagnostic))
        throw std::runtime_error("Cannot load Bass/Acid: " + diagnostic.toStdString());
    return proc;
}

void setPlainParam(VstEngineAudioProcessor& proc, const std::string& id, float value)
{
    auto* param = proc.parameters().getParameter(id);
    if (param == nullptr) throw std::runtime_error("Missing parameter: " + id);
    param->setValueNotifyingHost(param->convertTo0to1(value));
}

void disableEffects(VstEngineAudioProcessor& proc)
{
    if (!proc.applyInternalEffectsPreset("clean"))
        throw std::runtime_error("Cannot select dry effects preset");
}
void generate(VstEngineAudioProcessor& proc, std::string_view profile)
{
    juce::String diagnostic;
    if (!proc.generateSelectedPattern(profile, diagnostic))
        throw std::runtime_error("Generation failed: " + diagnostic.toStdString());
}

struct ScenarioResult {
    std::string name;
    bool passed { false };
    std::string details;
    AudioStats stats;
    int noteOnCount { 0 };
    int noteOffCount { 0 };
    std::string wavPath;
    std::string midiPath;
};

std::string escapeJson(const std::string& str)
{
    std::ostringstream ss;
    for (char c : str) {
        if (c == '"') ss << "\\\"";
        else if (c == '\\') ss << "\\\\";
        else if (c == '\b') ss << "\\b";
        else if (c == '\f') ss << "\\f";
        else if (c == '\n') ss << "\\n";
        else if (c == '\r') ss << "\\r";
        else if (c == '\t') ss << "\\t";
        else ss << c;
    }
    return ss.str();
}

// -----------------------------------------------------------------------------
// Scenario 1: Host MIDI note lifecycle
// -----------------------------------------------------------------------------
ScenarioResult runHostMidiLifecycle(const std::string& outputDir)
{
    ScenarioResult res;
    res.name = "host_midi_lifecycle";
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;
    HostPlayHead playHead;
    playHead.reset(145.0, 0.0);

    auto proc = makeStarterProcessor();
    proc->setPlayHead(&playHead);
    proc->prepareToPlay(sampleRate, blockSize);
    setPlainParam(*proc, "midiMode", 1.0f); // Piano Roll / Host MIDI only
    disableEffects(*proc);

    constexpr int noteNumber = 36;
    constexpr int channel = 1;
    juce::AudioBuffer<float> fullRender(2, 48000); // 1 second
    fullRender.clear();
    juce::MidiBuffer recordedMidi;

    int sampleCursor = 0;
    const int blocks = fullRender.getNumSamples() / blockSize;

    bool passed = true;
    std::ostringstream details;

    for (int b = 0; b < blocks; ++b) {
        juce::AudioBuffer<float> blockBuffer(2, blockSize);
        blockBuffer.clear();
        juce::MidiBuffer blockMidi;

        if (b == 0) {
            blockMidi.addEvent(juce::MidiMessage::noteOn(channel, noteNumber, 0.9f), 0);
            recordedMidi.addEvent(juce::MidiMessage::noteOn(channel, noteNumber, 0.9f), sampleCursor);
            ++res.noteOnCount;
        } else if (b == 5) {
            blockMidi.addEvent(juce::MidiMessage::noteOff(channel, noteNumber, 0.0f), 0);
            recordedMidi.addEvent(juce::MidiMessage::noteOff(channel, noteNumber, 0.0f), sampleCursor);
            ++res.noteOffCount;
        } else if (b == 15) {
            blockMidi.addEvent(juce::MidiMessage::noteOn(channel, noteNumber, 0.9f), 0);
            recordedMidi.addEvent(juce::MidiMessage::noteOn(channel, noteNumber, 0.9f), sampleCursor);
            ++res.noteOnCount;
        } else if (b == 50) {
            blockMidi.addEvent(juce::MidiMessage::noteOff(channel, noteNumber, 0.0f), 0);
            recordedMidi.addEvent(juce::MidiMessage::noteOff(channel, noteNumber, 0.0f), sampleCursor);
            ++res.noteOffCount;
        } else if (b == 60) {
            blockMidi.addEvent(juce::MidiMessage::noteOn(channel, noteNumber, 0.8f), 0);
            recordedMidi.addEvent(juce::MidiMessage::noteOn(channel, noteNumber, 0.8f), sampleCursor);
            ++res.noteOnCount;
        } else if (b == 65) {
            blockMidi.addEvent(juce::MidiMessage::noteOff(channel, noteNumber, 0.0f), 0);
            recordedMidi.addEvent(juce::MidiMessage::noteOff(channel, noteNumber, 0.0f), sampleCursor);
            ++res.noteOffCount;
        } else if (b == 70) {
            blockMidi.addEvent(juce::MidiMessage::noteOn(channel, noteNumber, 0.85f), 0);
            recordedMidi.addEvent(juce::MidiMessage::noteOn(channel, noteNumber, 0.85f), sampleCursor);
            ++res.noteOnCount;
        } else if (b == 75) {
            blockMidi.addEvent(juce::MidiMessage::noteOff(channel, noteNumber, 0.0f), 0);
            recordedMidi.addEvent(juce::MidiMessage::noteOff(channel, noteNumber, 0.0f), sampleCursor);
            ++res.noteOffCount;
        }

        proc->processBlock(blockBuffer, blockMidi);
        for (int ch = 0; ch < 2; ++ch)
            fullRender.copyFrom(ch, sampleCursor, blockBuffer, ch, 0, blockSize);

        playHead.advance(blockSize, sampleRate);
        sampleCursor += blockSize;
    }

    res.stats = computeStats(fullRender);
    if (res.stats.nonFiniteCount > 0) {
        passed = false;
        details << "Non-finite samples detected: " << res.stats.nonFiniteCount << "; ";
    }
    if (res.stats.peak < 0.05) {
        passed = false;
        details << "Audio output too quiet (peak: " << res.stats.peak << "); ";
    }

    double endTailMax = 0.0;
    for (int ch = 0; ch < 2; ++ch) {
        const float* p = fullRender.getReadPointer(ch, 85 * blockSize);
        for (int i = 0; i < (blocks - 85) * blockSize; ++i) {
            double val = std::abs(static_cast<double>(p[i]));
            if (val > endTailMax) endTailMax = val;
        }
    }
    if (endTailMax > 1.0e-3) {
        passed = false;
        details << "Hanging tail detected at end of lifecycle: max " << endTailMax << "; ";
    }

    res.passed = passed;
    res.details = details.str();
    res.wavPath = outputDir + "/host_midi_lifecycle.wav";
    res.midiPath = outputDir + "/host_midi_lifecycle.mid";
    writeWavFile(juce::File::getCurrentWorkingDirectory().getChildFile(res.wavPath), fullRender, sampleRate);
    writeMidiFile(juce::File::getCurrentWorkingDirectory().getChildFile(res.midiPath), recordedMidi, sampleRate, 145.0);
    proc->setPlayHead(nullptr);
    return res;
}

// -----------------------------------------------------------------------------
// Scenario 2: Internal generator at 145 BPM / 4/4 / fixed root & seed
// -----------------------------------------------------------------------------
ScenarioResult runInternalGenerator(const std::string& outputDir)
{
    ScenarioResult res;
    res.name = "internal_generator_145bpm_c2";
    constexpr double sampleRate = 44100.0;
    constexpr int blockSize = 512;
    HostPlayHead playHead;
    playHead.reset(145.0, 0.0);

    auto proc = makeStarterProcessor();
    proc->setPlayHead(&playHead);
    proc->prepareToPlay(sampleRate, blockSize);
    setPlainParam(*proc, "midiMode", 2.0f); // Generator mode
    setPlainParam(*proc, "rootNote", 36.0f); // C2 (MIDI 36)
    setPlainParam(*proc, "rngSeed", 1234.0f);
    disableEffects(*proc);

    juce::String diag;
    generate(*proc, "psy-bass");

    const int totalSamples = static_cast<int>(sampleRate * (8.0 * 60.0 / 145.0));
    const int blocks = totalSamples / blockSize;
    juce::AudioBuffer<float> fullRender(2, blocks * blockSize);
    fullRender.clear();

    int sampleCursor = 0;
    for (int b = 0; b < blocks; ++b) {
        juce::AudioBuffer<float> blockBuffer(2, blockSize);
        blockBuffer.clear();
        juce::MidiBuffer blockMidi;
        proc->processBlock(blockBuffer, blockMidi);
        for (int ch = 0; ch < 2; ++ch)
            fullRender.copyFrom(ch, sampleCursor, blockBuffer, ch, 0, blockSize);
        playHead.advance(blockSize, sampleRate);
        sampleCursor += blockSize;
    }

    res.stats = computeStats(fullRender);
    bool passed = true;
    std::ostringstream details;
    if (res.stats.nonFiniteCount > 0) {
        passed = false;
        details << "Non-finite samples detected; ";
    }
    if (res.stats.peak < 0.05) {
        passed = false;
        details << "Generator produced silence or near-silence (peak: " << res.stats.peak << "); ";
    }

    res.passed = passed;
    res.details = details.str();
    res.wavPath = outputDir + "/generator_145bpm_psy_bass.wav";
    writeWavFile(juce::File::getCurrentWorkingDirectory().getChildFile(res.wavPath), fullRender, sampleRate);
    proc->setPlayHead(nullptr);
    return res;
}

// -----------------------------------------------------------------------------
// Scenario 3: Multiple profiles and seeds
// -----------------------------------------------------------------------------
void runProfilesAndSeeds(const std::string& outputDir, const std::string& filter, std::vector<ScenarioResult>& out)
{
    for (const auto& [profileId, profileName] : {
             std::pair { "psy-bass", "Psy Bass" },
             std::pair { "forest-bass", "Forest Bass" },
             std::pair { "dark-bass", "Dark Bass" } })
    {
        bool havePriorPattern = false;
        std::uint32_t priorPattern {};
        for (const uint32_t seed : { 1234u, 98765u }) {
            std::string name = std::string("profile_") + profileId + "_seed_" + std::to_string(seed);
            if (!filter.empty() && filter != name) continue;

            ScenarioResult res;
            res.name = name;
            constexpr double sampleRate = 48000.0;
            constexpr int blockSize = 512;
            HostPlayHead playHead;
            playHead.reset(145.0, 0.0);

            auto proc = makeStarterProcessor();
            proc->setPlayHead(&playHead);
            proc->prepareToPlay(sampleRate, blockSize);
            setPlainParam(*proc, "midiMode", 2.0f);
            setPlainParam(*proc, "rootNote", 36.0f);
            setPlainParam(*proc, "rngSeed", static_cast<float>(seed));
            disableEffects(*proc);

            juce::String diag;
            generate(*proc, profileId);

            const int blocks = 100;
            juce::AudioBuffer<float> fullRender(2, blocks * blockSize);
            fullRender.clear();

            int sampleCursor = 0;
            for (int b = 0; b < blocks; ++b) {
                juce::AudioBuffer<float> blockBuffer(2, blockSize);
                blockBuffer.clear();
                juce::MidiBuffer blockMidi;
                proc->processBlock(blockBuffer, blockMidi);
                for (int ch = 0; ch < 2; ++ch)
                    fullRender.copyFrom(ch, sampleCursor, blockBuffer, ch, 0, blockSize);
                playHead.advance(blockSize, sampleRate);
                sampleCursor += blockSize;
            }

            res.stats = computeStats(fullRender);
            res.passed = (res.stats.nonFiniteCount == 0 && res.stats.peak > 0.05);
            const auto fingerprint = vstengine::sequence::patternFingerprint(
                vstengine::sequence::toPattern(proc->sequence()));
            if (havePriorPattern && fingerprint == priorPattern) {
                res.passed = false;
                res.details = "Selected distinct seeds produced the same pattern; ";
            }
            res.details += "pattern_fingerprint=" + std::to_string(fingerprint);
            havePriorPattern = true;
            priorPattern = fingerprint;
            res.wavPath = outputDir + "/" + res.name + ".wav";
            writeWavFile(juce::File::getCurrentWorkingDirectory().getChildFile(res.wavPath), fullRender, sampleRate);
            proc->setPlayHead(nullptr);
            out.push_back(std::move(res));
        }
    }
}

// -----------------------------------------------------------------------------
// Scenario 4: Sample rate and block size variations
// -----------------------------------------------------------------------------
void runRatesAndBlocks(const std::string& outputDir, const std::string& filter, std::vector<ScenarioResult>& out)
{
    for (const double sr : { 44100.0, 48000.0 }) {
        for (const int bs : { 64, 512 }) {
            std::string name = "rate_" + std::to_string(static_cast<int>(sr)) + "_block_" + std::to_string(bs);
            if (!filter.empty() && filter != name) continue;

            ScenarioResult res;
            res.name = name;
            HostPlayHead playHead;
            playHead.reset(145.0, 0.0);

            auto proc = makeStarterProcessor();
            proc->setPlayHead(&playHead);
            proc->prepareToPlay(sr, bs);
            setPlainParam(*proc, "midiMode", 2.0f);
            setPlainParam(*proc, "rootNote", 36.0f);
            setPlainParam(*proc, "rngSeed", 1234.0f);
            disableEffects(*proc);

            juce::String diag;
            generate(*proc, "psy-bass");

            const int totalSamples = static_cast<int>(sr * 0.5);
            const int blocks = totalSamples / bs;
            juce::AudioBuffer<float> fullRender(2, blocks * bs);
            fullRender.clear();

            int sampleCursor = 0;
            for (int b = 0; b < blocks; ++b) {
                juce::AudioBuffer<float> blockBuffer(2, bs);
                blockBuffer.clear();
                juce::MidiBuffer blockMidi;
                proc->processBlock(blockBuffer, blockMidi);
                for (int ch = 0; ch < 2; ++ch)
                    fullRender.copyFrom(ch, sampleCursor, blockBuffer, ch, 0, bs);
                playHead.advance(bs, sr);
                sampleCursor += bs;
            }

            res.stats = computeStats(fullRender);
            res.passed = (res.stats.nonFiniteCount == 0 && res.stats.peak > 0.05);
            res.wavPath = outputDir + "/" + res.name + ".wav";
            writeWavFile(juce::File::getCurrentWorkingDirectory().getChildFile(res.wavPath), fullRender, sr);
            proc->setPlayHead(nullptr);
            out.push_back(std::move(res));
        }
    }
}

// -----------------------------------------------------------------------------
// Scenario 5: Transport Start / Stop and Panic tail decay
// -----------------------------------------------------------------------------
ScenarioResult runTransportStopPanic(const std::string& outputDir)
{
    ScenarioResult res;
    res.name = "transport_start_stop_panic";
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;
    HostPlayHead playHead;
    playHead.reset(145.0, 0.0);

    auto proc = makeStarterProcessor();
    proc->setPlayHead(&playHead);
    proc->prepareToPlay(sampleRate, blockSize);
    setPlainParam(*proc, "midiMode", 2.0f);
    disableEffects(*proc);

    juce::String diag;
    generate(*proc, "psy-bass");

    // Play for 30 blocks
    for (int b = 0; b < 30; ++b) {
        juce::AudioBuffer<float> buf(2, blockSize);
        buf.clear();
        juce::MidiBuffer midi;
        proc->processBlock(buf, midi);
        playHead.advance(blockSize, sampleRate);
    }

    // Stop transport
    playHead.playing = false;
    juce::AudioBuffer<float> tailBuffer(2, 48000); // 1 sec tail
    tailBuffer.clear();
    int cursor = 0;
    const int tailBlocks = tailBuffer.getNumSamples() / blockSize;
    for (int b = 0; b < tailBlocks; ++b) {
        juce::AudioBuffer<float> buf(2, blockSize);
        buf.clear();
        juce::MidiBuffer midi;
        proc->processBlock(buf, midi);
        for (int ch = 0; ch < 2; ++ch)
            tailBuffer.copyFrom(ch, cursor, buf, ch, 0, blockSize);
        cursor += blockSize;
    }

    // Test Panic explicitly
    proc->requestPanic();
    juce::AudioBuffer<float> panicBuf(2, blockSize);
    panicBuf.clear();
    juce::MidiBuffer panicMidi;
    proc->processBlock(panicBuf, panicMidi);

    AudioStats tailStats = computeStats(tailBuffer);
    double lateTailMax = 0.0;
    for (int ch = 0; ch < 2; ++ch) {
        const float* p = tailBuffer.getReadPointer(ch, 24 * blockSize);
        for (int i = 0; i < (tailBlocks - 24) * blockSize; ++i) {
            double val = std::abs(static_cast<double>(p[i]));
            if (val > lateTailMax) lateTailMax = val;
        }
    }

    res.stats = tailStats;
    res.passed = (lateTailMax < 1.0e-3 && computeStats(panicBuf).peak < 1.0e-4);
    if (!res.passed) {
        std::ostringstream ss;
        ss << "Late tail max was: " << lateTailMax;
        res.details = ss.str();
    }
    res.wavPath = outputDir + "/transport_stop_tail.wav";
    writeWavFile(juce::File::getCurrentWorkingDirectory().getChildFile(res.wavPath), tailBuffer, sampleRate);
    proc->setPlayHead(nullptr);
    return res;
}

// -----------------------------------------------------------------------------
// Scenario 6: Save -> Restore -> Rerender determinism
// -----------------------------------------------------------------------------
ScenarioResult runSaveRestoreRerender(const std::string& outputDir)
{
    ScenarioResult res;
    res.name = "save_restore_rerender";
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;
    juce::MemoryBlock stateData;
    std::uint32_t originalPattern {};
    bool restoredPatternMatches {};

    juce::AudioBuffer<float> render1(2, 50 * blockSize);
    render1.clear();
    {
        HostPlayHead playHead;
        playHead.reset(145.0, 0.0);
        auto proc1 = makeStarterProcessor();
        proc1->setPlayHead(&playHead);
        proc1->prepareToPlay(sampleRate, blockSize);
        setPlainParam(*proc1, "midiMode", 2.0f);
        setPlainParam(*proc1, "rootNote", 36.0f);
        setPlainParam(*proc1, "rngSeed", 4242.0f);
        disableEffects(*proc1);

        juce::String diag;
        generate(*proc1, "forest-bass");
        originalPattern = vstengine::sequence::patternFingerprint(
            vstengine::sequence::toPattern(proc1->sequence()));
        proc1->getStateInformation(stateData);

        int cursor = 0;
        for (int b = 0; b < 50; ++b) {
            juce::AudioBuffer<float> buf(2, blockSize);
            buf.clear();
            juce::MidiBuffer midi;
            proc1->processBlock(buf, midi);
            for (int ch = 0; ch < 2; ++ch)
                render1.copyFrom(ch, cursor, buf, ch, 0, blockSize);
            playHead.advance(blockSize, sampleRate);
            cursor += blockSize;
        }
        proc1->setPlayHead(nullptr);
    }

    juce::AudioBuffer<float> render2(2, 50 * blockSize);
    render2.clear();
    {
        HostPlayHead playHead;
        playHead.reset(145.0, 0.0);
        auto proc2 = makeStarterProcessor();
        proc2->setPlayHead(&playHead);
        proc2->prepareToPlay(sampleRate, blockSize);
        proc2->setStateInformation(stateData.getData(), static_cast<int>(stateData.getSize()));
        disableEffects(*proc2);
        restoredPatternMatches = originalPattern == vstengine::sequence::patternFingerprint(
            vstengine::sequence::toPattern(proc2->sequence()));

        int cursor = 0;
        for (int b = 0; b < 50; ++b) {
            juce::AudioBuffer<float> buf(2, blockSize);
            buf.clear();
            juce::MidiBuffer midi;
            proc2->processBlock(buf, midi);
            for (int ch = 0; ch < 2; ++ch)
                render2.copyFrom(ch, cursor, buf, ch, 0, blockSize);
            playHead.advance(blockSize, sampleRate);
            cursor += blockSize;
        }
        proc2->setPlayHead(nullptr);
    }

    double maxDiff = 0.0;
    for (int ch = 0; ch < 2; ++ch) {
        const float* p1 = render1.getReadPointer(ch);
        const float* p2 = render2.getReadPointer(ch);
        for (int i = 0; i < render1.getNumSamples(); ++i) {
            double diff = std::abs(static_cast<double>(p1[i] - p2[i]));
            if (diff > maxDiff) maxDiff = diff;
        }
    }

    const auto stats1 = computeStats(render1);
    const auto stats2 = computeStats(render2);
    res.stats = stats2;

    const double peakDiff = std::abs(stats1.peak - stats2.peak);
    const double rmsDiff = std::abs(stats1.rms - stats2.rms);

    res.passed = (restoredPatternMatches && stats1.nonFiniteCount == 0
        && stats2.nonFiniteCount == 0 && maxDiff < 0.05 && rmsDiff < 0.01 && peakDiff < 0.01 && stats2.peak > 0.05);
    if (!res.passed) {
        std::ostringstream ss;
        ss << "Max diff: " << maxDiff << ", RMS diff: " << rmsDiff << ", Peak diff: " << peakDiff;
        res.details = ss.str();
    }
    res.wavPath = outputDir + "/save_restore_render.wav";
    writeWavFile(juce::File::getCurrentWorkingDirectory().getChildFile(res.wavPath), render2, sampleRate);
    return res;
}

// -----------------------------------------------------------------------------
// Scenario 7: Channel isolation and audition
// -----------------------------------------------------------------------------
ScenarioResult runRoutingChannelsAudition(const std::string& outputDir)
{
    ScenarioResult res;
    res.name = "routing_channels_audition";
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;
    HostPlayHead playHead;
    playHead.reset(145.0, 0.0);

    auto proc = makeStarterProcessor();
    proc->setPlayHead(&playHead);
    proc->prepareToPlay(sampleRate, blockSize);
    setPlainParam(*proc, "midiMode", 1.0f); // Piano roll mode
    disableEffects(*proc);

    // Channel 3 (unassigned) -> silence
    juce::AudioBuffer<float> ch3Buffer(2, blockSize);
    ch3Buffer.clear();
    juce::MidiBuffer ch3Midi;
    ch3Midi.addEvent(juce::MidiMessage::noteOn(3, 36, 0.8f), 0);
    proc->processBlock(ch3Buffer, ch3Midi);
    const auto ch3Stats = computeStats(ch3Buffer);

    // Channel 1 (assigned) -> audible
    proc->instrumentRack().reset();
    juce::AudioBuffer<float> ch1Buffer(2, blockSize);
    ch1Buffer.clear();
    juce::MidiBuffer ch1Midi;
    ch1Midi.addEvent(juce::MidiMessage::noteOn(1, 36, 0.8f), 0);
    proc->processBlock(ch1Buffer, ch1Midi);
    const auto ch1Stats = computeStats(ch1Buffer);

    // Audition via virtual keyboard on selected slot 0
    proc->instrumentRack().reset();
    proc->selectSlot(0);
    proc->keyboardState().noteOn(1, 36, 0.8f);
    juce::AudioBuffer<float> auditionBuffer(2, blockSize);
    auditionBuffer.clear();
    juce::MidiBuffer auditionMidi;
    proc->processBlock(auditionBuffer, auditionMidi);
    proc->keyboardState().noteOff(1, 36, 0.0f);
    const auto auditionStats = computeStats(auditionBuffer);

    bool passed = (ch3Stats.peak < 1.0e-6 && ch1Stats.peak > 0.05 && auditionStats.peak > 0.05);
    res.passed = passed;
    res.stats = ch1Stats;
    if (!passed) {
        std::ostringstream ss;
        ss << "Ch3 peak: " << ch3Stats.peak << ", Ch1 peak: " << ch1Stats.peak
           << ", Audition peak: " << auditionStats.peak;
        res.details = ss.str();
    }
    proc->setPlayHead(nullptr);
    return res;
}

// -----------------------------------------------------------------------------
// Scenario 8: Generated MIDI file and re-import playback
// -----------------------------------------------------------------------------
ScenarioResult runMidiExportReimport(const std::string& outputDir)
{
    ScenarioResult res;
    res.name = "midi_export_reimport_playback";
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;

    auto proc = makeStarterProcessor();
    proc->prepareToPlay(sampleRate, blockSize);
    setPlainParam(*proc, "rootNote", 36.0f);
    setPlainParam(*proc, "rngSeed", 1234.0f);
    disableEffects(*proc);

    juce::String diag;
    generate(*proc, "psy-bass");

    setPlainParam(*proc, "slot01MidiIn", 3.0f);

    // Create generated MIDI file
    juce::File midiFile = proc->createPartMidiFile(0);
    bool fileExists = midiFile.existsAsFile();
    juce::int64 fileSize = midiFile.getSize();

    // Read back exported MIDI
    int trackCount = 0;
    int eventCount = 0;
    int noteOnCount = 0;
    int noteOffCount = 0;

    std::string exportCopyPath = outputDir + "/exported_generated_bass.mid";
    if (fileExists && fileSize > 0) {
        juce::FileInputStream stream(midiFile);
        juce::MidiFile parsedMidi;
        if (parsedMidi.readFrom(stream)) {
            trackCount = parsedMidi.getNumTracks();
            if (trackCount > 0) {
                const auto* track = parsedMidi.getTrack(0);
                eventCount = track->getNumEvents();
                for (int i = 0; i < eventCount; ++i) {
                    const auto& msg = track->getEventPointer(i)->message;
                    if (msg.isNoteOn()) ++noteOnCount;
                    if (msg.isNoteOff()) ++noteOffCount;
                }
            }
        }
        const auto copy = juce::File::getCurrentWorkingDirectory().getChildFile(exportCopyPath);
        copy.deleteFile();
        if (!midiFile.copyFileTo(copy)) throw std::runtime_error("Cannot copy exported MIDI artifact");
    }
    res.midiPath = exportCopyPath;
    res.noteOnCount = noteOnCount;
    res.noteOffCount = noteOffCount;

    // Reopen the exported SMF and feed its actual note/controller events
    // through host MIDI. This file has musical ticks, no tempo map; render
    // explicitly at the same 145 BPM used by the diagnostics transport.
    juce::FileInputStream stream(midiFile);
    juce::MidiFile parsed;
    const bool parsedOk = parsed.readFrom(stream) && parsed.getNumTracks() > 0
        && parsed.getTimeFormat() > 0;
    if (!parsedOk || noteOnCount == 0 || noteOnCount != noteOffCount) {
        res.details = "Missing/invalid/unbalanced exported MIDI";
        return res;
    }
    const auto* track = parsed.getTrack(0);
    const double samplesPerTick = sampleRate * 60.0 / (145.0 * parsed.getTimeFormat());
    const int totalSamples = static_cast<int>(std::ceil(
        track->getEndTime() * samplesPerTick)) + static_cast<int>(sampleRate * 0.5);
    const int blocks = (totalSamples + blockSize - 1) / blockSize;
    juce::AudioBuffer<float> renderPlayback(2, blocks * blockSize);
    renderPlayback.clear();
    setPlainParam(*proc, "midiMode", 1.0f);
    HostPlayHead playHead;
    proc->setPlayHead(&playHead);
    playHead.reset(145.0);
    int eventIndex = 0;
    int playedOns = 0, playedOffs = 0;
    bool correctChannel = true;
    for (int start = 0; start < renderPlayback.getNumSamples(); start += blockSize) {
        juce::MidiBuffer midi;
        while (eventIndex < track->getNumEvents()) {
            const auto& msg = track->getEventPointer(eventIndex)->message;
            const int sample = static_cast<int>(std::llround(msg.getTimeStamp() * samplesPerTick));
            if (sample >= start + blockSize) break;
            ++eventIndex;
            if (msg.isMetaEvent()) continue;
            midi.addEvent(msg, std::max(0, sample - start));
            if ((msg.isNoteOn() || msg.isNoteOff()) && msg.getChannel() != 3)
                correctChannel = false;
            playedOns += msg.isNoteOn() ? 1 : 0;
            playedOffs += msg.isNoteOff() ? 1 : 0;
        }
        juce::AudioBuffer<float> blockBuf(2, blockSize);
        blockBuf.clear();
        proc->processBlock(blockBuf, midi);
        for (int ch = 0; ch < 2; ++ch)
            renderPlayback.copyFrom(ch, start, blockBuf, ch, 0, blockSize);
        playHead.advance(blockSize, sampleRate);
    }
    proc->setPlayHead(nullptr);
    res.stats = computeStats(renderPlayback);
    res.passed = fileExists && fileSize > 0 && correctChannel && playedOns == noteOnCount
        && playedOffs == noteOffCount && res.stats.nonFiniteCount == 0 && res.stats.peak > 0.05;
    res.wavPath = outputDir + "/midi_reimport_playback.wav";
    writeWavFile(juce::File::getCurrentWorkingDirectory().getChildFile(res.wavPath),
                 renderPlayback, sampleRate);
    std::ostringstream details;
    details << "Replayed file events: NoteOn=" << playedOns << ", NoteOff=" << playedOffs;
    res.details = details.str();
    return res;
}

} // namespace

int main(int argc, char* argv[])
{
    std::cout << "Starting bass_render_diagnostics..." << std::endl;
    try {
        std::string outputDir;
        std::string targetScenario;

        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--output-dir" && i + 1 < argc) {
                outputDir = argv[++i];
            } else if (arg == "--scenario" && i + 1 < argc) {
                targetScenario = argv[++i];
            } else if (arg == "--help") {
                std::cout << "Usage: bass_render_diagnostics [--output-dir <path>] [--scenario <name>]\n";
                return 0;
            }
        }

        if (outputDir.empty()) {
            if (std::filesystem::exists("CMakeLists.txt")) {
                outputDir = "build/bass-diagnostics";
            } else if (std::filesystem::exists("../CMakeLists.txt")) {
                outputDir = "bass-diagnostics";
            } else {
                outputDir = "build/bass-diagnostics";
            }
        }

        std::filesystem::create_directories(outputDir);
        std::cout << "=== Vox Electronic Engine: Bass Render Diagnostics ===\n";
        std::cout << "Output directory: " << outputDir << "\n\n" << std::flush;

        std::vector<ScenarioResult> results;
        int totalFailures = 0;

        auto recordResult = [&](ScenarioResult res) {
            std::cout << "[" << (res.passed ? "PASS" : "FAIL") << "] " << res.name << "\n";
            std::cout << "       Peak: " << std::fixed << std::setprecision(4) << res.stats.peak
                      << ", RMS: " << res.stats.rms
                      << ", NoteOns: " << res.noteOnCount
                      << ", NoteOffs: " << res.noteOffCount << "\n";
            if (!res.details.empty())
                std::cout << "       Details: " << res.details << "\n";
            std::cout << std::flush;
            if (!res.passed) ++totalFailures;
            results.push_back(std::move(res));
        };

        auto shouldRun = [&](const std::string& name) {
            return targetScenario.empty() || targetScenario == name;
        };

        if (shouldRun("host_midi_lifecycle")) {
            recordResult(runHostMidiLifecycle(outputDir));
        }

        if (shouldRun("internal_generator_145bpm_c2")) {
            recordResult(runInternalGenerator(outputDir));
        }

        {
            std::vector<ScenarioResult> profileResults;
            runProfilesAndSeeds(outputDir, targetScenario, profileResults);
            for (auto& r : profileResults) recordResult(std::move(r));
        }

        {
            std::vector<ScenarioResult> rateResults;
            runRatesAndBlocks(outputDir, targetScenario, rateResults);
            for (auto& r : rateResults) recordResult(std::move(r));
        }

        if (shouldRun("transport_start_stop_panic")) {
            recordResult(runTransportStopPanic(outputDir));
        }

        if (shouldRun("save_restore_rerender")) {
            recordResult(runSaveRestoreRerender(outputDir));
        }

        if (shouldRun("routing_channels_audition")) {
            recordResult(runRoutingChannelsAudition(outputDir));
        }

        if (shouldRun("midi_export_reimport_playback")) {
            recordResult(runMidiExportReimport(outputDir));
        }

        if (results.empty()) throw std::runtime_error("Unknown or empty scenario selection");
        std::string manifestPath = outputDir + "/manifest.json";
        std::ofstream manifest(manifestPath);
        if (!manifest) throw std::runtime_error("Cannot open manifest output");
        if (manifest.is_open()) {
            manifest << "{\n";
            manifest << "  \"test_suite\": \"bass_render_diagnostics\",\n";
            manifest << "  \"total_scenarios\": " << results.size() << ",\n";
            manifest << "  \"total_failures\": " << totalFailures << ",\n";
            manifest << "  \"scenarios\": [\n";
            for (std::size_t i = 0; i < results.size(); ++i) {
                const auto& r = results[i];
                manifest << "    {\n";
                manifest << "      \"name\": \"" << escapeJson(r.name) << "\",\n";
                manifest << "      \"passed\": " << (r.passed ? "true" : "false") << ",\n";
                manifest << "      \"peak\": " << std::fixed << std::setprecision(6) << r.stats.peak << ",\n";
                manifest << "      \"rms\": " << r.stats.rms << ",\n";
                manifest << "      \"non_finite\": " << r.stats.nonFiniteCount << ",\n";
                manifest << "      \"note_ons\": " << r.noteOnCount << ",\n";
                manifest << "      \"note_offs\": " << r.noteOffCount << ",\n";
                manifest << "      \"details\": \"" << escapeJson(r.details) << "\",\n";
                manifest << "      \"wav\": \"" << escapeJson(r.wavPath) << "\",\n";
                manifest << "      \"midi\": \"" << escapeJson(r.midiPath) << "\"\n";
                manifest << "    }" << (i + 1 < results.size() ? "," : "") << "\n";
            }
            manifest << "  ]\n";
            manifest << "}\n";
        }

        manifest.flush();
        if (!manifest) throw std::runtime_error("Manifest write failed");
        std::cout << "\n=== Summary: " << (results.size() - totalFailures) << "/" << results.size()
                  << " passed. Manifest written to " << manifestPath << " ===\n" << std::flush;

        return totalFailures == 0 ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << "EXCEPTION in main: " << e.what() << std::endl;
        return 2;
    } catch (...) {
        std::cerr << "UNKNOWN EXCEPTION in main" << std::endl;
        return 3;
    }
}
