// Bass-owned integration test against the actual Rack processor and native loader.
#include "PluginProcessor.h"
#include "instrument/HostParameterSchema.h"
#include "modules/BuiltInProvider.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {
constexpr auto bassId = "com.ultimavox.fullon-bass";
constexpr std::size_t slot = 2;
constexpr int blockSize = 128;
std::string dllPath;

void check(bool ok, const std::string& message) {
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

std::unique_ptr<VstEngineAudioProcessor> processor() {
    auto modules = vstengine::instrument::native::loadConfiguredModules(dllPath);
    check(modules.diagnostics.empty(), "native loader accepts Bass DLL");
    auto p = std::make_unique<VstEngineAudioProcessor>(
        vstengine::modules::createBuiltInProvider(), std::move(modules));
    check(p->nativeModuleDiagnostics().empty(), "Bass provider registers");
    for (const auto* id : {"slot01Enabled", "slot02Enabled"}) {
        auto* param = p->parameters().getParameter(id);
        check(param != nullptr, std::string("missing ") + id);
        param->setValueNotifyingHost(0.0f);
    }
    p->prepareToPlay(48000.0, blockSize);
    check(p->applyInternalEffectsPreset("clean"), "clean effects preset");
    return p;
}

void loadBass(VstEngineAudioProcessor& p) {
    bool found = false;
    for (const auto* descriptor : p.availableInstruments()) {
        if (descriptor->id != bassId) continue;
        found = descriptor->directParameters && descriptor->parameters.size() == 13;
        for (std::size_t i = 0; i < descriptor->parameters.size(); ++i)
            check(descriptor->parameters[i].poolIndex == static_cast<int>(i),
                  "Bass pool mapping " + std::to_string(i));
    }
    check(found, "Bass exposes 13 independent parameters");
    juce::String diagnostic;
    check(p.loadSlotInstrument(slot, bassId, diagnostic),
          "load Bass slot: " + diagnostic.toStdString());
}

int channel(VstEngineAudioProcessor& p) {
    const auto* value = p.parameters().getRawParameterValue("slot03MidiIn");
    check(value != nullptr && value->load() >= 1 && value->load() <= 16, "MIDI channel");
    return static_cast<int>(std::lround(value->load()));
}
void gain(VstEngineAudioProcessor& p, float db) {
    auto* param = p.parameters().getParameter(
        vstengine::instrument::hostparams::poolId(slot, 11));
    check(param != nullptr, "output gain host endpoint");
    param->setValueNotifyingHost(param->convertTo0to1(db));
}

struct Render { std::vector<float> samples; double energy(int from, int to) const {
    double result = 0;
    for (int i = from * blockSize; i < to * blockSize; ++i) {
        const auto sample = samples[static_cast<std::size_t>(i)];
        check(std::isfinite(sample), "finite audio");
        result += static_cast<double>(sample) * sample;
    }
    return result;
}};
Render render(VstEngineAudioProcessor& p, int midiChannel) {
    Render result;
    juce::AudioBuffer<float> audio(2, blockSize);
    juce::MidiBuffer midi;
    for (int block = 0; block < 80; ++block) {
        audio.clear(); midi.clear();
        if (block == 2) midi.addEvent(juce::MidiMessage::noteOn(midiChannel, 36, 0.9f), 0);
        if (block == 20) midi.addEvent(juce::MidiMessage::noteOff(midiChannel, 36), 0);
        p.processBlock(audio, midi);
        result.samples.insert(result.samples.end(), audio.getReadPointer(0),
                              audio.getReadPointer(0) + blockSize);
    }
    return result;
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 2) { std::cerr << "usage: bass_native_rack_tests <absolute Bass DLL path>\n"; return 2; }
    dllPath = argv[1];
    juce::ScopedJuceInitialiser_GUI gui;
    auto first = processor();
    loadBass(*first);
    const auto ch = channel(*first);
    gain(*first, 6.0f);
    const auto loud = render(*first, ch);
    check(loud.energy(0, 2) < 1e-10, "silent before NoteOn");
    check(loud.energy(3, 19) > 1e-5, "NoteOn produces audio through Rack");
    check(loud.energy(70, 80) < 1e-8, "NoteOff ends voice through Rack");
    gain(*first, -60.0f);
    const auto quiet = render(*first, ch);
    check(loud.energy(3, 19) > 100.0 * quiet.energy(3, 19),
          "host output gain controls actual Bass audio");

    gain(*first, -9.0f);
    render(*first, ch); // publish host parameter into the Rack before save
    juce::MemoryBlock state;
    first->getStateInformation(state);
    check(state.getSize() > 0, "project state saved");
    auto second = processor();
    second->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    check(second->instrumentRack().state()[slot].instrumentId == bassId,
          "native Bass identity restored");
    check(channel(*second) == ch, "MIDI channel restored");
    const auto* restoredGain = second->parameters().getRawParameterValue(
        vstengine::instrument::hostparams::poolId(slot, 11));
    check(restoredGain != nullptr && std::abs(restoredGain->load() + 9.0f) < 0.01f,
          "host gain restored");
    first->prepareToPlay(48000.0, blockSize);
    second->prepareToPlay(48000.0, blockSize);
    const auto a = render(*first, ch), b = render(*second, ch);
    check(a.energy(3, 19) > 1e-5 && b.energy(3, 19) > 1e-5,
          "restored Rack produces audio");
    check(a.samples.size() == b.samples.size(), "render lengths match");
    double maxDiff = 0;
    for (std::size_t i = 0; i < a.samples.size(); ++i)
        maxDiff = std::max(maxDiff, std::abs(static_cast<double>(a.samples[i] - b.samples[i])));
    check(maxDiff < 1e-5, "restored Rack render matches original");
    std::cout << "Bass native Rack integration passed\n";
}

