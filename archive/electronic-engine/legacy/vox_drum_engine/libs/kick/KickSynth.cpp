#include "kick/KickSynth.h"

#include <algorithm>

namespace vstengine::kick {
namespace {

constexpr double twoPi = juce::MathConstants<double>::twoPi;
// Click burst length. Short enough to read as an attack transient, long
// enough to survive low sample rates.
constexpr double clickTimeSeconds = 0.004;
// Release fade for explicit release() (panic / all-notes-off).
constexpr double releaseFadeSeconds = 0.002;
// Sub layer trim so sub = 1 with a long tail stays mixable, not dominant.
constexpr double subLevelTrim = 0.8;
// Fixed seed for the click noise generator: identical triggers always
// produce sample-identical output (no wall-time reseeding, ever).
constexpr std::uint32_t clickNoiseSeed = 0x9E3779B9u;

float nextClickNoise(std::uint32_t& state) noexcept
{
    // xorshift32: deterministic, allocation-free, cheap.
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return static_cast<float>(static_cast<double>(state) / 4294967296.0)
               * 2.0f
           - 1.0f;
}

} // namespace

void KickSynth::prepare(const double newSampleRate) noexcept
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    reset();
}

void KickSynth::setParameters(const KickParams& p) noexcept
{
    params = p;
}

void KickSynth::reset() noexcept
{
    active = false;
    voiceSamples = 0;
    maxVoiceSamples = 0;
    mainPhase = 0.0;
    subPhase = 0.0;
    velocityGain = 1.0f;
    noteOffsetSemitones = 0.0f;
    endMidi = 36.0;
    releaseRemaining = 0;
    releaseTotal = 0;
    pendingReleaseOffset = -1;
    clickNoiseState = clickNoiseSeed;
    clickLowpass = 0.0f;
    triggerCount = 0;
}

void KickSynth::trigger(const float velocity, const int noteNumber,
                        const int sampleOffset) noexcept
{
    if (triggerCount >= maxTriggersPerBlock)
        return; // capacity guard; real blocks stay far below this
    if (velocity <= 0.0f)
        return; // note-on with zero velocity is a note-off, not an attack

    auto& t = triggers[triggerCount++];
    t.sampleOffset = juce::jlimit(0, 1 << 20, sampleOffset);
    t.velocity = juce::jlimit(0.0f, 1.0f, velocity);
    t.noteNumber = juce::jlimit(0, 127, noteNumber);
}

void KickSynth::release(const int sampleOffset) noexcept
{
    // Earliest panic wins; a later release request never extends the fade.
    // Keep request even before render(): trigger and panic may share a block.
    const int safeOffset = juce::jlimit(0, 1 << 20, sampleOffset);
    pendingReleaseOffset = pendingReleaseOffset < 0
                               ? safeOffset
                               : std::min(pendingReleaseOffset, safeOffset);
}

int KickSynth::voiceLengthSamples() const noexcept
{
    const double body = std::max(static_cast<double>(params.bodyDecay), 0.01);
    const double tail = std::max(static_cast<double>(params.tail), 0.0);
    const double attack = 0.025 * static_cast<double>(params.transient);
    double seconds = (body + tail) * 8.0 + attack + clickTimeSeconds;
    seconds = juce::jlimit(0.02, 30.0, seconds);
    return static_cast<int>(seconds * sampleRate) + 1;
}

void KickSynth::startVoice(const float velocity,
                           const int noteNumber) noexcept
{
    active = true;
    voiceSamples = 0;
    maxVoiceSamples = voiceLengthSamples();
    velocityGain = juce::jlimit(0.0f, 1.0f, velocity);
    // The piano roll transposes relative to C4 (60); Tune stays the anchor.
    noteOffsetSemitones =
        static_cast<float>(juce::jlimit(-48, 48, noteNumber - 60));
    // Anchor frequency for this voice: Tune + Pitch End + note offset. The
    // pitch envelope then sweeps above this value by Pitch Start semitones.
    endMidi = static_cast<double>(params.tune)
              + static_cast<double>(params.pitchEnd)
              + static_cast<double>(noteOffsetSemitones);
    const double startPhaseDeg = juce::jlimit(0.0f, 360.0f, params.phase);
    mainPhase = startPhaseDeg * (twoPi / 360.0);
    subPhase = mainPhase;
    clickNoiseState = clickNoiseSeed;
    clickLowpass = 0.0f;
    releaseRemaining = 0;
    releaseTotal = 0;
}

void KickSynth::render(juce::AudioBuffer<float>& buffer,
                       const int numSamples) noexcept
{
    const int channels = buffer.getNumChannels();
    auto* left = channels > 0 ? buffer.getWritePointer(0) : nullptr;
    auto* right = channels > 1 ? buffer.getWritePointer(1) : nullptr;
    if (numSamples <= 0 || left == nullptr) {
        triggerCount = 0;
        return;
    }

    const double invSr = 1.0 / sampleRate;
    const double attackTime = 0.025 * static_cast<double>(params.transient);
    const double bodyTime = std::max(static_cast<double>(params.bodyDecay), 1e-3);
    const double tailTime = std::max(static_cast<double>(params.tail), 1e-3);
    const double pitchDecayTime =
        std::max(static_cast<double>(params.pitchDecay), 1e-4);
    const double curve = static_cast<double>(std::max(params.pitchCurve, 0.5f));
    const double drive = std::max(static_cast<double>(params.drive), 1.0);
    const double driveNorm = std::tanh(drive);
    const double clipCeiling = juce::jlimit(
        static_cast<double>(minClipCeiling), 1.0, static_cast<double>(params.clip));
    const double subAmount = static_cast<double>(params.sub) * subLevelTrim;
    const double clickAmount = static_cast<double>(params.click);
    const double tone =
        juce::jlimit(0.0, 1.0, static_cast<double>(params.clickTone));
    const double toneCoeff = 0.05 + 0.9 * tone;
    const double level = static_cast<double>(params.outputLevel);

    int nextTrigger = 0;

    for (int i = 0; i < numSamples; ++i) {
        while (nextTrigger < triggerCount
               && triggers[nextTrigger].sampleOffset <= i) {
            startVoice(triggers[nextTrigger].velocity,
                       triggers[nextTrigger].noteNumber);
            ++nextTrigger;
        }

        if (pendingReleaseOffset >= 0 && i >= pendingReleaseOffset) {
            pendingReleaseOffset = -1;
            if (active && releaseTotal == 0) {
                releaseTotal = juce::jmax(
                    1, static_cast<int>(releaseFadeSeconds * sampleRate));
                releaseRemaining = releaseTotal;
            }
        }

        if (!active)
            continue;

        const double t = static_cast<double>(voiceSamples) * invSr;

        double attackGain = 1.0;
        if (attackTime > 1e-9)
            attackGain = juce::jmin(1.0, t / attackTime);

        const double bodyGain = std::exp(-t / bodyTime);
        const double tailGain = std::exp(-t / tailTime);

        // Exponential pitch sweep from (end + pitchStart) down to end.
        const double u = juce::jmin(1.0, t / pitchDecayTime);
        const double sweep =
            static_cast<double>(params.pitchStart) * std::pow(1.0 - u, curve);
        const double midi = endMidi + sweep;
        const double freq = 440.0 * std::pow(2.0, (midi - 69.0) / 12.0);
        const double inc = twoPi * freq / sampleRate;

        mainPhase += inc;
        if (mainPhase >= twoPi)
            mainPhase -= twoPi;
        subPhase += 0.5 * inc;
        if (subPhase >= twoPi)
            subPhase -= twoPi;

        const float noise = nextClickNoise(clickNoiseState);
        clickLowpass += (noise - clickLowpass) * static_cast<float>(toneCoeff);
        const float clickSample = noise * static_cast<float>(1.0 - tone)
                                  + clickLowpass * static_cast<float>(tone);
        const double clickEnv =
            (t < clickTimeSeconds) ? 1.0 - t / clickTimeSeconds : 0.0;

        double x = std::sin(mainPhase) * bodyGain
                   + std::sin(subPhase) * subAmount * tailGain
                   + static_cast<double>(clickSample) * clickEnv * clickAmount;
        x *= attackGain;

        // Gain-compensated saturation then optional hard clip ceiling.
        x = std::tanh(x * drive) / driveNorm;
        x = juce::jlimit(-clipCeiling, clipCeiling, x);

        double fadeGain = 1.0;
        if (releaseRemaining > 0) {
            fadeGain = static_cast<double>(releaseRemaining)
                       / static_cast<double>(releaseTotal);
            --releaseRemaining;
        }

        const float out = static_cast<float>(
            x * static_cast<double>(velocityGain) * level * fadeGain);
        left[i] += out;
        if (right != nullptr)
            right[i] += out;

        ++voiceSamples;
        if (voiceSamples >= maxVoiceSamples
            || (releaseTotal > 0 && releaseRemaining == 0)) {
            active = false;
            releaseTotal = 0;
            releaseRemaining = 0;
        }
    }

    triggerCount = 0;
}

} // namespace vstengine::kick

