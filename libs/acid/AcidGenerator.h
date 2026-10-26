#pragma once

#include "sequence/Sequence.h"
#include <cstdint>
#include <string_view>

namespace vstengine::acid {

enum class AcidProfile : std::uint8_t {
    classic303 = 0,
    psyAcid,
    darkAcid,
    forestAcid,
    hiTechAcid,
    hypnoticAcid,
    technoDrive
};

struct AcidPatternParams {
    AcidProfile profile { AcidProfile::classic303 };
    float density { 0.70f };        // [0.0 .. 1.0] gate probability (0.70 on mockup)
    float variation { 0.50f };      // [0.0 .. 1.0] pitch/accent/slide complexity (0.50 on mockup)
    std::uint32_t length { 16 };    // [1 .. 64] active pattern steps (16 on mockup)
    float swing { 0.08f };          // [0.0 .. 0.5] swing amount (8% on mockup)
    std::uint8_t rootNote { 36 };   // Default C2 (MIDI 36) acid bassline register
};

class AcidGenerator final {
public:
    [[nodiscard]] static std::uint32_t stableSeed(
        std::uint32_t globalSeed, std::uint64_t slotId,
        std::string_view instrumentId) noexcept;

    [[nodiscard]] static sequence::Sequence generate(
        AcidProfile profile, std::uint32_t globalSeed, std::uint64_t slotId,
        std::string_view instrumentId = "com.ultimavox.acid") noexcept;

    [[nodiscard]] static sequence::Sequence generateWithParams(
        const AcidPatternParams& params, std::uint32_t globalSeed,
        std::uint64_t slotId,
        std::string_view instrumentId = "com.ultimavox.acid") noexcept;

    [[nodiscard]] static sequence::Sequence mutate(
        const sequence::Sequence& current, const AcidPatternParams& params,
        std::uint32_t mutationCounter, std::uint32_t globalSeed,
        std::uint64_t slotId) noexcept;
};

} // namespace vstengine::acid