#include "PluginProcessor.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <new>
#include <thread>
#include <vector>
#if defined(_MSC_VER)
#include <malloc.h>
#endif

namespace allocation_probe {
thread_local bool active{};
std::atomic<std::uint64_t> cppCalls{}, heapCalls{};
void recordCpp() noexcept {
  if (active)
    cppCalls.fetch_add(1, std::memory_order_relaxed);
}
void recordHeap() noexcept {
  if (active)
    heapCalls.fetch_add(1, std::memory_order_relaxed);
}
struct Scope final {
  Scope() {
    cppCalls.store(0);
    heapCalls.store(0);
    active = true;
  }
  ~Scope() { active = false; }
};
} // namespace allocation_probe

#if defined(VOX_TEST_WRAP_MALLOC)
extern "C" void *__real_malloc(std::size_t);
extern "C" void *__real_calloc(std::size_t, std::size_t);
extern "C" void *__real_realloc(void *, std::size_t);
extern "C" void *__wrap_malloc(std::size_t size) {
  allocation_probe::recordHeap();
  return __real_malloc(size);
}
extern "C" void *__wrap_calloc(std::size_t count, std::size_t size) {
  allocation_probe::recordHeap();
  return __real_calloc(count, size);
}
extern "C" void *__wrap_realloc(void *pointer, std::size_t size) {
  allocation_probe::recordHeap();
  return __real_realloc(pointer, size);
}
#endif

void *operator new(std::size_t size) {
  allocation_probe::recordCpp();
  if (auto *pointer = std::malloc(size == 0 ? 1 : size))
    return pointer;
  throw std::bad_alloc();
}
void *operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void *pointer) noexcept { std::free(pointer); }
void operator delete[](void *pointer) noexcept { std::free(pointer); }
void operator delete(void *pointer, std::size_t) noexcept {
  std::free(pointer);
}
void operator delete[](void *pointer, std::size_t) noexcept {
  std::free(pointer);
}
void *operator new(std::size_t size, const std::nothrow_t &) noexcept {
  allocation_probe::recordCpp();
  return std::malloc(size == 0 ? 1 : size);
}
void *operator new[](std::size_t size, const std::nothrow_t &tag) noexcept {
  return ::operator new(size, tag);
}
void operator delete(void *pointer, const std::nothrow_t &) noexcept {
  std::free(pointer);
}
void operator delete[](void *pointer, const std::nothrow_t &) noexcept {
  std::free(pointer);
}

namespace {
void *alignedAllocate(std::size_t size, std::size_t alignment) noexcept {
#if defined(_MSC_VER)
  return _aligned_malloc(size, alignment);
#else
  void *pointer{};
  return posix_memalign(&pointer, alignment, size) == 0 ? pointer : nullptr;
#endif
}
void alignedFree(void *pointer) noexcept {
#if defined(_MSC_VER)
  _aligned_free(pointer);
#else
  std::free(pointer);
#endif
}
} // namespace
void *operator new(std::size_t size, std::align_val_t alignment) {
  allocation_probe::recordCpp();
  if (auto *pointer =
          alignedAllocate(size, static_cast<std::size_t>(alignment)))
    return pointer;
  throw std::bad_alloc();
}
void *operator new[](std::size_t size, std::align_val_t alignment) {
  return ::operator new(size, alignment);
}
void operator delete(void *pointer, std::align_val_t) noexcept {
  alignedFree(pointer);
}
void operator delete[](void *pointer, std::align_val_t) noexcept {
  alignedFree(pointer);
}
void operator delete(void *pointer, std::size_t, std::align_val_t) noexcept {
  alignedFree(pointer);
}
void operator delete[](void *pointer, std::size_t, std::align_val_t) noexcept {
  alignedFree(pointer);
}

namespace {
int checks{};
void require(bool condition, const char *message) {
  ++checks;
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}
void setPlain(VstEngineAudioProcessor &p, const char *id, float plain) {
  auto *parameter = p.parameters().getParameter(id);
  require(parameter != nullptr, "real host endpoint exists");
  parameter->setValueNotifyingHost(parameter->convertTo0to1(plain));
}
void requireNoCallbackAllocations() {
  require(allocation_probe::cppCalls.load() == 0,
          "callback performs zero C++ allocations");
#if defined(VOX_TEST_WRAP_MALLOC)
  require(allocation_probe::heapCalls.load() == 0,
          "callback performs zero wrapped malloc/calloc/realloc calls");
#endif
}
void requireFinite(const juce::AudioBuffer<float> &audio) {
  for (int channel = 0; channel < audio.getNumChannels(); ++channel)
    for (int sample = 0; sample < audio.getNumSamples(); ++sample)
      require(std::isfinite(audio.getSample(channel, sample)),
              "boundary audio remains finite");
}

// MidiKeyboardState invokes listeners while its own lock remains held. This
// listener makes producer contention deterministic without touching internals.
class HeldKeyboardListener final : public juce::MidiKeyboardState::Listener {
public:
  std::atomic<bool> entered{}, release{};
  void handleNoteOn(juce::MidiKeyboardState *, int, int, float) override {
    entered.store(true, std::memory_order_release);
    while (!release.load(std::memory_order_acquire))
      std::this_thread::yield();
  }
  void handleNoteOff(juce::MidiKeyboardState *, int, int, float) override {}
};

void sysexAndDenseInputAreAllocationFree(VstEngineAudioProcessor &processor) {
  juce::AudioBuffer<float> audio(2, 64);
  juce::MidiBuffer input;
  // Construct the unsupported event before starting the callback probe. JUCE
  // MidiMessage would malloc if the callback materialized this metadata.
  std::vector<juce::uint8> payload(65536, 0x11);
  const auto sysex = juce::MidiMessage::createSysExMessage(
      payload.data(), static_cast<int>(payload.size()));
  input.addEvent(sysex, 0);
  for (int event = 0; event < 12000; ++event)
    input.addEvent(juce::MidiMessage::controllerEvent(1, 1, event & 127),
                   event % 64);
  input.addEvent(juce::MidiMessage::noteOn(1, 48, 0.7f), 0);
  input.addEvent(juce::MidiMessage::noteOff(1, 48), 63);
  {
    allocation_probe::Scope probe;
    processor.processBlock(audio, input);
  }
  requireNoCallbackAllocations();
  requireFinite(audio);
  processor.requestPanic();
  input.clear();
  {
    allocation_probe::Scope probe;
    processor.processBlock(audio, input);
  }
  requireNoCallbackAllocations();
  requireFinite(audio);
}

void audioDoesNotWaitForKeyboardLock(VstEngineAudioProcessor &processor) {
  auto &keyboard = processor.keyboardState();
  HeldKeyboardListener listener;
  keyboard.addListener(&listener);
  std::thread producer([&] { keyboard.noteOn(1, 48, 0.8f); });
  const auto producerDeadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (!listener.entered.load(std::memory_order_acquire) &&
         std::chrono::steady_clock::now() < producerDeadline)
    std::this_thread::yield();
  const bool entered = listener.entered.load(std::memory_order_acquire);
  juce::AudioBuffer<float> audio(2, 64);
  juce::MidiBuffer input;
  std::atomic<bool> callbackDone{};
  std::thread callback([&] {
    {
      allocation_probe::Scope probe;
      processor.processBlock(audio, input);
    }
    callbackDone.store(true, std::memory_order_release);
  });
  const auto callbackDeadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (!callbackDone.load(std::memory_order_acquire) &&
         std::chrono::steady_clock::now() < callbackDeadline)
    std::this_thread::yield();
  const bool finishedWithoutProducer =
      callbackDone.load(std::memory_order_acquire);
  // Always release and join, even for the old blocking implementation.
  listener.release.store(true, std::memory_order_release);
  producer.join();
  callback.join();
  keyboard.removeListener(&listener);
  keyboard.noteOff(1, 48, 0.0f);
  require(
      entered,
      "producer entered keyboard listener while holding keyboard state lock");
  require(finishedWithoutProducer,
          "audio callback completes while GUI keyboard lock remains held");
  requireNoCallbackAllocations();
  requireFinite(audio);
}

// A sustained deterministic note gate makes lost NoteOffs observable without
// relying on a transient Bass envelope decaying on its own.
class GateSignal final : public vstengine::instrument::InstrumentInstance {
  bool gate{}, bypass{};

public:
  bool prepare(const vstengine::instrument::PrepareSpec &) override {
    return true;
  }
  void reset() noexcept override { gate = false; }
  void suspend() noexcept override {}
  void resume() noexcept override {}
  void setBypassed(bool value) noexcept override { bypass = value; }
  void
  process(const vstengine::instrument::ProcessBlock &block) noexcept override {
    std::size_t event = 0;
    for (std::uint32_t sample = 0; sample < block.sampleCount; ++sample) {
      while (event < block.midi.size() &&
             block.midi[event].sampleOffset <= sample) {
        const auto &midi = block.midi[event++];
        if ((midi.data[0] & 0xf0) == 0x90)
          gate = midi.data[2] != 0;
        if ((midi.data[0] & 0xf0) == 0x80)
          gate = false;
      }
      for (auto *output : block.outputs)
        output[sample] = gate && !bypass ? 0.1f : 0.0f;
    }
  }
  bool setParameter(std::string_view, float) noexcept override { return false; }
  bool loadState(std::uint32_t version,
                 std::span<const std::byte> bytes) noexcept override {
    return version == 1 && bytes.empty();
  }
  bool saveState(std::span<std::byte>,
                 std::uint32_t &bytes) const noexcept override {
    bytes = 0;
    return true;
  }
  std::uint32_t latencySamples() const noexcept override { return 0; }
  std::uint32_t tailSamples() const noexcept override { return 0; }
};
class GateProvider final : public vstengine::instrument::InstrumentProvider {
  vstengine::instrument::InstrumentDescriptor descriptor;

public:
  GateProvider() {
    descriptor.id = "test.realtime.gate";
    descriptor.providerId = "test.realtime.provider";
    descriptor.name = "Sustained test gate";
    descriptor.vendor = "Tests";
    descriptor.budget.maxVoices = 1;
    descriptor.budget.maxMidiEventsPerBlock = 2048;
  }
  std::span<const vstengine::instrument::InstrumentDescriptor>
  descriptors() const noexcept override {
    return {&descriptor, 1};
  }
  std::unique_ptr<vstengine::instrument::InstrumentInstance>
  create(std::string_view id,
         const vstengine::instrument::CreateContext &) override {
    return id == descriptor.id ? std::make_unique<GateSignal>() : nullptr;
  }
};
void deferredKeyboardRecoveryDoesNotLoseFreshNoteOff() {
  auto processor = std::make_unique<VstEngineAudioProcessor>(
      std::make_unique<GateProvider>());
  processor->prepareToPlay(48000, 64);
  juce::String diagnostic;
  require(processor->loadSlotInstrument(0, "test.realtime.gate", diagnostic),
          "sustained fixture loads through production factory/Rack");
  setPlain(*processor, "midiMode", 1);
  juce::AudioBuffer<float> audio(2, 64);
  juce::MidiBuffer input;
  processor->requestPanic();
  processor->processBlock(audio, input);
  // A fresh key arrives after audio Panic but before the GUI services the
  // deferred visual-keyboard reset. Silent keyboard.reset would erase its
  // NoteOff ownership and cause an indefinitely held DSP note.
  processor->keyboardState().noteOn(1, 60, 0.8f);
  processor->processBlock(audio, input);
  require(audio.getSample(0, 63) > 0,
          "fresh post-Panic key is genuinely sounding");
  processor->serviceKeyboardUi();
  processor->processBlock(audio, input);
  processor->keyboardState().noteOff(1, 60, 0);
  processor->processBlock(audio, input);
  require(audio.getSample(0, 63) == 0,
          "deferred GUI recovery preserves fresh NoteOff ownership");
}

void concurrentKeyboardBurstIsBounded(VstEngineAudioProcessor &processor) {
  auto &keyboard = processor.keyboardState();
  juce::AudioBuffer<float> audio(2, 64);
  juce::MidiBuffer input;
  std::atomic<bool> producerDone{};
  std::thread producer([&] {
    for (int i = 0; i < 20000; ++i) {
      keyboard.noteOn(1, 36 + i % 24, 0.7f);
      keyboard.noteOff(1, 36 + i % 24, 0.0f);
    }
    producerDone.store(true, std::memory_order_release);
  });
  for (int block = 0; block < 256; ++block) {
    {
      allocation_probe::Scope probe;
      processor.processBlock(audio, input);
    }
    requireNoCallbackAllocations();
    requireFinite(audio);
  }
  producer.join();
  require(producerDone.load(),
          "serialized keyboard producer completes dense burst");
  processor.requestPanic();
  {
    allocation_probe::Scope probe;
    processor.processBlock(audio, input);
  }
  requireNoCallbackAllocations();
  for (int block = 0; block < 512; ++block)
    processor.processBlock(audio, input);
  for (int channel = 0; channel < 2; ++channel)
    for (int sample = 0; sample < 64; ++sample)
      require(audio.getSample(channel, sample) == 0,
              "Panic recovers audition burst to exact silence");
}
} // namespace

int main() {
  juce::ScopedJuceInitialiser_GUI gui;
  auto processor = std::make_unique<VstEngineAudioProcessor>();
  processor->prepareToPlay(48000, 64);
  setPlain(*processor, "midiMode", 1);
  juce::AudioBuffer<float> warm(2, 64);
  juce::MidiBuffer empty;
  for (int i = 0; i < 8; ++i)
    processor->processBlock(warm, empty);
  sysexAndDenseInputAreAllocationFree(*processor);
  audioDoesNotWaitForKeyboardLock(*processor);
  concurrentKeyboardBurstIsBounded(*processor);
  deferredKeyboardRecoveryDoesNotLoseFreshNoteOff();
  std::cout << "Rack realtime boundary tests passed (" << checks << ")";
#if defined(VOX_TEST_WRAP_MALLOC)
  std::cout << "; GNU malloc/calloc/realloc wrapping enabled";
#else
  std::cout << "; C++ allocation instrumentation only";
#endif
  std::cout << '\n';
}
