#include "acid/AcidEngine.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <span>

using vstengine::acid::AcidEngine;
static double energy(double rate, float decay, float sustain, int chunk) {
  AcidEngine e;
  e.setParameter("amp_attack", 0.0f);
  e.setParameter("amp_decay", decay);
  e.setParameter("amp_sustain", sustain);
  if (!e.prepare(rate, 512, 2))
    return -1;
  std::array<float, 512> l{}, r{};
  std::array<float *, 2> out{l.data(), r.data()};
  const std::array on{VoxMidiEventV1{0, 3, {0x90, 48, 100}}};
  const auto begin = static_cast<int>(std::ceil(
                         (0.0005 + 0.015 + decay * decay * 2.2) * rate)) +
                     8;
  const auto end = begin + static_cast<int>(0.02 * rate);
  double result = 0;
  for (int pos = 0; pos < end;) {
    const auto n = std::min(chunk, end - pos);
    e.process(out, n,
              pos == 0 ? std::span<const VoxMidiEventV1>(on)
                       : std::span<const VoxMidiEventV1>{});
    for (int i = std::max(0, begin - pos); i < n; ++i)
      result += double(l[i]) * l[i];
    pos += n;
  }
  return result;
}
int main() {
  int failures = 0;
  for (double rate : {8000., 44100., 48000., 96000., 384000.})
    for (float decay : {0.f, 0.2f, 0.5f})
      for (int chunk : {127, 512}) {
        const double silent = energy(rate, decay, 0.f, chunk);
        const double reference = energy(rate, decay, 1.f, chunk);
        if (!(reference > 1e-6 && silent <= reference * 1e-8)) {
          std::cerr << "FAIL decay deadline rate=" << rate << " decay=" << decay
                    << " chunk=" << chunk << " ratio=" << silent / reference
                    << '\n';
          ++failures;
        }
      }
  // Compare the same oscillator at two VCA settings so oscillator slope is
  // not mistaken for an envelope discontinuity when parameters are changed.
  AcidEngine test, reference;
  for (auto *e : {&test, &reference}) {
    e->setParameter("amp_attack", 0.f);
    e->setParameter("amp_decay", 0.5f);
    e->setParameter("amp_sustain", e == &test ? 0.f : 1.f);
    e->prepare(48000, 1, 2);
  }
  std::array<float, 1> a{}, b{}, c{}, d{};
  std::array<float *, 2> testOut{a.data(), b.data()},
      refOut{c.data(), d.data()};
  const std::array on{VoxMidiEventV1{0, 3, {0x90, 48, 100}}};
  float previous = -1.f, largestStep = 0.f;
  int comparisons = 0;
  for (int i = 0; i < 28000; ++i) {
    if (i == 26800)
      test.setParameter("amp_sustain", 1.f);
    const auto events = i == 0 ? std::span<const VoxMidiEventV1>(on)
                               : std::span<const VoxMidiEventV1>{};
    test.process(testOut, 1, events);
    reference.process(refOut, 1, events);
    if (std::abs(c[0]) > 0.01f) {
      const float ratio = a[0] / c[0];
      if (i >= 26800 && previous >= 0.f) {
        largestStep = std::max(largestStep, std::abs(ratio - previous));
        ++comparisons;
      }
      previous = ratio;
    }
  }
  for (double rate : {192000., 384000.}) {
    const double ratio =
        std::sqrt(energy(rate, 1.f, 0.5f, 127) / energy(rate, 1.f, 1.f, 127));
    if (!(std::isfinite(ratio) && std::abs(ratio - 0.5) <= 0.0003)) {
      std::cerr << "FAIL long decay residual rate=" << rate
                << " ratio=" << ratio << '\n';
      ++failures;
    }
  }
  if (!(comparisons > 100 && largestStep < 0.02f)) {
    std::cerr << "FAIL decay automation continuity: " << largestStep << '\n';
    ++failures;
  }
  std::cout << "Acid decay: " << 33 << " checks, " << failures << " failures\n";
  return failures != 0;
}
