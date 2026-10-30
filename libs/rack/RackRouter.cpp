#include "rack/RackRouter.h"
#include <algorithm>

namespace vstengine::rack {
namespace {
bool validChannelEvent(const VoxMidiEventV1& event) noexcept
{
    // VoxMidiEventV1 carries channel-voice messages, not SysEx or transport.
    // Check before using any data byte as an ownership-array index.
    if (event.size < 2 || event.size > 3
        || event.data[0] < 0x80 || event.data[0] >= 0xf0)
        return false;
    const auto type = event.data[0] & 0xf0;
    const auto expectedSize = type == 0xc0 || type == 0xd0 ? 2 : 3;
    return event.size == expectedSize && event.data[1] < 128
        && (event.size == 2 || event.data[2] < 128);
}
bool isNoteOn(const VoxMidiEventV1& event) noexcept
{
    return event.size >= 3 && (event.data[0] & 0xf0) == 0x90
        && event.data[2] != 0;
}
bool isNoteOff(const VoxMidiEventV1& event) noexcept
{
    return event.size >= 3
        && ((event.data[0] & 0xf0) == 0x80
            || ((event.data[0] & 0xf0) == 0x90 && event.data[2] == 0));
}
bool isChannelPanic(const VoxMidiEventV1& event) noexcept
{
    return event.size >= 3 && (event.data[0] & 0xf0) == 0xb0
        && (event.data[1] == 120 || event.data[1] == 123);
}
std::size_t channelIndex(const VoxMidiEventV1& event) noexcept
{
    return static_cast<std::size_t>(event.data[0] & 0x0f);
}
int findSlot(std::span<const ProcessSlotControls> slots,
             instrument::SlotId id) noexcept
{
    if (id == instrument::invalidSlotId) return -1;
    for (std::size_t i = 0; i < slots.size(); ++i)
        if (slots[i].slotId == id) return static_cast<int>(i);
    return -1;
}
bool accepts(const ProcessSlotControls& slot, const VoxMidiEventV1& event) noexcept
{
    if (!slot.enabled || slot.slotId == instrument::invalidSlotId
        || slot.routing.mode == RouteMode::off || !valid(slot.routing))
        return false;
    const auto channel = static_cast<std::uint8_t>(channelIndex(event) + 1);
    if (slot.routing.mode == RouteMode::channel
        && slot.routing.channel != channel)
        return false;
    if (!isNoteOn(event)) return true;
    return event.data[1] >= slot.routing.keyLow
        && event.data[1] <= slot.routing.keyHigh
        && event.data[2] >= slot.routing.velocityLow
        && event.data[2] <= slot.routing.velocityHigh;
}
} // namespace

bool RackRouter::DestinationBuffer::push(VoxMidiEventV1 event) noexcept
{
    if (size >= events.size()) {
        ++dropped;
        return false;
    }
    events[size++] = event;
    return true;
}

void RackRouter::DestinationBuffer::sortBySampleOffset() noexcept
{
    // Stable bounded insertion sort. Typical MIDI counts are tiny; no heap.
    for (std::size_t i = 1; i < size; ++i) {
        const auto value = events[i];
        auto position = i;
        while (position > 0
               && events[position - 1].sampleOffset > value.sampleOffset) {
            events[position] = events[position - 1];
            --position;
        }
        events[position] = value;
    }
}

void RackRouter::reset() noexcept
{
    releaseRecovery_.fill(instrument::invalidSlotId);
    for (auto& channel : ownership_)
        for (auto& note : channel)
            note.size = 0;
    for (auto& owner : auditionOwnership_) owner = {};
    for (auto& stream : generatedOwnership_) stream = {};
}

void RackRouter::forgetSlot(instrument::SlotId slotId) noexcept
{
    for (auto& pending : releaseRecovery_)
        if (pending == slotId) pending = instrument::invalidSlotId;
    for (auto& channel : ownership_)
        for (auto& notes : channel) {
            std::uint8_t write = 0;
            for (std::uint8_t read = 0; read < notes.size; ++read)
                if (notes.values[read].slotId != slotId)
                    notes.values[write++] = notes.values[read];
            notes.size = write;
        }
    for (auto& owner : auditionOwnership_)
        if (owner.slotId == slotId) owner = {};
    for (auto& stream : generatedOwnership_) {
        if (stream.sourceSlotId == slotId) stream = {};
        for (auto& owner : stream.notes)
            if (owner.slotId == slotId) owner = {};
    }
}

bool RackRouter::recoveryPending(instrument::SlotId slotId) const noexcept
{
    return slotId != instrument::invalidSlotId
        && std::find(releaseRecovery_.begin(), releaseRecovery_.end(), slotId)
            != releaseRecovery_.end();
}

void RackRouter::deferReleaseRecovery(instrument::SlotId slotId) noexcept
{
    if (slotId == instrument::invalidSlotId || recoveryPending(slotId)) return;
    for (auto& pending : releaseRecovery_)
        if (pending == instrument::invalidSlotId) {
            pending = slotId;
            return;
        }
}

void RackRouter::flushReleaseRecovery(
    std::span<const ProcessSlotControls> slots,
    std::array<DestinationBuffer, instrument::maxSlots>& output) noexcept
{
    for (const auto slotId : releaseRecovery_) {
        if (slotId == instrument::invalidSlotId) continue;
        const int index = findSlot(slots, slotId);
        if (index < 0) {
            forgetSlot(slotId);
            continue;
        }
        auto& destination = output[static_cast<std::size_t>(index)];
        if (destination.events.size() - destination.size < 16) continue;
        // Slot-level fail-safe, not a musical/channel-selective release.
        // Cover all channels for providers that implement channel-aware CC.
        for (std::uint8_t channel = 0; channel < 16; ++channel)
            destination.push({ 0, 3,
                { static_cast<std::uint8_t>(0xb0 | channel), 120, 0 } });
        forgetSlot(slotId);
    }
}

void RackRouter::routeAudition(
    std::span<const VoxMidiEventV1> input, instrument::SlotId selectedSlot,
    std::span<const ProcessSlotControls> slots,
    std::array<DestinationBuffer, instrument::maxSlots>& output,
    bool generated) noexcept
{
    if (generated) return;
    slots = slots.first(std::min(slots.size(), instrument::maxSlots));
    auto& ownership = auditionOwnership_;
    for (const auto& source : input) {
        if (!validChannelEvent(source)) continue;
        const auto note = source.data[1];
        if (isNoteOff(source)) {
            const auto owner = ownership[note];
            const int index = findSlot(slots, owner.slotId);
            if (index >= 0) {
                auto event = source;
                event.data[0] = static_cast<std::uint8_t>(
                    (source.data[0] & 0xf0) | owner.channel);
                event.data[1] = owner.transposedNote;
                if (!output[static_cast<std::size_t>(index)].push(event)) {
                    deferReleaseRecovery(owner.slotId);
                    continue;
                }
            }
            ownership[note] = {};
        } else if (isNoteOn(source)) {
            const int index = findSlot(slots, selectedSlot);
            if (index < 0 || !slots[static_cast<std::size_t>(index)].enabled)
                continue;
            if (recoveryPending(selectedSlot)) {
                ++output[static_cast<std::size_t>(index)].dropped;
                continue;
            }
            if (ownership[note].slotId != instrument::invalidSlotId) {
                const int old = findSlot(slots, ownership[note].slotId);
                if (old >= 0) {
                    auto off = source;
                    off.data[0] = static_cast<std::uint8_t>(0x80
                                                           | ownership[note].channel);
                    off.data[1] = ownership[note].transposedNote;
                    off.data[2] = 0;
                    if (!output[static_cast<std::size_t>(old)].push(off)) {
                        deferReleaseRecovery(ownership[note].slotId);
                        continue;
                    }
                }
            }
            if (output[static_cast<std::size_t>(index)].push(source))
                ownership[note] = { selectedSlot, note,
                    static_cast<std::uint8_t>(channelIndex(source)) };
            else
                ownership[note] = {};
        }
    }
}

void RackRouter::routeGenerated(
    std::span<const GeneratedSlotEvents> input,
    std::span<const ProcessSlotControls> slots,
    std::array<DestinationBuffer, instrument::maxSlots>& output) noexcept
{
    slots = slots.first(std::min(slots.size(), instrument::maxSlots));
    for (const auto& stream : input) {
        const int targetIndex = findSlot(slots, stream.slotId);
        if (targetIndex < 0) continue;
        GeneratedDomain* domain = nullptr;
        for (auto& candidate : generatedOwnership_)
            if (candidate.sourceSlotId == stream.slotId) {
                domain = &candidate;
                break;
            }
        if (domain == nullptr)
            for (auto& candidate : generatedOwnership_)
                if (candidate.sourceSlotId == instrument::invalidSlotId) {
                    candidate.sourceSlotId = stream.slotId;
                    domain = &candidate;
                    break;
                }
        if (domain == nullptr) continue;
        auto& ownership = domain->notes;
        for (const auto& source : stream.events) {
            if (!validChannelEvent(source)) continue;
            const auto note = source.size >= 2 ? source.data[1] : 0;
            if (isNoteOff(source)) {
                const auto owner = ownership[note];
                const int ownerIndex = findSlot(slots, owner.slotId);
                if (ownerIndex >= 0) {
                    auto event = source;
                    event.data[0] = static_cast<std::uint8_t>(
                        (source.data[0] & 0xf0) | owner.channel);
                    event.data[1] = owner.transposedNote;
                    if (output[static_cast<std::size_t>(ownerIndex)].push(event))
                        ownership[note] = {};
                    else
                        deferReleaseRecovery(owner.slotId);
                } else {
                    ownership[note] = {};
                }
                continue;
            }
            if (isNoteOn(source)) {
                if (recoveryPending(stream.slotId)) {
                    ++output[static_cast<std::size_t>(targetIndex)].dropped;
                    continue;
                }
                const auto previous = ownership[note];
                const int previousIndex = findSlot(slots, previous.slotId);
                if (previousIndex >= 0) {
                    auto off = source;
                    off.data[0] = static_cast<std::uint8_t>(
                        0x80 | previous.channel);
                    off.data[1] = previous.transposedNote;
                    off.data[2] = 0;
                    if (!output[static_cast<std::size_t>(previousIndex)].push(off)) {
                        deferReleaseRecovery(previous.slotId);
                        continue;
                    }
                }
                ownership[note] = {};
                if (!slots[static_cast<std::size_t>(targetIndex)].enabled)
                    continue;
                auto event = source;
                // Generated pattern pitch is already canonical root + offset.
                // External MIDI transpose/zones do not alter internal content.
                event.data[1] = note;
                if (output[static_cast<std::size_t>(targetIndex)].push(event))
                    ownership[note] = { stream.slotId, event.data[1],
                        static_cast<std::uint8_t>(channelIndex(source)) };
                continue;
            }
            if (slots[static_cast<std::size_t>(targetIndex)].enabled
                && !output[static_cast<std::size_t>(targetIndex)].push(source)
                && isChannelPanic(source))
                deferReleaseRecovery(stream.slotId);
        }
    }
}

void RackRouter::route(
    std::span<const VoxMidiEventV1> input,
    std::span<const ProcessSlotControls> slots,
    std::array<DestinationBuffer, instrument::maxSlots>& output) noexcept
{
    slots = slots.first(std::min(slots.size(), instrument::maxSlots));
    for (auto& destination : output) destination.clear();
    flushReleaseRecovery(slots, output);
    for (const auto& source : input) {
        if (!validChannelEvent(source)) continue;
        const auto channel = channelIndex(source);
        const auto note = source.size >= 2 ? source.data[1] : 0;
        if (isChannelPanic(source)) {
            std::array<bool, instrument::maxSlots> targets {};
            for (std::size_t slotIndex = 0; slotIndex < slots.size(); ++slotIndex)
                targets[slotIndex] = accepts(slots[slotIndex], source);
            for (const auto& owners : ownership_[channel])
                for (std::uint8_t ownerIndex = 0; ownerIndex < owners.size;
                     ++ownerIndex) {
                    const int slotIndex = findSlot(
                        slots, owners.values[ownerIndex].slotId);
                    if (slotIndex >= 0)
                        targets[static_cast<std::size_t>(slotIndex)] = true;
                }
            for (std::size_t slotIndex = 0; slotIndex < slots.size(); ++slotIndex)
                if (targets[slotIndex] && !output[slotIndex].push(source))
                    deferReleaseRecovery(slots[slotIndex].slotId);
            for (auto& owners : ownership_[channel]) owners.size = 0;
            continue;
        }
        if (isNoteOff(source)) {
            auto& owners = ownership_[channel][note];
            std::uint8_t retained = 0;
            for (std::uint8_t ownerIndex = 0; ownerIndex < owners.size;
                 ++ownerIndex) {
                const auto& owner = owners.values[ownerIndex];
                const int slotIndex = findSlot(slots, owner.slotId);
                if (slotIndex < 0) continue;
                auto event = source;
                event.data[1] = owner.transposedNote;
                if (!output[static_cast<std::size_t>(slotIndex)].push(event)) {
                    owners.values[retained++] = owner;
                    deferReleaseRecovery(owner.slotId);
                }
            }
            owners.size = retained;
            continue;
        }
        if (isNoteOn(source)) {
            auto& previous = ownership_[channel][note];
            std::uint8_t retained = 0;
            for (std::uint8_t ownerIndex = 0; ownerIndex < previous.size;
                 ++ownerIndex) {
                const auto& owner = previous.values[ownerIndex];
                const int slotIndex = findSlot(slots, owner.slotId);
                if (slotIndex < 0) continue;
                auto noteOff = source;
                noteOff.data[0] = static_cast<std::uint8_t>(0x80 | channel);
                noteOff.data[1] = owner.transposedNote;
                noteOff.data[2] = 0;
                if (!output[static_cast<std::size_t>(slotIndex)].push(noteOff)) {
                    previous.values[retained++] = owner;
                    deferReleaseRecovery(owner.slotId);
                }
            }
            previous.size = retained;
        }
        for (std::size_t slotIndex = 0; slotIndex < slots.size(); ++slotIndex) {
            const auto& slot = slots[slotIndex];
            if (!accepts(slot, source)) continue;
            if (isNoteOn(source) && recoveryPending(slot.slotId)) {
                ++output[slotIndex].dropped;
                continue;
            }
            auto event = source;
            if (isNoteOn(event)) {
                const int transposed = std::clamp(
                    static_cast<int>(event.data[1]) + slot.routing.transpose,
                    0, 127);
                event.data[1] = static_cast<std::uint8_t>(transposed);
            }
            if (output[slotIndex].push(event) && isNoteOn(event)) {
                auto& owners = ownership_[channel][note];
                if (owners.size < owners.values.size())
                    owners.values[owners.size++] = { slot.slotId, event.data[1],
                        static_cast<std::uint8_t>(channel) };
            }
        }
    }
}
} // namespace vstengine::rack
