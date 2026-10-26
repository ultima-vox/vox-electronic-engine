#pragma once
#include <juce_audio_basics/juce_audio_basics.h>

namespace vstengine::bass {

class PsyBassSound final : public juce::SynthesiserSound {
public:
    bool appliesToNote(int) override { return true; }
    bool appliesToChannel(int) override { return true; }
};

class PsyBassVoice final : public juce::SynthesiserVoice {
public:
    bool canPlaySound(juce::SynthesiserSound* sound) override;
    void startNote(int midiNoteNumber, float velocity,
                   juce::SynthesiserSound*, int) override;
    void stopNote(float velocity, bool allowTailOff) override;
    void pitchWheelMoved(int) override {}
    void controllerMoved(int, int) override {}
    void renderNextBlock(juce::AudioBuffer<float>&,
                         int startSample, int numSamples) override;

    // Lock-free monophonic module path. JUCE Synthesiser remains supported by
    // legacy tests, but active Rack calls these methods directly.
    void prepareDirect(double sampleRate) noexcept
    {
        setCurrentPlaybackSampleRate(sampleRate);
        outputGain.reset(sampleRate, .005);
        directActive = false;
    }
    void noteOnDirect(int note, float velocity) noexcept
    {
        directActive = true;
        startNote(note, velocity, nullptr, 0);
    }
    void noteOffDirect(bool allowTailOff = true) noexcept
    {
        stopNote(0.0f, allowTailOff);
        if (!allowTailOff) directActive = false;
    }
    void resetDirect() noexcept
    {
        noteOffDirect(false);
        phase = 0.0;
        resetFilterState();
    }

    // Drive / saturation
    void setDrive(float v) noexcept { drive = v; }

    // Amp envelope (per-parameter ADSR)
    void setAmpAttack(float v) noexcept { ampAttack = v; }
    void setAmpDecay(float v) noexcept { ampDecay = v; }
    void setAmpSustain(float v) noexcept { ampSustain = v; }
    void setAmpRelease(float v) noexcept { ampRelease = v; }

    // Pitch envelope
    void setPitchEnvelopeAmount(float v) noexcept { pitchEnvAmount = v; }
    void setPitchEnvelopeTime(float v) noexcept { pitchEnvTimeSeconds = v; }
    void setPitchEnvelopeCurve(float v) noexcept { pitchEnvCurve = v; }

    // Resonant low-pass filter
    void setFilterCutoff(float v) noexcept { filterCutoffTarget = v; }
    void setFilterResonance(float v) noexcept { filterResonance = v; }
    void setFilterDrive(float v) noexcept { filterDrive = v; }
    void setKeyTracking(float v) noexcept { keyTracking = v; }

    // Output level
    void setOutputLevel(float v) noexcept { outputLevel = v; }

    // Legato slide (portamento). The processor requests a glide for a specific
    // note before scheduling its note-on; the voice that actually receives
    // that note consumes the request in startNote(). Requests are cleared at
    // the top of every processBlock, so they can never fire twice.
    void requestGlide(int targetNoteNumber, float glideSeconds) noexcept
    {
        pendingGlideTargetNote = targetNoteNumber;
        pendingGlideSeconds = glideSeconds;
    }

    void clearPendingGlide() noexcept
    {
        pendingGlideTargetNote = -1;
        pendingGlideSeconds = 0.0f;
    }

    // Test/visibility accessor: true while the voice is performing a legato
    // glide between two notes. Deterministic glide requires the target note to
    // land on the same voice that holds the source pitch state.
    [[nodiscard]] bool isGliding() const noexcept
    {
        return glideRemainingSamples > 0;
    }

private:
    // Preallocated 2-pole low-pass state. Coefficients are recomputed in place
    // from the RBJ biquad equations (same response as a 2-pole IIR low-pass);
    // no coefficient objects are constructed and nothing is allocated, so
    // coefficient updates are realtime-safe at any rate.
    struct BiquadCoeffs {
        float b0 {}, b1 {}, b2 {}, a1 {}, a2 {};
    };

    void updateFilterCoefficients(double sampleRate, int midiNoteNumber) noexcept;
    void resetFilterState() noexcept
    {
        filterX1 = 0.0f;
        filterX2 = 0.0f;
        filterY1 = 0.0f;
        filterY2 = 0.0f;
    }

    // Oscillator
    double phase {};
    double baseFrequency {};
    float level {};
    int currentMidiNote { 60 };

    // Amp envelope
    float ampAttack { 0.001f };
    float ampDecay { 0.055f };
    float ampSustain { 0.72f };
    float ampRelease { 0.035f };
    juce::ADSR ampEnvelope;

    // Pitch envelope
    float pitchEnvAmount { 12.0f };
    float pitchEnvTimeSeconds { 0.018f };
    float pitchEnvCurve { 2.0f };
    int pitchEnvSamples {};
    int pitchEnvPosition {};

    // Filter (preallocated biquad: coefficients + per-sample state)
    float filterCutoffTarget { 0.5f };
    float filterCutoffSmooth { 0.5f };
    float lastCoefficientCutoff { -1.0f };
    float lastCoefficientResonance { -1.0f };
    int lastCoefficientNote { -1 };
    float lastCoefficientTracking { -1.0f };
    float filterResonance { 0.7f };
    float filterDrive { 1.0f };
    float keyTracking { 0.0f };
    BiquadCoeffs filterCoeffs;
    float filterX1 {}, filterX2 {}, filterY1 {}, filterY2 {};

    // Legato slide state
    int pendingGlideTargetNote { -1 };
    float pendingGlideSeconds { 0.0f };
    double glideCurrentFrequency {};
    int glideRemainingSamples {};
    int glideTotalSamples {};

    // Distortion + output
    float drive { 1.8f };
    float smoothedDrive { 1.8f };
    float outputLevel { 1.0f };
    juce::SmoothedValue<double> outputGain { 1.0 };
    bool directActive {};
};

} // namespace vstengine::bass
