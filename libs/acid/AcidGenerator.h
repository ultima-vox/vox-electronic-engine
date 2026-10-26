#pragma once

#include "sequence/Sequence.h"
#include <cstdint>
#include <string_view>

namespace vstengine::acid {

enum class AcidProfile : std::uint8_t {
    classic303,
    psyAcid,
    darkAcid,
    forestAcid,
    hiTechAcid,
    hypnoticAcid
};

class AcidGenerator final {
public:
    [[nodiscard]] static std::uint32_t stableSeed(
        std::uint32_t globalSeed, std::uint64_t slotId,
        std::string_view instrumentId) noexcept;
    [[nodiscard]] static sequence::Sequence generate(
        AcidProfile profile, std::uint32_t globalSeed, std::uint64_t slotId,
        std::string_view instrumentId = "com.ultimavox.acid") noexcept;
};

} // namespace vstengine::acid
