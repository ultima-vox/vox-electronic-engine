#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <cstdint>

namespace vstengine::kick {

// Plain (denormalized) parameter values. Mirrors the APVTS kick* parameters
// one to one; the shell copies them in once per audio block before rendering.
struct KickParams
{
    float pitchStart  { 12.0f };  // sweep amount above end pitch (semitones)
    float pitchEnd    { 0.0f };   // end pitch offset from Tune (semitones)
    float pitchDecay  { 0.03f };  // pitch sweep time (s)
    float pitchCurve  { 2.0f };   // sweep curvature (>1 = fast initial drop)
    float bodyDecay   { 0.16f };  // body amplitude decay time (s)
    float tail        { 0.30f };  // sub tail decay time (s)
    float click       { 0.5f };   // attack click amount (0..1)
    float clickTone   { 0.5f };   // click color: 0 = bright noise, 1 = tonal
    float drive       { 1.0f };   // saturation amount (1 = clean)
    float clip        { 1.0f };   // hard-clip ceiling (1 = effectively off)
    float transient   { 0.5f };   // attack softness (0 = instant, 1 = soft)
    float sub         { 0.4f };   // sub-octave layer amount (0..1)
    float tune        { 36.0f };  // fundamental as a MIDI note (C2 default)
    float phase       { 0.0f };   // oscillator start phase (degrees 0..360)
    float outputLevel { 1.0f };
};

// One-shot synthesized kick voice (issue #11 PHASE 5).
//
// Realtime contract (audio thread only):
//   - no allocation, no locks, no filesystem, no host/editor calls,
//   - all state preallocated in the object,
//   - deterministic: identical triggers from identical state produce
//     sample-identical output (fixed click-noise seed, explicit phase reset,
//     time derived from a sample counter so block splits cannot drift).
//
// Trigger model: a note-on on the kick MIDI channel starts (or re-attacks)
// the one-shot body. The incoming note number transposes the fundamental
// relative to C4 (60), so the piano roll can play the kick musically while
// the Tune parameter stays the production anchor. Note-offs do nothing (the
// voice decays by itself); an explicit release() fast-fades the voice so a
// host panic / all-notes-off can never leave a hanging tail. The voice also
// hard-stops after a parameter-bounded maximum length, so it can never run
// forever even with extreme decay settings.
class KickSynth final
{
public:
    static constexpr int maxTriggersPerBlock = 16;
    static constexpr float minClipCeiling = 0.1f;

    void prepare(double sampleRate) noexcept;
    void setParameters(const KickParams& p) noexcept;

    // Schedule a voice attack at sampleOffset within the block about to be
    // rendered. velocity <= 0 is ignored as ordinary note-off semantics.
    void trigger(float velocity, int noteNumber, int sampleOffset) noexcept;

    // Fast-fade the sounding voice starting at sampleOffset (panic path).
    void release(int sampleOffset) noexcept;

    // Adds this block's kick audio into the buffer (stereo-safe: the mono
    // voice is added equally to every available channel).
    void render(juce::AudioBuffer<float>& buffer, int numSamples) noexcept;

    // Full voice reset (prepareToPlay / release paths).
    void reset() noexcept;

    [[nodiscard]] bool isActive() const noexcept { return active; }

    // Total audible voice length for the current parameters, in samples.
    [[nodiscard]] int voiceLengthSamples() const noexcept;

private:
    struct Trigger
    {
        int sampleOffset { 0 };
        float velocity { 1.0f };
        int noteNumber { 60 };
    };

    void startVoice(float velocity, int noteNumber) noexcept;

    double sampleRate { 44100.0 };
    KickParams params {};

    // Voice state (written by startVoice/release, read by render).
    bool active { false };
    std::int64_t voiceSamples { 0 }; // samples since attack
    std::int64_t maxVoiceSamples { 0 };
    double mainPhase { 0.0 };        // radians
    double subPhase { 0.0 };         // radians (half rate)
    float velocityGain { 1.0f };
    float noteOffsetSemitones { 0.0f };
    double endMidi { 36.0 };

    // Release fade (preallocated; release() never allocates).
    int releaseRemaining { 0 };
    int releaseTotal { 0 };
    int pendingReleaseOffset { -1 };

    // Deterministic click noise (fixed seed; never reseeds from wall time).
    std::uint32_t clickNoiseState { 0x9E3779B9u };
    float clickLowpass { 0.0f };

    // Pending triggers for the next render() call.
    Trigger triggers[maxTriggersPerBlock] {};
    int triggerCount { 0 };
};

} // namespace vstengine::kick
