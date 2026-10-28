// Pool capacity MICRObenchmark (processor only). It does not measure DAW
// parameter enumeration, project handling or automation; those need real
// Cubase/REAPER runs and are reported separately.
#include "PluginProcessor.h"
#include "instrument/HostParameterSchema.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>
#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#pragma comment(lib, "psapi")
#endif

namespace {
using namespace vstengine;
using Clock = std::chrono::steady_clock;
constexpr const char *syntheticId = "test.bench.acidlike";
constexpr std::size_t acidControls = 80;

double ms(Clock::duration d) {
  return std::chrono::duration<double, std::milli>(d).count();
}
double percentile(std::vector<double> v, double p) {
  std::sort(v.begin(), v.end());
  return v[std::min(v.size() - 1, static_cast<std::size_t>(p * (v.size() - 1)))];
}
// Resident set in KiB, or -1 where unsupported.
long residentKiB() {
#if defined(_WIN32)
  PROCESS_MEMORY_COUNTERS counters{};
  if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
    return static_cast<long>(counters.WorkingSetSize / 1024);
  return -1;
#elif defined(__linux__)
  std::ifstream status("/proc/self/status");
  std::string key;
  long value = -1;
  while (status >> key)
    if (key == "VmRSS:") {
      status >> value;
      break;
    }
  return value;
#else
  return -1;
#endif
}

// Acid-like instrument: min(80, pool) typed direct parameters
// (floating, integer, boolean, choice in turn) on pool indices 0..N-1.
class Synthetic final : public instrument::InstrumentInstance {
  std::array<float, instrument::poolParametersPerSlot> values{};
  double phase{};

public:
  bool prepare(const instrument::PrepareSpec &) override { return true; }
  void reset() noexcept override { phase = 0; }
  void suspend() noexcept override {}
  void resume() noexcept override {}
  void setBypassed(bool) noexcept override {}
  void process(const instrument::ProcessBlock &b) noexcept override {
    for (std::uint32_t s = 0; s < b.sampleCount; ++s) {
      phase += 0.01 + values[0] * 0.001;
      const auto v = static_cast<float>(std::sin(phase)) * 0.01f;
      for (auto *channel : b.outputs)
        channel[s] = v;
    }
  }
  bool setParameter(std::string_view id, float v) noexcept override {
    if (id.size() < 2 || id[0] != 'p')
      return false;
    const auto index = static_cast<std::size_t>(std::atoi(id.data() + 1));
    if (index >= values.size())
      return false;
    values[index] = v;
    return true;
  }
  bool loadState(std::uint32_t, std::span<const std::byte>) noexcept override {
    return true;
  }
  bool saveState(std::span<std::byte>, std::uint32_t &n) const noexcept override {
    n = 0;
    return true;
  }
  std::uint32_t latencySamples() const noexcept override { return 0; }
  std::uint32_t tailSamples() const noexcept override { return 0; }
};

class SyntheticProvider final : public instrument::InstrumentProvider {
  instrument::InstrumentDescriptor d_;

public:
  SyntheticProvider() {
    d_.id = syntheticId;
    d_.providerId = "test.bench.provider";
    d_.name = "Acid-like bench";
    d_.vendor = "Tests";
    d_.directParameters = true;
    d_.budget.maxVoices = 1;
    d_.budget.maxMidiEventsPerBlock = 2048;
    d_.budget.maxStateBytes = 8;
    using T = instrument::ParameterType;
    const auto count = std::min(acidControls, instrument::poolParametersPerSlot);
    for (std::size_t i = 0; i < count; ++i) {
      instrument::ParameterDescriptor p;
      p.id = "p" + std::to_string(i);
      p.name = "Control " + std::to_string(i + 1);
      p.poolIndex = static_cast<std::int16_t>(i);
      switch (i % 4) {
      case 0: p.type = T::floating; p.maximum = 1; p.defaultValue = 0.5f; break;
      case 1: p.type = T::integer; p.maximum = 9; p.step = 1; p.defaultValue = 4; break;
      case 2: p.type = T::boolean; p.maximum = 1; p.step = 1; break;
      default:
        p.type = T::choice; p.maximum = 3; p.step = 1;
        p.choices = {"A", "B", "C", "D"};
      }
      d_.parameters.push_back(p);
    }
  }
  std::span<const instrument::InstrumentDescriptor> descriptors() const noexcept override {
    return {&d_, 1};
  }
  std::unique_ptr<instrument::InstrumentInstance>
  create(std::string_view id, const instrument::CreateContext &) override {
    return id == syntheticId ? std::make_unique<Synthetic>() : nullptr;
  }
};

// Fails loudly when the build does not carry the configured pool layout.
bool verifyLayout(VstEngineAudioProcessor &p) {
  std::size_t pool = 0;
  for (auto *parameter : p.getParameters())
    if (auto *id = dynamic_cast<juce::AudioProcessorParameterWithID *>(parameter))
      pool += instrument::hostparams::isPoolId(id->paramID.toStdString()) ? 1 : 0;
  const auto last = instrument::hostparams::poolId(15, instrument::poolParametersPerSlot - 1);
  const bool ok = pool == instrument::maxSlots * instrument::poolParametersPerSlot &&
                  p.parameters().getParameter("slot01.p001") != nullptr &&
                  p.parameters().getParameter(last) != nullptr;
  std::printf("layout pool_per_slot=%zu pool_endpoints=%zu total_parameters=%d last=%s %s\n",
              instrument::poolParametersPerSlot, pool, p.getParameters().size(),
              last.c_str(), ok ? "OK" : "MISMATCH");
  return ok;
}

void measureBlocks(VstEngineAudioProcessor &p, const char *label, int activeSlots,
                   double rate, int block) {
  constexpr int blocks = 20000;
  p.prepareToPlay(rate, block);
  juce::AudioBuffer<float> audio(2, block);
  juce::MidiBuffer midi;
  std::vector<double> times;
  times.reserve(blocks);
  for (int i = 0; i < blocks; ++i) {
    midi.clear();
    for (int slot = 0; slot < activeSlots; ++slot) {
      if (i % 64 == 0)
        midi.addEvent(juce::MidiMessage::noteOn(slot + 1, 48, 0.8f), 0);
      if (i % 64 == 32)
        midi.addEvent(juce::MidiMessage::noteOff(slot + 1, 48), 0);
      // Host automation: 8 changed endpoints per active slot and block.
      for (std::size_t k = 0; k < 8 && activeSlots > 0; ++k) {
        const auto index = (static_cast<std::size_t>(i) * 8 + k) %
                           std::min(acidControls, instrument::poolParametersPerSlot);
        if (auto *param = p.parameters().getParameter(
                instrument::hostparams::poolId(static_cast<std::size_t>(slot), index)))
          param->setValueNotifyingHost(static_cast<float>((i + k) % 100) / 100.0f);
      }
    }
    const auto start = Clock::now();
    p.processBlock(audio, midi);
    times.push_back(ms(Clock::now() - start) * 1000.0);
  }
  std::printf("%s slots=%d rate=%.0f block=%d block_us p50=%.1f p99=%.1f p99.9=%.1f "
              "max=%.1f budget_us=%.0f\n",
              label, activeSlots, rate, block, percentile(times, 0.5),
              percentile(times, 0.99), percentile(times, 0.999),
              *std::max_element(times.begin(), times.end()), block / rate * 1.0e6);
}
} // namespace

int main() {
  juce::ScopedJuceInitialiser_GUI gui;
  std::printf("host os=%s cpu=%s cores=%d\n",
              juce::SystemStats::getOperatingSystemName().toRawUTF8(),
              juce::SystemStats::getCpuModel().toRawUTF8(),
              juce::SystemStats::getNumCpus());

  constexpr int constructions = 20;
  std::vector<double> construct;
  for (int i = 0; i < constructions; ++i) {
    const auto start = Clock::now();
    auto p = std::make_unique<VstEngineAudioProcessor>();
    construct.push_back(ms(Clock::now() - start));
  }
  const auto rssBefore = residentKiB();
  auto builtin = std::make_unique<VstEngineAudioProcessor>();
  const auto rssAfter = residentKiB();
  if (!verifyLayout(*builtin))
    return 1;

  std::vector<double> save, restore;
  juce::MemoryBlock state;
  for (int i = 0; i < constructions; ++i) {
    state.reset();
    auto start = Clock::now();
    builtin->getStateInformation(state);
    save.push_back(ms(Clock::now() - start));
    start = Clock::now();
    builtin->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    restore.push_back(ms(Clock::now() - start));
  }
  std::printf("builtin construct_ms p50=%.2f p99=%.2f | state_bytes=%zu save_ms p50=%.2f "
              "restore_ms p50=%.2f | instance_rss_kib=%s\n",
              percentile(construct, 0.5), percentile(construct, 0.99), state.getSize(),
              percentile(save, 0.5), percentile(restore, 0.5),
              rssBefore < 0 || rssAfter < 0
                  ? "n/a"
                  : std::to_string(rssAfter - rssBefore).c_str());
  measureBlocks(*builtin, "builtin-default", 0, 48000, 128);

  // Acid-like instrument in 1/2/8/16 slots with host automation each block.
  for (const int slots : {1, 2, 8, 16}) {
    auto p = std::make_unique<VstEngineAudioProcessor>(std::make_unique<SyntheticProvider>());
    for (int slot = 0; slot < slots; ++slot) {
      juce::String diagnostic;
      if (!p->loadSlotInstrument(static_cast<std::size_t>(slot), syntheticId, diagnostic)) {
        std::printf("load failed: %s\n", diagnostic.toRawUTF8());
        return 1;
      }
    }
    for (const double rate : {44100.0, 48000.0, 96000.0})
      for (const int block : {64, 128, 512})
        measureBlocks(*p, "acidlike", slots, rate, block);
  }
  return 0;
}
