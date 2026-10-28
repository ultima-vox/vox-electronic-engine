// Pool capacity benchmark: measures what the fixed host layout costs inside
// the processor. It does not measure DAW enumeration or project handling;
// those need Cubase/REAPER.
#include "PluginProcessor.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;

double ms(Clock::duration d) {
  return std::chrono::duration<double, std::milli>(d).count();
}
double percentile(std::vector<double> v, double p) {
  std::sort(v.begin(), v.end());
  return v[std::min(v.size() - 1, static_cast<std::size_t>(p * (v.size() - 1)))];
}
long residentKiB() {
  std::ifstream status("/proc/self/status");
  std::string key;
  long value = 0;
  while (status >> key)
    if (key == "VmRSS:") {
      status >> value;
      return value;
    }
  return -1;
}
} // namespace

int main() {
  juce::ScopedJuceInitialiser_GUI gui;
  constexpr int constructions = 20, blocks = 20000;
  std::vector<double> construct;
  const auto rssBefore = residentKiB();
  for (int i = 0; i < constructions; ++i) {
    const auto start = Clock::now();
    auto p = std::make_unique<VstEngineAudioProcessor>();
    construct.push_back(ms(Clock::now() - start));
  }
  auto processor = std::make_unique<VstEngineAudioProcessor>();
  const auto rssOne = residentKiB();
  const auto parameters = processor->getParameters().size();

  std::vector<double> save, restore;
  juce::MemoryBlock state;
  for (int i = 0; i < constructions; ++i) {
    state.reset();
    auto start = Clock::now();
    processor->getStateInformation(state);
    save.push_back(ms(Clock::now() - start));
    start = Clock::now();
    processor->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    restore.push_back(ms(Clock::now() - start));
  }

  std::printf("pool=%zu parameters=%d state_bytes=%zu rss_kib_one_instance~%ld\n",
              vstengine::instrument::poolParametersPerSlot, parameters,
              state.getSize(), rssOne - rssBefore);
  std::printf("construct_ms p50=%.2f p99=%.2f | save_ms p50=%.2f | restore_ms p50=%.2f\n",
              percentile(construct, 0.5), percentile(construct, 0.99),
              percentile(save, 0.5), percentile(restore, 0.5));
  for (const double rate : {44100.0, 48000.0, 96000.0})
    for (const int block : {64, 128, 512}) {
      processor->prepareToPlay(rate, block);
      juce::AudioBuffer<float> audio(2, block);
      juce::MidiBuffer midi;
      std::vector<double> times;
      times.reserve(blocks);
      for (int i = 0; i < blocks; ++i) {
        midi.clear();
        if (i % 64 == 0)
          midi.addEvent(juce::MidiMessage::noteOn(1, 36, 0.8f), 0);
        if (i % 64 == 32)
          midi.addEvent(juce::MidiMessage::noteOff(1, 36), 0);
        const auto start = Clock::now();
        processor->processBlock(audio, midi);
        times.push_back(ms(Clock::now() - start) * 1000.0);
      }
      const auto budget = block / rate * 1.0e6;
      std::printf("rate=%.0f block=%d block_us p50=%.1f p99=%.1f p99.9=%.1f max=%.1f budget_us=%.0f\n",
                  rate, block, percentile(times, 0.5), percentile(times, 0.99),
                  percentile(times, 0.999), *std::max_element(times.begin(), times.end()),
                  budget);
    }
  return 0;
}
