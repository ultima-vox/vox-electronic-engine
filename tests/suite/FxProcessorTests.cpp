#include "Processor.h"
#include "FxEditor.h"
#include <cmath>
#include <cstdio>
#include <limits>
#include <span>

#ifdef VOX_TEST_INTERPOSE_MALLOC
#include "AllocationProbe.h"
#endif

namespace {
struct TestPlayHead : juce::AudioPlayHead {
    juce::Optional<PositionInfo> position;
    juce::Optional<PositionInfo> getPosition() const override { return position; }
    void set(bool playing, std::int64_t sample) {
        PositionInfo value; value.setIsPlaying(playing); value.setTimeInSamples(sample); position = value;
    }
};
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
    {
        vox::fx::FxPanel panel(p);
        panel.outputSlider().setValue(0.5, juce::sendNotificationSync);
        check(std::abs(p.outputParameter().get() - 0.5f) < 1e-6f, "editor knob controls host parameter");
        panel.durationSlider().setValue(8.0, juce::sendNotificationSync);
        check(std::abs(p.durationParameter().get() - 8.0f) < 1e-4f, "duration editor controls parameter");
        p.durationParameter().setValueNotifyingHost(p.durationParameter().convertTo0to1(4.0f));
        panel.familySelector().setSelectedId(2, juce::sendNotificationSync);
        check(p.family() == 1, "family selector updates structural state");
        p.setFamily(0);
        p.outputParameter().setValueNotifyingHost(1.0f);
        check(std::abs(panel.outputSlider().getValue() - 1.0) < 1e-6, "host updates editor knob");
    }
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
    check(p.outputPeak() == audio.getMagnitude(0, 0, 512), "meter follows actual output peak");
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

    p.audition(); block(p, audio, midi);
    check(audio.getMagnitude(0, 0, 512) > 0, "editor audition triggers sound");
    p.panic(); block(p, audio, midi);
    check(audio.getMagnitude(0, 300, 212) == 0, "editor panic fades sound");

    {
        vox::fx::FxProcessor transport; TestPlayHead playhead;
        transport.setPlayHead(&playhead); transport.prepareToPlay(48000, 512);
        juce::MidiBuffer events;
        playhead.set(true, 0);
        events.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);
        block(transport, audio, events);
        playhead.set(true, 512); block(transport, audio, events);
        check(audio.getMagnitude(0, 300, 212) > 0, "continuous transport preserves event");
        playhead.set(false, 1024); block(transport, audio, events);
        check(audio.getMagnitude(0, 300, 212) == 0, "transport stop fades event");
        playhead.set(true, 2000);
        events.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);
        block(transport, audio, events);
        playhead.set(true, 8000); block(transport, audio, events);
        check(audio.getMagnitude(0, 300, 212) == 0, "transport seek fades event");
        playhead.set(true, 8512);
        events.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);
        block(transport, audio, events);
        playhead.set(true, 1000); block(transport, audio, events);
        check(audio.getMagnitude(0, 300, 212) == 0, "loop jump fades event");
        transport.setPlayHead(nullptr);
    }

    {
        vox::fx::FxProcessor riser, down;
        riser.prepareToPlay(48000, 512); down.prepareToPlay(48000, 512);
        check(down.setFamily(1) && !down.setFamily(7), "bounded family selection");
        juce::AudioBuffer<float> a(2,512), b(2,512);
        juce::MidiBuffer ma, mb;
        ma.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);
        mb.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);
        block(riser, a, ma); block(down, b, mb);
        double difference = 0;
        for (int i = 0; i < 512; ++i) difference += std::abs(a.getSample(0, i) - b.getSample(0, i));
        check(b.getMagnitude(0, 0, 512) > 0 && difference > 1e-4, "family choice reaches real processor DSP");
        vox::fx::FxProcessor shepard;
        shepard.prepareToPlay(48000, 512); shepard.setFamily(2);
        juce::MidiBuffer mc;
        mc.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);
        block(shepard, b, mc);
        difference = 0;
        for (int i = 0; i < 512; ++i) difference += std::abs(a.getSample(0, i) - b.getSample(0, i));
        check(b.getMagnitude(0, 0, 512) > 0 && difference > 1e-4, "Shepard reaches processor DSP");
        vox::fx::FxProcessor impact;
        impact.prepareToPlay(48000, 512); impact.setFamily(3);
        juce::MidiBuffer md;
        md.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);
        block(impact, b, md);
        check(b.getMagnitude(0, 0, 512) > 0, "impact reaches processor DSP");
    }
    auto* gain = p.getParameters()[0];
    check(gain != nullptr, "output parameter exists");
    gain->setValueNotifyingHost(0.25f);
    p.durationParameter().setValueNotifyingHost(p.durationParameter().convertTo0to1(2.5f));
    p.setFamily(1);
    const auto saved = save(p);
    vox::fx::FxProcessor restored;
    restored.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    check(restored.lastStateError().isEmpty(), "valid state accepted");
    check(std::abs(restored.getParameters()[0]->getValue() - 0.25f) < 1e-6f, "parameter restored");
    check(std::abs(restored.durationParameter().get() - 2.5f) < 1e-4f, "duration restored");
    check(restored.family() == 1, "structural family restored");
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
            if (section.tag == vox::fx::fxParameterSectionTag) section.data[0] = std::byte{3};
        auto encoded = vox::preset::encode(decoded.document);
        if (encoded) {
            restored.setStateInformation(encoded->data(), static_cast<int>(encoded->size()));
            check(restored.lastStateError().isNotEmpty() && save(restored) == saved, "future FX section rejected transactionally");
        } else check(false, "encode future state");
    }
    {
        // FPRM v1 had only the output value. Restore it with the 4 s default.
        auto oldDoc = vox::preset::decode(std::span(static_cast<const std::byte*>(saved.getData()), saved.getSize()),
                                          vox::preset::makeTag("VXFX"), 1);
        auto& oldSections = oldDoc.document.sections;
        oldSections.erase(std::remove_if(oldSections.begin(), oldSections.end(), [] (const auto& section) {
            return section.tag == vox::fx::fxFamilySectionTag;
        }), oldSections.end());
        for (auto& section : oldSections) if (section.tag == vox::fx::fxParameterSectionTag) {
            section.data.resize(8); section.data[0] = std::byte{1};
        }
        auto old = vox::preset::encode(oldDoc.document);
        vox::fx::FxProcessor migrated;
        check(old.has_value(), "encode v1 state");
        if (old) {
            migrated.setStateInformation(old->data(), static_cast<int>(old->size()));
            check(migrated.lastStateError().isEmpty() && migrated.family() == 0 &&
                  std::abs(migrated.durationParameter().get() - 4.0f) < 1e-4f,
                  "v1 state migrates to four second duration");
        }
    }
    {
        vox::fx::FxProcessor durationTest; durationTest.prepareToPlay(48000, 512);
        durationTest.durationParameter().setValueNotifyingHost(0.0f);
        juce::MidiBuffer notes;
        notes.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);
        block(durationTest, audio, notes);
        for (int i = 0; i < 4; ++i) block(durationTest, audio, notes);
        check(audio.getMagnitude(0, 400, 112) == 0, "duration parameter affects triggered event");
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
