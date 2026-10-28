#include "rack/RackMigration.h"
#include <cmath>

namespace vstengine::rack {
namespace {
const instrument::ParameterDescriptor *
find(const instrument::InstrumentDescriptor &d, std::string_view id) {
  for (const auto &p : d.parameters)
    if (p.id == id)
      return &p;
  return nullptr;
}

bool validValue(float value, const instrument::ParameterDescriptor &p) {
  if (!std::isfinite(value) || value < p.minimum || value > p.maximum)
    return false;
  return p.type == instrument::ParameterType::floating ||
         value == std::round(value);
}
} // namespace

DirectMigration migrateAppendOnly(PersistentSlotState &state,
                                  const instrument::InstrumentDescriptor &current) {
  if (state.instrumentId != current.id || state.instrumentVersion == 0 ||
      state.instrumentVersion == current.instrumentVersion)
    return DirectMigration::notNeeded;
  if (!current.directParameters ||
      state.instrumentVersion > current.instrumentVersion ||
      (state.contentVersion != 0 &&
       state.contentVersion != current.contentVersion) ||
      state.instrumentStateVersion > current.stateVersion ||
      state.patternSchemaVersion > VOX_PATTERN_SCHEMA_V1 ||
      state.modulePayload.size() > current.budget.maxStateBytes ||
      state.directParameters.empty() ||
      state.directParameters.size() > current.parameters.size())
    return DirectMigration::incompatible;
  for (const auto &saved : state.directParameters) {
    const auto *p = find(current, saved.id);
    if (p == nullptr || saved.poolIndex < 0 ||
        saved.poolIndex != p->poolIndex || !validValue(saved.value, *p))
      return DirectMigration::incompatible;
  }
  // Duplicate saved IDs are rejected by state validation before this point;
  // with every saved ID present, the remainder are exactly the new parameters.
  auto migrated = state.directParameters;
  for (const auto &p : current.parameters) {
    bool present = false;
    for (const auto &saved : state.directParameters)
      present |= saved.id == p.id;
    if (!present)
      migrated.push_back({p.id,
                          instrument::hostparams::canonicalPlain(p.defaultValue, p),
                          p.poolIndex});
  }
  state.directParameters = std::move(migrated);
  state.instrumentVersion = current.instrumentVersion;
  return DirectMigration::migrated;
}

} // namespace vstengine::rack
