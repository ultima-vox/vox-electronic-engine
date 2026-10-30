#include "instrument/HostParameterSchema.h"
#include <cstdio>
#include <stdexcept>
#include <array>
#include <unordered_set>
#include <utility>

namespace vstengine::instrument::hostparams {
namespace {
void validate(std::size_t slotIndex, std::size_t macroIndex)
{
    if (slotIndex >= maxSlots || macroIndex >= macrosPerSlot)
        throw std::out_of_range("slot macro index");
}
} // namespace

std::uint32_t parameterSpecHash(const ParameterDescriptor &p) noexcept {
  std::uint32_t hash = 2166136261u;
  const auto bytes = [&hash](const void *data, std::size_t size) {
    const auto *b = static_cast<const unsigned char *>(data);
    for (std::size_t i = 0; i < size; ++i) {
      hash ^= b[i];
      hash *= 16777619u;
    }
  };
  const auto text = [&](const std::string &s) {
    const auto n = static_cast<std::uint32_t>(s.size());
    bytes(&n, sizeof n);
    bytes(s.data(), s.size());
  };
  const auto type = static_cast<std::uint32_t>(p.type);
  bytes(&type, sizeof type);
  bytes(&p.minimum, sizeof p.minimum);
  bytes(&p.maximum, sizeof p.maximum);
  bytes(&p.step, sizeof p.step);
  text(p.unit);
  const auto count = static_cast<std::uint32_t>(p.choices.size());
  bytes(&count, sizeof count);
  for (const auto &choice : p.choices)
    text(choice);
  return hash == 0 ? 1u : hash;
}

std::string macroId(std::size_t slotIndex, std::size_t macroIndex)
{
    validate(slotIndex, macroIndex);
    std::array<char, 32> value {};
    std::snprintf(value.data(), value.size(), "slot%02zuMacro%02zu",
                  slotIndex + 1, macroIndex + 1);
    return value.data();
}

std::string macroName(std::size_t slotIndex, std::size_t macroIndex)
{
    validate(slotIndex, macroIndex);
    return "Slot " + std::to_string(slotIndex + 1) + " "
         + std::string(macroLabels[macroIndex]);
}

std::string poolId(std::size_t slotIndex, std::size_t poolIndex) {
  if (slotIndex >= maxSlots || poolIndex >= poolParametersPerSlot)
    throw std::out_of_range("pool parameter identity");
  char id[16]{};
  std::snprintf(id, sizeof(id), "slot%02zu.p%03zu", slotIndex + 1, poolIndex + 1);
  return id;
}

bool isPoolId(std::string_view id) noexcept {
  const auto digits = [&](std::size_t from, std::size_t count) {
    for (std::size_t i = from; i < from + count; ++i)
      if (id[i] < '0' || id[i] > '9')
        return false;
    return true;
  };
  if (id.size() != 11 || !id.starts_with("slot") || id.substr(6, 2) != ".p" ||
      !digits(4, 2) || !digits(8, 3))
    return false;
  const auto slot = (id[4] - '0') * 10 + (id[5] - '0');
  const auto index = (id[8] - '0') * 100 + (id[9] - '0') * 10 + (id[10] - '0');
  return slot >= 1 && slot <= static_cast<int>(maxSlots) && index >= 1 &&
         index <= static_cast<int>(poolParametersPerSlot);
}

bool validateDirectCatalog(std::span<const InstrumentDescriptor *const> catalog,
                           std::string &diagnostic) {
  std::unordered_set<std::uint64_t> keys;
  for (const auto *descriptor : catalog)
    if (descriptor != nullptr && descriptor->directParameters) {
      if (descriptor->parameters.empty() ||
          descriptor->parameters.size() > maxDirectParameters) {
        diagnostic = "direct parameter count exceeds slot bound";
        return false;
      }
      std::array<bool, poolParametersPerSlot> pool{};
      for (const auto &parameter : descriptor->parameters) {
        if (!validDirectParameter(parameter)) {
          diagnostic = "invalid direct parameter metadata";
          return false;
        }
        if (std::exchange(pool[static_cast<std::size_t>(parameter.poolIndex)],
                          true)) {
          diagnostic = "duplicate direct parameter pool index";
          return false;
        }
        const auto key = directKey(descriptor->id, parameter.id);
        if (key == 0 || !keys.insert(key).second ||
            keys.size() > maxHostDirectParameters) {
          diagnostic = "direct catalog key collision or host bound exceeded";
          return false;
        }
      }
    }
  diagnostic.clear();
  return true;
}
} // namespace vstengine::instrument::hostparams
