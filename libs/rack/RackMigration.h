#pragma once
#include "rack/RackTypes.h"

namespace vstengine::rack {

enum class DirectMigration {
  notNeeded,   // same instrument version (or nothing to migrate)
  migrated,    // older append-only snapshot upgraded in place
  incompatible // left untouched; the Rack keeps it unresolved
};

// Production upgrade of a released instrument (owner decision, #78): a project
// saved with an older version of the SAME instrument opens after a .vins
// update, by appending the new parameters with their defaults. Allowed only
// when all of these hold, otherwise `state` is left exactly as it was:
//  - same InstrumentId and the same signed publisher identity;
//  - the current version declares the upgrade (upgradeFromVersion != 0) and
//    the saved version lies in [upgradeFromVersion, instrumentVersion);
//  - every saved value still exists under its id, keeps its recorded pool
//    index and parameter meaning (spec hash: type, range, step, unit,
//    choices) and is valid; nothing was removed;
//  - content version matches; native state, pattern and size stay within the
//    current limits.
// This is not a POC-format migration: pre-1.0 states have no matching
// declaration and are rejected. Correspondence is never inferred from position.
[[nodiscard]] DirectMigration
migrateAppendOnly(PersistentSlotState &state,
                  const instrument::InstrumentDescriptor &current);

} // namespace vstengine::rack
