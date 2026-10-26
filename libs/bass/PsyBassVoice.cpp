#include "PsyBassVoice.h"
#include <cmath>

namespace vstengine::bass {

bool PsyBassVoice::canPlaySound(juce::SynthesiserSound* sound)
{
    return dynamic_cast<PsyBassSound*>(sound) != nullptr;
}

void PsyBassVoice::updateFilterCoefficients(const double sampleRate,
                                            const int midiNoteNumber) noexcept
{
    lastCoefficientCutoff = filterCutoffSmooth;
    lastCoefficientResonance = filterResonance;
    lastCoefficientNote = midiNoteNumber;
    lastCoefficientTracking = keyTracking;

    // Map normalized cutoff [0,1] to Hz. The 2-pole low-pass (12 dB/oct)
    // keeps the bass body while the resonance adds the characteristic "acid"
    // response.
    const auto normalized = static_cast<double>(
        juce::jlimit(0.0f, 1.0f, filterCutoffSmooth));
    const auto baseCutoffHz = 35.0 * std::pow(12000.0 / 35.0, normalized);

    // Key tracking: higher notes open the filter. Reference note A2 (MIDI 45).
    const float keyOffset = keyTracking * static_cast<float>(midiNoteNumber - 45);
    const auto cutoffHz = juce::jlimit(
        25.0, 18000.0, baseCutoffHz * std::pow(2.0, keyOffset / 12.0));

    // Resonance -> Q (0.7..8.0)
    const auto Q = static_cast<double>(juce::jmap(
        juce::jlimit(0.0f, 1.0f, filterResonance), 0.7f, 8.0f));

    // RBJ cookbook low-pass biquad, computed in place. Same transfer function
    // as a resonant 2-pole IIR low-pass, but there is no coefficient object,
    // no reference counting and no heap traffic, so this is safe to call from
    // the realtime audio thread.
    const auto w0 = juce::MathConstants<double>::twoPi * cutoffHz / sampleRate;
    const auto cosW0 = std::cos(w0);
    const auto alpha = std::sin(w0) / (2.0 * Q);
    const auto a0 = 1.0 + alpha;

    filterCoeffs.b0 = static_cast<float>(((1.0 - cosW0) / 2.0) / a0);
    filterCoeffs.b1 = static_cast<float>((1.0 - cosW0) / a0);
    filterCoeffs.b2 = filterCoeffs.b0;
    filterCoeffs.a1 = static_cast<float>((-2.0 * cosW0) / a0);
    filterCoeffs.a2 = static_cast<float>((1.0 - alpha) / a0);
}

void PsyBassVoice::startNote(const int midiNoteNumber, const float velocity,
                             juce::SynthesiserSound*, const int)
{
    const auto targetFrequency =
        juce::MidiMessage::getMidiNoteInHertz(midiNoteNumber);

    // Legato slide: when a glide was requested for exactly this note and the
    // voice is still sounding (adjacent generated notes), glide from the
    // current pitch to the new pitch instead of jumping.
    if (isVoiceActive() && pendingGlideTargetNote == midiNoteNumber
        && pendingGlideSeconds > 0.0f) {
        glideCurrentFrequency = (glideRemainingSamples > 0)
            ? glideCurrentFrequency
            : baseFrequency;
        glideTotalSamples = juce::jmax(1,
            static_cast<int>(pendingGlideSeconds * getSampleRate()));
        glideRemainingSamples = glideTotalSamples;
    } else {
        glideRemainingSamples = 0;
        glideTotalSamples = 0;
        glideCurrentFrequency = targetFrequency;
    }

    clearPendingGlide();

    currentMidiNote = midiNoteNumber;
    baseFrequency = targetFrequency;
    phase = 0.0;
    level = velocity;

    // Reset smoothed controls to avoid clicks at note start.
    filterCutoffSmooth = juce::jlimit(0.0f, 1.0f, filterCutoffTarget);
    smoothedDrive = drive;
    outputGain.reset(getSampleRate(), .005);
    outputGain.setCurrentAndTargetValue(juce::jmax(0.0f, outputLevel));

    updateFilterCoefficients(getSampleRate(), midiNoteNumber);
    resetFilterState();

    // Pitch envelope
    pitchEnvPosition = 0;
    pitchEnvSamples = juce::jmax(
        1, static_cast<int>(pitchEnvTimeSeconds * getSampleRate()));

    // Per-parameter ADSR
    juce::ADSR::Parameters p;
    p.attack = juce::jmax(0.0001f, ampAttack);
    p.decay = juce::jmax(0.001f, ampDecay);
    p.sustain = juce::jlimit(0.0f, 1.0f, ampSustain);
    p.release = juce::jmax(0.001f, ampRelease);
    // JUCE ADSR parameters are latched on NoteOn. Do not change parameters
    // during playback; reset before retrigger as required by its contract.
    ampEnvelope.reset();
    ampEnvelope.setSampleRate(getSampleRate());
    ampEnvelope.setParameters(p);
    ampEnvelope.noteOn();
}

void PsyBassVoice::stopNote(const float, const bool allowTailOff)
{
    if (allowTailOff) {
        ampEnvelope.noteOff();
    } else {
        ampEnvelope.reset();
        clearCurrentNote();
    }
}

void PsyBassVoice::renderNextBlock(juce::AudioBuffer<float>& output,
                                   const int startSample, const int numSamples)
{
    if (!directActive && !isVoiceActive())
        return;

    // Gain compensation: reduce the pre-distortion level as drive increases so
    // the tanh saturation does not blow the output up.
    const float driveGainComp = 1.0f / (0.5f + drive * 0.18f);
    // Pre-filter drive (boosts the signal into the resonant filter).
    const float fd = juce::jlimit(0.0f, 5.0f, filterDrive);
    outputGain.setTargetValue(juce::jmax(0.0f, outputLevel));

    // One-pole smoothing constants (per sample). These prevent zipper noise
    // when the host automates drive / cutoff.
    constexpr float driveSmoothing = 0.0008f;
    constexpr float cutoffSmoothing = 0.0005f;

    for (int sample = 0; sample < numSamples; ++sample) {
        // --- Parameter smoothing ---
        smoothedDrive += driveSmoothing * (drive - smoothedDrive);
        filterCutoffSmooth += cutoffSmoothing * (filterCutoffTarget - filterCutoffSmooth);

        // Recompute the preallocated biquad coefficients in place when the
        // smoothed cutoff drifts far enough so cutoff automation is audible
        // while keeping updates cheap. Pure math, no allocation.
        if (std::abs(filterCutoffSmooth - lastCoefficientCutoff) > 0.01f
            || std::abs(filterResonance - lastCoefficientResonance) > 0.01f
            || currentMidiNote != lastCoefficientNote
            || keyTracking != lastCoefficientTracking) {
            updateFilterCoefficients(getSampleRate(), currentMidiNote);
        }

        // --- Pitch envelope ---
        const auto t = juce::jlimit(
            0.0, 1.0,
            static_cast<double>(pitchEnvPosition)
                / static_cast<double>(pitchEnvSamples));
        const auto semitones = pitchEnvAmount * std::pow(1.0 - t, pitchEnvCurve);

        // --- Legato slide: glide base pitch from the previous note ---
        double currentFrequency;
        if (glideRemainingSamples > 0) {
            const auto glideT =
                static_cast<double>(glideTotalSamples - glideRemainingSamples)
                    / static_cast<double>(glideTotalSamples);
            currentFrequency = glideCurrentFrequency
                + (baseFrequency - glideCurrentFrequency) * glideT;
            --glideRemainingSamples;
            glideCurrentFrequency = currentFrequency;
        } else {
            currentFrequency = baseFrequency;
        }

        const auto frequency = currentFrequency * std::pow(2.0, semitones / 12.0);

        // Saw-derived, phase-shaped core: dense deterministic harmonics give
        // filter drive and saturation enough material for rolling psy bass.
        const auto cycle = static_cast<float>(phase
            / juce::MathConstants<double>::twoPi);
        const auto saw = 2.0f * cycle - 1.0f;
        const auto pulse = cycle < 0.42f ? 1.0f : -1.0f;
        const auto raw = 0.72f * saw + 0.28f * pulse;

        // --- Amp envelope ---
        const auto envLevel = ampEnvelope.getNextSample();

        // --- Saturation / drive with gain compensation ---
        const auto shaped = std::tanh(raw * envLevel * smoothedDrive * driveGainComp)
            * 0.55f;

        // --- Filter pre-drive ---
        const auto driven = shaped * fd;

        // --- Resonant low-pass (preallocated biquad, direct form I) ---
        const auto x = driven;
        const auto y = filterCoeffs.b0 * x
            + filterCoeffs.b1 * filterX1
            + filterCoeffs.b2 * filterX2
            - filterCoeffs.a1 * filterY1
            - filterCoeffs.a2 * filterY2;
        filterX2 = filterX1;
        filterX1 = x;
        filterY2 = filterY1;
        filterY1 = y;
        const auto filtered = y;

        // --- Output level ---
        const auto out = static_cast<float>(filtered * outputGain.getNextValue() * level);

        for (int channel = 0; channel < output.getNumChannels(); ++channel)
            output.addSample(channel, startSample + sample, out);

        // --- Advance phase ---
        const auto phaseDelta = juce::MathConstants<double>::twoPi
            * frequency / getSampleRate();
        phase += phaseDelta;
        while (phase >= juce::MathConstants<double>::twoPi)
            phase -= juce::MathConstants<double>::twoPi;

        ++pitchEnvPosition;

        if (!ampEnvelope.isActive()) {
            directActive = false;
            clearCurrentNote();
            break;
        }
    }
}

} // namespace vstengine::bass
