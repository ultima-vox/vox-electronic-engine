#pragma once
#include "rack/RackTypes.h"

namespace vstengine::rack {

enum class DirectMigration {
  notNeeded,   // same instrument version (or nothing to migrate)
  migrated,    // older append-only snapshot upgraded in place
  incompatible // left untouched; the Rack keeps it unresolved
};

// Append-only direct-parameter migration for the SAME InstrumentId (owner
// decision 27/28.10.2026). An older snapshot is upgraded only when every saved
// named value still exists, keeps its recorded pool index and is valid under
// the current descriptor; only newly introduced parameters are filled from
// their canonical defaults. Correspondence is never inferred from position.
// Other version markers (content, native state, pattern) keep their strict
// rules. On anything else `state` is left exactly as it was.
[[nodiscard]] DirectMigration
migrateAppendOnly(PersistentSlotState &state,
                  const instrument::InstrumentDescriptor &current);

} // namespace vstengine::rack
