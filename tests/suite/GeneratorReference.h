#pragma once

// Test reference: the switch-based generator before the declarative style
// engine (see GeneratorReference.cpp). Same signatures as Generator.h.

#include "Generator.h"

namespace vox::drums::reference {

void generatePattern (const GeneratorSettings&, StoredPattern& pattern, const RoleMap& roles = RoleMap::identity());
void mutateLanes (const GeneratorSettings&, std::uint32_t laneMask, StoredPattern& pattern, const RoleMap& roles = RoleMap::identity());
void generateFill (const GeneratorSettings&, StoredPattern& pattern, int bar = -1, const RoleMap& roles = RoleMap::identity());
void generateVariation (const GeneratorSettings&, const StoredPattern& a, StoredPattern& b, const RoleMap& roles = RoleMap::identity());
void generateSong (const GeneratorSettings&, PatternStore::Everything& all, const RoleMap& roles = RoleMap::identity());

} // namespace vox::drums::reference
