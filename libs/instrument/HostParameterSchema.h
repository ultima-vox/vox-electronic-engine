#pragma once
#include "instrument/InstrumentContract.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <string_view>

namespace vstengine::instrument::hostparams {

inline constexpr std::array<std::string_view, macrosPerSlot> macroLabels {
    "Cutoff", "Resonance", "Envelope", "Decay",
    "Drive", "Accent", "Space", "Motion"
};

inline bool validDirectParameter(const ParameterDescriptor &p) noexcept {
  if (p.id.empty() || p.id.size() >= VOX_INSTRUMENT_MAX_ID_BYTES ||
      p.name.empty() || p.name.size() > 256 || p.unit.size() > 32 ||
      p.group.size() > 128 || !std::isfinite(p.minimum) ||
      !std::isfinite(p.maximum) || !std::isfinite(p.defaultValue) ||
      !std::isfinite(p.step) || p.minimum >= p.maximum ||
      p.defaultValue < p.minimum || p.defaultValue > p.maximum || p.step < 0 ||
      static_cast<double>(p.maximum) - p.minimum >
          std::numeric_limits<float>::max() ||
      p.step > p.maximum - p.minimum || p.preferredMacro != -1 ||
      !p.automatable ||
      static_cast<unsigned>(p.type) >
          static_cast<unsigned>(ParameterType::choice))
    return false;
  if (p.type != ParameterType::floating &&
      (std::floor(p.minimum) != p.minimum ||
       std::floor(p.maximum) != p.maximum ||
       std::floor(p.defaultValue) != p.defaultValue ||
       static_cast<double>(p.minimum) < -16777215.0 ||
       static_cast<double>(p.maximum) > 16777215.0 ||
       (p.step != 0 && p.step != 1)))
    return false;
  if (p.type == ParameterType::boolean && (p.minimum != 0 || p.maximum != 1))
    return false;
  if (p.type == ParameterType::choice) {
    if (p.minimum != 0 || p.choices.size() > 256 ||
        p.choices.size() != static_cast<std::size_t>(p.maximum) + 1)
      return false;
    for (const auto &choice : p.choices)
      if (choice.empty() || choice.size() > 128)
        return false;
  }
  return true;
}

inline float canonicalPlain(float value,
                            const ParameterDescriptor &parameter) noexcept {
  if (!std::isfinite(value))
    value = parameter.defaultValue;
  value = std::clamp(value, parameter.minimum, parameter.maximum);
  const double quantum =
      parameter.type == ParameterType::floating ? parameter.step : 1.0;
  if (quantum > 0.0) {
    const auto ticks =
        std::round((static_cast<double>(value) - parameter.minimum) / quantum);
    value = static_cast<float>(parameter.minimum + ticks * quantum);
  }
  return std::clamp(value, parameter.minimum, parameter.maximum);
}

std::string macroId(std::size_t slotIndex, std::size_t macroIndex);
std::string macroName(std::size_t slotIndex, std::size_t macroIndex);
// Host ID of pool endpoint `poolIndex` (0-based) in a slot: "slotNN.pKKK".
std::string poolId(std::size_t slotIndex, std::size_t poolIndex);
[[nodiscard]] bool isPoolId(std::string_view id) noexcept;
bool validateDirectCatalog(std::span<const InstrumentDescriptor *const>,
                           std::string &diagnostic);
// Stable across platforms and catalog order. Zero is reserved for unused
// entries.
inline constexpr std::uint64_t
directKey(std::string_view instrumentId,
          std::string_view parameterId) noexcept {
  std::uint64_t result = 14695981039346656037ULL;
  for (unsigned char c : instrumentId) {
    result ^= c;
    result *= 1099511628211ULL;
  }
  result ^= 0;
  result *= 1099511628211ULL;
  for (unsigned char c : parameterId) {
    result ^= c;
    result *= 1099511628211ULL;
  }
  return result;
}

} // namespace vstengine::instrument::hostparams
