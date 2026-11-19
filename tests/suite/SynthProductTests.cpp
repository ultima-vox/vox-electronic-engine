// SYN-01 product tests: ENG1 round-trip, host parameters, both parts, the
// shared voice cap, realtime allocation, and rejection of a bad engine blob.
#include "Processor.h"
#include "Parameters.h"
#include "FactoryBank.h"
#include "StateContainer.h"

#ifdef VOX_TEST_INTERPOSE_MALLOC
#include "AllocationProbe.h"
#endif

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {

int failures = 0;
int checks = 0;
int skips = 0;

void check (bool ok, const std::string& what)
{
    ++checks;
    if (! ok)
    {
        std::printf ("FAIL [synth product]: %s\n", what.c_str());
        ++failures;
    }
}

std::unique_ptr<vox::syn::SynthesisProcessor> make()
{
    auto* plugin = createPluginFilter();
    auto* synth = dynamic_cast<vox::syn::SynthesisProcessor*> (plugin);
    if (synth == nullptr)
        delete plugin;
    return std::unique_ptr<vox::syn::SynthesisProcessor> (synth);
}

juce::RangedAudioParameter* parameter (vox::syn::SynthesisProcessor& synth, const char* id)
{
    return dynamic_cast<juce::RangedAudioParameter*> (synth.parameters().getParameter (id));
}

void setPlain (vox::syn::SynthesisProcessor& synth, const char* id, float plain)
{
    auto* ranged = parameter (synth, id);
    ranged->setValueNotifyingHost (ranged->convertTo0to1 (plain));
}

float plain (vox::syn::SynthesisProcessor& synth, const char* id)
{
    auto* ranged = parameter (synth, id);
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (ranged))
        return static_cast<float> (choice->getIndex());
    if (auto* number = dynamic_cast<juce::AudioParameterFloat*> (ranged))
        return number->get();
    return ranged->convertFrom0to1 (ranged->getValue());
}

float peakOf (vox::syn::SynthesisProcessor& synth, int frames, int note, int velocity)
{
    synth.setRateAndBufferSizeDetails (48000.0, frames);
    synth.prepareToPlay (48000.0, frames);
    juce::AudioBuffer<float> buffer (2, frames);
    juce::MidiBuffer midi;
    if (note >= 0)
        midi.addEvent (juce::MidiMessage::noteOn (1, note, static_cast<juce::uint8> (velocity)), 0);
    float peak = 0.0f;
    bool finite = true;
    for (int block = 0; block < 6; ++block)
    {
        buffer.clear();
        synth.processBlock (buffer, midi);
        midi.clear();
        for (int i = 0; i < frames; ++i)
        {
            finite = finite && std::isfinite (buffer.getSample (0, i)) && std::isfinite (buffer.getSample (1, i));
            peak = std::max (peak, std::abs (buffer.getSample (0, i)));
        }
    }
    check (finite, "output is finite");
    return peak;
}

void testPartsAndCap()
{
    auto synth = make();
    check (synth != nullptr, "processor");
    check (std::abs (synth->getTailLengthSeconds() - vox::syn::engine::voiceTailSeconds()) < 1.0e-12,
           "tail is the documented voice bound while FX are off");
    synth->prepareToPlay (48000.0, 256);
    check (std::abs (synth->getTailLengthSeconds() - vox::syn::engine::voiceTailSeconds()) < 1.0e-12,
           "prepared tail stays the voice bound while every FX slot is off");
    synth->releaseResources();
    check (parameter (*synth, "syn.output")->getName (64) == vox::syn::outputParameterName, "output parameter name");

    setPlain (*synth, "syn.b.level", 0.0f);
    const float bass = peakOf (*synth, 256, 40, 110);
    setPlain (*synth, "syn.a.level", 0.0f);
    setPlain (*synth, "syn.b.level", 0.8f);
    synth->releaseResources();
    const float acid = peakOf (*synth, 256, 52, 110);
    check (bass > 1.0e-3f, "part A sounds");
    check (acid > 1.0e-3f, "part B sounds");

    setPlain (*synth, "syn.a.level", 0.6f);
    setPlain (*synth, "syn.a.engine", 2.0f);
    setPlain (*synth, "syn.b.engine", 2.0f);
    setPlain (*synth, "syn.b.level", 0.0f);
    synth->releaseResources();
    synth->prepareToPlay (48000.0, 256);
    for (int note = 0; note < 12; ++note)
    {
        juce::AudioBuffer<float> buffer (2, 128);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 48 + note, static_cast<juce::uint8> (100)), 0);
        synth->processBlock (buffer, midi);
        check (synth->activeVoices() <= 16, "combined voices stay <= 16");
    }
    check (synth->activeVoices() > 4, "VA engine index is the raw choice, not a normalised 0..1");
    check (synth->vaFadeLeft() > 0, "voice steal fades");
}

void testState()
{
    const auto tag = vox::preset::makeTag (VOX_TEST_STATE_TAG);
    auto source = make();
    setPlain (*source, "syn.a.cutoff", 0.15f);
    setPlain (*source, "syn.b.engine", 2.0f);
    setPlain (*source, "syn.b.slide", 0.05f);
    setPlain (*source, "syn.output", 0.25f);
    source->setEditorSize (1280, 820);

    juce::MemoryBlock saved;
    source->getStateInformation (saved);
    auto copy = make();
    copy->setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    check (copy->lastStateError().isEmpty(), "state accepted");
    check (std::abs (plain (*copy, "syn.a.cutoff") - 0.15f) < 1.0e-5f, "cutoff restored");
    check (plain (*copy, "syn.b.engine") == 2.0f, "engine restored");
    check (std::abs (plain (*copy, "syn.b.slide") - 0.05f) < 1.0e-5f, "slide restored");
    check (std::abs (plain (*copy, "syn.output") - 0.25f) < 1.0e-5f, "output restored");
    check (copy->editorWidth() == 1280 && copy->editorHeight() == 820, "UI restored with ENG1");
    juce::MemoryBlock again;
    copy->getStateInformation (again);
    check (again == saved, "restore then save is byte-identical");

    auto decoded = vox::preset::decode (
        std::span (static_cast<const std::byte*> (saved.getData()), saved.getSize()), tag, 1);
    check (decoded.ok(), "saved document decodes");
    vox::preset::StateDocument uiOnly;
    uiOnly.product = tag;
    uiOnly.schema = 1;
    for (const auto& section : decoded.document.sections)
        if (section.tag != vox::syn::engineSectionTag)
            uiOnly.sections.push_back (section);
    const auto uiBytes = vox::preset::encode (uiOnly);
    check (uiBytes.has_value(), "UI-only document encodes");
    auto bare = make();
    bare->setStateInformation (uiBytes->data(), static_cast<int> (uiBytes->size()));
    check (bare->lastStateError().isEmpty(), "missing ENG1 still loads");
    check (plain (*bare, "syn.a.engine") == 0.0f && plain (*bare, "syn.b.engine") == 1.0f, "missing ENG1 keeps defaults");
    juce::MemoryBlock withEngine;
    bare->getStateInformation (withEngine);
    auto rewritten = vox::preset::decode (
        std::span (static_cast<const std::byte*> (withEngine.getData()), withEngine.getSize()), tag, 1);
    check (rewritten.ok() && rewritten.document.find (vox::syn::engineSectionTag) != nullptr, "save adds ENG1");

    auto bad = decoded.document;
    bool sawEngine = false;
    for (auto& section : bad.sections)
        if (section.tag == vox::syn::engineSectionTag && section.data.size() >= 4)
        {
            section.data[0] = std::byte { 99 };
            sawEngine = true;
        }
    check (sawEngine, "ENG1 present");
    const auto badBytes = vox::preset::encode (bad);
    const auto before = plain (*copy, "syn.output");
    copy->setStateInformation (badBytes->data(), static_cast<int> (badBytes->size()));
    check (copy->lastStateError().isNotEmpty(), "bad ENG1 version rejected");
    check (std::abs (plain (*copy, "syn.output") - before) < 1.0e-6f, "rejected ENG1 does not change live parameters");
}

void testOutputAndRealtime()
{
    auto synth = make();
    setPlain (*synth, "syn.output", 0.0f);
    check (peakOf (*synth, 256, 36, 127) < 1.0e-6f, "output 0 silences new notes");

    synth->releaseResources();
    synth->prepareToPlay (48000.0, 128);
    juce::AudioBuffer<float> buffer (2, 128);
    juce::MidiBuffer midi;
    for (int i = 0; i < 300; ++i)
        midi.addEvent (juce::MidiMessage::noteOn (1, i % 40 + 36, static_cast<juce::uint8> (80)), i % 128);
    // A late event inserted into an earlier slot: the processor must sort.
    midi.addEvent (juce::MidiMessage::noteOn (1, 70, static_cast<juce::uint8> (100)), 0);
#ifndef VOX_TEST_INTERPOSE_MALLOC
#error "suite_synth_product_tests requires the allocation probe. A missing probe used to skip the check."
#endif
    if (! voxProbeAvailable())
    {
        ++skips;
        check (false, "allocation probe did not attach");
    }
    else
    {
        unsigned long calls = 0;
        voxProbeBegin();
        synth->processBlock (buffer, midi);
        calls = voxProbeEnd();
        check (calls == 0, "processBlock allocates nothing (" + std::to_string (calls) + ")");
    }
    check (synth->droppedMidi() > 0, "MIDI past the fixed queue is counted, not allocated");
    bool finite = true;
    for (int i = 0; i < 128; ++i)
        finite = finite && std::isfinite (buffer.getSample (0, i));
    check (finite && synth->activeVoices() <= 16, "a MIDI flood stays finite and inside the cap");
}

void silence (vox::syn::SynthesisProcessor& synth, int blocks, int frames)
{
    juce::AudioBuffer<float> buffer (2, frames);
    juce::MidiBuffer midi;
    for (int i = 0; i < blocks; ++i)
        synth.processBlock (buffer, midi);
}

bool slideKeepsPhase (float slide, float portamento)
{
    auto synth = make();
    setPlain (*synth, "syn.a.slide", slide);
    setPlain (*synth, "syn.a.portamento", portamento);
    setPlain (*synth, "syn.b.level", 0.0f);
    synth->prepareToPlay (48000.0, 64);
    juce::AudioBuffer<float> one (2, 1);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 36, static_cast<juce::uint8> (100)), 0);
    synth->processBlock (one, midi);
    midi.clear();
    bool parked = false;
    for (int i = 0; i < 8000 && ! parked; ++i)
    {
        synth->processBlock (one, midi);
        const double phase = synth->monoPhase (0);
        parked = phase >= 0.25 && phase <= 0.74;
    }
    if (! parked)
        return false;
    const double earlier = synth->monoPhase (0);
    synth->processBlock (one, midi);
    const double before = synth->monoPhase (0);
    double step = before - earlier;
    if (step < 0.0)
        step += 1.0;
    midi.addEvent (juce::MidiMessage::noteOn (1, 48, static_cast<juce::uint8> (100)), 0);
    synth->processBlock (one, midi);
    const double after = synth->monoPhase (0);
    double expected = before + step;
    if (expected >= 1.0)
        expected -= 1.0;
    const double error = std::abs (after - expected);
    return error < 0.02 || error > 0.98;
}

void testSlideVersusSnap()
{
    check (slideKeepsPhase (1.0f, 0.0f), "slide keeps the phase through the processor");
    check (slideKeepsPhase (0.0f, 1.0f), "portamento keeps the phase through the processor");
    check (! slideKeepsPhase (0.0f, 0.0f), "snap resets the phase through the processor");
}

void addModWheel (juce::MidiBuffer& midi, int count, int sample)
{
    for (int i = 0; i < count; ++i)
        midi.addEvent (juce::MidiMessage::controllerEvent (1, 1, i & 0x7f), sample);
}

void testOverflowReleases()
{
    constexpr int frames = 512;
    constexpr int extra = vox::syn::engine::maxEvents + 64;

    auto synth = make();
    synth->prepareToPlay (48000.0, frames);
    juce::AudioBuffer<float> buffer (2, frames);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 40, static_cast<juce::uint8> (100)), 0);
    synth->processBlock (buffer, midi);
    check (synth->monoGate (0) && synth->monoGate (1), "both parts hold the note");

    midi.clear();
    addModWheel (midi, extra, 0);
    midi.addEvent (juce::MidiMessage::noteOff (1, 40), frames - 1);
    const auto droppedBefore = synth->droppedMidi();
#ifndef VOX_TEST_INTERPOSE_MALLOC
#error "suite_synth_product_tests requires the allocation probe. A missing probe used to skip the check."
#endif
    if (! voxProbeAvailable())
    {
        ++skips;
        check (false, "allocation probe did not attach");
        synth->processBlock (buffer, midi);
    }
    else
    {
        voxProbeBegin();
        synth->processBlock (buffer, midi);
        const unsigned long calls = voxProbeEnd();
        check (calls == 0, "overflow processBlock allocates nothing (" + std::to_string (calls) + ")");
    }
    check (synth->droppedMidi() > droppedBefore, "saturated non-releases are counted");
    check (! synth->monoGate (0) && ! synth->monoGate (1), "note-off after a full queue is kept");
    silence (*synth, 90, frames);
    check (synth->activeVoices() == 0 && ! synth->monoGate (0), "released note reaches idle");

    midi.clear();
    midi.addEvent (juce::MidiMessage::noteOn (1, 40, static_cast<juce::uint8> (110)), 0);
    synth->processBlock (buffer, midi);
    check (synth->monoGate (0), "note is held again");
    midi.clear();
    for (int i = 0; i < extra; ++i)
        midi.addEvent (juce::MidiMessage::noteOff (1, 41), 0);
    midi.addEvent (juce::MidiMessage::noteOff (1, 40), frames - 1);
    synth->processBlock (buffer, midi);
    check (! synth->monoGate (0) && ! synth->monoGate (1), "the newest note-off survives a queue of note-offs");

    midi.clear();
    midi.addEvent (juce::MidiMessage::noteOn (1, 40, static_cast<juce::uint8> (100)), 0);
    synth->processBlock (buffer, midi);
    midi.clear();
    for (int i = 0; i < extra; ++i)
        midi.addEvent (juce::MidiMessage::noteOn (1, 48 + (i % 12), static_cast<juce::uint8> (90)), 0);
    midi.addEvent (juce::MidiMessage::controllerEvent (1, 120, 0), frames - 1);
    synth->processBlock (buffer, midi);
    check (synth->activeVoices() == 0 && ! synth->monoGate (0) && ! synth->monoGate (1),
           "CC120 after a saturated block kills held notes");

    midi.clear();
    midi.addEvent (juce::MidiMessage::controllerEvent (1, 64, 127), 0);
    midi.addEvent (juce::MidiMessage::noteOn (1, 40, static_cast<juce::uint8> (100)), 1);
    synth->processBlock (buffer, midi);
    midi.clear();
    midi.addEvent (juce::MidiMessage::noteOff (1, 40), 0);
    synth->processBlock (buffer, midi);
    check (synth->monoGate (0), "sustain holds the note after note-off");
    midi.clear();
    addModWheel (midi, extra, 0);
    midi.addEvent (juce::MidiMessage::controllerEvent (1, 64, 0), frames - 1);
    synth->processBlock (buffer, midi);
    check (! synth->monoGate (0) && ! synth->monoGate (1), "sustain release after a full queue is kept");

    midi.clear();
    midi.addEvent (juce::MidiMessage::noteOn (1, 40, static_cast<juce::uint8> (100)), 0);
    synth->processBlock (buffer, midi);
    midi.clear();
    addModWheel (midi, extra, 0);
    midi.addEvent (juce::MidiMessage::noteOn (1, 40, static_cast<juce::uint8> (0)), frames - 1);
    synth->processBlock (buffer, midi);
    check (! synth->monoGate (0), "note-on velocity 0 after a full queue releases");

    setPlain (*synth, "syn.a.engine", 2.0f);
    setPlain (*synth, "syn.b.engine", 2.0f);
    midi.clear();
    midi.addEvent (juce::MidiMessage::noteOn (1, 48, static_cast<juce::uint8> (100)), 0);
    midi.addEvent (juce::MidiMessage::noteOn (1, 52, static_cast<juce::uint8> (100)), 10);
    synth->processBlock (buffer, midi);
    check (synth->activeVoices() > 0, "VA notes are sounding");
    midi.clear();
    addModWheel (midi, extra, 0);
    midi.addEvent (juce::MidiMessage::controllerEvent (1, 123, 0), frames - 1);
    synth->processBlock (buffer, midi);
    silence (*synth, 200, frames);
    check (synth->activeVoices() == 0, "CC123 after a full queue leaves no stuck VA notes");
}

double chainTailSeconds (bool delay, bool reverb)
{
    vstengine::effects::EffectsChain chain;
    chain.prepare (48000.0, 128, 2);
    const auto enable = [&] (int index, vstengine::effects::EffectType type)
    {
        vstengine::effects::EffectSettings settings;
        settings.type = type;
        settings.enabled = true;
        settings.mix = 1.0f;
        settings.amount = 0.5f;
        settings.rate = 0.5f;
        settings.character = 0.5f;
        chain.setEffect (static_cast<std::size_t> (index), settings);
    };
    if (delay)
        enable (6, vstengine::effects::EffectType::delay);
    if (reverb)
        enable (7, vstengine::effects::EffectType::reverb);
    return static_cast<double> (chain.tailSamples()) / 48000.0;
}

float firstSample (vox::syn::SynthesisProcessor& synth)
{
    synth.prepareToPlay (48000.0, 128);
    juce::AudioBuffer<float> buffer (2, 128);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 36, static_cast<juce::uint8> (120)), 0);
    synth.processBlock (buffer, midi);
    return buffer.getSample (0, 64);
}

void testExposedFxTail()
{
    auto synth = make();
    const double voice = vox::syn::engine::voiceTailSeconds();
    const double delayTail = chainTailSeconds (true, false);
    const double reverbTail = chainTailSeconds (false, true);
    check (delayTail > voice && reverbTail > delayTail, "chain tails are longer than the voice and ordered");

    setPlain (*synth, "syn.fx.delay", 1.0f);
    check (std::abs (synth->getTailLengthSeconds() - std::max (voice, delayTail)) < 1.0e-9,
           "delay control reports the chain tail before the next callback");
    setPlain (*synth, "syn.fx.reverb", 1.0f);
    check (std::abs (synth->getTailLengthSeconds() - std::max (voice, reverbTail)) < 1.0e-9,
           "reverb control wins over delay, matching the chain");
    setPlain (*synth, "syn.fx.delay", 0.0f);
    setPlain (*synth, "syn.fx.reverb", 0.0f);
    check (std::abs (synth->getTailLengthSeconds() - voice) < 1.0e-12,
           "clearing the FX controls returns the voice bound immediately");

    auto dry = make();
    auto wet = make();
    setPlain (*wet, "syn.fx.delay", 1.0f);
    const float drySample = firstSample (*dry);
    const float wetSample = firstSample (*wet);
    check (std::abs (drySample - wetSample) > 1.0e-4f, "enabled delay changes the rendered block");

    setPlain (*synth, "syn.fx.delay", 1.0f);
    juce::MemoryBlock saved;
    synth->getStateInformation (saved);
    auto copy = make();
    copy->setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    check (std::abs (plain (*copy, "syn.fx.delay") - 1.0f) < 1.0e-5f, "delay control round-trips");
    check (std::abs (copy->getTailLengthSeconds() - std::max (voice, delayTail)) < 1.0e-9,
           "restored delay control reports the chain tail");
}

void testParameterAutomation()
{
    auto synth = make();
    check (vox::syn::parameterCatalog.size() == 75, "catalog exposes the part and FX controls");
    setPlain (*synth, "syn.a.cutoff", 0.05f);
    setPlain (*synth, "syn.a.sub", 1.0f);
    setPlain (*synth, "syn.b.wave", 1.0f);
    setPlain (*synth, "syn.fx.phaser", 1.0f);
    setPlain (*synth, "syn.fx.phaser.mix", 0.4f);
    juce::MemoryBlock saved;
    synth->getStateInformation (saved);
    auto copy = make();
    copy->setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    check (std::abs (plain (*copy, "syn.a.sub") - 1.0f) < 1.0e-5f, "sub restores");
    check (std::abs (plain (*copy, "syn.b.wave") - 1.0f) < 1.0e-5f, "wave restores");
    check (std::abs (plain (*copy, "syn.fx.phaser.mix") - 0.4f) < 1.0e-5f, "phaser mix restores");

    synth->prepareToPlay (48000.0, 128);
    juce::AudioBuffer<float> buffer (2, 128);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 36, static_cast<juce::uint8> (110)), 0);
    synth->processBlock (buffer, midi);
    const float low = buffer.getSample (0, 100);
    setPlain (*synth, "syn.a.cutoff", 0.95f);
    midi.clear();
    synth->processBlock (buffer, midi);
    bool finite = true;
    for (int i = 0; i < 128; ++i)
        finite = finite && std::isfinite (buffer.getSample (0, i));
    check (finite && std::abs (buffer.getSample (0, 100) - low) > 1.0e-5f, "cutoff automation changes the block");
}

void testProgramBrowser()
{
    check (vox::syn::factoryProgramsReady == 0, "no factory programs are approved");
    check (vox::syn::factoryPhrasesReady == 0, "no factory phrases are approved");
    check (vox::syn::factoryProgramCapacity == 600 && vox::syn::factoryPhraseCapacity == 120,
           "factory capacity is 600 programs and 120 phrases");

    auto synth = make();
    check (! synth->saveUserProgram ("", 0), "empty name is rejected");
    check (synth->saveUserProgram ("Night Acid", static_cast<int> (vox::syn::ProgramCategory::acid)), "save user program");
    setPlain (*synth, "syn.a.cutoff", 0.9f);
    check (synth->saveUserProgram ("Wide Pad", static_cast<int> (vox::syn::ProgramCategory::pad)), "save second program");
    check (synth->userProgramCount() == 2, "two user programs");
    check (synth->loadUserProgram (0), "load the first program");
    check (std::abs (plain (*synth, "syn.a.cutoff") - 0.55f) < 1.0e-5f, "loaded program restores cutoff");

    juce::MemoryBlock saved;
    synth->getStateInformation (saved);
    auto copy = make();
    copy->setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    check (copy->userProgramCount() == 2, "user bank restored");
    check (copy->userProgramName (1) == "Wide Pad", "program name restored");
    check (copy->userProgramCategory (1) == static_cast<int> (vox::syn::ProgramCategory::pad), "category restored");
    check (copy->loadUserProgram (1), "load restored program");
    check (std::abs (plain (*copy, "syn.a.cutoff") - 0.9f) < 1.0e-5f, "restored program recalls its cutoff");

    auto editor = std::unique_ptr<juce::AudioProcessorEditor> (synth->createEditor());
    check (editor != nullptr, "editor with the browser opens");
    if (editor != nullptr)
    {
        editor->setSize (1180, 760);
        juce::Image image (juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true);
        juce::Graphics graphics (image);
        editor->paintEntireComponent (graphics, true);
        synth->editorBeingDeleted (editor.get());
    }
}

void testPerformance()
{
    auto synth = make();
    synth->prepareToPlay (48000.0, 128);
    juce::AudioBuffer<float> buffer (2, 128);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 36, static_cast<juce::uint8> (100)), 0);
    midi.addEvent (juce::MidiMessage::noteOn (2, 48, static_cast<juce::uint8> (110)), 0);
    const auto start = std::chrono::steady_clock::now();
#ifndef VOX_TEST_INTERPOSE_MALLOC
#error "suite_synth_product_tests requires the allocation probe. A missing probe used to skip the check."
#endif
    unsigned long calls = 0;
    if (! voxProbeAvailable())
    {
        ++skips;
        check (false, "allocation probe did not attach");
    }
    else
        voxProbeBegin();
    for (int i = 0; i < 375; ++i)
    {
        synth->processBlock (buffer, midi);
        midi.clear();
    }
    if (voxProbeAvailable())
        calls = voxProbeEnd();
    const auto ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - start).count();
    std::printf ("synth perf: %.2f ms for 1.0 s audio, oversampling off, mallocs %lu\n", ms, calls);
    if (const char* path = std::getenv ("VOX_SYNTH_PERF_LOG"))
    {
        std::ofstream out (path);
        check (static_cast<bool> (out), "performance log opens");
        out << "synth perf: " << ms << " ms for 1.0 s audio, oversampling off, mallocs " << calls << "\n";
    }
    check (calls == 0, "performance render allocates nothing");
    check (ms < 1500.0, "1 s of audio stays under 1.5 s without oversampling");
    bool finite = true;
    for (int i = 0; i < 128; ++i)
        finite = finite && std::isfinite (buffer.getSample (0, i));
    check (finite, "performance render stays finite");
}

} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juce;
    testPartsAndCap();
    testState();
    testOutputAndRealtime();
    testSlideVersusSnap();
    testOverflowReleases();
    testExposedFxTail();
    testParameterAutomation();
    testProgramBrowser();
    testPerformance();
    if (failures == 0 && skips == 0)
        std::printf ("synth product tests passed (%d checks, 0 skipped)\n", checks);
    else
        std::printf ("synth product tests failed (%d checks, %d failed, %d skipped)\n", checks, failures, skips);
    return failures == 0 && skips == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
