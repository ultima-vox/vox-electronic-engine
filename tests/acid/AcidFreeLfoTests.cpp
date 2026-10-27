#include "acid/AcidEngine.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>

namespace {
using vstengine::acid::AcidEngine;
std::array<float, 4096> renderAfterSilence(int lfo, int silentSamples,
                                           int chunkSize = 512) {
  AcidEngine engine;
  engine.setParameter("mod_amount", 0.0f);
  engine.setParameter("env2_amount", 0.0f);
  engine.setParameter("step_amount", 0.0f);
  const auto prefix = std::string("lfo") + std::to_string(lfo) + "_";
  engine.setParameter(prefix + "rate", 0.5f); // 1 Hz: 0.05 * 400^0.5
  engine.setParameter(prefix + "depth", 1.0f);
  engine.setParameter(prefix + "wave", 2.0f / 3.0f); // square
  engine.setParameter(prefix + "target", 0.0f);      // filter cutoff
  engine.setParameter(prefix + "sync", 0.0f);
  engine.prepare(48000, 4096, 2);
  std::array<float, 4096> left{}, right{};
  std::array<float *, 2> outputs{left.data(), right.data()};
  while (silentSamples > 0) {
    const auto count = std::min(silentSamples, chunkSize);
    engine.process(outputs, static_cast<std::uint32_t>(count), {});
    silentSamples -= count;
  }
  const std::array on{VoxMidiEventV1{0, 3, {0x90, 48, 110}}};
  engine.process(outputs, 4096, on);
  return left;
}
} // namespace
int main() {
  int failures = 0;
  for (int lfo = 1; lfo <= 3; ++lfo) {
    const auto early = renderAfterSilence(lfo, 12000);
    const auto late = renderAfterSilence(lfo, 36000);
    double difference = 0.0;
    for (std::size_t i = 0; i < early.size(); ++i)
      difference += std::abs(static_cast<double>(early[i]) - late[i]);
    if (!(difference > 1.0)) {
      ++failures;
      std::cerr << "FAILED: free-running LFO" << lfo
                << " did not advance while voice was silent\n";
    }
    const auto nextPeriod = renderAfterSilence(lfo, 60000);
    const auto differentBlocks = renderAfterSilence(lfo, 12000, 127);
    double periodDiff = 0.0, partitionDiff = 0.0;
    for (std::size_t i = 0; i < early.size(); ++i) {
      periodDiff += std::abs(static_cast<double>(early[i]) - nextPeriod[i]);
      partitionDiff +=
          std::abs(static_cast<double>(early[i]) - differentBlocks[i]);
    }
    if (periodDiff > 1.0e-5) {
      ++failures;
      std::cerr << "FAILED: free-running LFO" << lfo
                << " did not repeat after one second\n";
    }
    if (partitionDiff > 1.0e-5) {
      ++failures;
      std::cerr << "FAILED: LFO silence phase changed with host block size\n";
    }
  }
  std::cout << "Acid free LFO: 9 checks, " << failures << " failures\n";
  return failures ? 1 : 0;
}
