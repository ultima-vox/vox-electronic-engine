// Differential equivalence of the #107 optimisations (review 6647774929):
// the current RackRouter and AudioRouting against frozen copies of their
// pre-optimisation versions (tests/rack/reference, from 98782ea), driven with
// the same randomized inputs. Everything observable must be identical:
//  - router: per destination slot, every event (offset, size, bytes) in
//    order, and the dropped count, over sequences of blocks that keep state
//    (note ownership, release recovery), with route changes, forgetSlot and
//    reset in between, floods past the 2048-event capacity, CC120/123,
//    velocity-0 NoteOns, non-note messages and invalid events;
//  - audio routing: Main, an active B2 host output, every return (after
//    finish(), FX tails included), bit for bit, with random slot send gains
//    (0, -0, non-zero), pre/post, audibility, ducking and module sends, and
//    silent blocks (+0/-0 only) on the slot, its output bus and module sends.
#include "RackRouterV1.h"
#include "AudioRoutingV1.h"
#include "rack/AudioRouting.h"
#include "rack/RackRouter.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace {
using namespace vstengine;
using rack::ProcessSlotControls;
using rack::RouteMode;

int checks = 0;
void check(bool ok, const std::string &message) {
  ++checks;
  if (!ok) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

std::mt19937 rng(20261105);
int uniform(int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); }
bool chance(double p) { return std::bernoulli_distribution(p)(rng); }

void randomRouting(ProcessSlotControls &slot, std::size_t index) {
  slot.slotId = chance(0.05) ? instrument::invalidSlotId
                             : static_cast<instrument::SlotId>(100 + index);
  slot.enabled = !chance(0.1);
  slot.routing.mode = chance(0.1) ? RouteMode::off : RouteMode::channel;
  // Few channels, so several slots often share one.
  slot.routing.channel = static_cast<std::uint8_t>(chance(0.6) ? uniform(1, 3) : uniform(1, 16));
  slot.routing.keyLow = static_cast<std::uint8_t>(chance(0.5) ? 0 : uniform(0, 100));
  slot.routing.keyHigh = static_cast<std::uint8_t>(
      chance(0.5) ? 127 : uniform(slot.routing.keyLow, 127));
  slot.routing.velocityLow = static_cast<std::uint8_t>(chance(0.5) ? 1 : uniform(1, 100));
  slot.routing.velocityHigh = static_cast<std::uint8_t>(
      chance(0.5) ? 127 : uniform(slot.routing.velocityLow, 127));
  slot.routing.transpose = static_cast<std::int8_t>(chance(0.5) ? 0 : uniform(-48, 48));
  if (chance(0.03)) // invalid routing: the router must ignore the slot
    slot.routing.keyLow = static_cast<std::uint8_t>(slot.routing.keyHigh + 1);
}

VoxMidiEventV1 randomEvent(std::uint32_t samples) {
  VoxMidiEventV1 e{};
  e.sampleOffset = static_cast<std::uint32_t>(uniform(0, static_cast<int>(samples) - 1));
  const int channel = chance(0.6) ? uniform(0, 2) : uniform(0, 15);
  const int note = chance(0.5) ? uniform(36, 40) : uniform(0, 127); // overlaps
  const int kind = uniform(0, 99);
  e.size = 3;
  if (kind < 35) {
    e.data[0] = static_cast<std::uint8_t>(0x90 | channel);
    e.data[1] = static_cast<std::uint8_t>(note);
    e.data[2] = static_cast<std::uint8_t>(chance(0.1) ? 0 : uniform(1, 127));
  } else if (kind < 65) {
    e.data[0] = static_cast<std::uint8_t>(0x80 | channel);
    e.data[1] = static_cast<std::uint8_t>(note);
    e.data[2] = static_cast<std::uint8_t>(uniform(0, 127));
  } else if (kind < 70) {
    e.data[0] = static_cast<std::uint8_t>(0xb0 | channel);
    e.data[1] = static_cast<std::uint8_t>(chance(0.5) ? 120 : 123);
    e.data[2] = 0;
  } else if (kind < 85) {
    e.data[0] = static_cast<std::uint8_t>(0xb0 | channel);
    e.data[1] = static_cast<std::uint8_t>(uniform(0, 119));
    e.data[2] = static_cast<std::uint8_t>(uniform(0, 127));
  } else if (kind < 90) {
    e.data[0] = static_cast<std::uint8_t>(0xe0 | channel);
    e.data[1] = static_cast<std::uint8_t>(uniform(0, 127));
    e.data[2] = static_cast<std::uint8_t>(uniform(0, 127));
  } else if (kind < 95) {
    e.size = 2;
    e.data[0] = static_cast<std::uint8_t>((chance(0.5) ? 0xc0 : 0xd0) | channel);
    e.data[1] = static_cast<std::uint8_t>(uniform(0, 127));
  } else { // invalid: wrong size, status byte or data byte
    e.size = static_cast<std::uint8_t>(uniform(0, 4));
    e.data[0] = static_cast<std::uint8_t>(uniform(0, 255));
    e.data[1] = static_cast<std::uint8_t>(uniform(0, 255));
    e.data[2] = static_cast<std::uint8_t>(uniform(0, 255));
  }
  return e;
}

template <class A, class B>
void sameOutput(const A &current, const B &reference, const std::string &where) {
  for (std::size_t s = 0; s < instrument::maxSlots; ++s) {
    const auto &a = current[s];
    const auto &b = reference[s];
    bool same = a.size == b.size && a.dropped == b.dropped;
    for (std::size_t i = 0; same && i < a.size; ++i)
      same = a.events[i].sampleOffset == b.events[i].sampleOffset &&
             a.events[i].size == b.events[i].size &&
             std::memcmp(a.events[i].data, b.events[i].data, 3) == 0;
    check(same, where + ": destination " + std::to_string(s) + " differs (" +
                    std::to_string(a.size) + "/" + std::to_string(a.dropped) + " vs " +
                    std::to_string(b.size) + "/" + std::to_string(b.dropped) + ")");
  }
}

void routerEquivalence() {
  auto current = std::make_unique<rack::RackRouter>();
  auto reference = std::make_unique<rack::reference::RackRouter>();
  auto outA = std::make_unique<std::array<rack::RackRouter::DestinationBuffer, instrument::maxSlots>>();
  auto outB = std::make_unique<
      std::array<rack::reference::RackRouter::DestinationBuffer, instrument::maxSlots>>();
  std::vector<ProcessSlotControls> slots(instrument::maxSlots);
  std::vector<VoxMidiEventV1> events;
  std::size_t blocks = 0, routed = 0, dropped = 0, shared = 0;
  for (int sequence = 0; sequence < 1000; ++sequence) {
    for (std::size_t i = 0; i < slots.size(); ++i)
      randomRouting(slots[i], i);
    current->reset();
    reference->reset();
    for (int block = 0; block < 24; ++block, ++blocks) {
      const auto samples = static_cast<std::uint32_t>(chance(0.5) ? 64 : uniform(1, 512));
      if (chance(0.15)) // a route change between blocks (ownership must follow)
        randomRouting(slots[static_cast<std::size_t>(uniform(0, 15))],
                      static_cast<std::size_t>(uniform(0, 15)));
      if (chance(0.05)) {
        const auto id = slots[static_cast<std::size_t>(uniform(0, 15))].slotId;
        current->forgetSlot(id);
        reference->forgetSlot(id);
      }
      if (chance(0.02)) {
        current->reset();
        reference->reset();
      }
      events.clear();
      // Occasionally a flood past one destination's capacity, so overflow,
      // dropped counts and release recovery are compared too.
      const int count = chance(0.08) ? uniform(2400, 3000) : uniform(0, 300);
      const bool flood = count > 2048;
      for (int k = 0; k < count; ++k) {
        auto e = randomEvent(samples);
        if (flood && chance(0.97)) { // almost all of a flood on channel 1
          e.data[0] = static_cast<std::uint8_t>((e.data[0] & 0xf0) | 0x00);
          if (e.size < 2 || e.data[0] < 0x80)
            e = VoxMidiEventV1{e.sampleOffset, 3, {0x90, 40, 100}};
        }
        events.push_back(e);
      }
      current->route(events, slots, *outA);
      reference->route(events, slots, *outB);
      sameOutput(*outA, *outB, "route sequence " + std::to_string(sequence) + " block " +
                                   std::to_string(block));
      for (std::size_t s = 0; s < instrument::maxSlots; ++s) {
        routed += (*outA)[s].size;
        dropped += (*outA)[s].dropped;
      }
      // Generated and audition streams share the ownership tables.
      if (chance(0.3)) {
        std::vector<VoxMidiEventV1> generated;
        for (int k = uniform(0, 20); k > 0; --k)
          generated.push_back(randomEvent(samples));
        const auto source = slots[static_cast<std::size_t>(uniform(0, 15))].slotId;
        const rack::RackRouter::GeneratedSlotEvents ga{source, generated};
        const rack::reference::RackRouter::GeneratedSlotEvents gb{source, generated};
        current->routeGenerated({&ga, 1}, slots, *outA);
        reference->routeGenerated({&gb, 1}, slots, *outB);
        sameOutput(*outA, *outB, "generated, sequence " + std::to_string(sequence));
      }
      if (chance(0.2)) {
        std::vector<VoxMidiEventV1> audition;
        for (int k = uniform(0, 8); k > 0; --k)
          audition.push_back(randomEvent(samples));
        const auto selected = slots[static_cast<std::size_t>(uniform(0, 15))].slotId;
        current->routeAudition(audition, selected, slots, *outA);
        reference->routeAudition(audition, selected, slots, *outB);
        sameOutput(*outA, *outB, "audition, sequence " + std::to_string(sequence));
      }
    }
    for (std::size_t a = 0; a < slots.size(); ++a)
      for (std::size_t b = a + 1; b < slots.size(); ++b)
        shared += slots[a].routing.channel == slots[b].routing.channel ? 1 : 0;
  }
  check(dropped > 0, "floods overflowed destinations (dropped " + std::to_string(dropped) + ")");
  check(shared > 0 && routed > 100000, "slots shared channels and many events were routed");
  std::cout << "router: " << blocks << " blocks, " << routed << " events routed, " << dropped
            << " dropped, identical to the pre-optimisation router\n";
}

void audioEquivalence() {
  constexpr std::uint32_t maxBlock = 256;
  const instrument::PrepareSpec spec{48000.0, maxBlock, 2};
  rack::AudioRouting current;
  rack::reference::AudioRouting reference;
  check(current.prepare(spec) && reference.prepare(spec), "both prepare");
  rack::AudioRoutingControls controls;
  std::vector<ProcessSlotControls> slots(instrument::maxSlots);
  for (std::size_t i = 0; i < slots.size(); ++i) {
    slots[i].slotId = static_cast<instrument::SlotId>(100 + i);
    slots[i].routing.mode = RouteMode::channel;
    slots[i].routing.channel = static_cast<std::uint8_t>(i + 1);
    controls.routeSlotIds[i] = slots[i].slotId;
  }
  std::array<rack::RackRouter::DestinationBuffer, instrument::maxSlots> routed{};
  auto buffer = [](std::size_t n) { return std::vector<float>(n); };
  std::array<std::vector<float>, 2> mainA{buffer(maxBlock), buffer(maxBlock)},
      mainB{buffer(maxBlock), buffer(maxBlock)}, hostA{buffer(maxBlock), buffer(maxBlock)},
      hostB{buffer(maxBlock), buffer(maxBlock)}, in{buffer(maxBlock), buffer(maxBlock)},
      aux{buffer(maxBlock), buffer(maxBlock)};
  std::array<std::array<std::vector<float>, 2>, rack::sendBusCount> moduleBus;
  for (auto &bus : moduleBus)
    bus = {buffer(maxBlock), buffer(maxBlock)};
  std::size_t blocks = 0, zeroGainSends = 0, silentBlocks = 0;
  for (int block = 0; block < 4000; ++block, ++blocks) {
    const auto n = static_cast<std::uint32_t>(uniform(1, static_cast<int>(maxBlock)));
    if (block % 50 == 0) { // new controls now and then
      for (std::size_t r = 0; r < instrument::maxSlots; ++r)
        for (auto &send : controls.sends[r]) {
          const int pick = uniform(0, 3);
          send.gain = pick == 0 ? 0.0f : pick == 1 ? -0.0f : static_cast<float>(uniform(1, 100)) / 100.0f;
          send.preFader = chance(0.5);
          zeroGainSends += send.gain == 0.0f ? 1 : 0;
        }
      for (std::size_t b = 0; b < rack::sendBusCount; ++b) {
        controls.returns[b].enabled = !chance(0.2);
        controls.returns[b].type = static_cast<effects::EffectType>(uniform(0, 7)); // delay/reverb tails too
      }
      controls.kickSource = slots[static_cast<std::size_t>(uniform(0, 15))].slotId;
      for (auto &d : controls.duckDepth)
        d = static_cast<float>(uniform(0, 10)) / 10.0f;
    }
    // Kick triggers in the source's routed MIDI drive the duck envelope.
    for (auto &r : routed)
      r.clear();
    const auto sourceIndex = static_cast<std::size_t>(controls.kickSource - 100);
    if (chance(0.3))
      (void)routed[sourceIndex].push({static_cast<std::uint32_t>(uniform(0, static_cast<int>(n) - 1)), 3,
                                      {0x90, controls.kickNote, 100}});
    check(current.begin(controls, slots, routed, n) == reference.begin(controls, slots, routed, n),
          "begin agrees");
    for (auto *v : {&mainA, &mainB, &hostA, &hostB})
      for (auto &c : *v)
        std::fill(c.begin(), c.end(), 0.0f);
    for (std::size_t slot = 0; slot < 6; ++slot) {
      // Some signals are silent this block (+0 / -0 only): the optimised mix
      // skips them, the reference adds them.
      // 0: sound, 1: silent (+0/-0 only), 2: tiny but not silent (a mix that
      // skipped "almost silent" blocks would differ).
      auto kind = [] { return uniform(0, 9) < 3 ? 1 : uniform(0, 9) < 2 ? 2 : 0; };
      const int quietInKind = kind(), quietAuxKind = kind();
      const bool quietIn = quietInKind == 1, quietAux = quietAuxKind == 1;
      std::array<int, rack::sendBusCount> quietBus{};
      for (auto &q : quietBus)
        q = kind();
      auto value = [](int k) {
        if (k == 1)
          return chance(0.5) ? 0.0f : -0.0f;
        const float x = static_cast<float>(uniform(-1000, 1000)) / 1000.0f;
        return k == 2 ? x * 1e-6f : x;
      };
      for (std::size_t s = 0; s < n; ++s)
        for (std::size_t c = 0; c < 2; ++c) {
          in[c][s] = value(quietInKind);
          aux[c][s] = value(quietAuxKind);
          for (std::size_t b = 0; b < moduleBus.size(); ++b)
            moduleBus[b][c][s] = value(quietBus[b]);
        }
      silentBlocks += (quietIn ? 1 : 0) + (quietAux ? 1 : 0);
      ProcessSlotControls control = slots[slot];
      control.level = static_cast<float>(uniform(0, 200)) / 100.0f;
      control.pan = static_cast<float>(uniform(-100, 100)) / 100.0f;
      const bool audible = !chance(0.1);
      rack::ModuleSends sends{};
      for (std::size_t b = 0; b < rack::sendBusCount; ++b)
        if (chance(0.5))
          sends[b] = {moduleBus[b][0].data(), moduleBus[b][1].data()};
      std::array<float *, 2> input{in[0].data(), in[1].data()}, auxIn{aux[0].data(), aux[1].data()},
          a{mainA[0].data(), mainA[1].data()}, b{mainB[0].data(), mainB[1].data()},
          ha{hostA[0].data(), hostA[1].data()}, hb{hostB[0].data(), hostB[1].data()};
      const bool withModuleSends = chance(0.5);
      current.mixSlot(slot, input, a, control, audible, n, withModuleSends ? &sends : nullptr);
      reference.mixSlot(slot, input, b, control, audible, n, withModuleSends ? &sends : nullptr);
      current.mixSlotOutput(slot, auxIn, ha, control, audible, n);
      reference.mixSlotOutput(slot, auxIn, hb, control, audible, n);
    }
    std::array<float *, 2> a{mainA[0].data(), mainA[1].data()}, b{mainB[0].data(), mainB[1].data()};
    current.finish(a, n);
    reference.finish(b, n);
    bool same = true;
    for (std::size_t c = 0; c < 2; ++c)
      same = same && std::memcmp(mainA[c].data(), mainB[c].data(), n * sizeof(float)) == 0 &&
             std::memcmp(hostA[c].data(), hostB[c].data(), n * sizeof(float)) == 0;
    check(same, "Main (returns and tails included) and the B2 host output are bit-identical, block " +
                    std::to_string(block));
  }
  check(zeroGainSends > 0 && silentBlocks > 1000,
        "zero-gain sends and silent (+/-0) slot, output and module-send blocks were exercised");
  std::cout << "audio routing: " << blocks
            << " blocks bit-identical to the pre-optimisation mix (Main, returns, B2)\n";
}
} // namespace

int main() {
  routerEquivalence();
  audioEquivalence();
  std::cout << "routing_equivalence_tests: " << checks << " checks passed\n";
  return 0;
}
