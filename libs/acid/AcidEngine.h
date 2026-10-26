#pragma once

#include "instrument/InstrumentAbi.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace vstengine::acid {

class AcidEngine final {
public:
    static constexpr std::uint32_t stateVersion = 1;
    static constexpr std::size_t parameterCount = 9;
    static constexpr std::size_t encodedStateBytes = 8 + parameterCount * 4;

    bool prepare(double sampleRate, std::uint32_t maximumBlockSize,
                 std::uint32_t outputChannels) noexcept;
    void reset() noexcept;
    void process(std::span<float*> outputs, std::uint32_t sampleCount,
                 std::span<const VoxMidiEventV1> midi) noexcept;

    bool setParameter(std::string_view id, float normalizedValue) noexcept;
    [[nodiscard]] float parameter(std::string_view id) const noexcept;

    bool loadState(std::uint32_t schemaVersion,
                   std::span<const std::byte> payload) noexcept;
    bool saveState(std::span<std::byte> destination,
                   std::uint32_t& bytesWritten) const noexcept;

    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }
    [[nodiscard]] bool isActive() const noexcept { return active_; }
    [[nodiscard]] std::uint32_t latencySamples() const noexcept { return 0; }
    [[nodiscard]] std::uint32_t tailSamples() const noexcept;

private:
    enum Parameter : std::size_t {
        waveform, cutoff, resonance, envelopeAmount, decay,
        accent, slide, drive, output, count
    };

    void noteOn(std::uint8_t note, std::uint8_t velocity) noexcept;
    void noteOff(std::uint8_t note) noexcept;
    void allNotesOff() noexcept;
    void pitchBend(std::uint8_t lsb, std::uint8_t msb) noexcept;
    [[nodiscard]] float oscillator(double increment) noexcept;
    [[nodiscard]] float filter(float input, float cutoffHz,
                               float resonanceAmount) noexcept;
    [[nodiscard]] static double midiFrequency(float note) noexcept;
    [[nodiscard]] static float polyBlep(double phase,
                                        double increment) noexcept;

    static constexpr std::array<std::string_view, parameterCount> parameterIds {
        "waveform", "cutoff", "resonance", "envelope", "decay",
        "accent", "slide", "drive", "output"
    };
    std::array<float, parameterCount> targets_ {
        0.05f, 0.46f, 0.72f, 0.70f, 0.48f, 0.58f, 0.30f, 0.28f, 0.72f
    };
    std::array<float, parameterCount> smoothed_ = targets_;

    double sampleRate_ { 48000.0 };
    std::uint32_t maximumBlockSize_ {};
    std::uint32_t outputChannels_ {};
    double phase_ {};
    double currentFrequency_ { 110.0 };
    double targetFrequency_ { 110.0 };
    float pitchBendSemitones_ {};
    float filterEnvelope_ {};
    float ampEnvelope_ {};
    float velocityGain_ { 1.0f };
    float accentGain_ {};
    float portamentoSeconds_ { 0.08f };
    std::array<float, 4> filterState_ {};
    std::uint8_t currentNote_ { 255 };
    bool gate_ {};
    bool portamentoEnabled_ {};
    bool active_ {};
    bool prepared_ {};
};

} // namespace vstengine::acid
