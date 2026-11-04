#include "PluginProcessor.h"
#include "modules/BuiltInProvider.h"
#include "rack/RackRouter.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <new>
#include <vector>
#if defined(_MSC_VER)
#include <malloc.h>
#endif

namespace allocation_probe {
std::atomic<bool> active { false };
std::atomic<std::uint64_t> count { 0 };
void record() noexcept
{
    if (active.load(std::memory_order_relaxed))
        count.fetch_add(1, std::memory_order_relaxed);
}
}

void* operator new(std::size_t size)
{
    allocation_probe::record();
    if (void* pointer = std::malloc(size)) return pointer;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size)
{
    return ::operator new(size);
}
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    allocation_probe::record();
    return std::malloc(size);
}
void* operator new[](std::size_t size, const std::nothrow_t& tag) noexcept
{
    return ::operator new(size, tag);
}
void operator delete(void* pointer, const std::nothrow_t&) noexcept { std::free(pointer); }
void operator delete[](void* pointer, const std::nothrow_t&) noexcept { std::free(pointer); }

namespace {
void* alignedAllocate(std::size_t size, std::size_t alignment) noexcept
{
#if defined(_MSC_VER)
    return _aligned_malloc(size, alignment);
#else
    void* pointer {};
    return posix_memalign(&pointer, alignment, size) == 0 ? pointer : nullptr;
#endif
}
void alignedFree(void* pointer) noexcept
{
#if defined(_MSC_VER)
    _aligned_free(pointer);
#else
    std::free(pointer);
#endif
}
} // namespace

void* operator new(std::size_t size, std::align_val_t alignment)
{
    allocation_probe::record();
    if (void* pointer = alignedAllocate(size, static_cast<std::size_t>(alignment)))
        return pointer;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size, std::align_val_t alignment)
{
    return ::operator new(size, alignment);
}
void operator delete(void* pointer, std::align_val_t) noexcept { alignedFree(pointer); }
void operator delete[](void* pointer, std::align_val_t) noexcept { alignedFree(pointer); }
void operator delete(void* pointer, std::size_t, std::align_val_t) noexcept { alignedFree(pointer); }
void operator delete[](void* pointer, std::size_t, std::align_val_t) noexcept { alignedFree(pointer); }

namespace {
constexpr double sampleRate = 48000.0;
constexpr int blockSize = 512;
constexpr int iterations = 96;
constexpr int instanceCount = 2;
constexpr double maxAggregateRealtimeRatio = 6.0;
constexpr std::size_t maxSerializedRackBytes = 20u * 1024u * 1024u;
int checks {};

void require(bool condition, const char* message)
{
    ++checks;
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void setPlain(VstEngineAudioProcessor& processor, const char* id, float value)
{
    auto* parameter = processor.parameters().getParameter(id);
    require(parameter != nullptr, "required host parameter exists");
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

juce::MidiBuffer denseMidi(int block)
{
    juce::MidiBuffer midi;
    for (int channel = 1; channel <= 16; ++channel) {
        const int note = 36 + (channel + block) % 36;
        midi.addEvent(juce::MidiMessage::noteOn(channel, note, 0.85f), 0);
        midi.addEvent(juce::MidiMessage::controllerEvent(channel, 1,
            (block * 7 + channel * 5) & 127), 64);
        midi.addEvent(juce::MidiMessage::noteOff(channel, note, 0.0f), 480);
    }
    return midi;
}

juce::MidiBuffer voiceSaturation(bool noteOn)
{
    juce::MidiBuffer midi;
    for (int channel = 1; channel <= 16; ++channel)
        for (int note = 0; note < 128; ++note)
            midi.addEvent(noteOn
                ? juce::MidiMessage::noteOn(channel, note, 0.7f)
                : juce::MidiMessage::noteOff(channel, note, 0.0f), note * 4);
    return midi;
}

void verifyFinite(const juce::AudioBuffer<float>& audio)
{
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
        for (int sample = 0; sample < audio.getNumSamples(); ++sample)
            require(std::isfinite(audio.getSample(channel, sample)),
                    "dense render contains no NaN/Inf");
}

struct Instance {
    explicit Instance(std::unique_ptr<VstEngineAudioProcessor> value)
        : processor(std::move(value)) {}
    std::unique_ptr<VstEngineAudioProcessor> processor;
    juce::AudioBuffer<float> audio { 2, blockSize };
};

std::unique_ptr<VstEngineAudioProcessor> makeDenseProcessor()
{
    auto processor = std::make_unique<VstEngineAudioProcessor>();
    processor->prepareToPlay(sampleRate, blockSize);
    setPlain(*processor, "midiMode", 1.0f);
    static constexpr std::array ids {
        vstengine::modules::bassInstrumentId,
        vstengine::modules::acidInstrumentId,
        vstengine::modules::leadInstrumentId,
        vstengine::modules::semanticFxInstrumentId,
        vstengine::modules::atmosInstrumentId
    };
    for (std::size_t slot = 0; slot < vstengine::instrument::maxSlots; ++slot) {
        juce::String diagnostic;
        require(processor->loadSlotInstrument(slot, ids[slot % ids.size()], diagnostic),
                "all 16 stress slots load");
        require(processor->assignSlotChannel(slot, static_cast<int>(slot + 1),
                    diagnostic),
                "stress slot gets independent MIDI channel");
    }
    return processor;
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juce;
    std::vector<Instance> instances;
    instances.reserve(instanceCount);
    for (int index = 0; index < instanceCount; ++index)
        instances.emplace_back(makeDenseProcessor());

    for (auto& instance : instances) {
        auto midi = denseMidi(0);
        instance.processor->processBlock(instance.audio, midi);
    }
    allocation_probe::count.store(0, std::memory_order_relaxed);
    for (auto& instance : instances) {
        for (const bool noteOn : { true, false }) {
            auto midi = voiceSaturation(noteOn);
            instance.audio.clear();
            allocation_probe::active.store(true, std::memory_order_release);
            instance.processor->processBlock(instance.audio, midi);
            allocation_probe::active.store(false, std::memory_order_release);
            verifyFinite(instance.audio);
        }
    }
    // Exercise maximum 16x4 send graph with all four returns and explicit kick
    // event ducking, not merely dry rack voices. Host automation precedes
    // scope.
    for (auto &instance : instances) {
      for (std::size_t slot = 0; slot < 16; ++slot) {
        for (std::size_t bus = 0; bus < 4; ++bus) {
          const auto id = vstengine::host::sendParameterId(slot, bus, "level");
          setPlain(*instance.processor, id.c_str(), 0.08f);
        }
        const auto id = "slot" +
                        juce::String(static_cast<int>(slot + 1))
                            .paddedLeft('0', 2)
                            .toStdString() +
                        ".duck.depth";
        setPlain(*instance.processor, id.c_str(), 0.7f);
      }
      setPlain(*instance.processor, "rack.duck.source", 1);
      setPlain(*instance.processor, "rack.duck.note", 37);
    }
    std::chrono::nanoseconds processTime {};
    double aggregateEnergy {};
    for (int block = 0; block < iterations; ++block) {
        for (auto& instance : instances) {
            auto midi = denseMidi(block);
            instance.audio.clear();
            const auto start = std::chrono::steady_clock::now();
            allocation_probe::active.store(true, std::memory_order_release);
            instance.processor->processBlock(instance.audio, midi);
            allocation_probe::active.store(false, std::memory_order_release);
            processTime += std::chrono::steady_clock::now() - start;
            verifyFinite(instance.audio);
            for (int channel = 0; channel < instance.audio.getNumChannels(); ++channel)
                for (int sample = 0; sample < instance.audio.getNumSamples(); ++sample) {
                    const auto value = instance.audio.getSample(channel, sample);
                    aggregateEnergy += static_cast<double>(value) * value;
                }
        }
    }
    require(allocation_probe::count.load(std::memory_order_relaxed) == 0,
            "warmed realtime processBlock performs zero heap allocations");
    require(aggregateEnergy > 1.0e-4,
            "dense rack stress renders real audible DSP output");
    const auto realtimeSeconds = iterations * blockSize / sampleRate;
    const auto ratio = std::chrono::duration<double>(processTime).count()
        / realtimeSeconds;
    require(ratio < maxAggregateRealtimeRatio,
            "two dense instances stay inside aggregate CPU budget");
    for (const auto& instance : instances) {
        require(std::isfinite(instance.processor->currentCpuLoad())
                    && instance.processor->currentCpuLoad() <= 4.0f,
                "processor CPU telemetry remains finite and bounded");
        for (const auto& runtime : instance.processor->instrumentRack().runtimeState())
            require(runtime.droppedMidiEvents == 0,
                    "valid dense MIDI causes no event drops");
        juce::MemoryBlock state;
        instance.processor->getStateInformation(state);
        require(state.getSize() <= maxSerializedRackBytes,
                "serialized 16-slot state stays inside RAM budget");
    }

    auto& processor = *instances.front().processor;
    static constexpr std::array ids {
        vstengine::modules::bassInstrumentId,
        vstengine::modules::acidInstrumentId,
        vstengine::modules::leadInstrumentId,
        vstengine::modules::semanticFxInstrumentId,
        vstengine::modules::atmosInstrumentId
    };
    for (int cycle = 0; cycle < 64; ++cycle) {
        const auto slot = static_cast<std::size_t>(cycle % 16);
        auto state = processor.instrumentRack().state();
        state[slot].instrumentId.clear();
        state[slot].modulePayload.clear();
        state[slot].patternPayload.clear();
        state[slot].modulationPayload.clear();
        std::string detail;
        require(processor.instrumentRack().replaceState(state, nullptr, detail),
                "slot unload commits transactionally");
        juce::String diagnostic;
        require(processor.loadSlotInstrument(slot, ids[(cycle + 1) % ids.size()], diagnostic),
                "slot recreates after unload");
        const int channel = static_cast<int>(slot + 1);
        require(processor.assignSlotChannel(slot, channel, diagnostic),
                "channel reassignment remains deterministic under churn");
    }
    processor.instrumentRack().reset();
    juce::MidiBuffer emptyMidi;
    juce::AudioBuffer<float> silence(2, blockSize);
    processor.processBlock(silence, emptyMidi);
    verifyFinite(silence);

    std::cout << "Performance stress passed (" << checks
              << "), aggregate realtime ratio " << ratio << '\n';
    return EXIT_SUCCESS;
}
