#include "acid/AcidEngine.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>

namespace vstengine::acid {
namespace {
constexpr std::uint32_t stateMagic = 0x44494341u; // "ACID", little-endian.
constexpr float pi = 3.14159265358979323846f;

void writeU32(std::byte* destination, std::uint32_t value) noexcept
{
    destination[0] = static_cast<std::byte>(value & 0xffu);
    destination[1] = static_cast<std::byte>((value >> 8) & 0xffu);
    destination[2] = static_cast<std::byte>((value >> 16) & 0xffu);
    destination[3] = static_cast<std::byte>((value >> 24) & 0xffu);
}

std::uint32_t readU32(const std::byte* source) noexcept
{
    return std::to_integer<std::uint32_t>(source[0])
        | (std::to_integer<std::uint32_t>(source[1]) << 8)
        | (std::to_integer<std::uint32_t>(source[2]) << 16)
        | (std::to_integer<std::uint32_t>(source[3]) << 24);
}

float flushDenormal(float value) noexcept
{
    return std::abs(value) < 1.0e-20f ? 0.0f : value;
}
} // namespace

bool AcidEngine::prepare(double sampleRate, std::uint32_t maximumBlockSize,
                         std::uint32_t outputChannels) noexcept
{
    if (!std::isfinite(sampleRate) || sampleRate < 8000.0
        || sampleRate > 384000.0 || maximumBlockSize == 0
        || outputChannels == 0 || outputChannels > 2)
        return false;
    sampleRate_ = sampleRate;
    maximumBlockSize_ = maximumBlockSize;
    outputChannels_ = outputChannels;
    prepared_ = true;
    reset();
    return true;
}

void AcidEngine::reset() noexcept
{
    phase_ = 0.0;
    currentFrequency_ = targetFrequency_ = 110.0;
    pitchBendSemitones_ = 0.0f;
    filterEnvelope_ = ampEnvelope_ = 0.0f;
    velocityGain_ = 1.0f;
    accentGain_ = 0.0f;
    portamentoSeconds_ = 0.08f;
    filterState_.fill(0.0f);
    currentNote_ = 255;
    gate_ = active_ = portamentoEnabled_ = false;
    smoothed_ = targets_;
}

bool AcidEngine::setParameter(std::string_view id, float value) noexcept
{
    if (!std::isfinite(value)) return false;
    for (std::size_t index = 0; index < parameterIds.size(); ++index)
        if (parameterIds[index] == id) {
            targets_[index] = std::clamp(value, 0.0f, 1.0f);
            return true;
        }
    return false;
}

float AcidEngine::parameter(std::string_view id) const noexcept
{
    for (std::size_t index = 0; index < parameterIds.size(); ++index)
        if (parameterIds[index] == id) return targets_[index];
    return 0.0f;
}

void AcidEngine::noteOn(std::uint8_t note, std::uint8_t velocity) noexcept
{
    const bool portamento = portamentoEnabled_ && currentNote_ != 255;
    const bool legato = (gate_ && active_) || portamento;
    currentNote_ = note;
    targetFrequency_ = midiFrequency(static_cast<float>(note)
                                     + pitchBendSemitones_);
    velocityGain_ = 0.55f + 0.45f * (static_cast<float>(velocity) / 127.0f);
    accentGain_ = velocity >= 108 ? targets_[accent] : 0.0f;
    gate_ = active_ = true;

    if (!legato) {
        // Deterministic hard trigger. Legato note-ons preserve phase/envelopes.
        phase_ = 0.0;
        currentFrequency_ = targetFrequency_;
        filterEnvelope_ = 1.0f;
        ampEnvelope_ = 0.0f;
    } else if (!gate_) {
        filterEnvelope_ = 1.0f;
        ampEnvelope_ = 0.0f;
    }
}

void AcidEngine::noteOff(std::uint8_t note) noexcept
{
    if (currentNote_ == note) gate_ = false;
}

void AcidEngine::allNotesOff() noexcept
{
    gate_ = false;
    currentNote_ = 255;
}

void AcidEngine::pitchBend(std::uint8_t lsb, std::uint8_t msb) noexcept
{
    const int value = static_cast<int>(lsb) | (static_cast<int>(msb) << 7);
    pitchBendSemitones_ = (static_cast<float>(value) - 8192.0f)
        * (2.0f / 8192.0f);
    if (currentNote_ != 255)
        targetFrequency_ = midiFrequency(static_cast<float>(currentNote_)
                                         + pitchBendSemitones_);
}

double AcidEngine::midiFrequency(float note) noexcept
{
    return 440.0 * std::exp2((static_cast<double>(note) - 69.0) / 12.0);
}

float AcidEngine::polyBlep(double phase, double increment) noexcept
{
    if (increment <= 0.0) return 0.0f;
    if (phase < increment) {
        const auto x = phase / increment;
        return static_cast<float>(x + x - x * x - 1.0);
    }
    if (phase > 1.0 - increment) {
        const auto x = (phase - 1.0) / increment;
        return static_cast<float>(x * x + x + x + 1.0);
    }
    return 0.0f;
}

float AcidEngine::oscillator(double increment) noexcept
{
    const float saw = static_cast<float>(2.0 * phase_ - 1.0)
        - polyBlep(phase_, increment);
    float square = phase_ < 0.5 ? 1.0f : -1.0f;
    square += polyBlep(phase_, increment);
    auto shifted = phase_ + 0.5;
    if (shifted >= 1.0) shifted -= 1.0;
    square -= polyBlep(shifted, increment);
    return saw + (square - saw) * smoothed_[waveform];
}

float AcidEngine::filter(float input, float cutoffHz,
                         float resonanceAmount) noexcept
{
    // Four nonlinear one-pole stages with bounded ladder-style feedback.
    const float coefficient = std::clamp(
        1.0f - std::exp(-2.0f * pi * cutoffHz
                        / static_cast<float>(sampleRate_)), 0.0f, 0.92f);
    const float feedback = 3.75f * resonanceAmount;
    float stageInput = std::tanh(input - feedback * filterState_[3]);
    for (auto& stage : filterState_) {
        stage += coefficient * (std::tanh(stageInput) - std::tanh(stage));
        stage = flushDenormal(std::clamp(stage, -4.0f, 4.0f));
        stageInput = stage;
    }
    return filterState_[3];
}

void AcidEngine::process(std::span<float*> outputs, std::uint32_t sampleCount,
                         std::span<const VoxMidiEventV1> midi) noexcept
{
    if (!prepared_ || sampleCount > maximumBlockSize_ || outputs.empty()
        || outputs.size() > outputChannels_)
        return;

    std::size_t eventIndex = 0;
    const float smoothing = 1.0f - std::exp(-1.0f
        / static_cast<float>(sampleRate_ * 0.006));
    for (std::uint32_t sample = 0; sample < sampleCount; ++sample) {
        while (eventIndex < midi.size()
               && midi[eventIndex].sampleOffset <= sample) {
            const auto& event = midi[eventIndex++];
            if (event.sampleOffset != sample || event.size == 0) continue;
            const auto type = event.data[0] & 0xf0u;
            if (type == 0x90u && event.size >= 3 && event.data[2] != 0)
                noteOn(event.data[1], event.data[2]);
            else if ((type == 0x80u || type == 0x90u) && event.size >= 3)
                noteOff(event.data[1]);
            else if (type == 0xe0u && event.size >= 3)
                pitchBend(event.data[1], event.data[2]);
            else if (type == 0xb0u && event.size >= 3
                     && event.data[1] == 65)
                portamentoEnabled_ = event.data[2] >= 64;
            else if (type == 0xb0u && event.size >= 3
                     && event.data[1] == 5) {
                const auto normalized = static_cast<float>(event.data[2]) / 127.0f;
                portamentoSeconds_ = std::max(0.001f,
                    2.0f * normalized * normalized);
            }
            else if (type == 0xb0u && event.size >= 3
                     && (event.data[1] == 120 || event.data[1] == 123))
                allNotesOff();
        }

        for (std::size_t index = 0; index < smoothed_.size(); ++index)
            smoothed_[index] += smoothing * (targets_[index] - smoothed_[index]);

        const float slideSeconds = portamentoEnabled_ ? portamentoSeconds_
            : 0.004f + 0.32f * smoothed_[slide] * smoothed_[slide];
        const double glide = 1.0 - std::exp(-1.0 / (sampleRate_ * slideSeconds));
        currentFrequency_ += (targetFrequency_ - currentFrequency_) * glide;
        currentFrequency_ = std::clamp(currentFrequency_, 8.0,
                                       sampleRate_ * 0.45);
        const double increment = currentFrequency_ / sampleRate_;

        const float envSeconds = 0.045f + 1.15f * smoothed_[decay]
            * smoothed_[decay] * (1.0f + 0.35f * accentGain_);
        filterEnvelope_ *= std::exp(-1.0f
            / (static_cast<float>(sampleRate_) * envSeconds));
        const float ampAttack = 1.0f - std::exp(-1.0f
            / static_cast<float>(sampleRate_ * 0.0015));
        const float ampRelease = 1.0f - std::exp(-1.0f
            / static_cast<float>(sampleRate_ * 0.028));
        ampEnvelope_ += (gate_ ? ampAttack : ampRelease)
            * ((gate_ ? 1.0f : 0.0f) - ampEnvelope_);

        const float baseCutoff = 45.0f * std::pow(400.0f, smoothed_[cutoff]);
        const float envelopeOctaves = (2.0f + 5.0f * smoothed_[envelopeAmount])
            * filterEnvelope_ * (1.0f + 0.65f * accentGain_);
        const float cutoffHz = std::clamp(
            baseCutoff * std::exp2(envelopeOctaves), 35.0f,
            static_cast<float>(sampleRate_ * 0.45));
        const float oscillatorSample = oscillator(increment);
        const float driven = std::tanh(oscillatorSample
            * (1.0f + 9.0f * smoothed_[drive] * smoothed_[drive]));
        float value = filter(driven, cutoffHz, smoothed_[resonance]);
        value = std::tanh(value * (1.1f + 1.8f * smoothed_[drive]));
        value *= ampEnvelope_ * velocityGain_ * (1.0f + 0.45f * accentGain_)
            * (0.08f + 0.42f * smoothed_[output]);
        if (!std::isfinite(value)) {
            filterState_.fill(0.0f);
            value = 0.0f;
        }
        for (auto* channel : outputs)
            if (channel != nullptr) channel[sample] += value;

        phase_ += increment;
        phase_ -= std::floor(phase_);
        if (!gate_ && ampEnvelope_ < 1.0e-5f) {
            active_ = false;
            currentNote_ = 255;
            ampEnvelope_ = 0.0f;
        }
    }
}

bool AcidEngine::saveState(std::span<std::byte> destination,
                           std::uint32_t& bytesWritten) const noexcept
{
    bytesWritten = static_cast<std::uint32_t>(encodedStateBytes);
    if (destination.size() < encodedStateBytes) return false;
    writeU32(destination.data(), stateMagic);
    writeU32(destination.data() + 4, stateVersion);
    for (std::size_t index = 0; index < targets_.size(); ++index)
        writeU32(destination.data() + 8 + index * 4,
                 std::bit_cast<std::uint32_t>(targets_[index]));
    return true;
}

bool AcidEngine::loadState(std::uint32_t schemaVersion,
                           std::span<const std::byte> payload) noexcept
{
    if (schemaVersion != stateVersion) return false;
    if (payload.empty()) return true;
    if (payload.size() != encodedStateBytes
        || readU32(payload.data()) != stateMagic
        || readU32(payload.data() + 4) != stateVersion)
        return false;
    std::array<float, parameterCount> decoded {};
    for (std::size_t index = 0; index < decoded.size(); ++index) {
        decoded[index] = std::bit_cast<float>(
            readU32(payload.data() + 8 + index * 4));
        if (!std::isfinite(decoded[index]) || decoded[index] < 0.0f
            || decoded[index] > 1.0f)
            return false;
    }
    targets_ = decoded;
    smoothed_ = decoded;
    return true;
}

std::uint32_t AcidEngine::tailSamples() const noexcept
{
    return static_cast<std::uint32_t>(sampleRate_ * 0.4);
}

} // namespace vstengine::acid
