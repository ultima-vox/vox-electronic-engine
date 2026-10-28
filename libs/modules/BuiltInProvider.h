#pragma once
#include "instrument/InstrumentContract.h"
#include <memory>

namespace vstengine::acid { class AcidParameterBridge; }
namespace vstengine::modules {
std::unique_ptr<instrument::InstrumentProvider> createBuiltInProvider(
    acid::AcidParameterBridge* controls = nullptr);
inline constexpr std::string_view bassInstrumentId = "com.ultimavox.psy-bass";
inline constexpr std::string_view acidInstrumentId = "com.ultimavox.acid";
inline constexpr std::string_view leadInstrumentId = "com.ultimavox.lead";
inline constexpr std::string_view semanticFxInstrumentId =
    "com.ultimavox.semantic-fx";
inline constexpr std::string_view atmosInstrumentId =
    "com.ultimavox.atmos-texture";
} // namespace vstengine::modules
