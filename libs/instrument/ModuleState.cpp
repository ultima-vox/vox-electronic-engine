#include "instrument/ModuleState.h"
#include "instrument/InstrumentRegistry.h"

namespace vstengine::instrument {
PreparedModuleState prepareModuleState(const PersistentModuleState& state,
                                       const InstrumentRegistry& registry,
                                       ResourceResolver* resources,
                                       std::uint32_t slotIndex)
{
    PreparedModuleState prepared;
    prepared.persistent = state;
    if (state.schemaVersion == 0 || state.slotId == 0
        || state.instrumentId.empty()) {
        prepared.resolution.status = ResolutionStatus::incompatibleSchema;
        prepared.resolution.diagnostic = "invalid persistent module state";
        return prepared;
    }
    prepared.resolution = registry.resolve(state.instrumentId);
    if (!prepared.resolution)
        return prepared;
    const auto& descriptor = *prepared.resolution.descriptor;
    // InstrumentId is durable identity; providerId records last resolution
    // source only. A compatible instrument may move between providers.
    if ((state.instrumentVersion != 0
            && state.instrumentVersion != descriptor.instrumentVersion)
        || (state.contentVersion != 0
            && state.contentVersion != descriptor.contentVersion)) {
        prepared.resolution.status = ResolutionStatus::incompatibleSchema;
        prepared.resolution.diagnostic = "instrument version/provider mismatch";
        prepared.resolution.descriptor = nullptr;
        prepared.resolution.provider = nullptr;
        return prepared;
    }
    if (state.schemaVersion > descriptor.stateVersion) {
        prepared.resolution.status = ResolutionStatus::incompatibleSchema;
        prepared.resolution.diagnostic = "instrument state schema is newer than module";
        prepared.resolution.descriptor = nullptr;
        prepared.resolution.provider = nullptr;
        return prepared;
    }
    if (state.payload.size()
        > descriptor.budget.maxStateBytes) {
        prepared.resolution.status = ResolutionStatus::budgetExceeded;
        prepared.resolution.diagnostic = "instrument state exceeds module budget";
        prepared.resolution.descriptor = nullptr;
        prepared.resolution.provider = nullptr;
        return prepared;
    }
    prepared.instance = registry.create(state.instrumentId,
        { state.slotId, resources, slotIndex }, prepared.resolution);
    if (prepared.instance
        && !prepared.instance->loadState(state.schemaVersion, state.payload)) {
        prepared.instance.reset();
        prepared.resolution.status = ResolutionStatus::incompatibleSchema;
        prepared.resolution.diagnostic = "instrument rejected persistent state";
        prepared.resolution.descriptor = nullptr;
        prepared.resolution.provider = nullptr;
    }
    return prepared;
}
} // namespace vstengine::instrument
