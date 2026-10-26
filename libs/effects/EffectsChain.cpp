#include "effects/EffectsChain.h"
#include <algorithm>
#include <bit>
#include <cmath>

namespace vstengine::effects {
namespace {
constexpr std::uint32_t stateMagic = 0x43465856u; // "VFXC", little-endian.
constexpr float pi = 3.14159265358979323846f;
constexpr std::array<EffectType, EffectsChain::effectCount> canonicalTypes {
    EffectType::distortion, EffectType::wavefolder, EffectType::phaser,
    EffectType::flanger, EffectType::chorus, EffectType::bitcrusher,
    EffectType::delay, EffectType::reverb
};

void writeU32(std::byte* out, std::uint32_t value) noexcept
{
    for (unsigned byte = 0; byte < 4; ++byte)
        out[byte] = static_cast<std::byte>((value >> (byte * 8)) & 0xffu);
}
std::uint32_t readU32(const std::byte* in) noexcept
{
    return std::to_integer<std::uint32_t>(in[0])
        | (std::to_integer<std::uint32_t>(in[1]) << 8)
        | (std::to_integer<std::uint32_t>(in[2]) << 16)
        | (std::to_integer<std::uint32_t>(in[3]) << 24);
}
void writeFloat(std::byte* out, float value) noexcept
{
    writeU32(out, std::bit_cast<std::uint32_t>(value));
}
float readFloat(const std::byte* in) noexcept
{
    return std::bit_cast<float>(readU32(in));
}
float sanitize(float value) noexcept
{
    return std::isfinite(value) ? std::clamp(value, -4.0f, 4.0f) : 0.0f;
}
float wetDry(float dry, float wet, float mix) noexcept
{
    return sanitize(dry + (wet - dry) * mix);
}
double advance(double phase, double increment) noexcept
{
    phase += increment;
    return phase >= 1.0 ? phase - std::floor(phase) : phase;
}
EffectSettings setting(EffectType type, bool enabled, float mix, float amount,
                       float rate, float character) noexcept
{
    return { type, enabled, mix, amount, rate, character };
}
} // namespace

EffectsChain::EffectsChain() noexcept
{
    for (std::size_t i = 0; i < effectCount; ++i)
        state_.effects[i] = setting(canonicalTypes[i], false, 0.0f,
                                    0.5f, 0.5f, 0.5f);
}

bool EffectsChain::prepare(double sampleRate, std::uint32_t maximumBlockSize,
                           std::uint32_t channels) noexcept
{
    if (!std::isfinite(sampleRate) || sampleRate < 8000.0
        || sampleRate > 384000.0 || maximumBlockSize == 0
        || channels == 0 || channels > 2)
        return false;
    try {
        StereoBuffer flanger, chorus, delay, reverb;
        const auto make = [channels](StereoBuffer& buffer, std::size_t size) {
            for (std::size_t channel = 0; channel < channels; ++channel)
                buffer[channel].assign(size, 0.0f);
        };
        make(flanger, static_cast<std::size_t>(sampleRate * 0.025) + 4);
        make(chorus, static_cast<std::size_t>(sampleRate * 0.060) + 4);
        make(delay, static_cast<std::size_t>(sampleRate * 2.0) + 4);
        make(reverb, static_cast<std::size_t>(sampleRate * 0.250) + 4);
        flangerBuffer_.swap(flanger);
        chorusBuffer_.swap(chorus);
        delayBuffer_.swap(delay);
        reverbBuffer_.swap(reverb);
    } catch (...) {
        return false;
    }
    sampleRate_ = sampleRate;
    maximumBlockSize_ = maximumBlockSize;
    channelCount_ = channels;
    prepared_ = true;
    reset();
    return true;
}

void EffectsChain::reset() noexcept
{
  flangerValid_ = chorusValid_ = delayValid_ = reverbValid_ = {};
  flangerWrite_ = chorusWrite_ = delayWrite_ = reverbWrite_ = 0;
  phaserState_ = {};
  bitHold_ = {};
  bitCounter_ = {};
  delayDamping_ = reverbDamping_ = {};
  phaserPhase_ = flangerPhase_ = chorusPhase_ = 0.0;
}

bool EffectsChain::valid(const EffectSettings& value,
                         std::size_t expectedIndex) noexcept
{
    return expectedIndex < canonicalTypes.size()
        && value.type == canonicalTypes[expectedIndex]
        && std::isfinite(value.mix) && std::isfinite(value.amount)
        && std::isfinite(value.rate) && std::isfinite(value.character)
        && value.mix >= 0.0f && value.mix <= 1.0f
        && value.amount >= 0.0f && value.amount <= 1.0f
        && value.rate >= 0.0f && value.rate <= 1.0f
        && value.character >= 0.0f && value.character <= 1.0f;
}

bool EffectsChain::setEffect(std::size_t index,
                             const EffectSettings& value) noexcept
{
    if (!valid(value, index)) return false;
    state_.effects[index] = value;
    return true;
}

float EffectsChain::readDelay(const std::vector<float> &buffer,
                              std::size_t write, float delaySamples,
                              std::size_t validSamples) noexcept {
  if (buffer.empty())
    return 0.0f;
  const float size = static_cast<float>(buffer.size());
  float position = static_cast<float>(write) - delaySamples;
  while (position < 0.0f)
    position += size;
  const auto first = static_cast<std::size_t>(position) % buffer.size();
  const auto second = (first + 1) % buffer.size();
  const float fraction = position - std::floor(position);
  const auto a = first < validSamples ? buffer[first] : 0.0f;
  const auto b = second < validSamples ? buffer[second] : 0.0f;
  return a + (b - a) * fraction;
}

void EffectsChain::processDistortion(std::span<float*> audio, std::uint32_t n,
                                     const EffectSettings& s) noexcept
{
    const float drive = 1.0f + 24.0f * s.amount * s.amount;
    const float bias = (s.character - 0.5f) * 0.35f;
    const float norm = 1.0f / std::max(0.01f, std::tanh(drive));
    for (auto* channel : audio)
        for (std::uint32_t i = 0; i < n; ++i) {
            const float dry = channel[i];
            const float wet = (std::tanh(dry * drive + bias)
                               - std::tanh(bias)) * norm;
            channel[i] = wetDry(dry, wet, s.mix);
        }
}

void EffectsChain::processWavefolder(std::span<float*> audio, std::uint32_t n,
                                     const EffectSettings& s) noexcept
{
    const float gain = 1.0f + 10.0f * s.amount;
    for (auto* channel : audio)
        for (std::uint32_t i = 0; i < n; ++i) {
            const float dry = channel[i];
            float folded = std::fmod(dry * gain + 1.0f, 4.0f);
            if (folded < 0.0f) folded += 4.0f;
            folded = std::abs(folded - 2.0f) - 1.0f;
            folded = folded * (0.75f + 0.25f * s.character);
            channel[i] = wetDry(dry, folded, s.mix);
        }
}

void EffectsChain::processPhaser(std::span<float*> audio, std::uint32_t n,
                                 const EffectSettings& s) noexcept
{
    const double increment = (0.04 + 7.96 * s.rate * s.rate) / sampleRate_;
    for (std::uint32_t i = 0; i < n; ++i) {
        const float lfo = 0.5f + 0.5f * std::sin(
            static_cast<float>(2.0 * pi * phaserPhase_));
        const float coefficient = std::clamp(
            0.08f + (0.78f * (0.15f + 0.85f * s.amount) * lfo),
            0.02f, 0.88f);
        for (std::size_t channel = 0; channel < audio.size(); ++channel) {
            const float dry = audio[channel][i];
            float wet = dry;
            for (auto& memory : phaserState_[channel]) {
                const float next = -coefficient * wet + memory;
                memory = sanitize(wet + coefficient * next);
                wet = next;
            }
            audio[channel][i] = wetDry(dry, wet, s.mix);
        }
        phaserPhase_ = advance(phaserPhase_, increment);
    }
}

void EffectsChain::processFlanger(std::span<float*> audio, std::uint32_t n,
                                  const EffectSettings& s) noexcept
{
    const double increment = (0.03 + 2.97 * s.rate * s.rate) / sampleRate_;
    for (std::uint32_t i = 0; i < n; ++i) {
        const float lfo = 0.5f + 0.5f * std::sin(
            static_cast<float>(2.0 * pi * flangerPhase_));
        const float delay = static_cast<float>(sampleRate_)
            * (0.0004f + (0.001f + 0.008f * s.amount) * lfo);
        for (std::size_t channel = 0; channel < audio.size(); ++channel) {
            const float dry = audio[channel][i];
            const float delayed =
                readDelay(flangerBuffer_[channel], flangerWrite_, delay,
                          flangerValid_[channel]);
            flangerBuffer_[channel][flangerWrite_] = sanitize(
                dry + delayed * (0.1f + 0.65f * s.character));
            flangerValid_[channel] = std::min(flangerValid_[channel] + 1,
                                              flangerBuffer_[channel].size());
            audio[channel][i] = wetDry(dry, delayed, s.mix);
        }
        for (std::size_t channel = audio.size(); channel < channelCount_;
             ++channel) {
          flangerBuffer_[channel][flangerWrite_] = 0.0f;
          flangerValid_[channel] = std::min(flangerValid_[channel] + 1,
                                            flangerBuffer_[channel].size());
        }
        flangerWrite_ = (flangerWrite_ + 1) % flangerBuffer_[0].size();
        flangerPhase_ = advance(flangerPhase_, increment);
    }
}

void EffectsChain::processChorus(std::span<float*> audio, std::uint32_t n,
                                 const EffectSettings& s) noexcept
{
    const double increment = (0.05 + 4.95 * s.rate * s.rate) / sampleRate_;
    for (std::uint32_t i = 0; i < n; ++i) {
        for (std::size_t channel = 0; channel < audio.size(); ++channel) {
            const double phase = chorusPhase_ + (channel == 0 ? 0.0 : 0.27);
            const float lfo = 0.5f + 0.5f * std::sin(
                static_cast<float>(2.0 * pi * phase));
            const float delay = static_cast<float>(sampleRate_)
                * (0.008f + (0.003f + 0.020f * s.amount) * lfo);
            const float dry = audio[channel][i];
            const float delayed =
                readDelay(chorusBuffer_[channel], chorusWrite_, delay,
                          chorusValid_[channel]);
            chorusBuffer_[channel][chorusWrite_] = dry;
            chorusValid_[channel] = std::min(chorusValid_[channel] + 1,
                                             chorusBuffer_[channel].size());
            audio[channel][i] = wetDry(dry, delayed, s.mix);
        }
        for (std::size_t channel = audio.size(); channel < channelCount_;
             ++channel) {
          chorusBuffer_[channel][chorusWrite_] = 0.0f;
          chorusValid_[channel] = std::min(chorusValid_[channel] + 1,
                                           chorusBuffer_[channel].size());
        }
        chorusWrite_ = (chorusWrite_ + 1) % chorusBuffer_[0].size();
        chorusPhase_ = advance(chorusPhase_, increment);
    }
}

void EffectsChain::processBitcrusher(std::span<float*> audio, std::uint32_t n,
                                     const EffectSettings& s) noexcept
{
    const auto hold = 1u + static_cast<std::uint32_t>(s.rate * s.rate * 31.0f);
    const float levels = std::exp2(4.0f + (1.0f - s.amount) * 12.0f);
    for (std::size_t channel = 0; channel < audio.size(); ++channel)
        for (std::uint32_t i = 0; i < n; ++i) {
            const float dry = audio[channel][i];
            if (bitCounter_[channel]++ % hold == 0)
                bitHold_[channel] = std::round(dry * levels) / levels;
            const float wet = bitHold_[channel]
                * (1.0f - 0.18f * s.character);
            audio[channel][i] = wetDry(dry, wet, s.mix);
        }
}

void EffectsChain::processDelay(std::span<float*> audio, std::uint32_t n,
                                const EffectSettings& s) noexcept
{
    const float delaySamples = static_cast<float>(sampleRate_)
        * (0.025f + 1.475f * s.rate * s.rate);
    const float feedback = 0.08f + 0.78f * s.amount;
    const float damping = 0.05f + 0.9f * s.character;
    for (std::uint32_t i = 0; i < n; ++i) {
        for (std::size_t channel = 0; channel < audio.size(); ++channel) {
            const float dry = audio[channel][i];
            const float tapped =
                readDelay(delayBuffer_[channel], delayWrite_,
                          delaySamples * (channel == 0 ? 1.0f : 0.755f),
                          delayValid_[channel]);
            delayDamping_[channel] += damping
                * (tapped - delayDamping_[channel]);
            delayBuffer_[channel][delayWrite_] = sanitize(
                dry + delayDamping_[channel] * feedback);
            delayValid_[channel] = std::min(delayValid_[channel] + 1,
                                            delayBuffer_[channel].size());
            audio[channel][i] = wetDry(dry, delayDamping_[channel], s.mix);
        }
        for (std::size_t channel = audio.size(); channel < channelCount_;
             ++channel) {
          delayBuffer_[channel][delayWrite_] = 0.0f;
          delayValid_[channel] =
              std::min(delayValid_[channel] + 1, delayBuffer_[channel].size());
        }
        delayWrite_ = (delayWrite_ + 1) % delayBuffer_[0].size();
    }
}

void EffectsChain::processReverb(std::span<float*> audio, std::uint32_t n,
                                 const EffectSettings& s) noexcept
{
    const float room = 0.035f + 0.145f * s.amount;
    const float feedback = 0.45f + 0.46f * s.amount;
    const float damping = 0.04f + 0.55f * (1.0f - s.character);
    constexpr std::array<float, 4> taps { 1.0f, 0.817f, 0.613f, 0.431f };
    for (std::uint32_t i = 0; i < n; ++i) {
        for (std::size_t channel = 0; channel < audio.size(); ++channel) {
            const float dry = audio[channel][i];
            float wet = 0.0f;
            for (const float tap : taps)
              wet += readDelay(reverbBuffer_[channel], reverbWrite_,
                               static_cast<float>(sampleRate_) * room * tap,
                               reverbValid_[channel]) *
                     0.25f;
            reverbDamping_[channel] += damping
                * (wet - reverbDamping_[channel]);
            const float cross =
                audio.size() > 1
                    ? readDelay(reverbBuffer_[1 - channel], reverbWrite_,
                                static_cast<float>(sampleRate_) * room * 0.727f,
                                reverbValid_[1 - channel])
                    : 0.0f;
            reverbBuffer_[channel][reverbWrite_] = sanitize(
                dry + (reverbDamping_[channel] + 0.17f * cross) * feedback);
            reverbValid_[channel] = std::min(reverbValid_[channel] + 1,
                                             reverbBuffer_[channel].size());
            audio[channel][i] = wetDry(dry, wet, s.mix);
        }
        for (std::size_t channel = audio.size(); channel < channelCount_;
             ++channel) {
          reverbBuffer_[channel][reverbWrite_] = 0.0f;
          reverbValid_[channel] = std::min(reverbValid_[channel] + 1,
                                           reverbBuffer_[channel].size());
        }
        reverbWrite_ = (reverbWrite_ + 1) % reverbBuffer_[0].size();
    }
}

void EffectsChain::process(std::span<float*> audio,
                           std::uint32_t sampleCount) noexcept
{
    if (!prepared_ || audio.empty() || audio.size() > channelCount_
        || sampleCount > maximumBlockSize_)
        return;
    for (std::size_t index = 0; index < effectCount; ++index) {
        const auto settings = state_.effects[index];
        if (!settings.enabled || settings.mix <= 0.0f) continue;
        switch (settings.type) {
        case EffectType::distortion: processDistortion(audio, sampleCount, settings); break;
        case EffectType::wavefolder: processWavefolder(audio, sampleCount, settings); break;
        case EffectType::phaser: processPhaser(audio, sampleCount, settings); break;
        case EffectType::flanger: processFlanger(audio, sampleCount, settings); break;
        case EffectType::chorus: processChorus(audio, sampleCount, settings); break;
        case EffectType::bitcrusher: processBitcrusher(audio, sampleCount, settings); break;
        case EffectType::delay: processDelay(audio, sampleCount, settings); break;
        case EffectType::reverb: processReverb(audio, sampleCount, settings); break;
        }
    }
    for (auto* channel : audio)
        for (std::uint32_t sample = 0; sample < sampleCount; ++sample)
            channel[sample] = sanitize(channel[sample]);
}

bool EffectsChain::saveState(std::span<std::byte> destination,
                             std::uint32_t& written) const noexcept
{
    written = 0;
    if (destination.size() < encodedStateBytes) return false;
    writeU32(destination.data(), stateMagic);
    writeU32(destination.data() + 4, stateVersion);
    std::size_t offset = 8;
    for (const auto& effect : state_.effects) {
        writeU32(destination.data() + offset,
                 static_cast<std::uint32_t>(effect.type)); offset += 4;
        writeU32(destination.data() + offset, effect.enabled ? 1u : 0u); offset += 4;
        for (const float value : { effect.mix, effect.amount, effect.rate,
                                  effect.character }) {
            writeFloat(destination.data() + offset, value); offset += 4;
        }
    }
    written = static_cast<std::uint32_t>(offset);
    return true;
}

bool EffectsChain::loadState(std::uint32_t schema,
                             std::span<const std::byte> payload) noexcept
{
    if (schema != stateVersion || payload.size() != encodedStateBytes
        || readU32(payload.data()) != stateMagic
        || readU32(payload.data() + 4) != stateVersion)
        return false;
    ChainState candidate;
    std::size_t offset = 8;
    for (std::size_t index = 0; index < effectCount; ++index) {
        auto& effect = candidate.effects[index];
        effect.type = static_cast<EffectType>(readU32(payload.data() + offset));
        offset += 4;
        const auto enabled = readU32(payload.data() + offset); offset += 4;
        if (enabled > 1) return false;
        effect.enabled = enabled != 0;
        effect.mix = readFloat(payload.data() + offset); offset += 4;
        effect.amount = readFloat(payload.data() + offset); offset += 4;
        effect.rate = readFloat(payload.data() + offset); offset += 4;
        effect.character = readFloat(payload.data() + offset); offset += 4;
        if (!valid(effect, index)) return false;
    }
    state_ = candidate;
    return true;
}

bool EffectsChain::applyPreset(std::string_view id) noexcept
{
    auto candidate = EffectsChain().state();
    if (id == "clean") {
    } else if (id == "psy-drive") {
        candidate.effects[0] = setting(EffectType::distortion, true, .62f, .58f, .5f, .42f);
        candidate.effects[1] = setting(EffectType::wavefolder, true, .20f, .28f, .5f, .55f);
    } else if (id == "wide-motion") {
        candidate.effects[2] = setting(EffectType::phaser, true, .28f, .52f, .24f, .5f);
        candidate.effects[3] = setting(EffectType::flanger, true, .18f, .32f, .17f, .38f);
        candidate.effects[4] = setting(EffectType::chorus, true, .42f, .55f, .22f, .5f);
    } else if (id == "lo-fi") {
        candidate.effects[0] = setting(EffectType::distortion, true, .35f, .42f, .5f, .7f);
        candidate.effects[5] = setting(EffectType::bitcrusher, true, .72f, .68f, .58f, .35f);
    } else if (id == "deep-space") {
        candidate.effects[6] = setting(EffectType::delay, true, .34f, .48f, .42f, .35f);
        candidate.effects[7] = setting(EffectType::reverb, true, .38f, .72f, .5f, .62f);
    } else {
        return false;
    }
    state_ = candidate;
    reset();
    return true;
}

std::uint32_t EffectsChain::tailSamples() const noexcept
{
    if (state_.effects[7].enabled && state_.effects[7].mix > 0.0f)
        return static_cast<std::uint32_t>(sampleRate_ * 8.0);
    if (state_.effects[6].enabled && state_.effects[6].mix > 0.0f)
        return static_cast<std::uint32_t>(sampleRate_ * 6.0);
    return 0;
}

} // namespace vstengine::effects
