#include "effects/EffectsChain.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>
#if defined(_MSC_VER)
#include <malloc.h>
#endif

// The probe covers ordinary/aligned C++ allocations on the callback thread.
// It deliberately does not claim to intercept arbitrary C malloc in providers.
namespace tail_allocation_probe {
std::atomic<bool> active{false};
std::atomic<std::uint64_t> count{0};
void record() noexcept {
  if (active.load(std::memory_order_relaxed))
    count.fetch_add(1, std::memory_order_relaxed);
}
} // namespace tail_allocation_probe
void *operator new(std::size_t n) {
  tail_allocation_probe::record();
  if (auto *p = std::malloc(std::max(n, std::size_t(1))))
    return p;
  throw std::bad_alloc();
}
void *operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void *p) noexcept { std::free(p); }
void operator delete[](void *p) noexcept { std::free(p); }
void operator delete(void *p, std::size_t) noexcept { std::free(p); }
void operator delete[](void *p, std::size_t) noexcept { std::free(p); }
void *operator new(std::size_t n, const std::nothrow_t &) noexcept {
  try {
    return ::operator new(n);
  } catch (...) {
    return nullptr;
  }
}
void *operator new[](std::size_t n, const std::nothrow_t &t) noexcept {
  return ::operator new(n, t);
}
void operator delete(void *p, const std::nothrow_t &) noexcept { std::free(p); }
void operator delete[](void *p, const std::nothrow_t &) noexcept {
  std::free(p);
}
namespace {
void *alignedMemory(std::size_t n, std::size_t a) noexcept {
#if defined(_MSC_VER)
  return _aligned_malloc(std::max(n, std::size_t(1)), a);
#else
  void *p{};
  return posix_memalign(&p, a, std::max(n, std::size_t(1))) == 0 ? p : nullptr;
#endif
}
void alignedFree(void *p) noexcept {
#if defined(_MSC_VER)
  _aligned_free(p);
#else
  std::free(p);
#endif
}
} // namespace
void *operator new(std::size_t n, std::align_val_t a) {
  tail_allocation_probe::record();
  if (auto *p = alignedMemory(n, static_cast<std::size_t>(a)))
    return p;
  throw std::bad_alloc();
}
void *operator new[](std::size_t n, std::align_val_t a) {
  return ::operator new(n, a);
}
void operator delete(void *p, std::align_val_t) noexcept { alignedFree(p); }
void operator delete[](void *p, std::align_val_t) noexcept { alignedFree(p); }
void operator delete(void *p, std::size_t, std::align_val_t) noexcept {
  alignedFree(p);
}
void operator delete[](void *p, std::size_t, std::align_val_t) noexcept {
  alignedFree(p);
}

namespace {
using namespace vstengine::effects;
constexpr std::uint32_t maximumBlock = 1024;
int checks{};
#define CHECK(c, m)                                                            \
  do {                                                                         \
    ++checks;                                                                  \
    if (!(c))                                                                  \
      throw std::runtime_error(std::string(m) + " (line " +                    \
                               std::to_string(__LINE__) + ")");                \
  } while (false)
struct Event {
  std::size_t sample;
  float left, right;
};
using Stereo = std::array<std::vector<float>, 2>;

void configure(EffectsChain &chain, double sr, std::uint32_t channels,
               EffectType type, bool serial = false, float mix = 1) {
  CHECK(chain.prepare(sr, maximumBlock, channels), "production FX prepares");
  const auto enable = [&](EffectType which) {
    CHECK(chain.setEffect(static_cast<std::size_t>(which),
                          {which, true, mix, 1, 1, 1}),
          "maximum feedback/rate effect setting accepted");
  };
  if (serial)
    enable(EffectType::delay);
  enable(type);
}
Stereo render(EffectsChain &chain, std::size_t count, std::uint32_t channels,
              std::span<const Event> events,
              std::span<const std::uint32_t> partitions) {
  Stereo result{std::vector<float>(count), std::vector<float>(count)};
  std::array<std::array<float, maximumBlock + 2>, 2> block{};
  std::size_t offset{}, partition{};
  while (offset < count) {
    const auto n = static_cast<std::uint32_t>(std::min<std::size_t>(
        count - offset, partitions[partition++ % partitions.size()]));
    CHECK(n > 0 && n <= maximumBlock,
          "fixture partitions obey prepared maximum");
    for (auto &c : block) {
      c.fill(0);
      c.front() = c.back() = 123;
    }
    for (const auto &e : events)
      if (e.sample >= offset && e.sample < offset + n) {
        block[0][1 + e.sample - offset] += e.left;
        if (channels == 2)
          block[1][1 + e.sample - offset] += e.right;
      }
    std::array<float *, 2> pointers{block[0].data() + 1, block[1].data() + 1};
    chain.process({pointers.data(), channels}, n);
    for (std::size_t c = 0; c < channels; ++c) {
      CHECK(block[c].front() == 123 && block[c].back() == 123,
            "DSP respects supplied buffer bounds");
      for (std::uint32_t s = 0; s < n; ++s) {
        CHECK(std::isfinite(block[c][s + 1]), "FX output is finite");
        result[c][offset + s] = block[c][s + 1];
      }
      // Not just the allocation-sized guard: unused suffix must also remain
      // untouched.
      for (std::uint32_t s = n; s < maximumBlock; ++s)
        CHECK(block[c][s + 1] == 0, "DSP respects sub-block sample count");
    }
    offset += n;
  }
  return result;
}
float peak(const Stereo &a, std::size_t first, std::size_t last) {
  float result{};
  last = std::min(last, a[0].size());
  for (const auto &c : a)
    for (std::size_t s = first; s < last; ++s)
      result = std::max(result, std::abs(c[s]));
  return result;
}
void zeroAfter(const Stereo &a, std::size_t start) {
  for (const auto &c : a)
    for (std::size_t s = start; s < c.size(); ++s)
      CHECK(c[s] == 0, "no wet history can sound after advertised deadline");
}
void same(const Stereo &a, const Stereo &b, float tolerance = 0.00001f) {
  CHECK(a[0].size() == b[0].size(), "reference lengths match");
  for (std::size_t c = 0; c < 2; ++c)
    for (std::size_t s = 0; s < a[c].size(); ++s)
      CHECK(std::abs(a[c][s] - b[c][s]) <= tolerance,
            "same musical input has partition-independent FX output");
}
constexpr std::array<std::uint32_t, 1> coarse{1024};
constexpr std::array<std::uint32_t, 5> irregular{31, 257, 1, 997, 83};

void declaredBoundsAndMaximumFeedbackActuallyExpire() {
  for (const std::uint32_t channels : {1u, 2u})
    for (const auto type :
         {EffectType::phaser, EffectType::flanger, EffectType::chorus,
          EffectType::bitcrusher, EffectType::delay, EffectType::reverb}) {
      EffectsChain chain;
      configure(chain, 8000, channels, type);
      const auto bound = chain.tailSamples();
      CHECK(bound > 0 && bound <= 8u * 8000,
            "declared time-effect policy is finite and at most eight seconds");
      const std::array<Event, 1> impulse{{{0, 0.7f, -0.2f}}};
      const auto rendered =
          render(chain, bound + 513, channels, impulse, irregular);
      CHECK(peak(rendered, 0, bound) > 0.001f,
            "test excites a real nonzero wet path");
      zeroAfter(rendered, bound + 1);
      CHECK(peak(rendered, 0, bound) < 1.5f,
            "maximum feedback does not grow an isolated impulse into clipping");
      if (type == EffectType::reverb)
        CHECK(peak(rendered, 7u * 8000, 8u * 8000) < peak(rendered, 0, 8000),
              "reverb feedback decays before policy truncation rather than "
              "growing");
    }
  EffectsChain chain;
  configure(chain, 48000, 2, EffectType::delay);
  CHECK(chain.tailSamples() == 6u * 48000,
        "delay's six-second policy scales with sample rate");
  const std::array<Event, 1> impulse{{{13, 0.6f, 0.1f}}};
  const auto rendered =
      render(chain, chain.tailSamples() + 1024, 2, impulse, coarse);
  zeroAfter(rendered, 13 + chain.tailSamples() + 1);
}

void serialEffectsRespectOneWholeChainDeadline() {
  EffectsChain a, b;
  configure(a, 8000, 2, EffectType::reverb, true);
  configure(b, 8000, 2, EffectType::reverb, true);
  CHECK(a.tailSamples() == 8u * 8000,
        "serial delay/reverb shares eight-second whole-chain bound instead of "
        "adding tails");
  const std::array<Event, 1> event{{{17, 0.7f, -0.13f}}};
  const auto count = 17 + a.tailSamples() + 1024;
  const auto x = render(a, count, 2, event, coarse),
             y = render(b, count, 2, event, irregular);
  same(x, y);
  zeroAfter(x, 17 + a.tailSamples() + 1);
  CHECK(peak(x, 1000, a.tailSamples()) > 0.001f,
        "serial wet network really rendered before expiration");
}

void newInputAfterExpiryMatchesFreshDSPWithinSameBlock() {
  for (const auto type :
       {EffectType::delay, EffectType::reverb, EffectType::flanger,
        EffectType::chorus, EffectType::phaser, EffectType::bitcrusher}) {
    EffectsChain old, fresh;
    configure(old, 8000, 2, type);
    configure(fresh, 8000, 2, type);
    const auto bound = old.tailSamples();
    const std::size_t restart = bound + 17, total = bound + 1 + 4096;
    const std::array<Event, 2> events{
        {{0, 0.65f, -0.2f}, {restart, 0.3f, 0.51f}}};
    const auto actual = render(old, total, 2, events, coarse);
    const std::array<Event, 1> freshEvent{{{16, 0.3f, 0.51f}}};
    const auto expected = render(fresh, 4096, 2, freshEvent, irregular);
    Stereo after{
        std::vector<float>(actual[0].begin() + bound + 1, actual[0].end()),
        std::vector<float>(actual[1].begin() + bound + 1, actual[1].end())};
    same(after, expected);
  }
}

void retriggerDuringFadeKeepsAttackAndExtendsTail() {
  EffectsChain chain;
  configure(chain, 8000, 2, EffectType::delay, false, 0.5f);
  const auto bound = chain.tailSamples();
  const std::size_t retrigger = bound - 17;
  const std::array<Event, 2> events{{{0, 0.7f, 0.2f}, {retrigger, 1, 0}}};
  const auto out =
      render(chain, retrigger + bound + 1024, 2, events, irregular);
  CHECK(out[0][retrigger] >= 0.5f,
        "fresh dry attack is not attenuated by a previous fading deadline");
  CHECK(peak(out, bound + 1, bound + 2048) > 0.001f,
        "new input restarts tail beyond the old deadline");
  zeroAfter(out, retrigger + bound + 1);
}

void arbitrarilyQuietFiniteInputIsNotNoiseGated() {
  EffectsChain a, b;
  configure(a, 8000, 2, EffectType::delay);
  configure(b, 8000, 2, EffectType::delay);
  const auto bound = a.tailSamples();
  const std::array<Event, 2> quietRetrigger{
      {{0, 0.7f, 0.2f}, {bound - 17, 1.0e-20f, 0}}};
  const std::array<Event, 1> oldOnly{{{0, 0.7f, 0.2f}}};
  const auto extended = render(a, bound + 1024, 2, quietRetrigger, coarse);
  const auto expired = render(b, bound + 1024, 2, oldOnly, coarse);
  CHECK(peak(expired, bound + 1, bound + 1024) == 0,
        "control impulse has really expired");
  CHECK(peak(extended, bound + 1, bound + 1024) > 0.001f,
        "tiny finite input resets deadline without an undocumented level "
        "threshold");
}

void disableAndMixBypassCannotResurrectHistory() {
  for (const auto type :
       {EffectType::flanger, EffectType::chorus, EffectType::delay,
        EffectType::reverb, EffectType::bitcrusher})
    for (const bool disable : {false, true}) {
      EffectsChain chain;
      configure(chain, 8000, 2, type);
      const std::array<Event, 1> impulse{{{0, 0.7f, 0.15f}}};
      (void)render(chain, 1024, 2, impulse, coarse);
      const auto index = static_cast<std::size_t>(type);
      auto setting = chain.state().effects[index];
      if (disable)
        setting.enabled = false;
      else
        setting.mix = 0;
      CHECK(chain.setEffect(index, setting), "time effect bypass accepts");
      const auto bypassed = render(chain, 1024, 2, {}, irregular);
      zeroAfter(bypassed, 0);
      setting.enabled = true;
      setting.mix = 1;
      CHECK(chain.setEffect(index, setting), "effect reenables");
      const auto resumed = render(chain, 16000, 2, {}, irregular);
      zeroAfter(resumed, 0);
    }
}

void memorylessAndBypassedPathsNeverGateLastActiveSample() {
  EffectsChain clean;
  CHECK(clean.prepare(8000, maximumBlock, 2), "clean chain prepares");
  CHECK(clean.tailSamples() == 0, "bypassed chain has no tail");
  const std::array<Event, 3> points{
      {{0, 0.25f, -0.4f}, {513, 1.0e-20f, -1.0e-20f}, {2047, 0.15f, 0.2f}}};
  const auto dry = render(clean, 2048, 2, points, irregular);
  for (const auto &e : points)
    CHECK(dry[0][e.sample] == e.left && dry[1][e.sample] == e.right,
          "bypassed finite dry samples including tiny/end-block samples pass "
          "unchanged");
  for (const auto type : {EffectType::distortion, EffectType::wavefolder}) {
    EffectsChain atStart, atEnd;
    configure(atStart, 8000, 2, type);
    configure(atEnd, 8000, 2, type);
    CHECK(atStart.tailSamples() == 0,
          "memoryless effects do not introduce a lifetime gate");
    const std::array<Event, 1> first{{{0, 0.2f, -0.1f}}},
        last{{{8191, 0.2f, -0.1f}}};
    const auto early = render(atStart, 1024, 2, first, coarse),
               late = render(atEnd, 8192, 2, last, irregular);
    CHECK(early[0][0] == late[0][8191] && early[1][0] == late[1][8191],
          "last active memoryless sample is unaffected by preceding silence");
  }
}

void schemaTwoIsExplicitAndOldStateRejectedTransactionally() {
  EffectsChain chain;
  configure(chain, 8000, 2, EffectType::reverb);
  CHECK(EffectsChain::stateVersion == 2,
        "changed audible FX tail policy has explicit schema two");
  std::array<std::byte, EffectsChain::encodedStateBytes> saved{};
  std::uint32_t written{};
  CHECK(chain.saveState(saved, written) && written == saved.size(),
        "schema two state saves");
  auto old = saved;
  old[4] = std::byte{1};
  old[5] = old[6] = old[7] = std::byte{};
  CHECK(!chain.loadState(1, old),
        "legacy schema one is not silently reinterpreted as changed DSP");
  CHECK(!chain.loadState(2, old),
        "embedded old version cannot masquerade as new state");
  std::array<std::byte, EffectsChain::encodedStateBytes> after{};
  CHECK(chain.saveState(after, written) && after == saved,
        "old-state rejection preserves current settings");
  EffectsChain restored;
  CHECK(restored.prepare(8000, maximumBlock, 2) && restored.loadState(2, saved),
        "schema two restores");
  const std::array<Event, 1> impulse{{{7, 0.4f, 0.1f}}};
  same(render(chain, 4096, 2, impulse, coarse),
       render(restored, 4096, 2, impulse, irregular));
}

void warmedExpiryResetAndRetriggersAllocateNothing() {
  EffectsChain chain;
  configure(chain, 8000, 2, EffectType::reverb, true);
  std::array<float, maximumBlock> left{}, right{};
  std::array<float *, 2> pointers{left.data(), right.data()};
  left[0] = 0.5f;
  chain.process(pointers, maximumBlock);
  tail_allocation_probe::count.store(0);
  tail_allocation_probe::active.store(true);
  for (int block = 0; block < 160; ++block) {
    left.fill(0);
    right.fill(0);
    if (block == 70)
      left[511] = 1.0e-20f;
    chain.process(pointers, maximumBlock);
  }
  tail_allocation_probe::active.store(false);
  CHECK(tail_allocation_probe::count.load() == 0,
        "warmed serial FX tail fades/deadline resets/retriggers allocate no "
        "C++ heap memory");
}
} // namespace
int main() {
  struct Test {
    const char *name;
    void (*run)();
  };
  const std::array<Test, 9> tests{
      {{"bounded max-feedback tails",
        declaredBoundsAndMaximumFeedbackActuallyExpire},
       {"serial whole-chain deadline/partitions",
        serialEffectsRespectOneWholeChainDeadline},
       {"expiry/retrigger fresh equivalence",
        newInputAfterExpiryMatchesFreshDSPWithinSameBlock},
       {"retrigger during fade", retriggerDuringFadeKeepsAttackAndExtendsTail},
       {"no hidden noise gate", arbitrarilyQuietFiniteInputIsNotNoiseGated},
       {"disable/bypass history", disableAndMixBypassCannotResurrectHistory},
       {"memoryless/dry samples",
        memorylessAndBypassedPathsNeverGateLastActiveSample},
       {"versioned state policy",
        schemaTwoIsExplicitAndOldStateRejectedTransactionally},
       {"allocation-free deadline path",
        warmedExpiryResetAndRetriggersAllocateNothing}}};
  int failed{};
  for (const auto &t : tests)
    try {
      t.run();
      std::cout << "PASS: " << t.name << '\n';
    } catch (const std::exception &e) {
      tail_allocation_probe::active.store(false);
      ++failed;
      std::cerr << "FAIL: " << t.name << ": " << e.what() << '\n';
    }
  std::cout << checks << " checks; " << failed << " failing scenarios\n";
  return failed ? 1 : 0;
}
