#include "midi/GeneratedNoteScheduler.h"
#include "sequence/Sequence.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

int testsRun = 0;
int testsPassed = 0;

#define require(cond, msg)                                                   \
    do {                                                                     \
        ++testsRun;                                                          \
        if (!(cond)) {                                                       \
            std::cerr << "FAIL: " << msg << " at line " << __LINE__ << "\n"; \
            std::exit(EXIT_FAILURE);                                         \
        }                                                                    \
        ++testsPassed;                                                       \
    } while (0)

int countNoteOns(const juce::MidiBuffer& midi)
{
    int count = 0;
    for (const auto metadata : midi)
        if (metadata.getMessage().isNoteOn())
            ++count;
    return count;
}

int countNoteOffs(const juce::MidiBuffer& midi)
{
    int count = 0;
    for (const auto metadata : midi)
        if (metadata.getMessage().isNoteOff())
            ++count;
    return count;
}

} // namespace

int main()
{
    using vstengine::midi::GeneratedNoteScheduler;
    using vstengine::midi::TransportFrame;
    using vstengine::sequence::Sequence;

    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;

    // Helper: all-gated 16-step sequence, ratchet=1, probability=1.
    auto makeSequence = [] {
        Sequence seq(16);
        seq.setTimingMode(vstengine::sequence::TimingMode::sixteenth);
        for (int i = 0; i < 16; ++i) {
            seq[i].gate = true;
            seq[i].noteOffset = 0;
            seq[i].velocity = 0.9f;
            seq[i].accent = false;
            seq[i].probability = 1.0f;
            seq[i].ratchetCount = 1;
            seq[i].slideDuration = 0.0f;
            seq[i].gateWidth = 0.5f;
        }
        return seq;
    };

    // 1: PPQ path schedules one note per step boundary at 120 BPM 1/16 grid.
    {
        const auto seq = makeSequence();
        GeneratedNoteScheduler s;
        s.reset(sampleRate);
        s.reseedProbability(1234);

        // Step duration in quarters = 0.25; at 120 BPM.
        // blocksPerQuarter = sampleRate / (bpm/60) / blockSize.
        juce::MidiBuffer midi;
        TransportFrame frame;
        frame.playing = true;
        frame.havePpq = true;
        frame.ppqAtBlockStart = 0.0;
        frame.bpm = 120.0;
        s.process(midi, seq, frame, blockSize, sampleRate, 1, 36, 1234);

        require(countNoteOns(midi) == 1, "PPQ: one boundary inside first block");
        require(countNoteOffs(midi) == 0, "PPQ: gate 0.5 extends past block");
        require(s.playHeadStep() == 0, "PPQ: playhead starts at step 0");

        // Advance block-by-block across 64 blocks. Recompute the expected
        // boundary count from the same constants the scheduler uses, so the
        // assertion can't drift against the implementation.
        const double stepQuarters = vstengine::sequence::stepDurationInQuarterNotes(
            vstengine::sequence::TimingMode::sixteenth);
        const double stepBlocks = (60.0 / frame.bpm) * sampleRate * stepQuarters
                                 / static_cast<double>(blockSize);
        const int totalBlocks = 64;
        // Boundaries fall on stepQuarters multiples; one fires at ppq 0 plus
        // one per stepBlocks thereafter (capped by the block count).
        const int expectedNotes = 1 + static_cast<int>(
            static_cast<double>(totalBlocks - 1) / stepBlocks);

        int totalNotes = countNoteOns(midi);
        double ppq = static_cast<double>(blockSize) / sampleRate
                     * (frame.bpm / 60.0);
        for (int b = 1; b < totalBlocks; ++b) {
            juce::MidiBuffer block;
            frame.ppqAtBlockStart = ppq;
            s.process(block, seq, frame, blockSize, sampleRate, 1, 36, 1234);
            totalNotes += countNoteOns(block);
            ppq += static_cast<double>(blockSize) / sampleRate
                   * (frame.bpm / 60.0);
        }
        require(totalNotes == expectedNotes,
                "PPQ: boundary count over 64 blocks matches grid math");
    }

    // 2: Seek detection flushes and re-anchors (no stuck notes).
    {
        const auto seq = makeSequence();
        GeneratedNoteScheduler s;
        s.reset(sampleRate);
        s.reseedProbability(7);

        TransportFrame frame;
        frame.playing = true;
        frame.havePpq = true;
        frame.bpm = 120.0;
        frame.ppqAtBlockStart = 0.0;

        // Run enough blocks to actually cross the first step boundary (at
        // 120 BPM a 1/16 step spans ~11.7 blocks of 512 samples), so a
        // generated note is sounding when we seek.
        double ppq = 0.0;
        for (int b = 0; b < 16; ++b) {
            juce::MidiBuffer block;
            frame.ppqAtBlockStart = ppq;
            s.process(block, seq, frame, blockSize, sampleRate, 1, 36, 7);
            ppq += static_cast<double>(blockSize) / sampleRate * 2.0;
        }
        juce::MidiBuffer seekBlock;
        frame.ppqAtBlockStart = ppq + 8.0; // big jump = seek
        s.process(seekBlock, seq, frame, blockSize, sampleRate, 1, 36, 7);

        // After a seek the previous note must be flushed (note-off present)
        // and the playhead must re-anchor to the new musical position.
        require(countNoteOffs(seekBlock) >= 1,
                "seek: sounding generated note flushed with note-off");
        require(s.playHeadStep() >= 0, "seek: playhead re-anchored");
    }

    // 3: Stop flushes; every note-on is matched by a note-off (no hang).
    {
        auto gated = makeSequence();
        for (int i = 0; i < 16; ++i)
            gated[i].gateWidth = 0.05f; // short gate: note-offs inside blocks

        GeneratedNoteScheduler s;
        s.reset(sampleRate);
        s.reseedProbability(42);

        TransportFrame frame;
        frame.playing = true;
        frame.havePpq = true;
        frame.ppqAtBlockStart = 0.0;
        frame.bpm = 120.0;

        juce::MidiBuffer all;
        double ppq = 0.0;
        const double quarterPerBlock =
            static_cast<double>(blockSize) / sampleRate * 2.0;
        for (int b = 0; b < 32; ++b) {
            juce::MidiBuffer block;
            frame.ppqAtBlockStart = ppq;
            s.process(block, gated, frame, blockSize, sampleRate, 3, 36, 42);
            for (const auto metadata : block)
                all.addEvent(metadata.getMessage(), metadata.samplePosition);
            ppq += quarterPerBlock;
        }

        // Transport stop: flush must cut the sounding note.
        frame.playing = false;
        juce::MidiBuffer stopBlock;
        s.process(stopBlock, gated, frame, blockSize, sampleRate, 3, 36, 42);
        require(s.playHeadStep() == -1, "stop: playhead cleared");

        for (const auto metadata : all)
            stopBlock.addEvent(metadata.getMessage(), metadata.samplePosition);

        require(countNoteOns(stopBlock) > 0, "stop: session produced notes");
        require(countNoteOns(stopBlock) == countNoteOffs(stopBlock),
                "stop: every note-on matched by note-off (no stuck notes)");
    }

    // 4: Fallback path (no PPQ) keeps the documented free-running behavior.
    {
        const auto seq = makeSequence();
        GeneratedNoteScheduler s;
        s.reset(sampleRate);
        s.reseedProbability(99);

        TransportFrame frame;
        frame.playing = true;
        frame.havePpq = false;
        frame.bpm = 120.0;

        // First note fires at offset 0 of the first block
        // (samplesUntilNextStep == 0 after reset).
        juce::MidiBuffer midi;
        s.process(midi, seq, frame, blockSize, sampleRate, 1, 36, 99);
        require(countNoteOns(midi) == 1, "fallback: first note at start");
        require(s.playHeadStep() == 0, "fallback: playhead at step 0");
    }

    // 5: Determinism: same seed + same PPQ path -> identical MIDI stream.
    {
        const auto seq = makeSequence();

        // Collect (sample offset, raw message bytes) for each build.
        std::vector<std::pair<int, std::vector<std::uint8_t>>> runs;
        for (int variant = 0; variant < 2; ++variant) {
            GeneratedNoteScheduler s;
            s.reset(sampleRate);
            s.reseedProbability(31337);
            TransportFrame frame;
            frame.playing = true;
            frame.havePpq = true;
            frame.bpm = 140.0;
            double ppq = 0.0;
            const double quarterPerBlock =
                static_cast<double>(blockSize) / sampleRate * (140.0 / 60.0);
            for (int blk = 0; blk < 16; ++blk) {
                juce::MidiBuffer block;
                frame.ppqAtBlockStart = ppq;
                s.process(block, seq, frame, blockSize, sampleRate, 2, 36,
                          31337);
                for (const auto metadata : block) {
                    const auto& msg = metadata.getMessage();
                    runs.emplace_back(
                        metadata.samplePosition,
                        std::vector<std::uint8_t>(
                            msg.getRawData(),
                            msg.getRawData() + msg.getRawDataSize()));
                }
                ppq += quarterPerBlock;
            }
        }

        const auto half = static_cast<int>(runs.size()) / 2;
        require(static_cast<int>(runs.size()) == 2 * half,
                "determinism: even number of events across two runs");
        for (int i = 0; i < half; ++i) {
            require(runs[static_cast<std::size_t>(i)].first
                        == runs[static_cast<std::size_t>(i + half)].first,
                    "determinism: identical sample offsets");
            require(runs[static_cast<std::size_t>(i)].second
                        == runs[static_cast<std::size_t>(i + half)].second,
                    "determinism: identical message bytes");
        }
    }

    // 6: Regression test for Issue #45 - PPQ note-off scheduling at arbitrary block offsets.
    // In pre-fix implementation, a loop countdown starting at offset 0 caused notes triggered
    // mid-block to emit NoteOff before NoteOn or fail to emit NoteOff across block boundaries,
    // causing continuous stuck tones ("beep").
    {
        GeneratedNoteScheduler s;
        s.reset(sampleRate);
        s.reseedProbability(1234);

        // Sequence with 1 note at step 0, gateWidth = 0.5 (1/16 note = 5000 samples @ 144 BPM)
        Sequence seq(16);
        seq.setTimingMode(vstengine::sequence::TimingMode::sixteenth);
        seq[0].gate = true;
        seq[0].noteOffset = 0;
        seq[0].velocity = 0.8f;
        seq[0].probability = 1.0f;
        seq[0].ratchetCount = 1;
        seq[0].gateWidth = 0.5f; // Duration = 2500 samples
        for (int i = 1; i < 16; ++i) seq[i].gate = false;

        TransportFrame frame;
        frame.playing = true;
        frame.havePpq = true;
        frame.bpm = 144.0; // 1 beat = 20000 samples, 1/16 step = 5000 samples, duration = 2500 samples

        // Start playback slightly before step 0 boundary (e.g. at ppq = 3.99, boundary at 4.0)
        // so that step 0 boundary lands at sample 350 inside a 512-sample block.
        const double ppqPerSample = (frame.bpm / 60.0) / sampleRate;
        const double ppqAtBlockStart = 4.0 - (350.0 * ppqPerSample);
        frame.ppqAtBlockStart = ppqAtBlockStart;

        juce::MidiBuffer block0;
        s.process(block0, seq, frame, blockSize, sampleRate, 1, 36, 1234);

        // Block 0 must contain NoteOn at sample 350, and NO NoteOff before sample 350!
        int noteOnSample = -1;
        for (const auto meta : block0) {
            if (meta.getMessage().isNoteOn()) noteOnSample = meta.samplePosition;
            if (meta.getMessage().isNoteOff()) {
                require(meta.samplePosition > noteOnSample,
                        "Issue #45 regression: NoteOff must never precede NoteOn");
            }
        }
        require(noteOnSample == 350, "Issue #45 regression: NoteOn placed accurately at mid-block offset 350");

        // Advance 4 blocks (4 * 512 = 2048 samples). Note duration is 2500 samples.
        // Off offset = 350 + 2500 = 2850.
        // Block 0: 350..511 (162 samples)
        // Block 1..4: 4 * 512 = 2048 samples (total 2210 samples elapsed)
        // Block 5: offset = 2850 - 5 * 512 = 290.
        double currentPpq = ppqAtBlockStart;
        for (int b = 1; b <= 4; ++b) {
            currentPpq += static_cast<double>(blockSize) * ppqPerSample;
            frame.ppqAtBlockStart = currentPpq;
            juce::MidiBuffer blk;
            s.process(blk, seq, frame, blockSize, sampleRate, 1, 36, 1234);
            require(countNoteOns(blk) == 0, "No new note during sustain");
            require(countNoteOffs(blk) == 0, "No premature note-off during sustain");
        }

        // Block 5: NoteOff must arrive exactly at sample 290
        currentPpq += static_cast<double>(blockSize) * ppqPerSample;
        frame.ppqAtBlockStart = currentPpq;
        juce::MidiBuffer block5;
        s.process(block5, seq, frame, blockSize, sampleRate, 1, 36, 1234);

        int noteOffSample = -1;
        for (const auto meta : block5) {
            if (meta.getMessage().isNoteOff()) noteOffSample = meta.samplePosition;
        }
        require(noteOffSample == 290, "Issue #45 regression: NoteOff arrives at exact sample 290 across blocks");

        // Subsequent blocks must be completely silent (heldNote == -1, no stuck note)
        currentPpq += static_cast<double>(blockSize) * ppqPerSample;
        frame.ppqAtBlockStart = currentPpq;
        juce::MidiBuffer block6;
        s.process(block6, seq, frame, blockSize, sampleRate, 1, 36, 1234);
        require(countNoteOns(block6) == 0 && countNoteOffs(block6) == 0,
                "Issue #45 regression: No lingering note after note-off");
    }

    std::cout << "GeneratedNoteScheduler tests passed (" << testsPassed << "/"
              << testsRun << ")\n";
    return EXIT_SUCCESS;
}
