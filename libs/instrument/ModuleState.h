#pragma once
#include "instrument/InstrumentContract.h"
#include <vector>

namespace vstengine::instrument {
class InstrumentRegistry;

struct PersistentModuleState {
    std::uint32_t schemaVersion { stateSchemaVersion };
    SlotId slotId {};
    InstrumentId instrumentId;
    std::string providerId;
    std::uint32_t instrumentVersion {};
    std::uint32_t contentVersion {};
    std::vector<std::byte> payload;
};

struct RuntimeModuleState {
    ResolutionStatus status { ResolutionStatus::missingModule };
    std::uint32_t activeNotes {};
    std::uint64_t droppedMidiEvents {};
    std::uint64_t processOverruns {};
};

struct PreparedModuleState {
    PersistentModuleState persistent;
    Resolution resolution;
    std::unique_ptr<InstrumentInstance> instance;
};

// Prepares replacement state without touching live state. Caller swaps only
// after all slots validate and instantiate successfully or remain unresolved.
PreparedModuleState prepareModuleState(const PersistentModuleState&,
                                       const InstrumentRegistry&,
                                       ResourceResolver*,
                                       std::uint32_t slotIndex = noSlotIndex);

} // namespace vstengine::instrument
