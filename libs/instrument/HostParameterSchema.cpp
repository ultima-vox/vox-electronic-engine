#include "instrument/HostParameterSchema.h"
#include <cstdio>
#include <stdexcept>
#include <unordered_set>

namespace vstengine::instrument::hostparams {
namespace {
void validate(std::size_t slotIndex, std::size_t macroIndex)
{
    if (slotIndex >= maxSlots || macroIndex >= macrosPerSlot)
        throw std::out_of_range("slot macro index");
}
} // namespace

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

std::string directId(std::size_t slotIndex, std::string_view instrumentId,
                     std::string_view parameterId) {
  if (slotIndex >= maxSlots || instrumentId.empty() || parameterId.empty())
    throw std::out_of_range("direct parameter identity");
  char prefix[24]{};
  std::snprintf(prefix, sizeof(prefix), "slot%02zu.sound.", slotIndex + 1);
  return std::string(prefix) + std::string(instrumentId) + "." +
         std::string(parameterId);
}

bool validateDirectCatalog(std::span<const InstrumentDescriptor *const> catalog,
                           std::string &diagnostic) {
  std::unordered_set<std::uint64_t> keys;
  std::unordered_set<std::string> names;
  for (const auto *descriptor : catalog)
    if (descriptor != nullptr && descriptor->directParameters) {
      if (descriptor->parameters.empty() ||
          descriptor->parameters.size() > maxDirectParameters) {
        diagnostic = "direct parameter count exceeds slot bound";
        return false;
      }
      for (const auto &parameter : descriptor->parameters) {
        if (!validDirectParameter(parameter)) {
          diagnostic = "invalid direct parameter metadata";
          return false;
        }
        const auto key = directKey(descriptor->id, parameter.id);
        if (key == 0 || !keys.insert(key).second ||
            !names.insert(directId(0, descriptor->id, parameter.id)).second ||
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
