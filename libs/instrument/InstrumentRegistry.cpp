#include "instrument/InstrumentRegistry.h"
#include "instrument/HostParameterSchema.h"
#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace vstengine::instrument {
namespace {
bool validId(std::string_view id)
{
    if (id.empty() || id.size() >= VOX_INSTRUMENT_MAX_ID_BYTES)
        return false;
    return std::all_of(id.begin(), id.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
            || c == '.' || c == '-' || c == '_';
    });
}
bool exceeds(const ResourceBudget& budget, const HostBudgetLimits& limits)
{
    return budget.maxVoices > limits.maxVoicesPerSlot
        || budget.maxMidiEventsPerBlock > limits.maxMidiEventsPerBlock
        || budget.maxStateBytes > limits.maxStateBytesPerSlot
        || budget.maxResourceBytes > limits.maxResourceBytesPerSlot
        || budget.maxLatencySamples > limits.maxLatencySamples
        || budget.maxTailSamples > limits.maxTailSamples
        || budget.maxPatternEvents > limits.maxPatternEventsPerSlot
        || budget.maxModulationRoutes > limits.maxModulationRoutesPerSlot
        || budget.scratchBytes > limits.maxScratchBytesPerSlot;
}
} // namespace

Resolution InstrumentRegistry::validate(InstrumentProvider& provider,
                                        const InstrumentDescriptor& descriptor) const
{
    Resolution result { ResolutionStatus::resolved, &descriptor, &provider, {} };
    if (descriptor.abiVersion != contractVersion) {
        result.status = ResolutionStatus::incompatibleAbi;
        result.diagnostic = "instrument ABI version mismatch";
    } else if (descriptor.descriptorVersion != descriptorSchemaVersion
               || descriptor.stateVersion == 0) {
        result.status = ResolutionStatus::incompatibleSchema;
        result.diagnostic = "instrument schema version mismatch";
    } else if (!validId(descriptor.id) || !validId(descriptor.providerId)
               || descriptor.name.empty()
               || descriptor.vendor.empty() || descriptor.budget.maxVoices == 0
               || descriptor.budget.maxMidiEventsPerBlock == 0
               || descriptor.instrumentVersion == 0
               || descriptor.minimumHostVersion > hostContractVersion
               || descriptor.contentVersion == 0
               || descriptor.supportedPresetSchemaVersions.empty()) {
        result.status = ResolutionStatus::invalidDescriptor;
        result.diagnostic = "invalid instrument descriptor";
    } else if (exceeds(descriptor.budget, limits_)) {
        result.status = ResolutionStatus::budgetExceeded;
        result.diagnostic = "instrument resource budget exceeds host limit";
    } else if (descriptor.directParameters &&
               (descriptor.parameters.empty() ||
                descriptor.parameters.size() > maxDirectParameters)) {
      result.status = ResolutionStatus::invalidDescriptor;
      result.diagnostic = "direct parameter count exceeds host limit";
    } else {
      std::unordered_set<std::string_view> ids;
      for (const auto &parameter : descriptor.parameters) {
        if (!validId(parameter.id) || parameter.name.empty() ||
            !std::isfinite(parameter.minimum) ||
            !std::isfinite(parameter.maximum) ||
            !std::isfinite(parameter.defaultValue) ||
            parameter.minimum >= parameter.maximum ||
            !std::isfinite(parameter.step) || parameter.step < 0.0f ||
            parameter.step > parameter.maximum - parameter.minimum ||
            parameter.defaultValue < parameter.minimum ||
            parameter.defaultValue > parameter.maximum ||
            parameter.preferredMacro >=
                static_cast<std::int8_t>(macrosPerSlot) ||
            (descriptor.directParameters &&
             !hostparams::validDirectParameter(parameter)) ||
            (descriptor.directParameters &&
             (static_cast<unsigned>(parameter.type) >
                  static_cast<unsigned>(ParameterType::choice) ||
              (parameter.type != ParameterType::floating &&
               (std::floor(parameter.minimum) != parameter.minimum ||
                std::floor(parameter.maximum) != parameter.maximum ||
                std::floor(parameter.defaultValue) != parameter.defaultValue ||
                static_cast<double>(parameter.minimum) < -2147483648.0 ||
                static_cast<double>(parameter.maximum) > 2147483647.0)) ||
              (parameter.type == ParameterType::boolean &&
               (parameter.minimum != 0 || parameter.maximum != 1)) ||
              (parameter.type == ParameterType::choice &&
               (parameter.minimum != 0 ||
                parameter.choices.size() !=
                    static_cast<std::size_t>(parameter.maximum) + 1)))) ||
            !ids.insert(parameter.id).second) {
          result.status = ResolutionStatus::invalidDescriptor;
          result.diagnostic = "invalid instrument parameter descriptor";
          break;
        }
      }
    }
    return result;
}

bool InstrumentRegistry::registerProvider(
    std::unique_ptr<InstrumentProvider> provider, std::string& diagnostic)
{
    if (provider == nullptr) {
        diagnostic = "null instrument provider";
        return false;
    }
    const auto offered = provider->descriptors();
    if (offered.empty()) {
        diagnostic = "instrument provider has no descriptors";
        return false;
    }
    for (const auto& descriptor : offered) {
        const auto validated = validate(*provider, descriptor);
        if (!validated) {
            if (validId(descriptor.id))
                rejected_.push_back(
                    { descriptor.id, validated.status, validated.diagnostic });
            diagnostic = validated.diagnostic;
            return false;
        }
        if (resolve(descriptor.id).status != ResolutionStatus::missingModule) {
            diagnostic = "duplicate instrument ID";
            return false;
        }
        for (const auto& content : provider->contentDescriptors(descriptor.id)) {
            constexpr auto presetClasses = VOX_PRESET_CLASS_SOUND
                | VOX_PRESET_CLASS_PATTERN | VOX_PRESET_CLASS_GENERATOR;
            if (content.instrumentId != descriptor.id || !validId(content.id)
                || content.name.empty() || content.schemaVersion == 0
                || content.contentVersion == 0
                || (content.supportedSequenceFields & ~VOX_SEQUENCE_ALL) != 0
                || (content.supportedPresetClasses & ~presetClasses) != 0
                || (content.macroValueMask & ~0xffu) != 0) {
                diagnostic = "invalid instrument content descriptor";
                return false;
            }
            for (std::size_t macro = 0; macro < macrosPerSlot; ++macro)
                if ((content.macroValueMask & (1u << macro)) != 0
                    && (!std::isfinite(content.macroValues[macro])
                        || content.macroValues[macro] < 0.0f
                        || content.macroValues[macro] > 1.0f)) {
                    diagnostic = "invalid instrument content macro value";
                    return false;
                }
        }
    }
    auto candidateCatalog = descriptors_;
    for (const auto &descriptor : offered)
      candidateCatalog.push_back(&descriptor);
    if (!hostparams::validateDirectCatalog(candidateCatalog, diagnostic))
      return false;
    auto* accepted = provider.get();
    providers_.push_back(std::move(provider));
    for (const auto& descriptor : accepted->descriptors())
        descriptors_.push_back(&descriptor);
    return true;
}

Resolution InstrumentRegistry::resolve(std::string_view instrumentId) const noexcept
{
    for (const auto& provider : providers_)
        for (const auto& descriptor : provider->descriptors())
            if (descriptor.id == instrumentId)
                return { ResolutionStatus::resolved, &descriptor,
                         provider.get(), {} };
    for (const auto& rejected : rejected_)
        if (rejected.id == instrumentId)
            return { rejected.status, nullptr, nullptr, rejected.diagnostic };
    return { ResolutionStatus::missingModule, nullptr, nullptr,
             "instrument module is missing" };
}

std::unique_ptr<InstrumentInstance> InstrumentRegistry::create(
    std::string_view instrumentId, const CreateContext& context,
    Resolution& result) const
{
    result = resolve(instrumentId);
    if (!result)
        return {};
    std::unique_ptr<InstrumentInstance> instance;
    try {
        instance = result.provider->create(instrumentId, context);
    } catch (...) {
        result.status = ResolutionStatus::constructionFailed;
        result.diagnostic = "instrument construction threw an exception";
        return {};
    }
    if (instance == nullptr) {
        result.status = ResolutionStatus::constructionFailed;
        result.diagnostic = "instrument construction failed";
    }
    return instance;
}
} // namespace vstengine::instrument
