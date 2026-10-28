#include "bass/FullOnBass.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace vstengine::bass {
namespace {
static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
constexpr std::size_t stateBytes = fullOnParameters.size() * sizeof(float);
static_assert(fullOnParameters.size() < 32);
constexpr std::uint32_t allParameters = (1u << fullOnParameters.size()) - 1u;
auto defaults() noexcept
{
    std::array<float, fullOnParameters.size()> result {};
    for (std::size_t i = 0; i < result.size(); ++i)
        result[i] = fullOnParameters[i].initial;
    return result;
}
}

FullOnBassInstance::FullOnBassInstance() noexcept : values(defaults()) {}

instrument::InstrumentDescriptor FullOnBassInstance::descriptor()
{
    instrument::InstrumentDescriptor result;
    result.id = fullOnInstrumentId;
    result.providerId = "com.ultimavox.builtin";
    result.name = "Full-on Bass";
    result.vendor = "Ultima Vox";
    result.capabilities = instrument::Capability::notes;
    result.budget = { 1, 2048, static_cast<std::uint32_t>(stateBytes),
                      0, 0, 48000, 2048, 0, 0 };
    for (const auto& parameter : fullOnParameters)
        result.parameters.push_back({ std::string(parameter.id),
            std::string(parameter.name), std::string(parameter.unit),
            parameter.minimum, parameter.maximum, parameter.initial, 0,
            instrument::ParameterType::floating, std::string(parameter.group),
            {}, true, false, -1 });
    return result;
}

bool FullOnBassInstance::prepare(const instrument::PrepareSpec& next)
{
    if (!std::isfinite(next.sampleRate) || next.sampleRate < 44100
        || next.sampleRate > 192000 || next.maximumBlockSize == 0
        || next.maximumBlockSize > 131072
        || next.outputChannels == 0 || next.outputChannels > 2)
        return false;
    spec = next;
    voice.prepareDirect(spec.sampleRate);
    dirtyParameters = allParameters;
    reset();
    return true;
}

void FullOnBassInstance::reset() noexcept
{
    voice.resetDirect();
    currentNote = currentChannel = -1;
    sustainDown.fill(false);
    keyHeld = false;
}
void FullOnBassInstance::suspend() noexcept { suspended = true; reset(); }
void FullOnBassInstance::resume() noexcept { suspended = false; }
void FullOnBassInstance::setBypassed(bool next) noexcept
{
    if (next && !bypassed) reset();
    bypassed = next;
}

bool FullOnBassInstance::setParameter(std::string_view id, float value) noexcept
{
    for (std::size_t i = 0; i < values.size(); ++i)
        if (fullOnParameters[i].id == id)
            return setParameterByIndex(i, value);
    return false;
}

bool FullOnBassInstance::setParameterByIndex(std::size_t index, float value) noexcept
{
    if (index >= values.size() || !std::isfinite(value)) return false;
    const auto clamped = std::clamp(value, fullOnParameters[index].minimum,
                                    fullOnParameters[index].maximum);
    if (values[index] != clamped) {
        values[index] = clamped;
        dirtyParameters |= 1u << index;
    }
    return true;
}

bool FullOnBassInstance::loadState(std::uint32_t schema,
                                 std::span<const std::byte> payload) noexcept
{
    if (schema != 1 || (!payload.empty() && payload.size() != stateBytes))
        return false;
    auto candidate = defaults();
    if (!payload.empty())
        for (std::size_t i = 0; i < candidate.size(); ++i) {
            std::uint32_t bits = 0;
            for (std::size_t byte = 0; byte < 4; ++byte)
                bits |= std::to_integer<std::uint32_t>(payload[i * 4 + byte])
                    << (byte * 8);
            candidate[i] = std::bit_cast<float>(bits);
            if (!std::isfinite(candidate[i])
                || candidate[i] < fullOnParameters[i].minimum
                || candidate[i] > fullOnParameters[i].maximum)
                return false;
        }
    values = candidate;
    dirtyParameters = allParameters;
    return true;
}

bool FullOnBassInstance::saveState(std::span<std::byte> destination,
                                 std::uint32_t& written) const noexcept
{
    written = 0;
    if (destination.size() < stateBytes) return false;
    for (std::size_t i = 0; i < values.size(); ++i) {
        const auto bits = std::bit_cast<std::uint32_t>(values[i]);
        for (std::size_t byte = 0; byte < 4; ++byte)
            destination[i * 4 + byte] = static_cast<std::byte>(
                (bits >> (byte * 8)) & 0xffu);
    }
    written = static_cast<std::uint32_t>(stateBytes);
    return true;
}

void FullOnBassInstance::updateVoice() noexcept
{
    if (dirtyParameters & (1u << 0)) voice.setFilterCutoff(std::log(values[0] / 35.0f)
        / std::log(12000.0f / 35.0f));
    if (dirtyParameters & (1u << 1)) voice.setFilterResonance(values[1]);
    if (dirtyParameters & (1u << 2)) voice.setPitchEnvelopeAmount(values[2]);
    if (dirtyParameters & (1u << 3)) voice.setPitchEnvelopeTime(values[3] * .001f);
    if (dirtyParameters & (1u << 4)) voice.setAmpAttack(values[4] * .001f);
    if (dirtyParameters & (1u << 5)) voice.setAmpDecay(values[5] * .001f);
    if (dirtyParameters & (1u << 6)) voice.setAmpSustain(values[6]);
    if (dirtyParameters & (1u << 7)) voice.setAmpRelease(values[7] * .001f);
    if (dirtyParameters & (1u << 8)) voice.setDrive(values[8]);
    if (dirtyParameters & (1u << 9)) voice.setFilterDrive(values[9]);
    if (dirtyParameters & (1u << 10)) voice.setKeyTracking(values[10]);
    if (dirtyParameters & (1u << 11)) voice.setOutputLevel(std::pow(10.0f, values[11] / 20.0f));
    if (dirtyParameters & (1u << 12)) voice.setPitchEnvelopeCurve(values[12]);
    dirtyParameters = 0;
}

std::uint32_t FullOnBassInstance::tailSamples() const noexcept
{
    return static_cast<std::uint32_t>(std::ceil(spec.sampleRate * .25));
}

void FullOnBassInstance::process(const instrument::ProcessBlock& block) noexcept
{
    if (bypassed || suspended || spec.sampleRate == 0
        || block.sampleCount > spec.maximumBlockSize
        || block.outputs.size() != spec.outputChannels
        || block.midi.size() > 2048) return;
    for (auto* output : block.outputs) if (output == nullptr) return;
    if (dirtyParameters != 0) {
        updateVoice();
    }
    juce::AudioBuffer<float> audio(block.outputs.data(),
        static_cast<int>(block.outputs.size()), static_cast<int>(block.sampleCount));
    std::uint32_t cursor = 0;
    for (const auto& event : block.midi) {
        // Invalid or unordered events must never replay an already-rendered
        // section, release a different note, or escape MIDI's 7-bit domain.
        if (event.sampleOffset < cursor || event.sampleOffset >= block.sampleCount
            || event.size != 3 || event.data[1] > 127 || event.data[2] > 127)
            continue;
        const auto type = event.data[0] & 0xf0;
        if (type != 0x90 && type != 0x80 && type != 0xb0) continue;
        if (event.sampleOffset > cursor)
            voice.renderNextBlock(audio, static_cast<int>(cursor),
                static_cast<int>(event.sampleOffset - cursor));
        cursor = event.sampleOffset;
        if (type == 0x90 && event.data[2] != 0) {
            currentNote = event.data[1];
            currentChannel = event.data[0] & 0x0f;
            keyHeld = true;
            voice.noteOnDirect(currentNote, event.data[2] / 127.0f);
        } else if ((type == 0x80 || type == 0x90)
                   && currentNote == event.data[1]
                   && currentChannel == (event.data[0] & 0x0f)) {
            keyHeld = false;
            if (!sustainDown[static_cast<std::size_t>(currentChannel)]) {
                voice.noteOffDirect(true);
                currentNote = -1;
            }
        } else if (type == 0xb0 && event.data[1] == 64) {
            const auto channel = static_cast<std::size_t>(event.data[0] & 0x0f);
            sustainDown[channel] = event.data[2] >= 64;
            if (!sustainDown[channel] && currentChannel == static_cast<int>(channel)
                && currentNote >= 0 && !keyHeld) {
                voice.noteOffDirect(true);
                currentNote = -1;
            }
        } else if (type == 0xb0 && (event.data[1] == 120 || event.data[1] == 123)) {
            const auto channel = static_cast<std::size_t>(event.data[0] & 0x0f);
            sustainDown[channel] = false;
            if (currentChannel == static_cast<int>(channel)) {
                voice.resetDirect();
                currentNote = currentChannel = -1;
                keyHeld = false;
            }
        }
    }
    if (cursor < block.sampleCount)
        voice.renderNextBlock(audio, static_cast<int>(cursor),
            static_cast<int>(block.sampleCount - cursor));
}
} // namespace vstengine::bass
