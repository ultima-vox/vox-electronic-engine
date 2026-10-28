#include "acid/AcidEngine.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <new>
#include <span>

namespace {
std::atomic<unsigned> allocations{};
}
void *operator new(std::size_t n) {
  allocations.fetch_add(1, std::memory_order_relaxed);
  if (auto *p = std::malloc(n ? n : 1))
    return p;
  throw std::bad_alloc();
}
void *operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void *p) noexcept { std::free(p); }
void operator delete[](void *p) noexcept { std::free(p); }
void operator delete(void *p, std::size_t) noexcept { std::free(p); }
void operator delete[](void *p, std::size_t) noexcept { std::free(p); }

namespace {
using vstengine::acid::AcidEngine;
int checks{}, failures{};
void check(bool value, const char *message) {
  ++checks;
  if (!value) {
    ++failures;
    std::cerr << "FAILED: " << message << '\n';
  }
}
struct Probe {
  AcidEngine engine;
  std::array<float, 512> left{}, right{};
  Probe() {
    engine.setParameter("amp_attack", 0);
    engine.setParameter("amp_release", 0);
    engine.setParameter("amp_sustain", 1);
    engine.setParameter("slide", 0);
    engine.setParameter("portamento", 0);
    engine.setParameter("mod_amount", 0);
    engine.prepare(48000, 512, 2);
  }
  void block(std::span<const VoxMidiEventV1> events = {}) {
    std::array<float *, 2> outputs{left.data(), right.data()};
    const auto before = allocations.load(std::memory_order_relaxed);
    engine.process(outputs, 512, events);
    check(before == allocations.load(std::memory_order_relaxed),
          "Acid process allocates no C++ heap storage");
  }
  void event(std::uint8_t status, std::uint8_t a, std::uint8_t b) {
    const std::array events{VoxMidiEventV1{0, 3, {status, a, b}}};
    block(events);
  }
  void settle() {
    for (int i = 0; i < 100; ++i)
      block();
  }
  bool silent() const {
    return std::all_of(left.begin(), left.end(),
                       [](float x) { return x == 0; }) &&
           std::all_of(right.begin(), right.end(),
                       [](float x) { return x == 0; });
  }
};
} // namespace
int main() {
  // A still-held first key must remain reachable after more than eight keys.
  Probe keys;
  for (std::uint8_t note = 48; note <= 56; ++note)
    keys.event(0x90, note, 100);
  for (std::uint8_t note = 56; note > 48; --note)
    keys.event(0x80, note, 0);
  keys.settle();
  check(!keys.silent() && keys.engine.isActive(),
        "ninth held key never evicts the first held key");
  keys.event(0x80, 48, 0);
  keys.settle();
  check(keys.silent() && !keys.engine.isActive(),
        "all held keys released reach exact silence");

  Probe channel;
  channel.event(0x90, 48, 100);
  channel.event(0x81, 48, 0);
  channel.settle();
  check(!channel.silent(),
        "foreign-channel NoteOff cannot release an owned note");
  channel.event(0x91, 48, 100);
  channel.event(0x80, 48, 0);
  channel.settle();
  check(!channel.silent(), "same pitch on two channels has separate ownership");
  channel.event(0x91, 48, 0);
  channel.settle();
  check(channel.silent(), "velocity-zero NoteOn releases its own channel");

  Probe repeated;
  repeated.event(0x90, 48, 100);
  repeated.event(0x90, 48, 100);
  repeated.event(0x80, 48, 0);
  repeated.settle();
  check(repeated.silent(), "same-channel re-press replaces its key ownership, "
                           "matching Rack retrigger");
  repeated.event(0x80, 48, 0);
  repeated.settle();
  check(repeated.silent(), "last repeated NoteOff releases the note");

  Probe control, unaffected;
  control.event(0x90, 48, 100);
  unaffected.event(0x90, 48, 100);
  control.event(0xe1, 127, 127);
  unaffected.block();
  check(control.left == unaffected.left && control.right == unaffected.right,
        "foreign-channel pitch bend cannot retune the active voice");
  control.event(0xb1, 123, 0);
  control.settle();
  check(!control.silent(),
        "foreign-channel All Notes Off leaves active channel playing");
  control.event(0xb0, 123, 0);
  control.settle();
  check(control.silent(), "owned-channel All Notes Off releases the voice");

  Probe velocity, reference;
  velocity.event(0x90, 48, 50);
  reference.event(0x90, 48, 50);
  velocity.event(0x90, 56, 100);
  reference.event(0x90, 56, 50);
  velocity.event(0x80, 56, 0);
  reference.event(0x80, 56, 0);
  check(velocity.left == reference.left && velocity.right == reference.right,
        "last-key fallback restores the held key's original velocity");

  Probe allKeys;
  std::array<VoxMidiEventV1, 2048> held{};
  for (unsigned key = 0; key < held.size(); ++key)
    held[key] = {0,
                 3,
                 {static_cast<std::uint8_t>(0x90u | key / 128u),
                  static_cast<std::uint8_t>(key % 128u), 100}};
  allKeys.block(held);
  for (unsigned key = 1; key < held.size(); ++key)
    held[key - 1] = {0,
                     3,
                     {static_cast<std::uint8_t>(0x80u | key / 128u),
                      static_cast<std::uint8_t>(key % 128u), 0}};
  allKeys.block(std::span(held).first(2047));
  check(allKeys.engine.isActive(),
        "all2048 channel/key identities fit without eviction");
  allKeys.event(0x80, 0, 0);
  allKeys.settle();
  check(!allKeys.engine.isActive() && allKeys.silent(),
        "complete channel/key release leaves no ghost note");

  Probe immediate;
  immediate.event(0x90, 48, 100);
  immediate.event(0xb0, 120, 0);
  check(immediate.silent() && !immediate.engine.isActive(),
        "All Sound Off reaches exact zero in its own block");

  Probe release;
  release.engine.setParameter("amp_release", 1);
  release.event(0x90, 48, 100);
  release.settle();
  release.event(0x80, 48, 0);
  release.event(0xb1, 120, 0);
  check(!release.silent(),
        "foreign All Sound Off cannot cut an owned release tail");
  release.event(0xb0, 120, 0);
  check(release.silent() && !release.engine.isActive(),
        "All Sound Off silences its channel's already releasing voice");

  Probe ccGlide, ccReference;
  for (auto *p : {&ccGlide, &ccReference}) {
    p->engine.setParameter("slide", 1);
    p->engine.setPlayModeOptions(true, false, false, false);
    p->settle();
    p->event(0x90, 48, 100);
  }
  ccGlide.event(0xb1, 65, 127);
  ccReference.block();
  ccGlide.event(0x90, 60, 100);
  ccReference.event(0x90, 60, 100);
  check(ccGlide.left == ccReference.left && ccGlide.right == ccReference.right,
        "foreign CC65 cannot enable active channel's slide");

  Probe outside;
  const std::array future{VoxMidiEventV1{512, 3, {0x90, 48, 100}}};
  outside.block(future);
  outside.block();
  check(outside.silent() && !outside.engine.isActive(),
        "out-of-block MIDI is not applied early to the next block");
  const std::array malformed{VoxMidiEventV1{0, 3, {0x90, 255, 255}}};
  outside.block(malformed);
  check(outside.silent() && !outside.engine.isActive(),
        "invalid MIDI data bytes never create a voice");
  std::cout << "Acid MIDI lifecycle: " << checks << " checks, " << failures
            << " failures\n";
  return failures ? 1 : 0;
}
