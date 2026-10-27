#if __has_include("acid/AcidEngine.h")
#include "acid/AcidEngine.h"
#else
#include "AcidEngine.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <numbers>

namespace vstengine::acid {

namespace {

constexpr std::uint32_t stateMagic = 0x44494341u; // "ACID" in little-endian
constexpr double twoPi = 2.0 * std::numbers::pi;

[[nodiscard]] inline float clamp01(float value) noexcept {
    return std::clamp(value, 0.0f, 1.0f);
}

[[nodiscard]] inline float fastTanh(float x) noexcept {
    // The cubic rational approximation is only bounded for |x| <= 3.
    // Keep saturation truly bounded: high Acid resonance/Drive must never
    // amplify the feedback path above unity because of the approximant.
    x = std::clamp(x, -3.0f, 3.0f);
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

} // namespace

float AcidEngine::AdsrState::advance(float attackSec, float decaySec,
                                     float sustainLevel, float releaseSec,
                                     bool gateOpen,
                                     double sampleRate) noexcept {
    switch (stage) {
        case EnvStage::idle:
            value = 0.0f;
            break;

        case EnvStage::attack: {
            const float step =
                1.0f / std::max(1.0f, static_cast<float>(attackSec * sampleRate));
            value += step;
            if (value >= 1.0f) {
                value = 1.0f;
                stage = EnvStage::decay;
            }
            break;
        }

        case EnvStage::decay: {
            const float coeff = std::exp(
                -1.0f / std::max(1.0f, static_cast<float>(decaySec * sampleRate)));
            value = sustainLevel + (value - sustainLevel) * coeff;
            if (std::abs(value - sustainLevel) < 1.0e-4f) {
                value = sustainLevel;
                stage = (sustainLevel <= 0.001f && !gateOpen) ? EnvStage::idle
                                                              : EnvStage::sustain;
            }
            break;
        }

        case EnvStage::sustain: {
            value = sustainLevel;
            if (!gateOpen) {
                stage = EnvStage::release;
            }
            break;
        }

        case EnvStage::release: {
            const float coeff = std::exp(
                -1.0f / std::max(1.0f, static_cast<float>(releaseSec * sampleRate)));
            value *= coeff;
            if (value < 1.0e-4f) {
                value = 0.0f;
                stage = EnvStage::idle;
            }
            break;
        }
    }
    return value;
}

float AcidEngine::cutoffNormalizedToHz(float norm) noexcept {
    return 20.0f * std::pow(900.0f, clamp01(norm)); // 20 Hz .. 18,000 Hz
}

float AcidEngine::cutoffHzToNormalized(float hz) noexcept {
    const float clamped = std::clamp(hz, 20.0f, 18000.0f);
    return std::log(clamped / 20.0f) / std::log(900.0f);
}

bool AcidEngine::prepare(double sampleRate, std::uint32_t maximumBlockSize,
                         std::uint32_t outputChannels) noexcept {
    // Comparing NaN with <= 0 is false; accepting it would poison phase
    // increments and eventually make float->integer tail conversion UB.
    if (!std::isfinite(sampleRate) || sampleRate <= 0.0
        || maximumBlockSize == 0 || outputChannels == 0) {
        prepared_ = false;
        return false;
    }

    sampleRate_ = sampleRate;
    maximumBlockSize_ = maximumBlockSize;
    outputChannels_ = outputChannels;
    reset();
    prepared_ = true;
    return true;
}

void AcidEngine::reset() noexcept {
    smoothed_ = targets_;
    phases_.fill(0.0);
    subPhases_.fill(0.0);
    triIntegrators_.fill(0.0);
    currentFrequency_ = 130.81;
    targetFrequency_ = 130.81;
    pitchBendSemitones_.fill(0.0f);

    ampEnv_.reset();
    fltEnv_.reset();
    modEnv_.reset();
    env2State_.reset();
    lfo1Phase_ = 0.0;
    lfo2Phase_ = 0.0;
    lfo3Phase_ = 0.0;
    lfoPhaseIncrement_.fill(0.0);
    lfoTargetIncrement_.fill(0.0);
    lfoIncrementSmoothing_ = 1.0 - std::exp(-1.0 / (0.004 * sampleRate_));
    transportBpm_ = 120.0;
    stepPpqPosition_ = 0.0;
    transportPlaying_ = false;

    accentCapacitor_ = 0.0f;
    accentGain_ = 0.0f;
    velocityGain_ = 1.0f;
    isAccentedNote_ = false;

    filterState_.fill(0.0f);
    postToneState_ = 0.0f;

    heldKeys_.fill({});
    latestHeldKey_ = noHeldKey;
    currentChannel_ = 0;
    currentNote_ = 255;
    gate_ = false;
    slideActive_ = false;
    glideSource_ = GlideSource::none;
    ccPortamento_.fill(false);
    active_ = false;
}

void AcidEngine::setPlayModeOptions(bool legato, bool retrigger,
                                    bool velToAccent,
                                    bool slideOnOverlap) noexcept {
    legatoEnabled_ = legato;
    retriggerEnabled_ = retrigger;
    velToAccentEnabled_ = velToAccent;
    slideOnOverlapEnabled_ = slideOnOverlap;

    std::uint8_t mask = 0;
    if (legato) mask |= 0x01u;
    if (retrigger) mask |= 0x02u;
    if (velToAccent) mask |= 0x04u;
    if (slideOnOverlap) mask |= 0x08u;
    targets_[Parameter::playModeFlags] = static_cast<float>(mask) / 15.0f;
}

void AcidEngine::setTransport(double bpm, double ppqPosition, bool playing) noexcept {
    if (!std::isfinite(bpm) || bpm <= 1.0 || bpm > 1000.0
        || !std::isfinite(ppqPosition)) {
        transportPlaying_ = false;
        return;
    }
    transportBpm_ = bpm;
    stepPpqPosition_ = ppqPosition;
    transportPlaying_ = playing;
}

void AcidEngine::process(std::span<float*> outputs, std::uint32_t sampleCount,
                         std::span<const VoxMidiEventV1> midi) noexcept {
    if (!prepared_ || outputs.empty() || sampleCount == 0) {
        return;
    }

    constexpr std::array<Parameter, 3> rateParameters {
        Parameter::lfo1Rate, Parameter::lfo2Rate, Parameter::lfo3Rate
    };
    // pow is expensive inside the audio sample loop. Convert each source
    // rate to cycles/sample only once per host block, then smooth phase
    // increments sample-by-sample in renderStereoSample().
    for (std::size_t i = 0; i < rateParameters.size(); ++i)
        lfoTargetIncrement_[i] = static_cast<double>(
            0.05f * std::pow(400.0f, targets_[rateParameters[i]])) / sampleRate_;

    std::size_t midiIndex = 0;
    const float smoothCoeff =
        1.0f - std::exp(-1.0f / static_cast<float>(0.004 * sampleRate_));
    const double ppqIncrement = transportPlaying_
        ? transportBpm_ / (60.0 * sampleRate_) : 0.0;

    for (std::uint32_t sample = 0; sample < sampleCount; ++sample) {
        while (midiIndex < midi.size() &&
               midi[midiIndex].sampleOffset <= sample) {
            handleMidiEvent(midi[midiIndex]);
            ++midiIndex;
        }

        for (std::size_t p = 0; p < parameterCount; ++p) {
            smoothed_[p] += smoothCoeff * (targets_[p] - smoothed_[p]);
        }

        float left = 0.0f;
        float right = 0.0f;
        renderStereoSample(left, right);
        // Musical position belongs to the host's audio timeline, not the
        // voice envelope. Advance across silence, release tails and any
        // sampleOffset before the first NoteOn, exactly once per sample.
        stepPpqPosition_ += ppqIncrement;

        if (outputs.size() == 1 && outputs[0] != nullptr) {
            outputs[0][sample] = 0.5f * (left + right);
        } else {
            if (outputs[0] != nullptr) {
                outputs[0][sample] = left;
            }
            if (outputs.size() > 1 && outputs[1] != nullptr) {
                outputs[1][sample] = right;
            }
            for (std::size_t ch = 2; ch < outputs.size(); ++ch) {
                if (outputs[ch] != nullptr) {
                    outputs[ch][sample] = 0.5f * (left + right);
                }
            }
        }
    }

    // Events at or beyond sampleCount do not belong to this block. Never
    // mutate the voice early; the caller owns scheduling into later blocks.

    active_ = gate_ ||
              (ampEnv_.stage != EnvStage::idle && ampEnv_.value > 1.0e-5f);
}

bool AcidEngine::setParameter(std::string_view id,
                              float normalizedValue) noexcept {
    if (!std::isfinite(normalizedValue)) return false;
    const float clamped = clamp01(normalizedValue);

    for (std::size_t i = 0; i < parameterCount; ++i) {
        if (parameterIds[i] == id) {
            targets_[i] = clamped;
            if (i == Parameter::playModeFlags) {
                const int mask =
                    static_cast<int>(std::round(clamped * 15.0f));
                legatoEnabled_ = (mask & 0x01) != 0;
                retriggerEnabled_ = (mask & 0x02) != 0;
                velToAccentEnabled_ = (mask & 0x04) != 0;
                slideOnOverlapEnabled_ = (mask & 0x08) != 0;
            }
            return true;
        }
    }
    return false;
}

float AcidEngine::parameter(std::string_view id) const noexcept {
    for (std::size_t i = 0; i < parameterCount; ++i) {
        if (parameterIds[i] == id) {
            return targets_[i];
        }
    }
    return 0.0f;
}

bool AcidEngine::loadState(std::uint32_t schemaVersion,
                           std::span<const std::byte> payload) noexcept {
    // The existing Rack creates a newly selected instrument with an empty
    // payload: retain factory defaults for both supported schemas.
    // Non-empty payloads must be fully validated transactionally.
    if (schemaVersion != legacyStateVersion
        && schemaVersion != previousStateVersion
        && schemaVersion != lfo1StateVersion
        && schemaVersion != env2StateVersion
        && schemaVersion != lfo23StateVersion
        && schemaVersion != stepStateVersion
        && schemaVersion != stateVersion)
        return false;
    if (payload.empty()) return true;
    if (payload.size() < 8) return false;

    std::uint32_t magic = 0;
    std::uint32_t version = 0;
    std::memcpy(&magic, payload.data(), sizeof(std::uint32_t));
    std::memcpy(&version, payload.data() + 4, sizeof(std::uint32_t));

    if (magic != stateMagic || version != schemaVersion) {
        return false;
    }

    // A single version table owns the serialized prefix length. v1 is a
    // special pre-v3 payload: preserve previously configured original
    // extension 9..35, but reset every feature appended after v3.
    const std::size_t storedParameters =
        schemaVersion == legacyStateVersion ? legacyParameterCount
        : schemaVersion == previousStateVersion ? previousParameterCount
        : schemaVersion == lfo1StateVersion ? lfo1ParameterCount
        : schemaVersion == env2StateVersion ? env2ParameterCount
        : schemaVersion == lfo23StateVersion ? lfo23ParameterCount
        : schemaVersion == stepStateVersion ? stepParameterCount
                                            : parameterCount;
    if (payload.size() < 8 + storedParameters * sizeof(float))
        return false;

    auto parsedTargets = targets_;
    const std::size_t factorySuffixStart =
        schemaVersion == legacyStateVersion ? previousParameterCount
                                           : storedParameters;
    std::copy(factoryTargets_.begin() + static_cast<std::ptrdiff_t>(factorySuffixStart),
              factoryTargets_.end(),
              parsedTargets.begin() + static_cast<std::ptrdiff_t>(factorySuffixStart));

    // Parse transactionally: malformed or non-finite data must never
    // partially change the live DSP. All legacy schemas use the same
    // validated normalized-float wire format for their stored prefix.
    for (std::size_t i = 0; i < storedParameters; ++i) {
        float value = 0.0f;
        std::memcpy(&value, payload.data() + 8 + i * sizeof(float), sizeof(float));
        if (!std::isfinite(value)) return false;
        parsedTargets[i] = clamp01(value);
    }

    targets_ = parsedTargets;
    smoothed_ = targets_;
    if (schemaVersion != legacyStateVersion)
        (void) setParameter("play_mode_flags", targets_[Parameter::playModeFlags]);
    return true;
}

bool AcidEngine::saveState(std::span<std::byte> destination,
                           std::uint32_t& bytesWritten) const noexcept {
    bytesWritten = 0;
    if (destination.size() < encodedStateBytes) {
        return false;
    }

    const std::uint32_t magic = stateMagic;
    const std::uint32_t version = stateVersion;
    std::memcpy(destination.data(), &magic, sizeof(std::uint32_t));
    std::memcpy(destination.data() + 4, &version, sizeof(std::uint32_t));

    for (std::size_t i = 0; i < parameterCount; ++i) {
        const float val = clamp01(targets_[i]);
        std::memcpy(destination.data() + 8 + i * sizeof(float), &val,
                    sizeof(float));
    }

    bytesWritten = static_cast<std::uint32_t>(encodedStateBytes);
    return true;
}

std::uint32_t AcidEngine::tailSamples() const noexcept {
    const float releaseSec =
        0.005f + smoothed_[Parameter::ampRelease] * 1.8f;
    const double count = static_cast<double>(releaseSec) * sampleRate_;
    return static_cast<std::uint32_t>(std::clamp(
        count, 0.0, static_cast<double>(std::numeric_limits<std::uint32_t>::max())));
}

void AcidEngine::handleMidiEvent(const VoxMidiEventV1& event) noexcept {
    // Follow the stable VoxMidiEventV1 ABI: sampleOffset, size, data[3].
    if (event.size != 3 || event.data[0] < 0x80u || event.data[0] >= 0xf0u ||
        event.data[1] >= 128u || event.data[2] >= 128u)
      return;
    const std::uint8_t channel = event.data[0] & 0x0fu;
    const std::uint8_t statusType = event.data[0] & 0xF0u;

    switch (statusType) {
        case 0x90u: {
            if (event.data[2] > 0) {
              noteOn(channel, event.data[1], event.data[2]);
            } else {
              noteOff(channel, event.data[1]);
            }
            break;
        }
        case 0x80u: {
          noteOff(channel, event.data[1]);
          break;
        }
        case 0xB0u: {
            if (event.data[1] == 65u) {
              ccPortamento_[channel] = (event.data[2] >= 64u);
            } else if (event.data[1] == 120u || event.data[1] == 123u) {
              channelNotesOff(channel, event.data[1] == 120u);
            }
            break;
        }
        case 0xE0u: {
          pitchBend(channel, event.data[1], event.data[2]);
          break;
        }
        default:
            break;
    }
}

void AcidEngine::noteOn(std::uint8_t channel, std::uint8_t note,
                        std::uint8_t velocity) noexcept {
  const bool wasGateOpen = gate_;
  const bool hadPreviousNote = currentNote_ < 128u;
  const auto key = static_cast<std::uint16_t>(channel * 128u + note);
  removeHeldKey(key);
  heldKeys_[key] = {latestHeldKey_, noHeldKey, velocity};
  if (latestHeldKey_ != noHeldKey)
    heldKeys_[latestHeldKey_].next = key;
  latestHeldKey_ = key;
  selectHeldKey(key);

  const bool overlapSlide = wasGateOpen && slideOnOverlapEnabled_;
  const bool hasPortamento = smoothed_[Parameter::portamento] > 0.01f;
  // A dedicated Slide knob governs only the overlapping/CC-legato glide.
  // General Portamento is separate: never use max(slide, portamento),
  // which makes two independent controls modify one another's timing.
  if ((overlapSlide || ccPortamento_[channel]) &&
      smoothed_[Parameter::slide] > 0.01f)
    glideSource_ = GlideSource::slide;
  else if (hasPortamento && hadPreviousNote)
    glideSource_ = GlideSource::portamento;
  else
    glideSource_ = GlideSource::none;
  slideActive_ = glideSource_ != GlideSource::none;

  if (!slideActive_ || !hadPreviousNote) {
    currentFrequency_ = targetFrequency_;
  }

  const float accentParam = smoothed_[Parameter::accent];

  const bool shouldRetrigger =
      retriggerEnabled_ || !wasGateOpen || !legatoEnabled_;

  if (shouldRetrigger) {
    ampEnv_.trigger(retriggerEnabled_);
    fltEnv_.trigger(retriggerEnabled_);
    modEnv_.trigger(retriggerEnabled_);
    env2State_.trigger(retriggerEnabled_);

    if (isAccentedNote_) {
      accentCapacitor_ = std::min(1.5f, accentCapacitor_ + 0.35f * accentParam);
    }
  }

  gate_ = true;
  active_ = true;
}

void AcidEngine::removeHeldKey(std::uint16_t key) noexcept {
  auto &held = heldKeys_[key];
  if (held.velocity == 0)
    return;
  if (held.previous != noHeldKey)
    heldKeys_[held.previous].next = held.next;
  if (held.next != noHeldKey)
    heldKeys_[held.next].previous = held.previous;
  if (latestHeldKey_ == key)
    latestHeldKey_ = held.previous;
  held = {};
}

void AcidEngine::selectHeldKey(std::uint16_t key) noexcept {
  currentNote_ = static_cast<std::uint8_t>(key % 128u);
  currentChannel_ = static_cast<std::uint8_t>(key / 128u);
  targetFrequency_ = midiFrequency(static_cast<float>(currentNote_) +
                                   pitchBendSemitones_[currentChannel_]);
  const auto velocity = heldKeys_[key].velocity;
  isAccentedNote_ = velToAccentEnabled_ && velocity >= 110u;
  velocityGain_ =
      std::clamp(static_cast<float>(velocity) / 127.0f, 0.15f, 1.0f);
  accentGain_ = isAccentedNote_ ? smoothed_[Parameter::accent] : 0.0f;
}

void AcidEngine::releaseVoice() noexcept {
  gate_ = false;
  slideActive_ = false;
  glideSource_ = GlideSource::none;
  ampEnv_.release();
  fltEnv_.release();
  modEnv_.release();
  env2State_.release();
}

void AcidEngine::restoreHeldVoice() noexcept {
  if (latestHeldKey_ == noHeldKey) {
    releaseVoice();
    return;
  }
  selectHeldKey(latestHeldKey_);
  if ((slideOnOverlapEnabled_ || ccPortamento_[currentChannel_]) &&
      smoothed_[Parameter::slide] > 0.01f)
    glideSource_ = GlideSource::slide;
  else if (smoothed_[Parameter::portamento] > 0.01f)
    glideSource_ = GlideSource::portamento;
  else
    glideSource_ = GlideSource::none;
  slideActive_ = glideSource_ != GlideSource::none;
  if (!slideActive_)
    currentFrequency_ = targetFrequency_;
}

void AcidEngine::noteOff(std::uint8_t channel, std::uint8_t note) noexcept {
  const auto key = static_cast<std::uint16_t>(channel * 128u + note);
  const bool wasLatest = latestHeldKey_ == key;
  removeHeldKey(key);
  // An unmatched or background NoteOff cannot change pitch, glide or gate.
  if (wasLatest)
    restoreHeldVoice();
}

void AcidEngine::channelNotesOff(std::uint8_t channel,
                                 bool immediate) noexcept {
  const auto previousLatest = latestHeldKey_;
  for (std::uint16_t note = 0; note < 128; ++note)
    removeHeldKey(static_cast<std::uint16_t>(channel * 128u + note));
  if (latestHeldKey_ == previousLatest) {
    // Key ownership ends at NoteOff, but the channel still owns its VCA
    // release. CC120 must silence that tail without affecting another channel.
    if (immediate && latestHeldKey_ == noHeldKey && currentChannel_ == channel)
      allNotesOff();
    return;
  }
  if (immediate && latestHeldKey_ == noHeldKey)
    allNotesOff();
  else
    restoreHeldVoice();
}

void AcidEngine::allNotesOff() noexcept {
  heldKeys_.fill({});
  latestHeldKey_ = noHeldKey;
  currentChannel_ = 0;
  currentNote_ = 255;
  gate_ = false;
  slideActive_ = false;
  glideSource_ = GlideSource::none;
  ampEnv_.reset();
  fltEnv_.reset();
  modEnv_.reset();
  env2State_.reset();
  accentCapacitor_ = 0.0f;
  filterState_.fill(0.0f);
  active_ = false;
}

void AcidEngine::pitchBend(std::uint8_t channel, std::uint8_t lsb,
                           std::uint8_t msb) noexcept {
  const int bendValue = (static_cast<int>(msb) << 7) | static_cast<int>(lsb);
  const float normalized = static_cast<float>(bendValue - 8192) / 8192.0f;
  pitchBendSemitones_[channel] = normalized * 2.0f;
  if (currentNote_ < 128u && channel == currentChannel_) {
    targetFrequency_ = midiFrequency(static_cast<float>(currentNote_) +
                                     pitchBendSemitones_[channel]);
  }
}

void AcidEngine::renderStereoSample(float& outLeft, float& outRight) noexcept {
    if (ampEnv_.stage == EnvStage::idle && !gate_) {
        outLeft = 0.0f;
        outRight = 0.0f;
        return;
    }

    // 1. Advance Modulation Envelope (MODULATION card: ENV 1)
    const float modAttack =
        0.0008f + smoothed_[Parameter::modAttack] * smoothed_[Parameter::modAttack] * 1.2f;
    const float modDecay =
        0.020f + smoothed_[Parameter::modDecay] * smoothed_[Parameter::modDecay] * 2.4f;
    const float modSustain = clamp01(smoothed_[Parameter::modSustain]);
    const float modRelease =
        0.005f + smoothed_[Parameter::modRelease] * smoothed_[Parameter::modRelease] * 2.0f;
    const float modVal =
        modEnv_.advance(modAttack, modDecay, modSustain, modRelease, gate_, sampleRate_) *
        smoothed_[Parameter::modAmount];
    const ModTarget mTarget = decodeModTarget(smoothed_[Parameter::modTarget]);

    // ENV2 is a separate ADSR with its own MIDI trigger, tail and destination.
    // In particular, changing ENV1 ADSR never changes ENV2's stage or depth.
    const float env2AttackSec =
        0.0008f + smoothed_[Parameter::env2Attack]
                   * smoothed_[Parameter::env2Attack] * 1.2f;
    const float env2DecaySec =
        0.020f + smoothed_[Parameter::env2Decay]
                  * smoothed_[Parameter::env2Decay] * 2.4f;
    const float env2SustainValue = clamp01(smoothed_[Parameter::env2Sustain]);
    const float env2ReleaseSec =
        0.005f + smoothed_[Parameter::env2Release]
                   * smoothed_[Parameter::env2Release] * 2.0f;
    const float env2Value = env2State_.advance(
        env2AttackSec, env2DecaySec, env2SustainValue, env2ReleaseSec,
        gate_, sampleRate_) * smoothed_[Parameter::env2Amount];
    const ModTarget env2Target = decodeModTarget(smoothed_[Parameter::env2Target]);

    // Three distinct continuously running bipolar oscillators. Each owns
    // its phase, Rate, Depth, Wave and Target. The helper makes the waveform
    // transfer function identical without sharing any control state.
    const auto renderLfo = [this](std::size_t index, Parameter depth,
                                  Parameter wave, Parameter sync,
                                  double& phase) noexcept {
        // Sync enabled: derive phase directly from authoritative PPQ. This
        // survives transport stop/start, looping and arbitrary DAW seeks
        // without depending on previous audio block length or audio thread
        // state. Sync off keeps the original continuously running oscillator.
        double samplePhase = 0.0;
        if (smoothed_[sync] >= 0.5f) {
            constexpr std::array<double, 8> beatsPerCycle {
                4.0, 2.0, 1.0, 0.5, 0.25, 1.0 / 3.0, 1.0 / 6.0, 0.125
            };
            constexpr std::array<Parameter, 3> rateIds {
                Parameter::lfo1Rate, Parameter::lfo2Rate, Parameter::lfo3Rate
            };
            const int rhythm = std::clamp(static_cast<int>(std::round(
                smoothed_[rateIds[index]] * 7.0f)), 0, 7);
            const double cycleLength = beatsPerCycle[rhythm];
            const double cycles = stepPpqPosition_ / cycleLength;
            // A finite host double can still overflow after division by a
            // short musical note (e.g. DBL_MAX / 0.125). Avoid inf - inf,
            // which otherwise poisons phase and filter calculations.
            const double wrapped = std::isfinite(cycles)
                ? cycles - std::floor(cycles)
                : std::fmod(stepPpqPosition_, cycleLength) / cycleLength;
            samplePhase = wrapped - std::floor(wrapped);
        } else {
            // At the established 4ms smoothing constant, changes in
            // free-running frequency remain smooth; no per-sample pow().
            lfoPhaseIncrement_[index] += lfoIncrementSmoothing_ *
                (lfoTargetIncrement_[index] - lfoPhaseIncrement_[index]);
            phase += lfoPhaseIncrement_[index];
            if (phase >= 1.0) phase -= std::floor(phase);
            samplePhase = phase;
        }
        float signal = 0.0;
        switch (std::min(3, static_cast<int>(
                    std::round(smoothed_[wave] * 3.0f)))) {
            case 0: signal = static_cast<float>(std::sin(twoPi * samplePhase)); break;
            case 1: signal = static_cast<float>(
                1.0 - 4.0 * std::abs(samplePhase - 0.5)); break;
            case 2: signal = samplePhase < 0.5 ? 1.0f : -1.0f; break;
            default: signal = static_cast<float>(2.0 * samplePhase - 1.0); break;
        }
        return signal * smoothed_[depth];
    };
    const float lfo1Value = renderLfo(
        0u, Parameter::lfo1Depth,
        Parameter::lfo1Wave, Parameter::lfo1Sync, lfo1Phase_);
    const float lfo2Value = renderLfo(
        1u, Parameter::lfo2Depth,
        Parameter::lfo2Wave, Parameter::lfo2Sync, lfo2Phase_);
    const float lfo3Value = renderLfo(
        2u, Parameter::lfo3Depth,
        Parameter::lfo3Wave, Parameter::lfo3Sync, lfo3Phase_);
    const ModTarget lfo1Target = decodeModTarget(smoothed_[Parameter::lfo1Target]);
    const ModTarget lfo2Target = decodeModTarget(smoothed_[Parameter::lfo2Target]);
    const ModTarget lfo3Target = decodeModTarget(smoothed_[Parameter::lfo3Target]);

    // STEP source is *not* the note sequencer. Its 16 independent bipolar
    // values repeat on the host musical timeline. No allocations, locks or
    // reliance on UI state during the audio callback.
    float stepValue = 0.0f;
    if (transportPlaying_) {
        constexpr std::array<double, 4> stepsPerQuarter { 2.0, 4.0, 8.0, 3.0 };
        const int rhythm = std::clamp(static_cast<int>(
            std::round(smoothed_[Parameter::stepRate] * 3.0f)), 0, 3);
        const double quarters = std::max(0.0, stepPpqPosition_);
        const double absoluteStep = quarters * stepsPerQuarter[rhythm];
        // Overflow in a finite PPQ multiplication must never lead to
        // fmod(inf, 16) -> NaN -> undefined float-to-int index conversion.
        const double cycle = std::isfinite(absoluteStep)
            ? std::fmod(absoluteStep, static_cast<double>(stepCount))
            : std::fmod(quarters,
                static_cast<double>(stepCount) / stepsPerQuarter[rhythm])
                  * stepsPerQuarter[rhythm];
        const int index = std::clamp(static_cast<int>(cycle), 0,
                                    static_cast<int>(stepCount) - 1);
        const float phase = static_cast<float>(cycle - index);
        const float current = smoothed_[Parameter::step01 + index] * 2.0f - 1.0f;
        const float previous = smoothed_[
            Parameter::step01 + ((index + stepCount - 1) % stepCount)] * 2.0f - 1.0f;
        const float smoothing = smoothed_[Parameter::stepSmooth];
        const float transition = smoothing <= 0.00001f ? 1.0f
            : std::clamp(phase / smoothing, 0.0f, 1.0f);
        stepValue = (previous + (current - previous) * transition)
                    * smoothed_[Parameter::stepAmount];
    }
    const ModTarget stepTarget = decodeModTarget(smoothed_[Parameter::stepTarget]);
    auto modulation = [&](ModTarget target) noexcept {
        return (mTarget == target ? modVal : 0.0f)
             + (env2Target == target ? env2Value : 0.0f)
             + (lfo1Target == target ? lfo1Value : 0.0f)
             + (lfo2Target == target ? lfo2Value : 0.0f)
             + (lfo3Target == target ? lfo3Value : 0.0f)
             + (stepTarget == target ? stepValue : 0.0f);
    };

    // 2. Glide / Slide & Oscillator Tune + Fine + Pitch Modulation
    if (slideActive_) {
        const float glideAmount = glideSource_ == GlideSource::slide
            ? smoothed_[Parameter::slide] : smoothed_[Parameter::portamento];
        const double slideTimeSec =
            0.006 + static_cast<double>(glideAmount) * 0.240;
        const double glideCoeff =
            1.0 - std::exp(-1.0 / (slideTimeSec * sampleRate_));
        currentFrequency_ += glideCoeff * (targetFrequency_ - currentFrequency_);
    } else {
        currentFrequency_ = targetFrequency_;
    }

    const float tuneSemitones =
        (smoothed_[Parameter::oscTune] - 0.5f) * 48.0f; // -24 .. +24 st
    const float fineSemitones =
        (smoothed_[Parameter::oscFine] - 0.5f) * 2.0f;  // -100 .. +100 cents
    const float pitchModSemitones =
        modulation(ModTarget::pitch) * 12.0f;

    const double tunedFreq =
        currentFrequency_ *
        std::pow(2.0, static_cast<double>(tuneSemitones + fineSemitones +
                                          pitchModSemitones) /
                          12.0);
    const double baseIncrement =
        std::clamp(tunedFreq / sampleRate_, 1.0e-5, 0.48);

    // 3. Unison Engine (1..4 voices with Detune & Stereo Spread from PERFORMANCE card)
    const std::size_t numVoices = std::clamp<std::size_t>(
        1u + static_cast<std::size_t>(std::round(
                 smoothed_[Parameter::unisonVoices] * (maxUnisonVoices - 1))),
        1u, maxUnisonVoices);
    const float detuneAmount = smoothed_[Parameter::unisonDetune];
    const float pwMod =
        modulation(ModTarget::pulseWidth) * 0.35f;
    const float pulseWidth = std::clamp(
        0.50f + (smoothed_[Parameter::oscPulseWidth] - 0.5f) * 0.85f + pwMod,
        0.08f, 0.92f);

    float oscMonoMix = 0.0f;
    float oscStereoDiff = 0.0f;

    for (std::size_t v = 0; v < numVoices; ++v) {
        float voiceDetuneRatio = 1.0f;
        float voiceSideSign = 0.0f;
        if (numVoices > 1) {
            const float centered =
                (static_cast<float>(v) / static_cast<float>(numVoices - 1)) * 2.0f -
                1.0f;
            voiceDetuneRatio =
                std::pow(2.0f, (centered * detuneAmount * 0.45f) / 12.0f);
            voiceSideSign = centered;
        }

        const float s = renderVoiceOscillator(
            v, baseIncrement * static_cast<double>(voiceDetuneRatio),
            smoothed_[Parameter::waveform], pulseWidth);
        oscMonoMix += s;
        oscStereoDiff += s * voiceSideSign;
    }

    const float voiceNorm = 1.0f / std::sqrt(static_cast<float>(numVoices));
    oscMonoMix *= voiceNorm;
    oscStereoDiff *= voiceNorm * smoothed_[Parameter::stereoSpread];

    // 4. Filter Envelope (303 MEG + ADSR) & C13 Accent Capacitor
    const float fltAttack =
        0.0005f + smoothed_[Parameter::fltAttack] * smoothed_[Parameter::fltAttack] * 0.8f;
    const float fltDecayNorm = smoothed_[Parameter::decay];
    const float fltDecay = isAccentedNote_
                               ? (0.12f + fltDecayNorm * 0.12f)
                               : (0.03f + fltDecayNorm * fltDecayNorm * 2.2f);
    const float fltSustain = clamp01(smoothed_[Parameter::fltSustain]);
    const float fltRelease =
        0.010f + smoothed_[Parameter::fltRelease] * smoothed_[Parameter::fltRelease] * 1.5f;
    const float fltEnvVal = fltEnv_.advance(
        fltAttack, fltDecay, fltSustain, fltRelease, gate_, sampleRate_);

    const float c13Discharge =
        std::exp(-1.0f / static_cast<float>(0.35 * sampleRate_));
    accentCapacitor_ *= c13Discharge;

    // 5. Modulated Filter Cutoff & Resonance
    const float baseCutoffHz =
        cutoffNormalizedToHz(smoothed_[Parameter::cutoff]);
    const float effectiveEnvMod =
        smoothed_[Parameter::envelopeAmount] + accentGain_ * 0.45f;
    const float modCutoffOctaves =
        modulation(ModTarget::filterCutoff) * 3.0f;

    const float noteOffsetSemitones =
        (currentNote_ < 128u) ? (static_cast<float>(currentNote_) - 48.0f) : 0.0f;
    const float keyTrackOctaves =
        (noteOffsetSemitones / 12.0f) * smoothed_[Parameter::keyTracking];

    const float envOctaves =
        (fltEnvVal * effectiveEnvMod * 4.2f) +
        (accentCapacitor_ * smoothed_[Parameter::resonance] * 1.35f) +
        modCutoffOctaves + keyTrackOctaves;

    const float modulatedCutoffHz = std::clamp(
        baseCutoffHz * std::pow(2.0f, envOctaves),
        20.0f, static_cast<float>(sampleRate_ * 0.46));

    const float resMod =
        modulation(ModTarget::resonance) * 0.25f;
    const float effectiveResonance = std::clamp(
        smoothed_[Parameter::resonance] + accentGain_ * 0.18f + resMod,
        0.0f, 0.98f);

    const FilterMode fMode = decodeFilterMode(smoothed_[Parameter::filterMode]);
    const float filtered =
        processFilter(oscMonoMix, modulatedCutoffHz, effectiveResonance, fMode);

    // 6. Distortion / Drive with Post-Filter Tone control
    const float driveMod =
        modulation(ModTarget::drive) * 0.35f;
    const float effectiveDrive =
        clamp01(smoothed_[Parameter::drive] + driveMod);
    const DistortionMode dMode =
        decodeDistortionMode(smoothed_[Parameter::distMode]);
    const float distorted = processDistortion(
        filtered, effectiveDrive, smoothed_[Parameter::distTone],
        smoothed_[Parameter::distMix], dMode);

    // 7. VCA Amplitude Envelope + Accent Boost + Stereo Spread & Pan
    const float ampAttack =
        0.0005f + smoothed_[Parameter::ampAttack] * smoothed_[Parameter::ampAttack] * 0.8f;
    const float ampDecay =
        0.015f + smoothed_[Parameter::ampDecay] * smoothed_[Parameter::ampDecay] * 2.2f;
    const float ampSustain = clamp01(smoothed_[Parameter::ampSustain]);
    const float ampRelease =
        0.004f + smoothed_[Parameter::ampRelease] * smoothed_[Parameter::ampRelease] * 1.8f;
    const float vcaEnv = ampEnv_.advance(
        ampAttack, ampDecay, ampSustain, ampRelease, gate_, sampleRate_);

    const float accentVcaBoost = 1.0f + accentGain_ * 0.42f;
    const float totalGain =
        vcaEnv * velocityGain_ * accentVcaBoost * smoothed_[Parameter::output];

    const float panMod = modulation(ModTarget::pan) * 0.4f;
    const float panNorm = clamp01(smoothed_[Parameter::pan] + panMod);
    const float panAngle =
        panNorm * static_cast<float>(std::numbers::pi * 0.5);

    const float stereoLeft = (distorted + oscStereoDiff * 0.28f) * totalGain;
    const float stereoRight = (distorted - oscStereoDiff * 0.28f) * totalGain;

    outLeft = stereoLeft * std::cos(panAngle);
    outRight = stereoRight * std::sin(panAngle);
}

float AcidEngine::renderVoiceOscillator(std::size_t voiceIdx,
                                        double increment,
                                        float waveMorph,
                                        float pulseWidth) noexcept {
    double& phase = phases_[voiceIdx];
    double& subPhase = subPhases_[voiceIdx];
    double& triInt = triIntegrators_[voiceIdx];

    phase += increment;
    if (phase >= 1.0) {
        phase -= 1.0;
    }

    subPhase += increment * 0.5;
    if (subPhase >= 1.0) {
        subPhase -= 1.0;
    }

    float saw = static_cast<float>(2.0 * phase - 1.0);
    saw -= polyBlep(phase, increment);

    const double pw = static_cast<double>(pulseWidth);
    float square = (phase < pw) ? 1.0f : -1.0f;
    square += polyBlep(phase, increment);
    square -= polyBlep(std::fmod(phase - pw + 1.0, 1.0), increment);

    const float sine = static_cast<float>(std::sin(twoPi * phase)) * 0.8f +
                       static_cast<float>(std::sin(twoPi * subPhase)) * 0.2f;

    triInt = 0.998 * triInt + 4.0 * increment * static_cast<double>(square);
    const float triangle = std::clamp(static_cast<float>(triInt), -1.0f, 1.0f);

    const float m = clamp01(waveMorph) * 3.0f;
    if (m <= 1.0f) {
        return sine * (1.0f - m) + square * m;
    }
    if (m <= 2.0f) {
        const float t = m - 1.0f;
        return square * (1.0f - t) + saw * t;
    }
    const float t = m - 2.0f;
    return saw * (1.0f - t) + triangle * t;
}

float AcidEngine::processFilter(float input, float cutoffHz,
                                float resonanceAmount,
                                FilterMode mode) noexcept {
    const float wd = static_cast<float>(twoPi) * cutoffHz;
    const float T = 1.0f / static_cast<float>(sampleRate_);
    const float wa = (2.0f / T) * std::tan(wd * T * 0.5f);
    const float g = wa * T * 0.5f;
    const float G = g / (1.0f + g);

    const float G2 = G * G;
    const float G3 = G2 * G;
    const float G4 = G2 * G2;
    const float S = G3 * filterState_[0] + G2 * filterState_[1] +
                    G * filterState_[2] + filterState_[3];

    const float maxK = (mode == FilterMode::diode18dB) ? 4.35f : 3.95f;
    const float k = resonanceAmount * maxK;

    const float compensatedInput = input * (1.0f + 0.35f * resonanceAmount);
    const float u = (compensatedInput - k * fastTanh(S)) / (1.0f + k * G4);

    const float v0 = (u - filterState_[0]) * G;
    const float lp1 = fastTanh(v0 + filterState_[0]);
    filterState_[0] = lp1 + v0;

    const float v1 = (lp1 - filterState_[1]) * G;
    const float lp2 = v1 + filterState_[1];
    filterState_[1] = lp2 + v1;

    const float v2 = (lp2 - filterState_[2]) * G;
    const float lp3 = v2 + filterState_[2];
    filterState_[2] = lp3 + v2;

    const float v3 = (lp3 - filterState_[3]) * G;
    const float lp4 = v3 + filterState_[3];
    filterState_[3] = lp4 + v3;

    switch (mode) {
        case FilterMode::lowPass24dB:
            return lp4;
        case FilterMode::diode18dB:
            return lp3;
        case FilterMode::bandPass12dB:
            return lp2 - lp4;
        case FilterMode::highPass12dB:
            return u - 2.0f * lp1 + lp2;
        default:
            return lp4;
    }
}

float AcidEngine::processDistortion(float drySignal, float driveNorm,
                                    float toneNorm, float mixNorm,
                                    DistortionMode mode) noexcept {
    if (mixNorm <= 0.0001f) {
        return drySignal;
    }

    const float driveGain = 1.0f + driveNorm * 14.0f;
    const float x = drySignal * driveGain;
    float wet = x;

    switch (mode) {
        case DistortionMode::analogTube: {
            const float bias = 0.18f * driveNorm;
            wet = fastTanh(x + bias) - fastTanh(bias);
            break;
        }
        case DistortionMode::softClip: {
            wet = fastTanh(x);
            break;
        }
        case DistortionMode::hardClip: {
            const float pos = std::min(x, 0.85f);
            const float neg = std::max(pos, -0.70f);
            wet = fastTanh(neg * 1.3f);
            break;
        }
        case DistortionMode::wavefold: {
            wet = std::sin(x * 1.6f);
            break;
        }
    }

    // Tone control maps [0..1] to 1.2 kHz .. 16 kHz post-saturation lowpass
    const float toneCutoffHz =
        1200.0f * std::pow(13.3f, clamp01(toneNorm));
    const float toneCoeff =
        std::exp(-static_cast<float>(twoPi) * toneCutoffHz /
                 static_cast<float>(sampleRate_));
    postToneState_ = (1.0f - toneCoeff) * wet + toneCoeff * postToneState_;

    return drySignal * (1.0f - mixNorm) + postToneState_ * mixNorm;
}

double AcidEngine::midiFrequency(float note) noexcept {
    return 440.0 * std::pow(2.0, (static_cast<double>(note) - 69.0) / 12.0);
}

float AcidEngine::polyBlep(double phase, double increment) noexcept {
    if (phase < increment) {
        const double t = phase / increment;
        return static_cast<float>(t + t - t * t - 1.0);
    }
    if (phase > 1.0 - increment) {
        const double t = (phase - 1.0) / increment;
        return static_cast<float>(t * t + t + t + 1.0);
    }
    return 0.0f;
}

AcidEngine::FilterMode AcidEngine::decodeFilterMode(float normalized) noexcept {
    const int idx = std::clamp(static_cast<int>(normalized * 3.99f), 0, 3);
    return static_cast<FilterMode>(idx);
}

AcidEngine::DistortionMode AcidEngine::decodeDistortionMode(float normalized) noexcept {
    const int idx = std::clamp(static_cast<int>(normalized * 3.99f), 0, 3);
    return static_cast<DistortionMode>(idx);
}

AcidEngine::ModTarget AcidEngine::decodeModTarget(float normalized) noexcept {
    const int idx = std::clamp(static_cast<int>(normalized * 5.99f), 0, 5);
    return static_cast<ModTarget>(idx);
}

} // namespace vstengine::acid