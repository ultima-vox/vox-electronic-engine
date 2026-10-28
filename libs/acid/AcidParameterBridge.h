#pragma once
#include "acid/AcidEngine.h"
#include "instrument/InstrumentContract.h"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <string>
#include <string_view>

namespace vstengine::acid {

// Stable additive host IDs. The eight historical Rack host macros remain intact.
inline std::string hostParameterId(std::size_t slot, std::string_view semantic)
{
    char prefix[24] {};
    std::snprintf(prefix, sizeof(prefix), "slot%02zuAcid_", slot + 1);
    return std::string(prefix) + std::string(semantic);
}

// Scoped to one plugin instance. No process-global/static registry, no locks,
// allocations, shared mutable UI component, or engine pointer in the editor.
class AcidParameterBridge final {
public:
    static constexpr std::size_t firstExtended = instrument::macrosPerSlot;
    static constexpr std::size_t flagCount = 4;
    struct Slot {
        std::atomic<instrument::SlotId> id { instrument::invalidSlotId };
        std::array<std::atomic<float>*, AcidEngine::parameterCount> raw {};
        std::array<std::atomic<float>*, flagCount> flags {};
    };
    // Bind only before audio starts; host atomics live inside processor APVTS.
    void bind(std::size_t slot, std::size_t parameter, std::atomic<float>* value) noexcept
    {
        if (slot < slots_.size() && parameter >= firstExtended
            && parameter != AcidEngine::playModeFlags
            && parameter < AcidEngine::parameterCount)
            slots_[slot].raw[parameter] = value;
    }
    void bindFlag(std::size_t slot, std::size_t flag, std::atomic<float>* value) noexcept
    {
        if (slot < slots_.size() && flag < flagCount)
            slots_[slot].flags[flag] = value;
    }
    // Bind slot identity only on control thread, while host audio is suspended.
    void setSlotId(std::size_t slot, instrument::SlotId id) noexcept
    {
        if (slot < slots_.size())
            slots_[slot].id.store(id, std::memory_order_release);
    }
    void apply(instrument::SlotId id, AcidEngine& engine) const noexcept
    {
        if (id == instrument::invalidSlotId) return;
        for (const auto& slot : slots_) {
            if (slot.id.load(std::memory_order_acquire) != id) continue;
            for (std::size_t p = firstExtended; p < AcidEngine::parameterCount; ++p)
                if (p != AcidEngine::playModeFlags)
                    if (const auto* value = slot.raw[p])
                    (void) engine.setParameter(AcidEngine::parameterIds[p],
                                               value->load(std::memory_order_relaxed));
            if (slot.flags[0] && slot.flags[1] && slot.flags[2] && slot.flags[3])
                engine.setPlayModeOptions(
                    slot.flags[0]->load(std::memory_order_relaxed) >= 0.5f,
                    slot.flags[1]->load(std::memory_order_relaxed) >= 0.5f,
                    slot.flags[2]->load(std::memory_order_relaxed) >= 0.5f,
                    slot.flags[3]->load(std::memory_order_relaxed) >= 0.5f);
            return;
        }
    }
private:
    std::array<Slot, instrument::maxSlots> slots_ {};
};
} // namespace vstengine::acid
