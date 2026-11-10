#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace vstengine::effects {

enum class EffectType : std::uint32_t {
    distortion, wavefolder, phaser, flanger, chorus, bitcrusher, delay, reverb
};

struct EffectSettings {
    EffectType type {};
    bool enabled {};
    float mix { 0.0f };
    float amount { 0.5f };
    float rate { 0.5f };
    float character { 0.5f };
};

struct ChainState {
  static constexpr std::uint32_t schemaVersion = 2;
  std::array<EffectSettings, 8> effects{};
};

class EffectsChain final {
public:
    static constexpr std::uint32_t stateVersion = ChainState::schemaVersion;
    static constexpr std::size_t effectCount = 8;
    static constexpr std::size_t encodedStateBytes = 8 + effectCount * 24;

    EffectsChain() noexcept;
    bool prepare(double sampleRate, std::uint32_t maximumBlockSize,
                 std::uint32_t channels) noexcept;
    void reset() noexcept;
    void process(std::span<float*> channels, std::uint32_t sampleCount) noexcept;

    bool setEffect(std::size_t index, const EffectSettings&) noexcept;
    [[nodiscard]] ChainState state() const noexcept { return state_; }
    bool loadState(std::uint32_t schemaVersion,
                   std::span<const std::byte> payload) noexcept;
    bool saveState(std::span<std::byte> destination,
                   std::uint32_t& bytesWritten) const noexcept;
    bool applyPreset(std::string_view presetId) noexcept;

    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }
    [[nodiscard]] std::uint32_t latencySamples() const noexcept { return 0; }
    [[nodiscard]] std::uint32_t tailSamples() const noexcept;

private:
  void resetEffect(std::size_t) noexcept;
  std::vector<float> tailGain_;
  std::vector<std::uint8_t> resetAfter_;
  std::uint32_t tailRemaining_{};
  using StereoBuffer = std::array<std::vector<float>, 2>;
  static bool valid(const EffectSettings &, std::size_t expectedIndex) noexcept;
  void processDistortion(std::span<float *>, std::uint32_t,
                         const EffectSettings &) noexcept;
  void processWavefolder(std::span<float *>, std::uint32_t,
                         const EffectSettings &) noexcept;
  void processPhaser(std::span<float *>, std::uint32_t,
                     const EffectSettings &) noexcept;
  void processFlanger(std::span<float *>, std::uint32_t,
                      const EffectSettings &) noexcept;
  void processChorus(std::span<float *>, std::uint32_t,
                     const EffectSettings &) noexcept;
  void processBitcrusher(std::span<float *>, std::uint32_t,
                         const EffectSettings &) noexcept;
  void processDelay(std::span<float *>, std::uint32_t,
                    const EffectSettings &) noexcept;
  void processReverb(std::span<float *>, std::uint32_t,
                     const EffectSettings &) noexcept;
  static float readDelay(const std::vector<float> &, std::size_t write,
                         float delaySamples, std::size_t validSamples) noexcept;

  ChainState state_;
  double sampleRate_{48000.0};
  std::uint32_t maximumBlockSize_{};
  std::uint32_t channelCount_{};
  StereoBuffer flangerBuffer_;
  StereoBuffer chorusBuffer_;
  StereoBuffer delayBuffer_;
  StereoBuffer reverbBuffer_;
  // Sequential ring writes after reset form [0, validSamples). Reads outside
  // that interval are zero until the first wrap. Reset is O(1), no bulk
  // clear.
  std::array<std::size_t, 2> flangerValid_{}, chorusValid_{}, delayValid_{},
      reverbValid_{};
  std::size_t flangerWrite_{};
  std::size_t chorusWrite_{};
  std::size_t delayWrite_{};
  std::size_t reverbWrite_{};
  std::array<std::array<float, 4>, 2> phaserState_{};
  std::array<float, 2> bitHold_{};
  std::array<std::uint32_t, 2> bitCounter_{};
  std::array<float, 2> delayDamping_{};
  std::array<float, 2> reverbDamping_{};
  double phaserPhase_{};
  double flangerPhase_{};
  double chorusPhase_{};
  bool prepared_{};
};

} // namespace vstengine::effects
