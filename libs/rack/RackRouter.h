#pragma once
#include "rack/RackTypes.h"
#include <array>
#include <span>

namespace vstengine::rack {

class RackRouter final {
public:
    static constexpr std::size_t eventCapacity = 2048;
    struct DestinationBuffer {
        std::array<VoxMidiEventV1, eventCapacity> events {};
        std::size_t size {};
        std::uint64_t dropped {};
        void clear() noexcept { size = 0; }
        bool push(VoxMidiEventV1 event) noexcept;
        void sortBySampleOffset() noexcept;
        std::span<const VoxMidiEventV1> view() const noexcept
        {
            return { events.data(), size };
        }
    };
    struct GeneratedSlotEvents {
        instrument::SlotId slotId { instrument::invalidSlotId };
        std::span<const VoxMidiEventV1> events;
    };

    void reset() noexcept;
    void forgetSlot(instrument::SlotId) noexcept;
    void route(std::span<const VoxMidiEventV1> input,
               std::span<const ProcessSlotControls> slots,
               std::array<DestinationBuffer, instrument::maxSlots>& output)
        noexcept;
    void routeAudition(std::span<const VoxMidiEventV1> input,
                       instrument::SlotId selectedSlot,
                       std::span<const ProcessSlotControls> slots,
                       std::array<DestinationBuffer, instrument::maxSlots>& output,
                       bool generated = false)
        noexcept;
    void routeGenerated(std::span<const GeneratedSlotEvents> input,
                        std::span<const ProcessSlotControls> slots,
                        std::array<DestinationBuffer, instrument::maxSlots>& output)
        noexcept;

private:
    struct Owner {
        instrument::SlotId slotId {};
        std::uint8_t transposedNote {};
        std::uint8_t channel {};
    };
    struct Owners {
        std::array<Owner, instrument::maxSlots> values {};
        std::uint8_t size {};
    };
    std::array<std::array<Owners, 128>, 16> ownership_ {};
    std::array<Owner, 128> auditionOwnership_ {};
    struct GeneratedDomain {
        instrument::SlotId sourceSlotId { instrument::invalidSlotId };
        std::array<Owner, 128> notes {};
    };
    std::array<GeneratedDomain, instrument::maxSlots> generatedOwnership_ {};
    // Runtime-only, fixed-capacity fail-safe. A lost release silences the
    // affected slot at the next block, before accepting fresh notes.
    std::array<instrument::SlotId, instrument::maxSlots> releaseRecovery_ {};
    bool recoveryPending(instrument::SlotId) const noexcept;
    void deferReleaseRecovery(instrument::SlotId) noexcept;
    void flushReleaseRecovery(
        std::span<const ProcessSlotControls>,
        std::array<DestinationBuffer, instrument::maxSlots>&) noexcept;
};

} // namespace vstengine::rack
