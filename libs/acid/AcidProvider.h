#pragma once

#include "instrument/InstrumentContract.h"
#include <memory>

namespace vstengine::acid {
class AcidParameterBridge;
inline constexpr std::string_view instrumentId = "com.ultimavox.acid";
std::unique_ptr<instrument::InstrumentProvider> createAcidProvider(AcidParameterBridge* controls = nullptr);
// Query factory sound values without copying presets into the host layer.
bool factorySoundParameterValue(std::string_view presetId,
                                std::string_view semanticId, float& value) noexcept;
} // namespace vstengine::acid
