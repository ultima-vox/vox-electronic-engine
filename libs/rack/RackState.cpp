#include "rack/RackState.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <limits>
#include <type_traits>

namespace vstengine::rack::state {
namespace {
constexpr auto rackType = "RACK";
constexpr auto slotType = "SLOT";
bool validString(const juce::var& value, std::size_t maxBytes = 256)
{
    return value.toString().getNumBytesAsUTF8() <= maxBytes;
}
bool validStableId(std::string_view id, bool allowEmpty = false)
{
    if (id.empty()) return allowEmpty;
    if (id.size() >= VOX_INSTRUMENT_MAX_ID_BYTES) return false;
    return std::all_of(id.begin(), id.end(), [](const unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
            || c == '.' || c == '-' || c == '_';
    });
}
template <typename UInt>
bool readUnsigned(const juce::var& value, UInt& result)
{
    static_assert(std::is_unsigned_v<UInt>);
    const auto text = value.toString().toStdString();
    if (text.empty()) return false;
    std::uint64_t parsed {};
    const auto converted = std::from_chars(text.data(), text.data() + text.size(),
                                           parsed, 10);
    if (converted.ec != std::errc {} || converted.ptr != text.data() + text.size()
        || parsed > static_cast<std::uint64_t>(std::numeric_limits<UInt>::max()))
        return false;
    result = static_cast<UInt>(parsed);
    return true;
}
bool readPlain(const juce::var &value, float &result) {
  if (!(value.isString() || value.isDouble() || value.isInt() ||
        value.isInt64()))
    return false;
  const auto text = value.toString().toStdString();
  double parsed{};
  const auto converted = std::from_chars(text.data(), text.data() + text.size(),
                                         parsed, std::chars_format::general);
  if (converted.ec != std::errc{} ||
      converted.ptr != text.data() + text.size() || !std::isfinite(parsed) ||
      parsed > std::numeric_limits<float>::max() ||
      parsed < -std::numeric_limits<float>::max())
    return false;
  result = static_cast<float>(parsed);
  return true;
}
bool readSigned(const juce::var& value, int& result)
{
    const auto text = value.toString().toStdString();
    if (text.empty()) return false;
    int parsed {};
    const auto converted = std::from_chars(text.data(), text.data() + text.size(),
                                           parsed, 10);
    if (converted.ec != std::errc {} || converted.ptr != text.data() + text.size())
        return false;
    result = parsed;
    return true;
}
} // namespace

bool validateSlot(const PersistentSlotState& slot, std::string& diagnostic)
{
    if (slot.schemaVersion != static_cast<std::uint32_t>(schemaVersion)
        || slot.slotId == instrument::invalidSlotId
        || !validStableId(slot.instrumentId, true)
        || !validStableId(slot.resolvedProviderId, true)
        || slot.soundPreset.size() > 256 || slot.patternPreset.size() > 256) {
        diagnostic = "invalid rack slot identity or metadata";
        return false;
    }
    if (!valid(slot.routing)
        || (slot.routing.mode != RouteMode::off
            && slot.routing.mode != RouteMode::channel)
        || slot.routing.keyHigh > 127 || slot.routing.velocityHigh > 127
        || slot.outputDestination != 0) {
        diagnostic = "invalid rack routing or unsupported output";
        return false;
    }
    if (!std::isfinite(slot.level) || slot.level < 0.0f || slot.level > 2.0f
        || !std::isfinite(slot.pan) || slot.pan < -1.0f || slot.pan > 1.0f) {
        diagnostic = "invalid rack mix state";
        return false;
    }
    for (std::size_t macro = 0; macro < slot.macros.size(); ++macro)
        if (!std::isfinite(slot.macros[macro]) || slot.macros[macro] < 0.0f
            || slot.macros[macro] > 1.0f
            || !validStableId(slot.macroAssignments[macro], true)) {
            diagnostic = "invalid rack macro state";
            return false;
        }
    if (slot.patternSchemaVersion == 0
        || slot.modulePayload.size() > maxModulePayloadBytes
        || slot.patternPayload.size() > maxPatternPayloadBytes
        || slot.modulationPayload.size() > maxModulationPayloadBytes) {
        diagnostic = "invalid rack pattern schema or payload exceeds host budget";
        return false;
    }
    if (slot.directParameters.size() > instrument::maxDirectParameters) {
      diagnostic = "direct parameter state exceeds host limit";
      return false;
    }
    for (std::size_t i = 0; i < slot.directParameters.size(); ++i) {
      const auto &parameter = slot.directParameters[i];
      if (!validStableId(parameter.id) || !std::isfinite(parameter.value) ||
          parameter.poolIndex < -1 ||
          parameter.poolIndex >=
              static_cast<std::int16_t>(instrument::poolParametersPerSlot)) {
        diagnostic = "invalid direct parameter state";
        return false;
      }
      for (std::size_t previous = 0; previous < i; ++previous)
        if (slot.directParameters[previous].id == parameter.id) {
          diagnostic = "duplicate direct parameter ID";
          return false;
        }
    }
    if (!slot.directParameters.empty() &&
        std::any_of(slot.macroAssignments.begin(), slot.macroAssignments.end(),
                    [](const auto &id) { return !id.empty(); })) {
      diagnostic = "macro assignments cannot own direct instrument parameters";
      return false;
    }
    return true;
}

bool validate(const std::array<PersistentSlotState, instrument::maxSlots>& slots,
              std::string& diagnostic)
{
    for (std::size_t index = 0; index < slots.size(); ++index) {
        if (!validateSlot(slots[index], diagnostic)) return false;
        for (std::size_t previous = 0; previous < index; ++previous)
        {
            if (slots[previous].slotId == slots[index].slotId) {
                diagnostic = "duplicate rack SlotId";
                return false;
            }
            if (slots[index].routing.mode == RouteMode::channel
                && slots[previous].routing.mode == RouteMode::channel
                && slots[previous].routing.channel == slots[index].routing.channel) {
                diagnostic = "duplicate rack MIDI channel";
                return false;
            }
        }
    }
    diagnostic.clear();
    return true;
}

juce::ValueTree serialize(
    const std::array<PersistentSlotState, instrument::maxSlots>& slots)
{
    juce::ValueTree root(rackType);
    root.setProperty("schemaVersion", schemaVersion, nullptr);
    for (const auto& slot : slots) {
        juce::ValueTree child(slotType);
        child.setProperty("schemaVersion", static_cast<juce::int64>(schemaVersion), nullptr);
        child.setProperty("slotId", juce::String(slot.slotId), nullptr);
        child.setProperty("instrumentId", juce::String(slot.instrumentId), nullptr);
        child.setProperty("providerId", juce::String(slot.resolvedProviderId), nullptr);
        child.setProperty("instrumentVersion", static_cast<juce::int64>(slot.instrumentVersion), nullptr);
        child.setProperty("instrumentStateVersion", static_cast<juce::int64>(slot.instrumentStateVersion), nullptr);
        child.setProperty("contentVersion", static_cast<juce::int64>(slot.contentVersion), nullptr);
        child.setProperty("routeMode", static_cast<int>(slot.routing.mode), nullptr);
        child.setProperty("channel", static_cast<int>(slot.routing.channel), nullptr);
        child.setProperty("keyLow", static_cast<int>(slot.routing.keyLow), nullptr);
        child.setProperty("keyHigh", static_cast<int>(slot.routing.keyHigh), nullptr);
        child.setProperty("velocityLow", static_cast<int>(slot.routing.velocityLow), nullptr);
        child.setProperty("velocityHigh", static_cast<int>(slot.routing.velocityHigh), nullptr);
        child.setProperty("transpose", static_cast<int>(slot.routing.transpose), nullptr);
        child.setProperty("enabled", slot.enabled, nullptr);
        child.setProperty("mute", slot.mute, nullptr);
        child.setProperty("solo", slot.solo, nullptr);
        child.setProperty("locked", slot.locked, nullptr);
        child.setProperty("level", slot.level, nullptr);
        child.setProperty("pan", slot.pan, nullptr);
        child.setProperty("output", static_cast<int>(slot.outputDestination), nullptr);
        child.setProperty("soundPreset", juce::String(slot.soundPreset), nullptr);
        child.setProperty("patternPreset", juce::String(slot.patternPreset), nullptr);
        child.setProperty("patternSchemaVersion",
                          static_cast<juce::int64>(slot.patternSchemaVersion), nullptr);
        for (std::size_t i = 0; i < slot.macros.size(); ++i) {
            child.setProperty("macro" + juce::String(static_cast<int>(i + 1)),
                              slot.macros[i], nullptr);
            child.setProperty("macroMap" + juce::String(static_cast<int>(i + 1)),
                              juce::String(slot.macroAssignments[i]), nullptr);
        }
        juce::MemoryBlock payload(slot.modulePayload.data(), slot.modulePayload.size());
        child.setProperty("modulePayload", payload.toBase64Encoding(), nullptr);
        juce::MemoryBlock pattern(slot.patternPayload.data(), slot.patternPayload.size());
        child.setProperty("patternPayload", pattern.toBase64Encoding(), nullptr);
        juce::MemoryBlock modulation(slot.modulationPayload.data(),
                                     slot.modulationPayload.size());
        child.setProperty("modulationPayload", modulation.toBase64Encoding(),
                          nullptr);
        for (const auto &parameter : slot.directParameters) {
          juce::ValueTree direct("SOUND_PARAMETER");
          direct.setProperty("id", juce::String(parameter.id), nullptr);
          direct.setProperty("value", parameter.value, nullptr);
          if (parameter.poolIndex >= 0)
            direct.setProperty("pool", static_cast<int>(parameter.poolIndex),
                               nullptr);
          child.appendChild(direct, nullptr);
        }
        root.appendChild(child, nullptr);
    }
    return root;
}

bool deserialize(const juce::ValueTree& root,
                 std::array<PersistentSlotState, instrument::maxSlots>& result,
                 std::string& diagnostic)
{
    std::uint32_t encodedSchema {};
    if (!root.isValid() || !root.hasType(rackType)
        || !readUnsigned(root.getProperty("schemaVersion"), encodedSchema)
        || encodedSchema < 1
        || encodedSchema > static_cast<std::uint32_t>(schemaVersion)
        || root.getNumChildren() != static_cast<int>(instrument::maxSlots)) {
        diagnostic = "invalid rack state root";
        return false;
    }
    std::array<PersistentSlotState, instrument::maxSlots> parsed;
    for (int i = 0; i < root.getNumChildren(); ++i) {
        const auto child = root.getChild(i);
        auto& slot = parsed[static_cast<std::size_t>(i)];
        if (!child.hasType(slotType)
            || !validString(child.getProperty("instrumentId"))
            || !validString(child.getProperty("providerId"))
            || !validString(child.getProperty("soundPreset"))
            || !validString(child.getProperty("patternPreset"))) {
            diagnostic = "invalid rack slot encoding";
            return false;
        }
        if (!readUnsigned(child.getProperty("schemaVersion"), slot.schemaVersion)
            || !readUnsigned(child.getProperty("slotId"), slot.slotId)
            || !readUnsigned(child.getProperty("instrumentVersion"),
                             slot.instrumentVersion)
            || !readUnsigned(child.getProperty("instrumentStateVersion"),
                             slot.instrumentStateVersion)
            || !readUnsigned(child.getProperty("contentVersion"),
                             slot.contentVersion)) {
            diagnostic = "invalid rack unsigned metadata";
            return false;
        }
        if (slot.schemaVersion != encodedSchema) {
            diagnostic = "incompatible rack slot schema";
            return false;
        }
        slot.instrumentId = child.getProperty("instrumentId").toString().toStdString();
        slot.resolvedProviderId = child.getProperty("providerId").toString().toStdString();
        if (!validStableId(slot.instrumentId, true)
            || !validStableId(slot.resolvedProviderId, true)) {
            diagnostic = "invalid rack stable ID";
            return false;
        }
        int routeMode {}, channel {}, keyLow {}, keyHigh {}, velocityLow {},
            velocityHigh {}, transpose {};
        if (!readSigned(child.getProperty("routeMode", -1), routeMode)
            || !readSigned(child.getProperty("channel", 0), channel)
            || !readSigned(child.getProperty("keyLow", -1), keyLow)
            || !readSigned(child.getProperty("keyHigh", -1), keyHigh)
            || !readSigned(child.getProperty("velocityLow", 0), velocityLow)
            || !readSigned(child.getProperty("velocityHigh", 0), velocityHigh)
            || !readSigned(child.getProperty("transpose", 99), transpose)
            || routeMode < static_cast<int>(RouteMode::off)
            || routeMode > static_cast<int>(RouteMode::channel)
            || channel < 1 || channel > 16 || keyLow < 0 || keyLow > 127
            || keyHigh < 0 || keyHigh > 127 || velocityLow < 1
            || velocityLow > 127 || velocityHigh < 1 || velocityHigh > 127
            || transpose < -48 || transpose > 48) {
            diagnostic = "invalid rack routing metadata";
            return false;
        }
        slot.routing.mode = static_cast<RouteMode>(routeMode);
        slot.routing.channel = static_cast<std::uint8_t>(channel);
        slot.routing.keyLow = static_cast<std::uint8_t>(keyLow);
        slot.routing.keyHigh = static_cast<std::uint8_t>(keyHigh);
        slot.routing.velocityLow = static_cast<std::uint8_t>(velocityLow);
        slot.routing.velocityHigh = static_cast<std::uint8_t>(velocityHigh);
        slot.routing.transpose = static_cast<std::int8_t>(transpose);
        slot.enabled = static_cast<bool>(child.getProperty("enabled", false));
        slot.mute = static_cast<bool>(child.getProperty("mute", false));
        slot.solo = static_cast<bool>(child.getProperty("solo", false));
        slot.locked = static_cast<bool>(child.getProperty("locked", false));
        slot.level = static_cast<float>(child.getProperty("level", 1.0));
        slot.pan = static_cast<float>(child.getProperty("pan", 0.0));
        if (!readUnsigned(child.getProperty("output"), slot.outputDestination)) {
            diagnostic = "invalid rack output metadata";
            return false;
        }
        slot.soundPreset = child.getProperty("soundPreset").toString().toStdString();
        slot.patternPreset = child.getProperty("patternPreset").toString().toStdString();
        if (encodedSchema >= 2) {
            if (!readUnsigned(child.getProperty("patternSchemaVersion"),
                              slot.patternSchemaVersion)) {
                diagnostic = "invalid pattern schema metadata";
                return false;
            }
        } else {
            slot.patternSchemaVersion = VOX_PATTERN_SCHEMA_V1;
        }
        if (slot.patternSchemaVersion == 0) {
            diagnostic = "invalid pattern schema metadata";
            return false;
        }
        for (std::size_t macro = 0; macro < slot.macros.size(); ++macro) {
            slot.macros[macro] = static_cast<float>(child.getProperty(
                "macro" + juce::String(static_cast<int>(macro + 1)), 0.0));
            slot.macroAssignments[macro] = child.getProperty(
                "macroMap" + juce::String(static_cast<int>(macro + 1))).toString().toStdString();
            if (!std::isfinite(slot.macros[macro]) || slot.macros[macro] < 0.0f
                || slot.macros[macro] > 1.0f
                || !validStableId(slot.macroAssignments[macro], true)) {
                diagnostic = "invalid rack macro state";
                return false;
            }
        }
        const auto encodedPayload = child.getProperty("modulePayload").toString();
        constexpr auto maxEncodedBytes =
            4u * ((maxModulePayloadBytes + 2u) / 3u);
        if (encodedPayload.getNumBytesAsUTF8()
            > static_cast<int>(maxEncodedBytes)) {
            diagnostic = "rack module payload exceeds host budget";
            return false;
        }
        juce::MemoryBlock payload;
        if (!payload.fromBase64Encoding(encodedPayload)) {
            diagnostic = "invalid rack module payload";
            return false;
        }
        if (payload.getSize() > maxModulePayloadBytes) {
            diagnostic = "rack module payload exceeds host budget";
            return false;
        }
        slot.modulePayload.resize(payload.getSize());
        if (!slot.modulePayload.empty())
            std::memcpy(slot.modulePayload.data(), payload.getData(), payload.getSize());
        if (encodedSchema >= 2) {
            const auto encodedPattern = child.getProperty("patternPayload").toString();
            constexpr auto maxEncodedPatternBytes =
                4u * ((maxPatternPayloadBytes + 2u) / 3u);
            if (encodedPattern.getNumBytesAsUTF8()
                    > static_cast<int>(maxEncodedPatternBytes)) {
                diagnostic = "rack pattern payload exceeds host budget";
                return false;
            }
            juce::MemoryBlock pattern;
            if (!pattern.fromBase64Encoding(encodedPattern)
                || pattern.getSize() > maxPatternPayloadBytes) {
                diagnostic = "invalid rack pattern payload";
                return false;
            }
            slot.patternPayload.resize(pattern.getSize());
            if (!slot.patternPayload.empty())
                std::memcpy(slot.patternPayload.data(), pattern.getData(),
                            pattern.getSize());
        }
        if (encodedSchema >= 3) {
            const auto encodedModulation =
                child.getProperty("modulationPayload").toString();
            constexpr auto maxEncodedModulationBytes =
                4u * ((maxModulationPayloadBytes + 2u) / 3u);
            if (encodedModulation.getNumBytesAsUTF8()
                    > static_cast<int>(maxEncodedModulationBytes)) {
                diagnostic = "rack modulation payload exceeds host budget";
                return false;
            }
            juce::MemoryBlock modulation;
            if (!modulation.fromBase64Encoding(encodedModulation)
                || modulation.getSize() > maxModulationPayloadBytes) {
                diagnostic = "invalid rack modulation payload";
                return false;
            }
            slot.modulationPayload.resize(modulation.getSize());
            if (!slot.modulationPayload.empty())
                std::memcpy(slot.modulationPayload.data(), modulation.getData(),
                            modulation.getSize());
        }
        if (encodedSchema >= 4) {
          if (child.getNumChildren() >
              static_cast<int>(instrument::maxDirectParameters)) {
            diagnostic = "direct parameter count exceeds host limit";
            return false;
          }
          for (const auto direct : child) {
            float value{};
            int pool{-1};
            if (!direct.hasType("SOUND_PARAMETER") ||
                !validString(direct.getProperty("id"),
                             VOX_INSTRUMENT_MAX_ID_BYTES) ||
                !readPlain(direct.getProperty("value"), value) ||
                (direct.hasProperty("pool") &&
                 (!readSigned(direct.getProperty("pool"), pool) || pool < 0 ||
                  pool >= static_cast<int>(instrument::poolParametersPerSlot)))) {
              diagnostic = "invalid direct parameter encoding";
              return false;
            }
            slot.directParameters.push_back(
                {direct.getProperty("id").toString().toStdString(), value,
                 static_cast<std::int16_t>(pool)});
          }
        }
        slot.schemaVersion = schemaVersion;
    }
    if (!validate(parsed, diagnostic)) return false;
    result = std::move(parsed);
    return true;
}
} // namespace vstengine::rack::state
