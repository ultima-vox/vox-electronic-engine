#include "acid/AcidEngine.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>

namespace {
int checks{}, failures{};
void check(bool value, const char *message) {
  ++checks;
  if (!value) {
    ++failures;
    std::cerr << "FAILED: " << message << '\n';
  }
}
} // namespace
int main() {
  using vstengine::acid::AcidEngine;
  for (double rate : {44100., 48000., 96000., 192000., 384000.}) {
    for (float release : {0.f, 0.35f, 1.f}) {
      AcidEngine engine;
      engine.setParameter("amp_attack", 0);
      engine.setParameter("amp_sustain", 1);
      engine.setParameter("amp_release", release);
      check(engine.prepare(rate, 512, 2),
            "supported Acid sample rate prepares");
      std::array<float, 512> left{}, right{};
      std::array<float *, 2> outputs{left.data(), right.data()};
      const std::array on{VoxMidiEventV1{0, 3, {0x90, 48, 100}}};
      const std::array off{VoxMidiEventV1{0, 3, {0x80, 48, 0}}};
      engine.process(outputs, 512, on);
      engine.process(outputs, 512, {});
      const double duration = 0.004f + release * release * 1.8f;
      const auto deadline =
          static_cast<std::uint32_t>(std::ceil(duration * rate)) + 2;
      const auto declared = engine.tailSamples();
      check(declared >= deadline,
            "declared tail covers selected Release duration");
      bool finite = true, silentAfter = true;
      double firstHalfEnergy = 0;
      for (std::uint32_t elapsed = 0; elapsed < deadline + 512;) {
        const auto count =
            std::min<std::uint32_t>(512, deadline + 512 - elapsed);
        engine.process(outputs, count,
                       elapsed == 0 ? std::span<const VoxMidiEventV1>(off)
                                    : std::span<const VoxMidiEventV1>{});
        for (std::uint32_t i = 0; i < count; ++i) {
          finite = finite && std::isfinite(left[i]) && std::isfinite(right[i]);
          if (elapsed + i >= deadline)
            silentAfter = silentAfter && left[i] == 0 && right[i] == 0;
          if (elapsed + i < deadline / 2)
            firstHalfEnergy += left[i] * left[i] + right[i] * right[i];
        }
        elapsed += count;
      }
      check(finite, "Release render remains finite");
      check(firstHalfEnergy > 1e-10,
            "Release keeps a real audible decay before its deadline");
      check(silentAfter && !engine.isActive(),
            "Release reaches exact silence within its displayed duration");
      check(engine.tailSamples() == declared,
            "tail declaration is stable after rendering");
    }
  }
  {
    AcidEngine engine;
    engine.setParameter("amp_sustain", 1);
    engine.setParameter("amp_release", 1);
    engine.prepare(48000, 512, 2);
    std::array<float, 512> left{}, right{};
    std::array<float *, 2> outputs{left.data(), right.data()};
    const std::array on{VoxMidiEventV1{0, 3, {0x90, 48, 100}}};
    const std::array off{VoxMidiEventV1{0, 3, {0x80, 48, 0}}};
    engine.process(outputs, 512, on);
    engine.process(outputs, 512, off);
    const auto bound = engine.tailSamples();
    for (unsigned elapsed = 0; elapsed < bound + 512; elapsed += 512) {
      engine.setParameter("amp_release", (elapsed / 512) % 2 ? 0.2f : 1.0f);
      engine.process(outputs, 512, {});
    }
    check(!engine.isActive(), "Release automation cannot extend a voice beyond "
                              "declared maximum tail");
    check(engine.tailSamples() == bound,
          "Release automation never changes the stable host tail declaration");
  }
  for (double rate : {1e-320, 1e300, std::numeric_limits<double>::quiet_NaN(),
                      std::numeric_limits<double>::infinity()}) {
    AcidEngine engine;
    check(!engine.prepare(rate, 512, 2),
          "unsafe sample rates are rejected before DSP arithmetic");
  }
  std::cout << "Acid release: " << checks << " checks, " << failures
            << " failures\n";
  return failures ? 1 : 0;
}
