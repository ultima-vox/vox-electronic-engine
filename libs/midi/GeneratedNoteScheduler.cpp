#include "GeneratedNoteScheduler.h"
#include "transport/PpqSync.h"
#include <cmath>

namespace vstengine::midi {

void GeneratedNoteScheduler::reset(const double sampleRate) noexcept
{
    currentStep = 0;
    heldNote = -1;
    heldChannel = 1;
    samplesUntilNextStep = 0.0;
    samplesUntilNoteOff = -1.0;
    ratchetsRemaining = 0;
    samplesUntilNextRatchet = 0.0;
    subNoteDuration = 0.0;
    lastGeneratedNote = -1;
    playedStepCounter = 0;
    transportWasPlaying = false;
    continuityValid = false;
    lastBlockEndPpq = 0.0;
    playHeadStep_.store(-1, std::memory_order_relaxed);
    clearGlideRequests();
    juce::ignoreUnused(sampleRate);
}

void GeneratedNoteScheduler::reseedProbability(
    const std::uint32_t seed) noexcept
{
    // Deterministic probability RNG: seeded from the rngSeed parameter so the
    // same project state always reproduces the same probability decisions from
    // the same playback start point.
    probabilityState.state = seed;
}

void GeneratedNoteScheduler::flush(juce::MidiBuffer& midi,
                                   const int offset) noexcept
{
    // Cut any sounding generated note deterministically. Called on host stop,
    // source-mode switches, seek/loop realigns and any flush path so a
    // generated note can never hang past the position that scheduled it.
    if (heldNote >= 0)
        midi.addEvent(juce::MidiMessage::noteOff(heldChannel, heldNote), offset);

    heldNote = -1;
    heldChannel = 1;
    samplesUntilNoteOff = -1.0;
    samplesUntilNextStep = 0.0;
    ratchetsRemaining = 0;
    samplesUntilNextRatchet = 0.0;
    subNoteDuration = 0.0;
    lastGeneratedNote = -1;
    playHeadStep_.store(-1, std::memory_order_relaxed);
    continuityValid = false;
    clearGlideRequests();
}

void GeneratedNoteScheduler::process(
    juce::MidiBuffer& midi, const vstengine::sequence::Sequence& sequence,
    const TransportFrame& transport, const int numSamples,
    const double sampleRate, const int channel, const int rootNote,
    const std::uint32_t seed)
{
    // Stop always flushes generated note state so no generated note can hang.
    if (!transport.playing) {
        flush(midi, 0);
        transportWasPlaying = false;
        return;
    }

    // Host-PPQ-synchronized scheduling when the host provides PPQ (Cubase
    // does); otherwise the documented, tested BPM-derived free-running
    // fallback.
    if (transport.havePpq)
        processPpq(midi, sequence, numSamples, channel, rootNote,
                   transport.ppqAtBlockStart, transport.bpm, sampleRate, seed);
    else
        processFallback(midi, sequence, numSamples, channel, rootNote,
                        transport.bpm, sampleRate, seed);
}

void GeneratedNoteScheduler::processPpq(
    juce::MidiBuffer& midi, const vstengine::sequence::Sequence& sequence,
    const int numSamples, const int channel, const int rootNote,
    const double ppqAtBlockStart, const double bpm, const double sampleRate,
    const std::uint32_t seed)
{
    const double samplesPerQuarter =
        sampleRate * 60.0 / juce::jmax(20.0, bpm);
    const double stepsPerQuarter =
        vstengine::sequence::stepsPerQuarterNote(sequence.getTimingMode());
    const int seqSize = sequence.size();
    const double blockDurationQuarters =
        static_cast<double>(numSamples) / samplesPerQuarter;
    const double blockEndPpq = ppqAtBlockStart + blockDurationQuarters;

    // Transport (re)start: reseed the deterministic probability RNG exactly
    // like the free-running fallback so the same project state restarted from
    // the same point makes the same probability decisions.
    if (!transportWasPlaying) {
        transportWasPlaying = true;
        playedStepCounter = 0;
        probabilityState.state = seed;
    }

    // Seek / loop-wrap / first-playing-block detection. When the host PPQ
    // jumps instead of continuing from the previous block's end, hard-realign:
    // flush any sounding generated note safely and re-anchor to the reported
    // musical position. Probability rolls stay monotonic (no reseed) so a
    // given playback path remains deterministic.
    //
    // Tolerance is half a step of the current canonical grid: far below any
    // real seek/loop distance (>= one step) yet far above host rounding and
    // smoothly-automated tempo drift, so grid drift never hard-cuts notes.
    const double jumpTolerance = 0.5 / stepsPerQuarter;
    const bool jump = !continuityValid
        || std::abs(ppqAtBlockStart - lastBlockEndPpq) > jumpTolerance;

    if (jump) {
        flush(midi, 0);
        ratchetsRemaining = 0;
        samplesUntilNextRatchet = 0.0;
        subNoteDuration = 0.0;
        lastGeneratedNote = -1;
    }

    // The UI playhead mirrors the canonical step containing the block-start
    // musical position.
    const auto grid = vstengine::sync::resolveStepGrid(
        ppqAtBlockStart, seqSize, stepsPerQuarter);
    playHeadStep_.store(grid.stepIndex, std::memory_order_relaxed);

    // Check if a note carried over from a previous block expires in this block
    // before any new boundary onset.
    if (heldNote >= 0 && samplesUntilNoteOff >= 0.0) {
        if (samplesUntilNoteOff < static_cast<double>(numSamples)) {
            const int offOffset = juce::jlimit(
                0, numSamples - 1, static_cast<int>(samplesUntilNoteOff));
            midi.addEvent(
                juce::MidiMessage::noteOff(heldChannel, heldNote), offOffset);
            heldNote = -1;
            samplesUntilNoteOff = -1.0;
        } else {
            samplesUntilNoteOff -= static_cast<double>(numSamples);
        }
    }

    // Carried ratchet sub-notes across block boundary.
    while (ratchetsRemaining > 0
           && samplesUntilNextRatchet < static_cast<double>(numSamples)) {
        const int rOnset =
            juce::jmax(0, static_cast<int>(samplesUntilNextRatchet));
        if (heldNote >= 0) {
            midi.addEvent(
                juce::MidiMessage::noteOff(heldChannel, heldNote), rOnset);
            heldNote = -1;
            samplesUntilNoteOff = -1.0;
        }
        const auto& step = sequence[(currentStep + seqSize - 1) % seqSize];
        triggerNote(midi, rOnset, channel, rootNote, step,
                    samplesPerQuarter / stepsPerQuarter, subNoteDuration,
                    sampleRate);
        const double rOffOffset = samplesUntilNextRatchet + samplesUntilNoteOff;
        if (rOffOffset < static_cast<double>(numSamples)) {
            const int offOffset = juce::jlimit(
                rOnset, numSamples - 1, static_cast<int>(rOffOffset));
            midi.addEvent(
                juce::MidiMessage::noteOff(heldChannel, heldNote), offOffset);
            heldNote = -1;
            samplesUntilNoteOff = -1.0;
        } else {
            samplesUntilNoteOff =
                rOffOffset - static_cast<double>(numSamples);
        }
        --ratchetsRemaining;
        samplesUntilNextRatchet += subNoteDuration;
    }

    // Pending ratchets use an offset relative to the current block. Advance
    // even when no onset fits this block, or a distant ratchet never arrives.
    if (ratchetsRemaining > 0)
        samplesUntilNextRatchet -= static_cast<double>(numSamples);

    // Schedule every step boundary that starts inside this block. Onsets are
    // derived from absolute musical coordinates, so starting the transport
    // from an arbitrary locator, a seek, and a cycle-loop wrap all land on the
    // correct sequence step. The canonical stepsPerQuarterNote() grid is the
    // single source of truth, identical to MIDI export.
    const auto firstBoundary = vstengine::sync::nextStepBoundary(
        ppqAtBlockStart, stepsPerQuarter);

    for (auto boundary = firstBoundary;; ++boundary) {
        const double boundaryPpq =
            static_cast<double>(boundary) / stepsPerQuarter;
        if (boundaryPpq >= blockEndPpq)
            break;

        const double boundaryOffsetSamples =
            (boundaryPpq - ppqAtBlockStart) * samplesPerQuarter;
        const int boundaryOffset =
            juce::jmax(0, static_cast<int>(boundaryOffsetSamples));
        if (boundaryOffset >= numSamples)
            break;

        const auto wrapped = ((boundary % seqSize) + seqSize) % seqSize;
        const auto& step = sequence[static_cast<int>(wrapped)];

        // Advance the probability RNG exactly once per step boundary (same
        // rule as the free-running path and MIDI export), then evaluate.
        ++playedStepCounter;
        const bool passesProbability = vstengine::sequence::shouldPlayStep(
            probabilityState, step.probability);

        // Remember the last boundary's step so a carried ratchet tail can
        // re-read its step from the canonical sequence next block.
        currentStep = (static_cast<int>(wrapped) + 1) % seqSize;
        ratchetsRemaining = 0;
        samplesUntilNextRatchet = 0.0;

        if (!(passesProbability && step.gate))
            continue;

        const int ratchet = juce::jlimit(1, 8, step.ratchetCount);
        const double samplesPerStep = samplesPerQuarter / stepsPerQuarter;
        const double subNoteSamples =
            samplesPerStep / static_cast<double>(ratchet);
        subNoteDuration = subNoteSamples;

        for (int r = 0; r < ratchet; ++r) {
            const double onOffsetSamples =
                boundaryOffsetSamples
                + static_cast<double>(r) * subNoteSamples;

            if (onOffsetSamples < static_cast<double>(numSamples)) {
                const int onOffset =
                    juce::jmax(0, static_cast<int>(onOffsetSamples));
                if (heldNote >= 0) {
                    midi.addEvent(
                        juce::MidiMessage::noteOff(heldChannel, heldNote),
                        onOffset);
                    heldNote = -1;
                    samplesUntilNoteOff = -1.0;
                }
                triggerNote(
                    midi, onOffset,
                    channel, rootNote, step, samplesPerStep, subNoteSamples,
                    sampleRate);
                const double offOffsetSamples =
                    onOffsetSamples + samplesUntilNoteOff;
                if (offOffsetSamples < static_cast<double>(numSamples)) {
                    const int offOffset = juce::jlimit(
                        onOffset, numSamples - 1,
                        static_cast<int>(offOffsetSamples));
                    midi.addEvent(
                        juce::MidiMessage::noteOff(heldChannel, heldNote),
                        offOffset);
                    heldNote = -1;
                    samplesUntilNoteOff = -1.0;
                } else {
                    samplesUntilNoteOff =
                        offOffsetSamples - static_cast<double>(numSamples);
                }
            } else {
                // The tail of this step's ratchets lands in the next block:
                // continue it with the sample-domain countdown (same mechanics
                // as the free-running path).
                ratchetsRemaining = ratchet - r;
                samplesUntilNextRatchet =
                    onOffsetSamples - static_cast<double>(numSamples);
                break;
            }
        }
    }

    continuityValid = true;
    lastBlockEndPpq = blockEndPpq;
}

void GeneratedNoteScheduler::processFallback(
    juce::MidiBuffer& midi, const vstengine::sequence::Sequence& sequence,
    const int numSamples, const int channel, const int rootNote,
    const double bpm, const double sampleRate, const std::uint32_t seed)
{
    // Deterministic probability RNG: reseeded on every transport restart so
    // playback from the same project state reproduces the same decisions.
    if (!transportWasPlaying) {
        transportWasPlaying = true;
        playedStepCounter = 0;
        lastGeneratedNote = -1;
        probabilityState.state = seed;
    }

    const auto samplesPerQuarter = sampleRate * 60.0 / juce::jmax(20.0, bpm);

    // Canonical timing: step duration always derives from the sequence's
    // timing mode (same formula as MIDI export).
    const auto samplesPerStep =
        samplesPerQuarter
        / vstengine::sequence::stepsPerQuarterNote(sequence.getTimingMode());
    const auto seqSize = sequence.size();

    for (int offset = 0; offset < numSamples; ++offset) {
        if (samplesUntilNoteOff >= 0.0) {
            samplesUntilNoteOff -= 1.0;

            if (samplesUntilNoteOff <= 0.0 && heldNote >= 0) {
                midi.addEvent(
                    juce::MidiMessage::noteOff(heldChannel, heldNote), offset);
                heldNote = -1;
                samplesUntilNoteOff = -1.0;
            }
        }

        // Ratchet sub-notes: retrigger the step note evenly within the step.
        if (ratchetsRemaining > 0) {
            samplesUntilNextRatchet -= 1.0;

            if (samplesUntilNextRatchet <= 0.0) {
                const auto& step =
                    sequence[(currentStep + seqSize - 1) % seqSize];
                triggerNote(midi, offset, channel, rootNote, step,
                            samplesPerStep, subNoteDuration, sampleRate);
                --ratchetsRemaining;
                samplesUntilNextRatchet += subNoteDuration;
            }
        }

        samplesUntilNextStep -= 1.0;

        if (samplesUntilNextStep <= 0.0) {
            playHeadStep_.store(currentStep, std::memory_order_relaxed);
            const auto& step = sequence[static_cast<int>(currentStep)];

            // Advance RNG once per step regardless of gate state, then evaluate
            // probability. Uses the shared shouldPlayStep rule so realtime +
            // export produce identical pass/skip decisions for the same seed.
            ++playedStepCounter;
            const bool passesProbability = vstengine::sequence::shouldPlayStep(
                probabilityState, step.probability);

            if (passesProbability && step.gate) {
                const auto ratchet = juce::jlimit(1, 8, step.ratchetCount);
                subNoteDuration = samplesPerStep / static_cast<double>(ratchet);
                triggerNote(midi, offset, channel, rootNote, step,
                            samplesPerStep, subNoteDuration, sampleRate);
                ratchetsRemaining = ratchet - 1;
                samplesUntilNextRatchet = subNoteDuration;
            }

            currentStep = (currentStep + 1) % seqSize;
            samplesUntilNextStep += samplesPerStep;
        }
    }
}

void GeneratedNoteScheduler::triggerNote(
    juce::MidiBuffer& midi, const int offset, const int channel,
    const int rootNote, const vstengine::sequence::Step& step,
    const double samplesPerStep, const double subNoteSamples,
    const double sampleRate)
{
    heldChannel = channel;
    heldNote = juce::jlimit(0, 127, rootNote + step.noteOffset);
    const auto velocity =
        step.accent ? 0.95f : juce::jlimit(0.0f, 1.0f, step.velocity);

    // Slide semantics: slideDuration is measured in sequence steps. A note is
    // slide-eligible when it directly follows an audible generated note in the
    // playback stream (previous step's note or previous ratchet sub-note) with
    // a different pitch. The synth voice then glides from its current pitch to
    // this note's pitch over slideDuration * stepDuration.
    const bool sliding = step.slideDuration > 0.0f && lastGeneratedNote >= 0
        && lastGeneratedNote != heldNote;
    if (sliding) {
        const auto seconds = static_cast<float>(
            step.slideDuration * samplesPerStep / sampleRate);
        queueGlideRequest(heldNote, seconds);
        // Standard MIDI portamento sideband keeps frozen note-event ABI and
        // reaches any compliant module. CC5 maps 0..2 seconds quadratically.
        const auto normalized = std::sqrt(std::clamp(seconds / 2.0f, 0.0f, 1.0f));
        midi.addEvent(juce::MidiMessage::controllerEvent(
            heldChannel, 65, 127), offset);
        midi.addEvent(juce::MidiMessage::controllerEvent(
            heldChannel, 5, juce::jlimit(0, 127,
                static_cast<int>(std::lround(normalized * 127.0f)))), offset);
    } else {
        midi.addEvent(juce::MidiMessage::controllerEvent(
            heldChannel, 65, 0), offset);
    }

    midi.addEvent(
        juce::MidiMessage::noteOn(heldChannel, heldNote, velocity), offset);
    lastGeneratedNote = heldNote;

    // Sub-note gate never overlaps the next sub-note (gateWidth <= 1), so
    // generated notes can never get stuck.
    samplesUntilNoteOff = subNoteSamples
        * juce::jlimit(0.0, 1.0, static_cast<double>(step.gateWidth));
}

void GeneratedNoteScheduler::queueGlideRequest(
    const int noteNumber, const float glideSeconds) noexcept
{
    // Fixed-capacity queue: a real block can never come close to capacity
    // (see header comment). If an impossible block ever overflows, dropping
    // the extra glide keeps the schedule deterministic and allocation-free.
    if (glideRequestCount >= maxGlideRequests)
        return;
    glideRequests[static_cast<std::size_t>(glideRequestCount)] =
        GlideRequest { noteNumber, glideSeconds };
    ++glideRequestCount;
}

} // namespace vstengine::midi
