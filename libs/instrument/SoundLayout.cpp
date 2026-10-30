#include "instrument/SoundLayout.h"

#include "instrument/InstrumentContract.h"

#include <set>

namespace vstengine::instrument {
namespace {
bool fail(std::string &why, std::string text) {
  why = std::move(text);
  return false;
}

bool validTitle(const std::string &text) {
  if (text.empty() || text.size() > maxSoundTitle)
    return false;
  for (const auto c : text)
    if (static_cast<unsigned char>(c) < 0x20 || c == 0x7f)
      return false;
  return true;
}

bool kindMatches(SoundControlKind kind, ParameterType type) {
  switch (kind) {
  case SoundControlKind::toggle:
    return type == ParameterType::boolean;
  case SoundControlKind::selector:
    return type == ParameterType::choice;
  case SoundControlKind::knob:
    return type == ParameterType::floating || type == ParameterType::integer;
  }
  return false;
}
} // namespace

bool validateSoundLayout(const SoundLayout &layout, const InstrumentDescriptor &descriptor,
                         std::string &why) {
  if (!descriptor.directParameters)
    return fail(why, "sound layout: the instrument has no direct parameters");
  if (layout.sections.empty() || layout.sections.size() > maxSoundSections)
    return fail(why, "sound layout: 1.." + std::to_string(maxSoundSections) + " sections");
  std::set<std::string> seen;
  for (const auto &section : layout.sections) {
    if (!validTitle(section.title))
      return fail(why, "sound layout: section title must be 1.." +
                           std::to_string(maxSoundTitle) + " printable characters");
    if (section.controls.empty())
      return fail(why, "sound layout: section '" + section.title + "' has no controls");
    for (const auto &control : section.controls) {
      const ParameterDescriptor *parameter = nullptr;
      for (const auto &p : descriptor.parameters)
        if (p.id == control.parameter)
          parameter = &p;
      if (parameter == nullptr || parameter->poolIndex < 0 ||
          parameter->poolIndex >= static_cast<std::int16_t>(poolParametersPerSlot))
        return fail(why, "sound layout: '" + control.parameter +
                             "' is not a direct parameter of " + descriptor.id);
      if (!kindMatches(control.kind, parameter->type))
        return fail(why, "sound layout: control kind does not match the type of '" +
                             control.parameter + "'");
      if (!control.label.empty() && !validTitle(control.label))
        return fail(why, "sound layout: label of '" + control.parameter + "' must be 1.." +
                             std::to_string(maxSoundTitle) + " printable characters");
      if (!seen.insert(control.parameter).second)
        return fail(why, "sound layout: '" + control.parameter + "' appears twice");
    }
  }
  for (const auto &p : descriptor.parameters)
    if (p.poolIndex >= 0 && seen.count(p.id) == 0)
      return fail(why, "sound layout: direct parameter '" + p.id + "' has no control");
  return true;
}

} // namespace vstengine::instrument
