#include "Processor.h"
#include <cmath>
#include <cstdio>
#include <limits>
#include <span>

#ifdef VOX_TEST_INTERPOSE_MALLOC
#include "AllocationProbe.h"
#endif

namespace {
int failures = 0;
void check (bool condition, const char* message) {
    if (!condition) { std::printf("FAIL: %s\n", message); ++failures; }
}
juce::MemoryBlock save (vox::fx::FxProcessor& processor) {
    juce::MemoryBlock data; processor.getStateInformation(data); return data;
}
void block (vox::fx::FxProcessor& p, juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi) {
    p.processBlock(audio, midi); midi.clear();
}
}
int main() {
    juce::ScopedJuceInitialiser_GUI gui;
    vox::fx::FxProcessor p;
    p.prepareToPlay(48000, 512);
    juce::AudioBuffer<float> audio(2, 512);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 100);
#ifdef VOX_TEST_INTERPOSE_MALLOC
    voxProbeBegin();
#endif
    p.processBlock(audio, midi);
#ifdef VOX_TEST_INTERPOSE_MALLOC
    const auto allocations = voxProbeEnd();
    if (voxProbeAvailable()) check(allocations == 0, "no allocations on processor audio thread");
#endif
    midi.clear();
    check(audio.getMagnitude(0, 0, 100) == 0, "MIDI offset preserves leading silence");
    check(audio.getMagnitude(0, 101, 411) > 0, "MIDI trigger makes processor audio");
    midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
    block(p, audio, midi);
    check(audio.getMagnitude(0, 0, 512) > 0, "one-shot ignores note off");
    midi.addEvent(juce::MidiMessage::allSoundOff(1), 100);
    block(p, audio, midi);
    check(audio.getMagnitude(0, 350, 162) == 0, "CC120 fades to silence");
    midi.addEvent(juce::MidiMessage::noteOn(2, 61, static_cast<juce::uint8>(100)), 0);
    midi.addEvent(juce::MidiMessage::allNotesOff(1), 64);
    block(p, audio, midi);
    check(audio.getMagnitude(0, 350, 162) > 0, "channel-specific panic");
    midi.addEvent(juce::MidiMessage::allNotesOff(2), 0);
    block(p, audio, midi);
    check(audio.getMagnitude(0, 300, 212) == 0, "CC123 fades channel");
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);
    block(p, audio, midi);
    p.reset(); block(p, audio, midi);
    check(audio.getMagnitude(0, 300, 212) == 0, "reset fades active event");

    auto* gain = p.getParameters()[0];
    check(gain != nullptr, "output parameter exists");
    gain->setValueNotifyingHost(0.25f);
    const auto saved = save(p);
    vox::fx::FxProcessor restored;
    restored.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    check(restored.lastStateError().isEmpty(), "valid state accepted");
    check(std::abs(restored.getParameters()[0]->getValue() - 0.25f) < 1e-6f, "parameter restored");
    check(save(restored) == saved, "state deterministic roundtrip");
    auto damaged = saved;
    static_cast<unsigned char*>(damaged.getData())[20] ^= 1;
    restored.setStateInformation(damaged.getData(), static_cast<int>(damaged.getSize()));
    check(restored.lastStateError().isNotEmpty() && save(restored) == saved, "corrupt state rejected transactionally");
    auto decoded = vox::preset::decode(std::span(static_cast<const std::byte*>(saved.getData()), saved.getSize()),
                                       vox::preset::makeTag("VXFX"), 1);
    check(decoded.ok(), "decode state");
    if (decoded.ok()) {
        for (auto& section : decoded.document.sections)
            if (section.tag == vox::fx::fxParameterSectionTag) section.data[0] = std::byte{2};
        auto encoded = vox::preset::encode(decoded.document);
        if (encoded) {
            restored.setStateInformation(encoded->data(), static_cast<int>(encoded->size()));
            check(restored.lastStateError().isNotEmpty() && save(restored) == saved, "future FX section rejected transactionally");
        } else check(false, "encode future state");
    }
    restored.prepareToPlay(48000, 512);
    restored.getParameters()[0]->setValueNotifyingHost(0.0f);
    for (int i = 0; i < 2; ++i) block(restored, audio, midi);
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);
    block(restored, audio, midi);
    check(audio.getMagnitude(0, 0, 512) == 0, "host output gain controls DSP");
    std::printf("FX processor: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
