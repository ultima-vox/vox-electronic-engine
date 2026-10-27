#include "rack/RackRouter.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

namespace {
int run = 0;
#define REQUIRE(c, m) do { ++run; if (!(c)) { std::cerr << "FAILED: " << m \
    << " line " << __LINE__ << '\n'; return EXIT_FAILURE; } } while (false)
VoxMidiEventV1 note(std::uint8_t status, std::uint8_t value,
                    std::uint8_t velocity = 100)
{
    return { 0, 3, { status, value, velocity } };
}
VoxMidiEventV1 control(std::uint8_t channel, std::uint8_t controller,
                       std::uint32_t sampleOffset = 0)
{
    return { sampleOffset, 3,
             { static_cast<std::uint8_t>(0xb0 | channel), controller, 0 } };
}
} // namespace

int main()
{
    using namespace vstengine;
    std::array<rack::ProcessSlotControls, instrument::maxSlots> slots {};
    slots[0].slotId = instrument::initialSlotId(0);
    slots[0].routing = { rack::RouteMode::channel, 1, 36, 60, 50, 127, 12 };
    slots[1].slotId = instrument::initialSlotId(1);
    // Router level: two targets on one channel still route deterministically.
    // The one-instrument-one-channel contract is enforced by RackState and
    // the processor (see RackIntegrationTests / RackShellIntegrationTests).
    slots[1].routing = { rack::RouteMode::channel, 1, 48, 72, 1, 127, -12 };
    slots[2].slotId = instrument::initialSlotId(2);
    slots[2].routing = { rack::RouteMode::off, 2, 0, 127, 1, 127, 0 };

    rack::RackRouter router;
    std::array<rack::RackRouter::DestinationBuffer, instrument::maxSlots> output;
    const std::array input { note(0x90, 48, 100) };
    router.route(input, slots, output);
    REQUIRE(output[0].size == 1 && output[0].events[0].data[1] == 60,
            "CH1 zone routes and transposes Bass");
    REQUIRE(output[1].size == 1 && output[1].events[0].data[1] == 36,
            "same-channel target routes independently");
    REQUIRE(output[2].size == 0, "OFF slot receives nothing");

    const std::array wrongChannel { note(0x91, 40, 100) };
    router.route(wrongChannel, slots, output);
    REQUIRE(output[0].size == 0, "wrong channel blocked");
    REQUIRE(output[1].size == 0, "same-channel key zone still enforced");

    router.reset();
    const std::array lowVelocity { note(0x90, 48, 20) };
    router.route(lowVelocity, slots, output);
    REQUIRE(output[0].size == 0 && output[1].size == 1,
            "velocity zones isolate overlapping slots");

    router.route(input, slots, output);
    std::swap(slots[0], slots[1]);
    slots[1].routing.mode = rack::RouteMode::off;
    const std::array off { note(0x80, 48, 0) };
    router.route(off, slots, output);
    REQUIRE(output[0].size == 1 && output[0].events[0].data[1] == 36,
            "NoteOff follows same-channel SlotId after reorder");
    REQUIRE(output[1].size == 1 && output[1].events[0].data[1] == 60,
            "NoteOff follows Bass SlotId after route disabled");

    router.reset();
    router.route(off, slots, output);
    REQUIRE(output[0].size == 0 && output[1].size == 0,
            "reset clears note ownership");

    for (const std::uint8_t controller : { std::uint8_t { 120 },
                                           std::uint8_t { 123 } }) {
        std::array<rack::ProcessSlotControls, instrument::maxSlots> panicSlots {};
        panicSlots[0].slotId = instrument::initialSlotId(0);
        panicSlots[0].routing = { rack::RouteMode::channel, 1 };
        panicSlots[1].slotId = instrument::initialSlotId(1);
        panicSlots[1].routing = { rack::RouteMode::channel, 1 };
        panicSlots[2].slotId = instrument::initialSlotId(2);
        panicSlots[2].routing = { rack::RouteMode::off, 1 };
        panicSlots[3].slotId = instrument::initialSlotId(3);
        panicSlots[3].routing = { rack::RouteMode::channel, 2 };

        auto panicRouter = std::make_unique<rack::RackRouter>();
        auto panicOutput = std::make_unique<std::array<
            rack::RackRouter::DestinationBuffer, instrument::maxSlots>>();
        const std::array held { note(0x90, 52) };
        panicRouter->route(held, panicSlots, *panicOutput);
        panicSlots[1].routing.channel = 2; // Owner must survive live reroute.
        panicSlots[2].routing.mode = rack::RouteMode::channel;
        const std::array panic { control(0, controller, 17) };
        panicRouter->route(panic, panicSlots, *panicOutput);
        REQUIRE((*panicOutput)[0].size == 1
                    && (*panicOutput)[0].events[0].sampleOffset == 17,
                "channel panic reaches current target once");
        REQUIRE((*panicOutput)[1].size == 1,
                "channel panic reaches prior SlotId owner after reroute");
        REQUIRE((*panicOutput)[2].size == 1,
                "channel panic reaches newly assigned current target");
        REQUIRE((*panicOutput)[3].size == 0,
                "channel panic cannot affect unrelated channel");
        panicRouter->route(std::array { note(0x80, 52, 0) }, panicSlots,
                           *panicOutput);
        REQUIRE((*panicOutput)[0].size == 0 && (*panicOutput)[1].size == 0,
                "channel panic clears source-channel ownership");
    }

    {
        std::array<rack::ProcessSlotControls, instrument::maxSlots> edgeSlots {};
        edgeSlots[0].slotId = instrument::initialSlotId(0);
        edgeSlots[0].routing = { rack::RouteMode::channel, 1 };
        auto edgeRouter = std::make_unique<rack::RackRouter>();
        auto edgeOutput = std::make_unique<std::array<
            rack::RackRouter::DestinationBuffer, instrument::maxSlots>>();
        auto first = note(0x90, 40);
        first.sampleOffset = 0;
        auto last = note(0x90, 41);
        last.sampleOffset = 511;
        edgeRouter->route(std::array { first, last }, edgeSlots, *edgeOutput);
        REQUIRE((*edgeOutput)[0].size == 2
                    && (*edgeOutput)[0].events[0].sampleOffset == 0
                    && (*edgeOutput)[0].events[1].sampleOffset == 511,
                "block-edge MIDI offsets preserved exactly");
    }

    {
        std::array<rack::ProcessSlotControls, instrument::maxSlots> fullSlots {};
        fullSlots[0].slotId = instrument::initialSlotId(0);
        fullSlots[0].routing = { rack::RouteMode::channel, 1 };
        auto fullRouter = std::make_unique<rack::RackRouter>();
        auto fullOutput = std::make_unique<std::array<
            rack::RackRouter::DestinationBuffer, instrument::maxSlots>>();
        std::vector<VoxMidiEventV1> events(
            rack::RackRouter::eventCapacity, control(0, 1));
        events.push_back(note(0x90, 60));
        fullRouter->route(events, fullSlots, *fullOutput);
        REQUIRE((*fullOutput)[0].size == rack::RackRouter::eventCapacity
                    && (*fullOutput)[0].dropped == 1,
                "full destination reports deterministic overflow");
        fullRouter->route(std::array { note(0x80, 60, 0) }, fullSlots,
                          *fullOutput);
        REQUIRE((*fullOutput)[0].size == 0,
                "dropped NoteOn never commits phantom ownership");

        for (std::size_t i = 0; i < rack::RackRouter::eventCapacity; ++i)
            (*fullOutput)[0].push(control(0, 1));
        fullRouter->routeAudition(std::array { note(0x90, 61) },
                                  fullSlots[0].slotId, fullSlots, *fullOutput);
        for (auto& destination : *fullOutput) destination.clear();
        fullRouter->routeAudition(std::array { note(0x80, 61, 0) },
                                  fullSlots[0].slotId, fullSlots, *fullOutput);
        REQUIRE((*fullOutput)[0].size == 0,
                "dropped audition NoteOn never commits phantom ownership");
    }
    {
        std::array<rack::ProcessSlotControls, instrument::maxSlots> generatedSlots {};
        generatedSlots[0].slotId = instrument::initialSlotId(0);
        generatedSlots[0].routing = { rack::RouteMode::off, 1, 0, 127, 1, 127, 12 };
        generatedSlots[1].slotId = instrument::initialSlotId(1);
        generatedSlots[1].routing = { rack::RouteMode::channel, 16, 0, 127, 1, 127, -12 };
        auto generatedRouter = std::make_unique<rack::RackRouter>();
        auto generatedOutput = std::make_unique<std::array<
            rack::RackRouter::DestinationBuffer, instrument::maxSlots>>();
        generatedRouter->route(std::span<const VoxMidiEventV1> {},
                               generatedSlots, *generatedOutput);
        const std::array firstOn { note(0x90, 60) };
        const std::array secondOn { note(0x90, 60) };
        const std::array streams {
            rack::RackRouter::GeneratedSlotEvents { generatedSlots[0].slotId,
                                                    firstOn },
            rack::RackRouter::GeneratedSlotEvents { generatedSlots[1].slotId,
                                                    secondOn }
        };
        generatedRouter->routeGenerated(streams, generatedSlots, *generatedOutput);
        REQUIRE((*generatedOutput)[0].size == 1
                    && (*generatedOutput)[0].events[0].data[1] == 60
                    && (*generatedOutput)[1].size == 1
                    && (*generatedOutput)[1].events[0].data[1] == 60,
                "generated streams target SlotId and bypass route transpose/off");
        for (auto& destination : *generatedOutput) destination.clear();
        const std::array firstOff { note(0x80, 60, 0) };
        const std::array firstOffStream {
            rack::RackRouter::GeneratedSlotEvents { generatedSlots[0].slotId,
                                                    firstOff }
        };
        generatedRouter->routeGenerated(firstOffStream, generatedSlots,
                                        *generatedOutput);
        REQUIRE((*generatedOutput)[0].size == 1 && (*generatedOutput)[1].size == 0,
                "same-pitch generated ownership isolated per source SlotId");
        for (auto& destination : *generatedOutput) destination.clear();
        const std::array secondOffStream {
            rack::RackRouter::GeneratedSlotEvents { generatedSlots[1].slotId,
                                                    firstOff }
        };
        generatedRouter->routeGenerated(secondOffStream, generatedSlots,
                                        *generatedOutput);
        REQUIRE((*generatedOutput)[0].size == 0 && (*generatedOutput)[1].size == 1,
                "second generated owner survives first source NoteOff");
    }
    {
        rack::RackRouter::DestinationBuffer ordered;
        auto late = note(0x90, 60); late.sampleOffset = 100;
        auto early = note(0x80, 60, 0); early.sampleOffset = 0;
        ordered.push(late); ordered.push(early); ordered.sortBySampleOffset();
        REQUIRE(ordered.events[0].sampleOffset == 0
                    && ordered.events[1].sampleOffset == 100,
                "mixed MIDI sources stable-sort before DSP");
    }
    // A lost release must recover without waiting for a second NoteOff.
    // Exercise every source through the same next-block boundary used by Rack.
    for (int domain = 0; domain < 3; ++domain) {
        auto safeRouter = std::make_unique<rack::RackRouter>();
        auto safeOutput = std::make_unique<std::array<
            rack::RackRouter::DestinationBuffer, instrument::maxSlots>>();
        std::array<rack::ProcessSlotControls, instrument::maxSlots> safeSlots {};
        safeSlots[0].slotId = instrument::initialSlotId(0);
        safeSlots[0].routing = { rack::RouteMode::channel, 1 };
        safeSlots[1].slotId = instrument::initialSlotId(1);
        safeSlots[1].routing = { rack::RouteMode::channel, 2 };
        const auto sourceSlotId = safeSlots[0].slotId;
        auto send = [&](std::span<const VoxMidiEventV1> events) {
            if (domain == 0) safeRouter->route(events, safeSlots, *safeOutput);
            else if (domain == 1)
                safeRouter->routeAudition(events, sourceSlotId,
                                          safeSlots, *safeOutput);
            else {
                const std::array streams {
                    rack::RackRouter::GeneratedSlotEvents {
                        sourceSlotId, events }
                };
                safeRouter->routeGenerated(streams, safeSlots, *safeOutput);
            }
        };
        send(std::array { note(0x90, 60) });
        safeRouter->route(std::array { note(0x91, 65) }, safeSlots, *safeOutput);
        std::vector<VoxMidiEventV1> flood(
            rack::RackRouter::eventCapacity, control(0, 1));
        if (domain == 0) {
            flood.push_back(note(0x80, 60, 0));
            send(flood);
        } else {
            safeRouter->route(flood, safeSlots, *safeOutput);
            send(std::array { note(0x80, 60, 0) });
        }
        REQUIRE((*safeOutput)[0].dropped == 1,
                "overflowed release is reported for every source domain");
        std::swap(safeSlots[0], safeSlots[1]);
        safeSlots[1].routing.mode = rack::RouteMode::off;
        safeSlots[1].enabled = false;
        safeRouter->route({}, safeSlots, *safeOutput);
        REQUIRE((*safeOutput)[1].size == 16 && (*safeOutput)[0].size == 0,
                "next block recovers only affected SlotId after reorder/disable");
        for (std::size_t channel = 0; channel < 16; ++channel)
            REQUIRE((*safeOutput)[1].events[channel].data[0] == 0xb0 + channel
                        && (*safeOutput)[1].events[channel].data[1] == 120
                        && (*safeOutput)[1].events[channel].sampleOffset == 0,
                    "bounded slot fail-safe covers all channels at block start");
        safeRouter->route({}, safeSlots, *safeOutput);
        send(std::array { note(0x80, 60, 0) });
        REQUIRE((*safeOutput)[0].size == 0 && (*safeOutput)[1].size == 0,
                "old release after recovery has no stale ownership in any domain");
        safeRouter->route(std::array { note(0x81, 65, 0) }, safeSlots, *safeOutput);
        REQUIRE((*safeOutput)[0].size == 1 && (*safeOutput)[1].size == 0,
                "recovery leaves another slot's held-note ownership intact");
        safeRouter->route({}, safeSlots, *safeOutput);
        REQUIRE((*safeOutput)[0].size == 0 && (*safeOutput)[1].size == 0,
                "release recovery does not repeat in later blocks");
    }
    // Lost channel panic, recovery lifecycle and fresh-note ordering.
    for (const std::uint8_t controller : { std::uint8_t { 120 },
                                           std::uint8_t { 123 } }) {
        auto recoveryRouter = std::make_unique<rack::RackRouter>();
        auto recoveryOutput = std::make_unique<std::array<
            rack::RackRouter::DestinationBuffer, instrument::maxSlots>>();
        std::array<rack::ProcessSlotControls, instrument::maxSlots> targets {};
        targets[0].slotId = instrument::initialSlotId(0);
        targets[0].routing = { rack::RouteMode::channel, 1 };
        auto overflowPanic = [&] {
            recoveryRouter->route(std::array { note(0x90, 60) }, targets,
                                  *recoveryOutput);
            std::vector<VoxMidiEventV1> flood(
                rack::RackRouter::eventCapacity, control(0, 1));
            flood.push_back(control(0, controller));
            recoveryRouter->route(flood, targets, *recoveryOutput);
        };
        overflowPanic();
        recoveryRouter->route(std::array { note(0x90, 64) }, targets,
                              *recoveryOutput);
        REQUIRE((*recoveryOutput)[0].size == 17
                    && (*recoveryOutput)[0].events[16].data[0] == 0x90
                    && (*recoveryOutput)[0].events[16].data[1] == 64,
                "lost panic recovers before new same-offset notes");
        REQUIRE((*recoveryOutput)[0].events[0].data[1] == 120,
                "panic recovery is All Sound Off even for lost CC123");
        overflowPanic();
        recoveryRouter->forgetSlot(targets[0].slotId);
        recoveryRouter->route({}, targets, *recoveryOutput);
        REQUIRE((*recoveryOutput)[0].size == 0,
                "instrument replacement cancels obsolete recovery");
        overflowPanic();
        recoveryRouter->reset();
        recoveryRouter->route({}, targets, *recoveryOutput);
        REQUIRE((*recoveryOutput)[0].size == 0,
                "reset cancels queued recovery");
        overflowPanic();
        targets[0].slotId = instrument::initialSlotId(7);
        recoveryRouter->route({}, targets, *recoveryOutput);
        REQUIRE((*recoveryOutput)[0].size == 0,
                "removed SlotId cannot panic its replacement by array index");
    }
    // Audition and generated note ownership keeps the original MIDI channel.
    for (int domain = 1; domain <= 2; ++domain) {
        auto channelRouter = std::make_unique<rack::RackRouter>();
        auto channelOutput = std::make_unique<std::array<
            rack::RackRouter::DestinationBuffer, instrument::maxSlots>>();
        std::array<rack::ProcessSlotControls, instrument::maxSlots> targets {};
        targets[0].slotId = instrument::initialSlotId(0);
        auto send = [&](const VoxMidiEventV1& event) {
            const std::array events { event };
            if (domain == 1)
                channelRouter->routeAudition(events, targets[0].slotId,
                                             targets, *channelOutput);
            else {
                const std::array streams {
                    rack::RackRouter::GeneratedSlotEvents { targets[0].slotId, events }
                };
                channelRouter->routeGenerated(streams, targets, *channelOutput);
            }
        };
        send(note(0x93, 60));
        for (auto& destination : *channelOutput) destination.clear();
        send(note(0x91, 60));
        REQUIRE((*channelOutput)[0].size == 2
                    && (*channelOutput)[0].events[0].data[0] == 0x83
                    && (*channelOutput)[0].events[1].data[0] == 0x91,
                "retrigger releases the old channel before accepting the new one");
        for (auto& destination : *channelOutput) destination.clear();
        send(note(0x85, 60, 0));
        REQUIRE((*channelOutput)[0].size == 1
                    && (*channelOutput)[0].events[0].data[0] == 0x81,
                "NoteOff returns to the original sounding channel");
    }
    // Reject malformed bytes/lengths before all three ownership lookups.
    {
        auto invalidRouter = std::make_unique<rack::RackRouter>();
        auto invalidOutput = std::make_unique<std::array<
            rack::RackRouter::DestinationBuffer, instrument::maxSlots>>();
        std::array<rack::ProcessSlotControls, instrument::maxSlots> targets {};
        targets[0].slotId = instrument::initialSlotId(0);
        targets[0].routing = { rack::RouteMode::channel, 1 };
        const std::array malformed {
            note(0x90, 128), note(0x80, 255, 0), note(0x90, 60, 255),
            VoxMidiEventV1 { 2, 2, { 0x90, 60, 0 } },
            VoxMidiEventV1 { 2, 4, { 0x90, 60, 90 } },
            VoxMidiEventV1 { 2, 3, { 0x70, 60, 90 } },
            VoxMidiEventV1 { 2, 1, { 0xf8, 0, 0 } }
        };
        invalidRouter->route(malformed, targets, *invalidOutput);
        invalidRouter->routeAudition(malformed, targets[0].slotId,
                                     targets, *invalidOutput);
        const std::array streams {
            rack::RackRouter::GeneratedSlotEvents { targets[0].slotId, malformed }
        };
        invalidRouter->routeGenerated(streams, targets, *invalidOutput);
        REQUIRE((*invalidOutput)[0].size == 0,
                "malformed channel events cannot reach DSP or ownership arrays");
        invalidRouter->route(std::array { note(0x80, 60, 0) }, targets,
                             *invalidOutput);
        REQUIRE((*invalidOutput)[0].size == 0,
                "invalid events never create phantom note ownership");
        const std::array pressure { VoxMidiEventV1 { 7, 2, { 0xd0, 99, 255 } } };
        invalidRouter->route(pressure, targets, *invalidOutput);
        REQUIRE((*invalidOutput)[0].size == 1
                    && (*invalidOutput)[0].events[0].sampleOffset == 7,
                "valid two-byte channel messages ignore unused payload bytes");
        invalidRouter->route({}, targets, *invalidOutput);
        invalidRouter->routeAudition(std::array { note(0x90, 60) },
                                     instrument::invalidSlotId, targets, *invalidOutput);
        const std::array emptySlotStreams {
            rack::RackRouter::GeneratedSlotEvents { instrument::invalidSlotId, pressure }
        };
        invalidRouter->routeGenerated(emptySlotStreams, targets, *invalidOutput);
        REQUIRE(std::all_of(invalidOutput->begin(), invalidOutput->end(),
                    [](const auto& buffer) { return buffer.size == 0; }),
                "invalid SlotId cannot resolve to an empty slot");
        for (const int field : { 0, 1, 2 }) {
            auto bad = targets[0].routing;
            if (field == 0) bad.mode = static_cast<rack::RouteMode>(255);
            if (field == 1) bad.keyHigh = 128;
            if (field == 2) bad.velocityHigh = 128;
            REQUIRE(!rack::valid(bad), "invalid routing enum/7-bit zones rejected");
            targets[0].routing = bad;
            invalidRouter->route(std::array { note(0x90, 60) }, targets,
                                 *invalidOutput);
            REQUIRE((*invalidOutput)[0].size == 0,
                    "invalid routing cannot bypass channel authorization");
            targets[0].routing = { rack::RouteMode::channel, 1 };
        }
    }
    {
        auto layerRouter = std::make_unique<rack::RackRouter>();
        auto layerOutput = std::make_unique<std::array<
            rack::RackRouter::DestinationBuffer, instrument::maxSlots>>();
        std::array<rack::ProcessSlotControls, instrument::maxSlots> targets {};
        for (std::size_t i = 0; i < targets.size(); ++i) {
            targets[i].slotId = instrument::initialSlotId(i);
            targets[i].routing = { rack::RouteMode::channel, 1 };
        }
        layerRouter->route(std::array { note(0x90, 60) }, targets, *layerOutput);
        std::vector<VoxMidiEventV1> flood(
            rack::RackRouter::eventCapacity, control(0, 1));
        flood.push_back(note(0x80, 60, 0));
        layerRouter->route(flood, targets, *layerOutput);
        layerRouter->route({}, targets, *layerOutput);
        for (const auto& destination : *layerOutput)
            REQUIRE(destination.size == 16,
                    "all sixteen same-channel targets can queue and flush recovery");
    }
    {
        auto generatedRouter = std::make_unique<rack::RackRouter>();
        auto generatedOutput = std::make_unique<std::array<
            rack::RackRouter::DestinationBuffer, instrument::maxSlots>>();
        std::array<rack::ProcessSlotControls, instrument::maxSlots> targets {};
        targets[0].slotId = instrument::initialSlotId(0);
        targets[0].routing = { rack::RouteMode::channel, 1 };
        std::vector<VoxMidiEventV1> flood(
            rack::RackRouter::eventCapacity, control(0, 1));
        generatedRouter->route(flood, targets, *generatedOutput);
        const std::array panic { control(0, 123) };
        const std::array streams {
            rack::RackRouter::GeneratedSlotEvents { targets[0].slotId, panic }
        };
        generatedRouter->routeGenerated(streams, targets, *generatedOutput);
        // An audition NoteOn cannot be admitted behind an already lost release.
        (*generatedOutput)[0].clear();
        generatedRouter->routeAudition(std::array { note(0x90, 62) },
                                       targets[0].slotId, targets, *generatedOutput);
        REQUIRE((*generatedOutput)[0].size == 0,
                "pending recovery rejects fresh notes even if capacity is freed");
        generatedRouter->route({}, targets, *generatedOutput);
        REQUIRE((*generatedOutput)[0].size == 16,
                "lost generated panic also schedules autonomous recovery");
    }
    for (int domain = 1; domain <= 2; ++domain) {
        auto retriggerRouter = std::make_unique<rack::RackRouter>();
        auto retriggerOutput = std::make_unique<std::array<
            rack::RackRouter::DestinationBuffer, instrument::maxSlots>>();
        std::array<rack::ProcessSlotControls, instrument::maxSlots> targets {};
        targets[0].slotId = instrument::initialSlotId(0);
        targets[0].routing = { rack::RouteMode::channel, 1 };
        targets[1].slotId = instrument::initialSlotId(1);
        auto send = [&](instrument::SlotId target) {
            const std::array on { note(0x90, 60) };
            if (domain == 1)
                retriggerRouter->routeAudition(on, target, targets, *retriggerOutput);
            else {
                const std::array stream {
                    rack::RackRouter::GeneratedSlotEvents { target, on }
                };
                retriggerRouter->routeGenerated(stream, targets, *retriggerOutput);
            }
        };
        send(targets[0].slotId);
        std::vector<VoxMidiEventV1> flood(
            rack::RackRouter::eventCapacity, control(0, 1));
        retriggerRouter->route(flood, targets, *retriggerOutput);
        // Audition switches slots while still holding the same key.
        send(targets[domain == 1 ? 1 : 0].slotId);
        REQUIRE((*retriggerOutput)[1].size == 0,
                "failed old-owner release cannot start a new audition owner");
        retriggerRouter->route({}, targets, *retriggerOutput);
        REQUIRE((*retriggerOutput)[0].size == 16
                    && (*retriggerOutput)[1].size == 0,
                "lost synthetic retrigger release recovers its original slot");
    }
    {
        auto boundedRouter = std::make_unique<rack::RackRouter>();
        auto boundedOutput = std::make_unique<std::array<
            rack::RackRouter::DestinationBuffer, instrument::maxSlots>>();
        std::array<rack::ProcessSlotControls, instrument::maxSlots + 1> tooMany {};
        tooMany.back().slotId = instrument::initialSlotId(instrument::maxSlots);
        tooMany.back().routing = { rack::RouteMode::channel, 1 };
        const std::array on { note(0x90, 60) };
        boundedRouter->route(on, tooMany, *boundedOutput);
        boundedRouter->routeAudition(on, tooMany.back().slotId, tooMany, *boundedOutput);
        const std::array stream {
            rack::RackRouter::GeneratedSlotEvents { tooMany.back().slotId, on }
        };
        boundedRouter->routeGenerated(stream, tooMany, *boundedOutput);
        REQUIRE(std::all_of(boundedOutput->begin(), boundedOutput->end(),
                    [](const auto& buffer) { return buffer.size == 0; }),
                "oversized slot spans cannot index past bounded outputs");
    }
    std::cout << "Rack router tests passed (" << run << ")\n";
    return EXIT_SUCCESS;
}
