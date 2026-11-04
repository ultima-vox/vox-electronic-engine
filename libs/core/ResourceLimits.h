#pragma once
#include <cstddef>
namespace vstengine::core {
// Accommodates sixteen bounded module/pattern/modulation payloads, Base64/XML
// expansion and the complete fixed host parameter catalog. Enforced before
// parse.
inline constexpr std::size_t maxSerializedProjectBytes = 32u * 1024u * 1024u;
} // namespace vstengine::core
