// SYN-01 product tests: ENG1 round-trip, host parameters, both parts, the
// shared voice cap, realtime allocation, and rejection of a bad engine blob.
#include "Processor.h"
#include "Parameters.h"
#include "FactoryBank.h"
#include "FactoryContent.h"
#include "PhraseSchedule.h"
#include "StateContainer.h"

#ifdef VOX_TEST_INTERPOSE_MALLOC
#include "AllocationProbe.h"
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <tuple>
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
    check (vox::syn::factoryProgramsInstalled == 600, "600 factory programs are installed candidates");
    check (vox::syn::factoryPhrasesInstalled == 120, "120 factory phrases are installed candidates");
    check (vox::syn::factoryProgramsHumanApproved == 0, "no factory program is human-approved");
    check (vox::syn::factoryPhrasesHumanApproved == 0, "no factory phrase is human-approved");
    check (vox::syn::factoryProgramCount() == 600 && vox::syn::factoryPhraseCount() == 120,
           "delivered counts match the installed tables");
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

std::string envOrEmpty (const char* key)
{
#ifdef _MSC_VER
    char* buffer = nullptr;
    std::size_t length = 0;
    if (_dupenv_s (&buffer, &length, key) != 0 || buffer == nullptr)
        return {};
    std::string value (buffer);
    std::free (buffer);
    return value;
#else
    const char* value = std::getenv (key);
    return value != nullptr ? value : std::string {};
#endif
}

void appendPerf (const std::string& line)
{
    const auto path = envOrEmpty ("VOX_SYNTH_PERF_LOG");
    if (path.empty())
        return;
    std::ofstream out (path, std::ios::app);
    check (static_cast<bool> (out), "performance log opens");
    out << line << '\n';
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
    const auto line = "synth perf: " + std::to_string (ms) + " ms for 1.0 s audio, oversampling off, mallocs " + std::to_string (calls);
    std::printf ("%s\n", line.c_str());
    appendPerf (line);
    check (calls == 0, "performance render allocates nothing");
    check (ms < 1500.0, "1 s of audio stays under 1.5 s without oversampling");
    bool finite = true;
    for (int i = 0; i < 128; ++i)
        finite = finite && std::isfinite (buffer.getSample (0, i));
    check (finite, "performance render stays finite");
}

void putU32 (std::vector<std::byte>& out, std::uint32_t value)
{
    for (int shift = 0; shift < 32; shift += 8)
        out.push_back (static_cast<std::byte> ((value >> shift) & 0xffu));
}

std::vector<std::byte> uiSectionBytes()
{
    std::vector<std::byte> data;
    putU32 (data, 1180u);
    putU32 (data, 760u);
    return data;
}

std::vector<std::byte> engSection (std::initializer_list<std::pair<const char*, float>> entries)
{
    std::vector<std::byte> data;
    putU32 (data, 1u);
    putU32 (data, static_cast<std::uint32_t> (entries.size()));
    for (const auto& entry : entries)
    {
        const auto length = static_cast<std::uint32_t> (std::strlen (entry.first));
        putU32 (data, length);
        for (std::uint32_t i = 0; i < length; ++i)
            data.push_back (static_cast<std::byte> (entry.first[i]));
        std::uint32_t bits = 0;
        std::memcpy (&bits, &entry.second, sizeof bits);
        putU32 (data, bits);
    }
    return data;
}

void applyDocument (vox::syn::SynthesisProcessor& synth, const vox::preset::StateDocument& document)
{
    const auto bytes = vox::preset::encode (document);
    check (bytes.has_value(), "document encodes");
    if (! bytes)
        return;
    synth.setStateInformation (bytes->data(), static_cast<int> (bytes->size()));
}

struct SmfNote { int tick = 0, note = 0, velocity = 0, duration = 0; };

int readVarlen (const std::vector<unsigned char>& data, std::size_t& at)
{
    int value = 0;
    for (int i = 0; i < 4; ++i)
    {
        if (at >= data.size())
            return -1;
        const unsigned char byte = data[at++];
        value = (value << 7) | (byte & 0x7f);
        if ((byte & 0x80u) == 0)
            return value;
    }
    return -1;
}

std::vector<SmfNote> readSmf (const std::string& path, std::string& error)
{
    std::ifstream in (path, std::ios::binary);
    const std::vector<unsigned char> data ((std::istreambuf_iterator<char> (in)), {});
    const auto fail = [&] (const char* why)
    {
        error = why;
        return std::vector<SmfNote> {};
    };
    auto be16 = [&] (std::size_t at)
    {
        return (static_cast<unsigned> (data[at]) << 8) | data[at + 1];
    };
    auto be32 = [&] (std::size_t at)
    {
        return (static_cast<unsigned> (data[at]) << 24) | (static_cast<unsigned> (data[at + 1]) << 16)
             | (static_cast<unsigned> (data[at + 2]) << 8) | data[at + 3];
    };
    if (data.size() < 22 || std::memcmp (data.data(), "MThd", 4) != 0)
        return fail ("bad header");
    if (be32 (4) != 6 || be16 (8) != 0 || be16 (10) != 1 || be16 (12) != 960)
        return fail ("not a type-0 960 file");
    if (std::memcmp (data.data() + 14, "MTrk", 4) != 0)
        return fail ("missing track");
    if (static_cast<std::size_t> (22 + be32 (18)) != data.size())
        return fail ("track length does not match the file");

    bool tempo = false, notice = false, ended = false;
    std::vector<SmfNote> pending[128];
    std::vector<SmfNote> notes;
    int tick = 0;
    int running = 0;
    std::size_t at = 22;
    while (at < data.size())
    {
        const int delta = readVarlen (data, at);
        if (delta < 0)
            return fail ("bad delta");
        tick += delta;
        if (at >= data.size())
            return fail ("truncated event");
        int status = data[at];
        if (status == 0xff)
        {
            ++at;
            if (at >= data.size())
                return fail ("bad meta");
            const int type = data[at++];
            const int length = readVarlen (data, at);
            if (length < 0 || at + static_cast<std::size_t> (length) > data.size())
                return fail ("bad meta length");
            if (type == 0x51)
            {
                if (length != 3)
                    return fail ("tempo size");
                const int micros = (data[at] << 16) | (data[at + 1] << 8) | data[at + 2];
                if (micros != 500000)
                    return fail ("tempo is not 120 bpm");
                tempo = true;
            }
            else if (type == 0x01)
            {
                const std::string text (reinterpret_cast<const char*> (data.data() + at), static_cast<std::size_t> (length));
                if (text != vox::syn::phraseSmfLimit)
                    return fail ("missing slide/accent limit");
                notice = true;
            }
            else if (type == 0x2f)
            {
                if (length != 0 || at != data.size())
                    return fail ("end of track is not last");
                ended = true;
            }
            at += static_cast<std::size_t> (length);
            continue;
        }
        if (status < 0x80)
        {
            if (running == 0)
                return fail ("running status without a status");
            status = running;
        }
        else
        {
            running = status;
            ++at;
        }
        const int kind = status & 0xf0;
        if (kind != 0x90 && kind != 0x80)
            return fail ("unexpected MIDI status");
        if (at + 1 >= data.size())
            return fail ("short note");
        const int note = data[at++];
        const int velocity = data[at++];
        if ((kind == 0x90 && velocity > 0))
            pending[note].push_back ({ tick, note, velocity, 0 });
        else
        {
            if (pending[note].empty())
                return fail ("note-off without a note-on");
            const auto start = pending[note].front();
            pending[note].erase (pending[note].begin());
            notes.push_back ({ start.tick, note, start.velocity, tick - start.tick });
        }
    }
    if (! tempo || ! notice || ! ended)
        return fail ("missing tempo, limit text or end of track");
    for (const auto& queue : pending)
        if (! queue.empty())
            return fail ("file ends with a sounding note");
    error.clear();
    return notes;
}

void testFactoryContent()
{
    check (vox::syn::factoryProgramCount() == 600, "600 programs installed");
    check (vox::syn::factoryPhraseCount() == 120, "120 phrases installed");
    check (static_cast<int> (vox::syn::ProgramCategory::user) == 8, "UBNK user category ordinal stays 8");
    std::set<std::string> ids, names, timbreKeys, phraseIds, pitchKeys;
    int perRole[7] = {};
    int phraseRole[7] = {};
    int psyBass = 0, vaBass = 0;
    for (int i = 0; i < vox::syn::factoryProgramCount(); ++i)
    {
        const auto& program = vox::syn::factoryProgram (i);
        check (program.id != nullptr && std::strlen (program.id) == 6, "program id");
        check (std::string (program.purpose).find ("not approved") != std::string::npos,
               "program text does not claim approval");
        check (std::string (program.purpose).find ("riser") == std::string::npos
                   && std::string (program.purpose).find ("downlifter") == std::string::npos
                   && std::string (program.purpose).find ("impact") == std::string::npos,
               "transition FX stays out of the synth bank");
        const int role = static_cast<int> (program.role);
        check (role >= 0 && role < 7, "primary role");
        ++perRole[role];
        if (role == static_cast<int> (vox::syn::FactoryRole::bass))
        {
            if (program.engineA == 0)
                ++psyBass;
            else
                ++vaBass;
        }
        else if (role == static_cast<int> (vox::syn::FactoryRole::acid))
            check (program.engineA == 1, "acid role uses the 303 engine");
        else
            check (program.engineA == 2, "non-bass role uses VA");
        ids.insert (program.id);
        names.insert (program.name);
        std::string key;
        for (std::size_t p = 0; p < vox::syn::parameterCatalog.size(); ++p)
        {
            const float value = program.values[p];
            check (value >= vox::syn::parameterCatalog[p].minimum && value <= vox::syn::parameterCatalog[p].maximum,
                   std::string (program.id) + " " + vox::syn::parameterCatalog[p].id);
            if (p == vox::syn::outputParameterIndex)
                continue;
            key.append (reinterpret_cast<const char*> (&value), sizeof value);
        }
        timbreKeys.insert (key);
    }
    check (static_cast<int> (ids.size()) == 600 && static_cast<int> (names.size()) == 600
               && static_cast<int> (timbreKeys.size()) == 600,
           "ids, names and timbre vectors are unique with output excluded");
    for (int role = 0; role < 7; ++role)
        check (perRole[role] == vox::syn::factoryRoleQuota[static_cast<std::size_t> (role)], "role quota");
    check (psyBass > 0 && vaBass > 0, "bass covers Psy Bass and VA");

    auto synth = make();
    synth->prepareToPlay (48000.0, 256);
    std::set<std::uint64_t> audioHashes;
    const auto audit = envOrEmpty ("VOX_FACTORY_AUDIT");
    std::ofstream auditFile;
    if (! audit.empty())
    {
        auditFile.open (audit);
        check (static_cast<bool> (auditFile), "factory render manifest opens");
        auditFile << "kind,id,style,role,peak,rms,clipped,hash\n";
    }
    for (int i = 0; i < vox::syn::factoryProgramCount(); ++i)
    {
        const auto& program = vox::syn::factoryProgram (i);
        check (synth->loadFactoryProgram (i), "program loads");
        check (std::abs (plain (*synth, "syn.a.cutoff") - program.values[2]) < 1.0e-5f, "cutoff loaded");
        juce::AudioBuffer<float> buffer (2, 256);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, program.auditionNote, static_cast<juce::uint8> (100)), 0);
        double sum = 0.0;
        float peak = 0.0f;
        int clipped = 0;
        int counted = 0;
        std::uint64_t hash = 1469598103934665603ull;
        bool finite = true;
        for (int block = 0; block < 94; ++block)
        {
            if (block == 47)
            {
                midi.clear();
                midi.addEvent (juce::MidiMessage::noteOff (1, program.auditionNote), 0);
            }
            synth->processBlock (buffer, midi);
            midi.clear();
            for (int sample = 0; sample < 256; ++sample)
            {
                const float left = buffer.getSample (0, sample);
                finite = finite && std::isfinite (left);
                peak = std::max (peak, std::abs (left));
                sum += static_cast<double> (left) * left;
                if (std::abs (left) > 1.0f)
                    ++clipped;
                ++counted;
                const auto bits = static_cast<std::uint32_t> (left * 1000.0f);
                hash ^= bits;
                hash *= 1099511628211ull;
            }
        }
        check (finite && peak > 1.0e-4f, std::string (program.id) + " audition is not silent");
        check (audioHashes.insert (hash).second, std::string (program.id) + " audition hash is unique");
        if (auditFile)
            auditFile << "program," << program.id << "," << program.style << ","
                      << vox::syn::factoryRoleNames[static_cast<std::size_t> (program.role)] << ","
                      << peak << "," << std::sqrt (sum / counted) << "," << clipped << "," << hash << "\n";
        juce::MidiBuffer quiet;
        for (int block = 0; block < 40 && synth->activeVoices() > 0; ++block)
            synth->processBlock (buffer, quiet);
    }
    check (! synth->loadFactoryProgram (600), "program index is bounded");

    setPlain (*synth, "syn.a.engine", 2.0f);
    setPlain (*synth, "syn.b.engine", 2.0f);
    setPlain (*synth, "syn.a.level", 0.7f);
    setPlain (*synth, "syn.b.level", 0.0f);
    setPlain (*synth, "syn.a.attack", 0.0f);
    setPlain (*synth, "syn.a.decay", 0.2f);
    setPlain (*synth, "syn.a.release", 0.12f);
    setPlain (*synth, "syn.a.sustain", 0.4f);
    setPlain (*synth, "syn.fx.delay", 0.0f);
    setPlain (*synth, "syn.fx.reverb", 0.0f);
    synth->releaseResources();
    synth->prepareToPlay (48000.0, 256);
    for (int i = 0; i < vox::syn::factoryPhraseCount(); ++i)
    {
        const auto& phrase = vox::syn::factoryPhrase (i);
        check (phrase.noteCount > 0 && phrase.ticksPerQuarter == 960 && phrase.lengthTicks == 3840, "phrase timing");
        ++phraseRole[static_cast<int> (phrase.role)];
        phraseIds.insert (phrase.id);
        std::string pitches;
        int previous = -1;
        for (int n = 0; n < phrase.noteCount; ++n)
        {
            const auto& note = phrase.notes[n];
            check (note.tick >= previous && note.note >= 0 && note.note <= 127, "phrase note range");
            check (note.velocity >= 1 && note.velocity <= 127 && note.duration > 0, "phrase gate");
            previous = note.tick;
            pitches += std::to_string (note.tick) + ":" + std::to_string (note.note) + ":"
                     + std::to_string (note.velocity) + ":" + (note.slide ? "s" : "") + (note.accent ? "a" : "") + ";";
        }
        check (pitchKeys.insert (pitches).second, std::string (phrase.id) + " pitch row is unique without duration salt");

        const auto scheduled = vox::syn::schedulePhrase (phrase.notes, phrase.noteCount);
        for (int n = 0; n < phrase.noteCount; ++n)
        {
            const auto& note = phrase.notes[n];
            bool matched = false;
            for (const auto& event : scheduled)
                matched = matched || (event.on && event.tick == note.tick && event.note == note.note
                                       && event.velocity == note.velocity && event.accent == note.accent);
            check (matched, std::string (phrase.id) + " schedule keeps velocity and accent");
        }
        int lastTick = 0;
        bool slideBeforeEnd = false;
        for (int n = 0; n < phrase.noteCount; ++n)
            lastTick = std::max (lastTick, phrase.notes[n].tick);
        for (int n = 0; n < phrase.noteCount; ++n)
            slideBeforeEnd = slideBeforeEnd || (phrase.notes[n].slide && phrase.notes[n].tick < lastTick);
        if (slideBeforeEnd)
        {
            bool tied = false;
            for (const auto& event : scheduled)
                tied = tied || (event.on && event.slide);
            check (tied, std::string (phrase.id) + " slide ties into the next note");
        }

        std::string smfError;
        const auto smf = readSmf (std::string (VOX_FACTORY_MIDI_DIR) + "/" + phrase.id + ".mid", smfError);
        check (smfError.empty(), std::string (phrase.id) + " " + smfError);
        check (static_cast<int> (smf.size()) == phrase.noteCount, std::string (phrase.id) + " SMF note count");
        if (static_cast<int> (smf.size()) == phrase.noteCount)
        {
            for (int n = 0; n < phrase.noteCount; ++n)
            {
                const auto& note = phrase.notes[n];
                bool matched = false;
                for (const auto& fileNote : smf)
                    matched = matched || (fileNote.tick == note.tick && fileNote.note == note.note
                                           && fileNote.velocity == note.velocity && fileNote.duration == note.duration);
                check (matched, std::string (phrase.id) + " SMF matches tick, note, velocity, duration");
            }
        }

        std::vector<vox::syn::AuditionEvent> play;
        int lastSample = 0;
        for (const auto& event : scheduled)
        {
            vox::syn::AuditionEvent item;
            item.sample = vox::syn::phraseSample (event.tick, 48000.0);
            item.on = event.on;
            item.note = event.note;
            item.velocity = event.velocity;
            item.slide = event.slide;
            item.accent = event.accent;
            if (event.on)
                check (item.velocity == event.velocity && item.velocity > 0, std::string (phrase.id) + " audition uses the phrase velocity");
            play.push_back (item);
            lastSample = std::max (lastSample, item.sample);
        }
        juce::AudioBuffer<float> buffer (2, 256);
        std::size_t at = 0;
        const int end = lastSample + 16000;
        float peak = 0.0f;
        bool finite = true;
        for (int origin = 0; origin < end; origin += 256)
        {
            std::vector<vox::syn::AuditionEvent> block;
            while (at < play.size() && play[at].sample < origin + 256)
            {
                auto item = play[at++];
                item.sample -= origin;
                block.push_back (item);
            }
            synth->audition (buffer, block.data(), static_cast<int> (block.size()));
            for (int sample = 0; sample < 256; ++sample)
            {
                const float left = buffer.getSample (0, sample);
                finite = finite && std::isfinite (left);
                peak = std::max (peak, std::abs (left));
            }
        }
        check (at == play.size(), std::string (phrase.id) + " schedule played through note-off");
        check (finite && peak > 1.0e-4f, std::string (phrase.id) + " full phrase is finite and audible");
        juce::AudioBuffer<float> tail (2, 256);
        for (int block = 0; block < 80 && synth->activeVoices() > 0; ++block)
            synth->audition (tail, nullptr, 0);
        check (synth->activeVoices() == 0, std::string (phrase.id) + " releases");
        if (auditFile)
            auditFile << "phrase," << phrase.id << "," << phrase.style << ","
                      << vox::syn::factoryRoleNames[static_cast<std::size_t> (phrase.role)] << ","
                      << peak << ",,0,\n";
    }
    check (phraseRole[0] == 30 && phraseRole[1] == 25 && phraseRole[5] == 25 && phraseRole[2] == 20
               && phraseRole[4] + phraseRole[6] == 20,
           "phrase quotas are bass 30, acid 25, arp 25, lead 20, pad+stab 20");
    check (static_cast<int> (phraseIds.size()) == 120, "phrase ids are unique");
}

void playScheduled (vox::syn::SynthesisProcessor& synth, const std::vector<vox::syn::ScheduledEvent>& scheduled,
                   double rate, int block, float& peak, bool& finite)
{
    std::vector<vox::syn::AuditionEvent> play;
    int last = 0;
    for (const auto& event : scheduled)
    {
        vox::syn::AuditionEvent item;
        item.sample = vox::syn::phraseSample (event.tick, rate);
        item.on = event.on;
        item.note = event.note;
        item.velocity = event.velocity;
        item.slide = event.slide;
        item.accent = event.accent;
        play.push_back (item);
        last = std::max (last, item.sample);
    }
    juce::AudioBuffer<float> buffer (2, block);
    std::size_t at = 0;
    const int end = last + static_cast<int> (rate * 0.4);
    peak = 0.0f;
    finite = true;
    for (int origin = 0; origin < end; origin += block)
    {
        std::vector<vox::syn::AuditionEvent> slice;
        while (at < play.size() && play[at].sample < origin + block)
        {
            auto item = play[at++];
            item.sample -= origin;
            slice.push_back (item);
        }
        synth.audition (buffer, slice.data(), static_cast<int> (slice.size()));
        for (int sample = 0; sample < block; ++sample)
        {
            const float left = buffer.getSample (0, sample);
            finite = finite && std::isfinite (left);
            peak = std::max (peak, std::abs (left));
        }
    }
    juce::AudioBuffer<float> tail (2, block);
    const int guard = static_cast<int> (rate * 0.6 / static_cast<double> (block)) + 2;
    for (int i = 0; i < guard && synth.activeVoices() > 0; ++i)
        synth.audition (tail, nullptr, 0);
}

void testPhraseClock()
{
    const int ticks[] = { 0, 240, 960, 3840 };
    const double rates[] = { 44100.0, 48000.0, 96000.0 };
    for (double rate : rates)
        for (int tick : ticks)
        {
            const int sample = vox::syn::phraseSample (tick, rate);
            const double musical = static_cast<double> (tick) * 60.0 / (120.0 * 960.0);
            check (std::abs (static_cast<double> (sample) / rate - musical) <= 0.5 / rate + 1.0e-9,
                   "tick lands within half a sample");
        }

    const auto& phrase = vox::syn::factoryPhrase (0);
    const auto scheduled = vox::syn::schedulePhrase (phrase.notes, phrase.noteCount);
    const auto run = [&] (double rate, int block)
    {
        auto synth = make();
        setPlain (*synth, "syn.a.engine", 2.0f);
        setPlain (*synth, "syn.b.engine", 2.0f);
        setPlain (*synth, "syn.a.level", 0.7f);
        setPlain (*synth, "syn.b.level", 0.0f);
        setPlain (*synth, "syn.a.release", 0.1f);
        setPlain (*synth, "syn.b.release", 0.1f);
        setPlain (*synth, "syn.a.decay", 0.15f);
        synth->prepareToPlay (rate, block);
        float peak = 0.0f;
        bool finite = false;
        playScheduled (*synth, scheduled, rate, block, peak, finite);
        check (finite && peak > 1.0e-4f, "phrase is audible at this rate and block");
        check (synth->activeVoices() == 0, "phrase releases at this rate and block");
    };
    run (44100.0, 64);
    run (48000.0, 256);
    run (96000.0, 1000);
}

void armMono (vox::syn::SynthesisProcessor& synth, float engine)
{
    setPlain (synth, "syn.a.engine", engine);
    setPlain (synth, "syn.b.level", 0.0f);
    setPlain (synth, "syn.a.level", 0.8f);
    setPlain (synth, "syn.a.slide", 0.0f);
    setPlain (synth, "syn.a.portamento", 0.0f);
    setPlain (synth, "syn.a.cutoff", 0.45f);
    setPlain (synth, "syn.a.sustain", 0.8f);
    setPlain (synth, "syn.a.decay", 0.4f);
    setPlain (synth, "syn.fx.delay", 0.0f);
    setPlain (synth, "syn.fx.reverb", 0.0f);
    synth.prepareToPlay (48000.0, 256);
}

void testArticulation()
{
    auto acid = make();
    armMono (*acid, 1.0f);
    vox::syn::AuditionEvent first { 0, true, 48, 70, false, false };
    juce::AudioBuffer<float> buffer (2, 256);
    acid->audition (buffer, &first, 1);
    for (int i = 0; i < 8; ++i)
        acid->audition (buffer, nullptr, 0);
    const double before = acid->monoFrequency (0);
    vox::syn::AuditionEvent glide[2] = {
        { 0, true, 60, 64, true, true },
        { 1, false, 48, 0, false, false },
    };
    acid->audition (buffer, glide, 2);
    check (acid->glideSource (0) == 1, "acid slide flag glides with the knob at 0");
    check (acid->monoNote (0) == 60, "acid slide lands on the new note");
    check (acid->monoFrequency (0) > before && acid->monoFrequency (0) < before * 1.5,
           "acid slide does not snap to the target");
    for (int i = 0; i < 6; ++i)
        acid->audition (buffer, nullptr, 0);
    check (acid->monoAccent (0) > 0.5f, "acid accent flag is not a velocity rewrite");

    auto host = make();
    armMono (*host, 1.0f);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 50, static_cast<juce::uint8> (120)), 0);
    host->processBlock (buffer, midi);
    for (int i = 0; i < 6; ++i)
    {
        midi.clear();
        host->processBlock (buffer, midi);
    }
    check (host->monoAccent (0) > 0.5f, "host acid velocity 120 is still an accent");
    auto quiet = make();
    armMono (*quiet, 1.0f);
    midi.clear();
    midi.addEvent (juce::MidiMessage::noteOn (1, 50, static_cast<juce::uint8> (70)), 0);
    quiet->processBlock (buffer, midi);
    for (int i = 0; i < 6; ++i)
    {
        midi.clear();
        quiet->processBlock (buffer, midi);
    }
    check (quiet->monoAccent (0) < 0.05f, "host acid velocity 70 is not an accent");

    auto psy = make();
    armMono (*psy, 0.0f);
    psy->audition (buffer, &first, 1);
    for (int i = 0; i < 4; ++i)
        psy->audition (buffer, nullptr, 0);
    const double psyBefore = psy->monoFrequency (0);
    psy->audition (buffer, glide, 2);
    check (psy->glideSource (0) == 1, "psy slide flag glides");
    check (psy->monoFrequency (0) > psyBefore && psy->monoFrequency (0) < psyBefore * 1.5, "psy slide does not snap");
    for (int i = 0; i < 6; ++i)
        psy->audition (buffer, nullptr, 0);
    check (psy->monoAccent (0) > 0.5f, "psy accent flag raises the voice");
    auto psyHost = make();
    armMono (*psyHost, 0.0f);
    midi.clear();
    midi.addEvent (juce::MidiMessage::noteOn (1, 40, static_cast<juce::uint8> (120)), 0);
    psyHost->processBlock (buffer, midi);
    for (int i = 0; i < 6; ++i)
    {
        midi.clear();
        psyHost->processBlock (buffer, midi);
    }
    check (psyHost->monoAccent (0) < 0.05f, "host psy velocity is not an accent");

    auto va = make();
    armMono (*va, 2.0f);
    setPlain (*va, "syn.b.engine", 2.0f);
    setPlain (*va, "syn.a.decay", 0.3f);
    setPlain (*va, "syn.a.sustain", 0.6f);
    setPlain (*va, "syn.b.decay", 0.3f);
    setPlain (*va, "syn.b.sustain", 0.6f);
    vox::syn::AuditionEvent chord[2] = {
        { 0, true, 60, 90, true, false },
        { 0, true, 67, 90, true, false },
    };
    va->audition (buffer, chord, 2);
    check (va->activeVoices() == 4, "VA ignores slide and stays polyphonic on both parts");

    auto dry = make();
    armMono (*dry, 2.0f);
    setPlain (*dry, "syn.a.decay", 0.05f);
    setPlain (*dry, "syn.a.sustain", 0.0f);
    vox::syn::AuditionEvent plain { 0, true, 72, 80, false, false };
    dry->audition (buffer, &plain, 1);
    float plainPeak = 0.0f;
    for (int sample = 0; sample < 256; ++sample)
        plainPeak = std::max (plainPeak, std::abs (buffer.getSample (0, sample)));
    auto marked = make();
    armMono (*marked, 2.0f);
    setPlain (*marked, "syn.a.decay", 0.05f);
    setPlain (*marked, "syn.a.sustain", 0.0f);
    vox::syn::AuditionEvent accented { 0, true, 72, 80, false, true };
    marked->audition (buffer, &accented, 1);
    float accentPeak = 0.0f;
    for (int sample = 0; sample < 256; ++sample)
        accentPeak = std::max (accentPeak, std::abs (buffer.getSample (0, sample)));
    check (accentPeak > plainPeak * 1.15f, "VA accent raises gain without changing velocity");
}

void testHistoricalUbnk()
{
    std::ifstream in (VOX_UBNK_FIXTURE, std::ios::binary);
    const std::vector<char> bytes ((std::istreambuf_iterator<char> (in)), {});
    check (! bytes.empty(), "b0f230f UBNK fixture is present");
    auto synth = make();
    synth->setStateInformation (bytes.data(), static_cast<int> (bytes.size()));
    check (synth->lastStateError().isEmpty(), "historical UBNK loads: " + synth->lastStateError().toStdString());
    check (std::abs (plain (*synth, "syn.a.cutoff") - 0.2f) < 1.0e-5f, "historical ENG1 cutoff restored");
    check (synth->userProgramCount() == 4, "four historical programs");
    const int categories[] = { 0, 3, 7, 8 };
    const char* names[] = { "Old Bass", "Old Pluck", "Old FX", "Old User" };
    for (int i = 0; i < 4; ++i)
    {
        check (synth->userProgramCategory (i) == categories[i], "historical category ordinal");
        check (synth->userProgramName (i) == names[i], "historical program name");
    }
    check (synth->loadUserProgram (0), "load historical bass");
    check (std::abs (plain (*synth, "syn.a.cutoff") - 0.41f) < 1.0e-5f, "historical program value restored");

    juce::MemoryBlock saved;
    synth->getStateInformation (saved);
    auto copy = make();
    copy->setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    check (copy->lastStateError().isEmpty(), "re-saved historical bank loads");
    for (int i = 0; i < 4; ++i)
    {
        check (copy->userProgramCategory (i) == categories[i], "category survives re-save");
        check (copy->userProgramName (i) == names[i], "name survives re-save");
    }
    check (copy->loadUserProgram (3), "load historical user slot");
    check (copy->userProgramCategory (3) == 8, "user ordinal 8 survives re-save");
    check (std::abs (plain (*copy, "syn.a.cutoff") - 0.61f) < 1.0e-5f, "historical user value survives re-save");
}

void testPhrasePlayer()
{
    int slideJoins = 0;
    for (int i = 0; i < vox::syn::factoryPhraseCount(); ++i)
    {
        const auto& phrase = vox::syn::factoryPhrase (i);
        const auto authored = vox::syn::schedulePhrase (phrase.notes, phrase.noteCount, vox::syn::PhraseProjection::authored);
        const auto played = vox::syn::schedulePhrase (phrase.notes, phrase.noteCount, vox::syn::PhraseProjection::performance);
        for (int n = 0; n < phrase.noteCount; ++n)
        {
            const int off = phrase.notes[n].tick + phrase.notes[n].duration;
            bool found = false;
            for (const auto& event : authored)
                found = found || (! event.on && event.note == phrase.notes[n].note && event.tick == off);
            check (found, std::string (phrase.id) + " authored NoteOff stays at tick+duration");
        }
        for (int n = 0; n < phrase.noteCount; ++n)
        {
            if (! phrase.notes[n].slide)
                continue;
            int dest = -1;
            for (int j = 0; j < phrase.noteCount; ++j)
                if (phrase.notes[j].tick > phrase.notes[n].tick && (dest < 0 || phrase.notes[j].tick < dest))
                    dest = phrase.notes[j].tick;
            if (dest < 0)
                continue;
            ++slideJoins;
            bool tied = false;
            for (const auto& event : played)
                tied = tied || (! event.on && event.note == phrase.notes[n].note && event.tick == dest);
            check (tied, std::string (phrase.id) + " performance holds a slide gate to the next note");
            bool into = false;
            for (const auto& event : played)
                into = into || (event.on && event.tick == dest && event.slide);
            check (into, std::string (phrase.id) + " performance marks the destination slide");
        }
        std::string error;
        const auto fileNotes = readSmf (std::string (VOX_FACTORY_MIDI_DIR) + "/" + phrase.id + ".mid", error);
        check (error.empty(), std::string (phrase.id) + " " + error);
        const auto exported = vox::syn::exportPhraseSmf (phrase.notes, phrase.noteCount);
        const auto exportedPath = std::string ("/tmp/") + phrase.id + "-policy.mid";
        {
            std::ofstream out (exportedPath, std::ios::binary);
            out.write (reinterpret_cast<const char*> (exported.data()), static_cast<std::streamsize> (exported.size()));
        }
        std::string exportError;
        const auto roundTrip = readSmf (exportedPath, exportError);
        check (exportError.empty() && static_cast<int> (roundTrip.size()) == phrase.noteCount, std::string (phrase.id) + " C++ SMF export parses");
        if (static_cast<int> (fileNotes.size()) == phrase.noteCount && static_cast<int> (roundTrip.size()) == phrase.noteCount)
        {
            for (int n = 0; n < phrase.noteCount; ++n)
            {
                const auto& note = phrase.notes[n];
                bool inFile = false;
                bool inExport = false;
                for (const auto& fileNote : fileNotes)
                    inFile = inFile || (fileNote.tick == note.tick && fileNote.note == note.note
                                         && fileNote.velocity == note.velocity && fileNote.duration == note.duration);
                for (const auto& fileNote : roundTrip)
                    inExport = inExport || (fileNote.tick == note.tick && fileNote.note == note.note
                                             && fileNote.velocity == note.velocity && fileNote.duration == note.duration);
                check (inFile && inExport, std::string (phrase.id) + " file and C++ export share the authored note");
            }
        }
    }
    check (slideJoins > 0, "at least one slide join exists");

    const auto& acid = vox::syn::factoryPhrase (30);
    check (std::string (acid.id) == "FP-031", "FP-031 is the acid slide example");
    bool authored700 = false;
    bool played720 = false;
    const auto authored = vox::syn::schedulePhrase (acid.notes, acid.noteCount, vox::syn::PhraseProjection::authored);
    const auto played = vox::syn::schedulePhrase (acid.notes, acid.noteCount, vox::syn::PhraseProjection::performance);
    for (const auto& event : authored)
        authored700 = authored700 || (! event.on && event.note == 46 && event.tick == 700);
    for (const auto& event : played)
        played720 = played720 || (! event.on && event.note == 46 && event.tick == 720);
    check (authored700 && played720, "FP-031 authored NoteOff is 700 and performance NoteOff is 720");

    auto edited = make();
    edited->setPhraseIndex (30);
    auto changed = edited->phraseNote (2);
    check (changed.tick == 480 && changed.velocity == 120, "FP-031 step 2 is the slide note");
    changed.velocity = 42;
    edited->setPhraseNote (2, changed);
    const auto bytes = edited->exportCurrentPhrase();
    {
        std::ofstream out ("/tmp/FP-031-edited.mid", std::ios::binary);
        out.write (reinterpret_cast<const char*> (bytes.data()), static_cast<std::streamsize> (bytes.size()));
    }
    std::string editError;
    const auto editedNotes = readSmf ("/tmp/FP-031-edited.mid", editError);
    bool editedVelocity = false;
    for (const auto& note : editedNotes)
        editedVelocity = editedVelocity || (note.tick == 480 && note.note == 46 && note.velocity == 42 && note.duration == 220);
    check (editError.empty() && editedVelocity, "edited velocity is in the exported SMF");
    std::string originalError;
    const auto original = readSmf (std::string (VOX_FACTORY_MIDI_DIR) + "/FP-031.mid", originalError);
    bool originalKept = false;
    for (const auto& note : original)
        originalKept = originalKept || (note.tick == 480 && note.velocity == 120);
    check (originalKept, "factory SMF is not rewritten by an edit");

    auto join = make();
    setPlain (*join, "syn.a.engine", 1.0f);
    setPlain (*join, "syn.b.level", 0.0f);
    setPlain (*join, "syn.a.slide", 0.0f);
    setPlain (*join, "syn.a.portamento", 0.0f);
    join->prepareToPlay (48000.0, 256);
    juce::AudioBuffer<float> buffer (2, 256);
    vox::syn::AuditionEvent first { 0, true, 48, 70, false, false };
    join->audition (buffer, &first, 1);
    vox::syn::AuditionEvent pair[2] = {
        { 0, true, 48, 64, true, true },
        { 1, false, 48, 0, false, false },
    };
    join->audition (buffer, pair, 2);
    check (join->monoGate (0) && join->monoNote (0) == 48, "processor same-note join keeps the new gate");
    check (join->glideSource (0) == 1, "processor same-note join glides");

    auto split = make();
    setPlain (*split, "syn.a.engine", 1.0f);
    setPlain (*split, "syn.b.level", 0.0f);
    split->prepareToPlay (48000.0, 256);
    vox::syn::AuditionEvent late { 200, true, 50, 90, false, false };
    split->audition (buffer, &late, 1);
    check (split->monoGate (0), "note-on late in the block is sounding");
    vox::syn::AuditionEvent off { 0, false, 50, 0, false, false };
    split->audition (buffer, &off, 1);
    check (! split->monoGate (0), "note-off in the next block releases it");

    auto offsets = make();
    setPlain (*offsets, "syn.a.engine", 1.0f);
    setPlain (*offsets, "syn.b.level", 0.0f);
    offsets->prepareToPlay (48000.0, 64);
    juce::AudioBuffer<float> small (2, 64);
    vox::syn::AuditionEvent chord[3] = {
        { 0, true, 60, 80, false, false },
        { 10, true, 64, 80, false, false },
        { 20, true, 67, 80, false, false },
    };
    offsets->audition (small, chord, 3);
    check (offsets->monoGate (0) && offsets->monoNote (0) == 67, "later offset in the block wins the mono note");

    auto player = make();
    setPlain (*player, "syn.a.engine", 1.0f);
    setPlain (*player, "syn.b.level", 0.0f);
    setPlain (*player, "syn.a.slide", 0.0f);
    player->prepareToPlay (48000.0, 256);
    player->setPhraseIndex (0);
    const int firstTick = player->phraseNote (0).tick;
    const int firstNote = player->phraseNote (0).note;
    const int at = vox::syn::phraseSample (firstTick, 48000.0);
    player->setPhrasePlaying (true);
    int started = -1;
    juce::MidiBuffer silent;
    for (int origin = 0; origin < at + 256; origin += 256)
    {
        player->processBlock (buffer, silent);
        silent.clear();
        if (started < 0 && player->monoGate (0) && player->monoNote (0) == firstNote)
            started = origin;
    }
    check (started == (at / 256) * 256, "internal clock starts the note in the block that contains its sample");

    auto host = make();
    setPlain (*host, "syn.a.engine", 1.0f);
    setPlain (*host, "syn.b.level", 0.0f);
    host->prepareToPlay (48000.0, 128);
    host->setPhraseIndex (0);
    host->setPhraseClock (true, true, static_cast<double> (firstTick) / 960.0, 120.0);
    host->setPhrasePlaying (true);
    juce::AudioBuffer<float> hostBuffer (2, 128);
    host->processBlock (hostBuffer, silent);
    check (host->monoGate (0) && host->monoNote (0) == firstNote, "host ppq places the note in this block");
    host->setPhrasePlaying (false);
    host->processBlock (hostBuffer, silent);
    check (! host->monoGate (0), "stop releases the phrase");
}

void testStateEdges()
{
    auto synth = make();
    setPlain (*synth, "syn.a.cutoff", 0.77f);
    setPlain (*synth, "syn.fx.delay", 1.0f);
    const auto before = plain (*synth, "syn.a.cutoff");

    vox::preset::StateDocument old;
    old.product = vox::preset::makeTag (VOX_TEST_STATE_TAG);
    old.schema = 1;
    old.sections.push_back ({ vox::suite::uiSectionTag, uiSectionBytes() });
    old.sections.push_back ({ vox::syn::engineSectionTag, engSection ({
        { "syn.a.cutoff", 0.11f },
        { "syn.output", 0.22f },
    }) });
    applyDocument (*synth, old);
    check (synth->lastStateError().isEmpty(), "SYN-01 style ENG1 with missing ids loads");
    check (std::abs (plain (*synth, "syn.a.cutoff") - 0.11f) < 1.0e-5f, "old cutoff kept");
    check (std::abs (plain (*synth, "syn.output") - 0.22f) < 1.0e-5f, "old output kept");
    check (std::abs (plain (*synth, "syn.fx.delay") - 0.0f) < 1.0e-5f, "missing FX id falls back to the default");
    check (std::abs (plain (*synth, "syn.a.sub") - vox::syn::parameterCatalog[vox::syn::partAExtraIndex].initial) < 1.0e-5f,
           "missing extra id falls back to the default");

    setPlain (*synth, "syn.a.cutoff", before);
    juce::MemoryBlock saved;
    synth->getStateInformation (saved);
    auto corrupt = saved;
    auto* bytes = static_cast<unsigned char*> (corrupt.getData());
    bytes[corrupt.getSize() / 2] ^= 0x5a;
    synth->setStateInformation (corrupt.getData(), static_cast<int> (corrupt.getSize()));
    check (synth->lastStateError().isNotEmpty(), "corrupt state is rejected");
    check (std::abs (plain (*synth, "syn.a.cutoff") - before) < 1.0e-5f, "corrupt state does not change live parameters");

    synth->setStateInformation (saved.getData(), 24);
    check (synth->lastStateError().isNotEmpty(), "truncated state is rejected");
    check (std::abs (plain (*synth, "syn.a.cutoff") - before) < 1.0e-5f, "truncated state does not change live parameters");

    vox::preset::StateDocument oversized;
    oversized.product = old.product;
    oversized.schema = 1;
    oversized.sections.push_back ({ vox::suite::uiSectionTag, uiSectionBytes() });
    std::vector<std::byte> bank;
    putU32 (bank, 1u);
    putU32 (bank, 129u);
    oversized.sections.push_back ({ vox::syn::userBankTag, bank });
    applyDocument (*synth, oversized);
    check (synth->lastStateError().isNotEmpty(), "UBNK over 128 is rejected");
    check (std::abs (plain (*synth, "syn.a.cutoff") - before) < 1.0e-5f, "oversized bank does not change live parameters");

    const float nan = std::numeric_limits<float>::quiet_NaN();
    vox::preset::StateDocument badValue;
    badValue.product = old.product;
    badValue.schema = 1;
    badValue.sections.push_back ({ vox::suite::uiSectionTag, uiSectionBytes() });
    badValue.sections.push_back ({ vox::syn::engineSectionTag, engSection ({ { "syn.output", nan } }) });
    applyDocument (*synth, badValue);
    check (synth->lastStateError().isNotEmpty(), "non-finite ENG1 is rejected");

    setPlain (*synth, "syn.a.cutoff", 0.33f);
    for (int i = 0; i < vox::syn::maxUserPrograms; ++i)
        check (synth->saveUserProgram ("P" + std::to_string (i), static_cast<int> (vox::syn::ProgramCategory::bass)), "fill user bank");
    check (! synth->saveUserProgram ("overflow", 0), "user bank stops at 128");
    setPlain (*synth, "syn.a.cutoff", 0.91f);
    juce::AudioBuffer<float> buffer (2, 64);
    juce::MidiBuffer midi;
    synth->prepareToPlay (48000.0, 64);
    synth->processBlock (buffer, midi);
    check (synth->loadUserProgram (0), "load during automation");
    check (std::abs (plain (*synth, "syn.a.cutoff") - 0.33f) < 1.0e-5f, "loaded program wins over the later automation");
    juce::MemoryBlock full;
    synth->getStateInformation (full);
    auto copy = make();
    copy->setStateInformation (full.getData(), static_cast<int> (full.getSize()));
    check (copy->lastStateError().isEmpty() && copy->userProgramCount() == 128, "full user bank round-trips");
}

void testIntegrationStress()
{
    auto synth = make();
    const char* enables[] = {
        "syn.fx.distortion", "syn.fx.wavefolder", "syn.fx.phaser", "syn.fx.flanger",
        "syn.fx.chorus", "syn.fx.bitcrusher", "syn.fx.delay", "syn.fx.reverb"
    };
    for (const char* id : enables)
        setPlain (*synth, id, 1.0f);
    setPlain (*synth, "syn.fx.distortion.mix", 0.35f);
    setPlain (*synth, "syn.fx.wavefolder.mix", 0.25f);
    setPlain (*synth, "syn.fx.phaser.mix", 0.3f);
    setPlain (*synth, "syn.fx.flanger.mix", 0.25f);
    setPlain (*synth, "syn.fx.chorus.mix", 0.3f);
    setPlain (*synth, "syn.fx.bitcrusher.mix", 0.2f);
    setPlain (*synth, "syn.fx.delay.mix", 0.3f);
    setPlain (*synth, "syn.fx.reverb.mix", 0.3f);
    setPlain (*synth, "syn.a.engine", 2.0f);
    setPlain (*synth, "syn.b.engine", 2.0f);
    setPlain (*synth, "syn.a.level", 0.45f);
    setPlain (*synth, "syn.b.level", 0.35f);
    check (std::abs (synth->getTailLengthSeconds() - 8.0) < 1.0e-9, "all-slot stress still reports the reverb tail");

    double worstRatio = 0.0;
    double worstMs = 0.0;
    double worstDeadline = 0.0;
    unsigned long mallocs = 0;
    int clipped = 0;
    int counted = 0;
    bool finite = true;
    const double rates[] = { 44100.0, 48000.0, 96000.0 };
    const int blocks[] = { 32, 128, 512 };
    for (double rate : rates)
    {
        for (int block : blocks)
        {
            setPlain (*synth, "syn.a.engine", 2.0f);
            setPlain (*synth, "syn.b.engine", 2.0f);
            setPlain (*synth, "syn.a.level", 0.45f);
            setPlain (*synth, "syn.b.level", 0.35f);
            synth->releaseResources();
            synth->prepareToPlay (rate, block);
            juce::AudioBuffer<float> buffer (2, block);
            juce::MidiBuffer midi;
            for (int note = 0; note < 8; ++note)
                midi.addEvent (juce::MidiMessage::noteOn (1, 36 + note, static_cast<juce::uint8> (100)), 0);
            synth->processBlock (buffer, midi);
            check (synth->activeVoices() == 16, "two VA parts use the full 16 voices");
            midi.clear();
            midi.addEvent (juce::MidiMessage::noteOn (1, 70, static_cast<juce::uint8> (120)), 0);
            synth->processBlock (buffer, midi);
            check (synth->activeVoices() == 16, "the 17th note steals inside the 16-voice cap");

            midi.clear();
            for (int i = 0; i < vox::syn::engine::maxEvents + 40; ++i)
                midi.addEvent (juce::MidiMessage::controllerEvent (1, 1, i & 0x7f), 0);
            midi.addEvent (juce::MidiMessage::controllerEvent (1, 123, 0), block - 1);
            synth->processBlock (buffer, midi);
            const int drain = static_cast<int> (rate * 1.5 / static_cast<double> (block)) + 1;
            for (int i = 0; i < drain; ++i)
            {
                midi.clear();
                synth->processBlock (buffer, midi);
            }
            check (synth->activeVoices() == 0,
                   "CC123 under load leaves no stuck voice (" + std::to_string (synth->activeVoices()) + ")");

            midi.clear();
            for (int note = 0; note < 8; ++note)
                midi.addEvent (juce::MidiMessage::noteOn (1, 40 + note, static_cast<juce::uint8> (110)), 0);
            synth->processBlock (buffer, midi);
            setPlain (*synth, "syn.a.engine", 0.0f);
            setPlain (*synth, "syn.b.engine", 1.0f);
            midi.clear();
            synth->processBlock (buffer, midi);
            check (synth->activeVoices() <= 16, "engine switch stays inside the cap");
            setPlain (*synth, "syn.a.engine", 2.0f);
            setPlain (*synth, "syn.b.engine", 2.0f);

            const double deadlineMs = 1000.0 * static_cast<double> (block) / rate;
            for (std::size_t p = 0; p < vox::syn::parameterCatalog.size(); ++p)
            {
                const auto& entry = vox::syn::parameterCatalog[p];
                const float value = entry.choice ? static_cast<float> (p % 3) : (p % 2 == 0 ? 0.15f : 0.85f);
                setPlain (*synth, entry.id, value);
                midi.clear();
                if (p == 0)
                    midi.addEvent (juce::MidiMessage::noteOn (1, 48, static_cast<juce::uint8> (100)), 0);
#ifndef VOX_TEST_INTERPOSE_MALLOC
#error "suite_synth_product_tests requires the allocation probe."
#endif
                if (voxProbeAvailable())
                    voxProbeBegin();
                const auto start = std::chrono::steady_clock::now();
                synth->processBlock (buffer, midi);
                const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - start).count();
                if (voxProbeAvailable())
                    mallocs += voxProbeEnd();
                worstRatio = std::max (worstRatio, ms / deadlineMs);
                if (ms > worstMs)
                {
                    worstMs = ms;
                    worstDeadline = deadlineMs;
                }
                for (int sample = 0; sample < block; ++sample)
                {
                    const float left = buffer.getSample (0, sample);
                    finite = finite && std::isfinite (left) && std::isfinite (buffer.getSample (1, sample));
                    if (std::abs (left) > 1.0f)
                        ++clipped;
                    ++counted;
                }
            }
        }
    }
    const auto line = "synth stress: worst " + std::to_string (worstMs) + " ms vs deadline " + std::to_string (worstDeadline)
                      + " ms, ratio " + std::to_string (worstRatio) + ", mallocs " + std::to_string (mallocs)
                      + ", clipped " + std::to_string (clipped) + "/" + std::to_string (counted)
                      + ", oversampling off";
    std::printf ("%s\n", line.c_str());
    appendPerf (line);
    check (finite, "stress output stays finite");
    check (mallocs == 0, "stress processBlock allocates nothing");
    check (worstRatio < 1.0, "worst block stays inside its audio deadline");
    setPlain (*synth, "syn.fx.reverb", 0.0f);
    setPlain (*synth, "syn.fx.delay", 0.0f);
    check (synth->getTailLengthSeconds() < 2.0, "disabling delay and reverb drops the long tail");
}

void testPhraseHardening()
{
    for (int i = 0; i < vox::syn::factoryPhraseCount(); ++i)
    {
        const auto& phrase = vox::syn::factoryPhrase (i);
        vox::syn::PhraseSnapshot snap;
        check (vox::syn::fillPerformanceSnapshot (phrase.notes, phrase.noteCount, phrase.lengthTicks, snap), "snapshot fits");
        const auto played = vox::syn::schedulePhrase (phrase.notes, phrase.noteCount, vox::syn::PhraseProjection::performance);
        check (snap.eventCount == static_cast<int> (played.size()), std::string (phrase.id) + " snapshot event count");
        if (snap.eventCount == static_cast<int> (played.size()))
            for (int e = 0; e < snap.eventCount; ++e)
                check (snap.events[e].tick == played[static_cast<std::size_t> (e)].tick
                           && snap.events[e].on == played[static_cast<std::size_t> (e)].on
                           && snap.events[e].note == played[static_cast<std::size_t> (e)].note
                           && snap.events[e].velocity == played[static_cast<std::size_t> (e)].velocity
                           && snap.events[e].slide == played[static_cast<std::size_t> (e)].slide
                           && snap.events[e].accent == played[static_cast<std::size_t> (e)].accent,
                       std::string (phrase.id) + " snapshot matches the performance projection");
    }

    auto player = make();
    setPlain (*player, "syn.a.engine", 1.0f);
    setPlain (*player, "syn.b.level", 0.0f);
    player->prepareToPlay (48000.0, 128);
    player->setPhraseIndex (1);
    const int notes = player->phraseNoteCount();
    for (int i = 0; i < notes; ++i)
    {
        auto note = player->phraseNote (i);
        note.tick = i * 240;
        note.duration = 200;
        note.note = 36 + (i % 12);
        note.velocity = 70 + (i % 40);
        note.slide = i + 1 < notes;
        note.accent = i % 4 == 0;
        player->setPhraseNote (i, note);
    }
    player->setPhrasePlaying (true);
    juce::AudioBuffer<float> buffer (2, 128);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 48, static_cast<juce::uint8> (90)), 0);
    midi.clear();
    player->setPhraseClock (true, true, 0.0, 120.0);
#ifndef VOX_TEST_INTERPOSE_MALLOC
#error "suite_synth_product_tests requires the allocation probe. A missing probe used to skip the check."
#endif
    unsigned long calls = 0;
    const auto started = std::chrono::steady_clock::now();
    if (voxProbeAvailable())
        voxProbeBegin();
    for (int i = 0; i < 375; ++i)
    {
        midi.addEvent (juce::MidiMessage::noteOn (1, 48, static_cast<juce::uint8> (90)), 0);
        if (i == 200)
            player->setPhraseClock (true, true, 0.0, 120.0);
        if (i > 200)
            player->setPhraseClock (true, true, (i - 200) * (128.0 / 48000.0) * 2.0, 120.0);
        auto tweaked = player->phraseNote (0);
        tweaked.velocity = 60 + (i % 40);
        player->setPhraseNote (0, tweaked);
        player->processBlock (buffer, midi);
        midi.clear();
    }
    if (voxProbeAvailable())
        calls = voxProbeEnd();
    const auto ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - started).count();
    const auto line = "synth phrase: " + std::to_string (ms) + " ms for player-on audio, mallocs " + std::to_string (calls);
    std::printf ("%s\n", line.c_str());
    appendPerf (line);
    check (calls == 0, "player-on processBlock allocates nothing");
    check (ms < 1500.0, "player-on audio stays under 1.5 s");
    player->setPhrasePlaying (false);

    auto saturated = make();
    setPlain (*saturated, "syn.a.engine", 2.0f);
    setPlain (*saturated, "syn.b.level", 0.0f);
    saturated->prepareToPlay (48000.0, 250);
    saturated->setPhraseIndex (0);
    for (int i = 1; i < saturated->phraseNoteCount(); ++i)
    {
        auto note = saturated->phraseNote (i);
        note.tick = 3000;
        note.slide = false;
        saturated->setPhraseNote (i, note);
    }
    auto first = saturated->phraseNote (0);
    first.tick = 0;
    first.duration = 10;
    first.note = 40;
    first.velocity = 100;
    first.slide = false;
    first.accent = false;
    saturated->setPhraseNote (0, first);
    saturated->setPhrasePlaying (true);
    juce::AudioBuffer<float> block (2, 250);
    juce::MidiBuffer host;
    host.addEvent (juce::MidiMessage::noteOn (1, 72, static_cast<juce::uint8> (100)), 10);
    saturated->processBlock (block, host);
    check (saturated->vaNoteHeld (0, 40), "phrase note is held before the flood");
    check (saturated->vaNoteHeld (0, 72), "host note is held before the flood");
    juce::MidiBuffer flood;
    for (int i = 0; i < 300; ++i)
        flood.addEvent (juce::MidiMessage::controllerEvent (1, 1, i % 127), 0);
    unsigned long floodCalls = 0;
    if (voxProbeAvailable())
        voxProbeBegin();
    saturated->processBlock (block, flood);
    if (voxProbeAvailable())
        floodCalls = voxProbeEnd();
    check (floodCalls == 0, "saturated phrase block allocates nothing");
    check (! saturated->vaNoteHeld (0, 40), "phrase NoteOff is admitted inside a full host buffer");
    check (saturated->vaNoteHeld (0, 72), "phrase NoteOff does not release the host note");

    auto stopping = make();
    setPlain (*stopping, "syn.a.engine", 2.0f);
    setPlain (*stopping, "syn.b.level", 0.0f);
    stopping->prepareToPlay (48000.0, 128);
    stopping->setPhraseIndex (0);
    for (int i = 1; i < stopping->phraseNoteCount(); ++i)
    {
        auto note = stopping->phraseNote (i);
        note.tick = 3000;
        stopping->setPhraseNote (i, note);
    }
    auto held = stopping->phraseNote (0);
    held.tick = 0;
    held.duration = 960;
    held.note = 40;
    held.velocity = 90;
    held.slide = false;
    stopping->setPhraseNote (0, held);
    stopping->setPhrasePlaying (true);
    juce::AudioBuffer<float> small (2, 128);
    juce::MidiBuffer live;
    live.addEvent (juce::MidiMessage::noteOn (1, 72, static_cast<juce::uint8> (110)), 0);
    stopping->processBlock (small, live);
    check (stopping->vaNoteHeld (0, 40) && stopping->vaNoteHeld (0, 72), "phrase and host notes are both held");
    juce::MidiBuffer stopFlood;
    for (int i = 0; i < 300; ++i)
        stopFlood.addEvent (juce::MidiMessage::controllerEvent (1, 1, 20), 0);
    stopping->setPhrasePlaying (false);
    stopping->processBlock (small, stopFlood);
    check (! stopping->vaNoteHeld (0, 40), "stop releases the phrase note while the host buffer is full");
    check (stopping->vaNoteHeld (0, 72), "stop does not send a channel panic over host notes");

    auto edited = make();
    edited->setPhraseIndex (30);
    auto changed = edited->phraseNote (2);
    const int factoryVelocity = changed.velocity;
    changed.velocity = 42;
    edited->setPhraseNote (2, changed);
    const auto before = edited->exportCurrentPhrase();
    juce::MemoryBlock saved;
    edited->getStateInformation (saved);
    auto reopened = make();
    reopened->setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    check (reopened->lastStateError().isEmpty(), "PHRS state accepted");
    check (reopened->phraseIndex() == 30, "phrase index restored");
    check (reopened->phraseNote (2).velocity == 42, "edited velocity restored");
    check (reopened->exportCurrentPhrase() == before, "restored phrase exports the same SMF");
    {
        auto editor = std::unique_ptr<juce::AudioProcessorEditor> (reopened->createEditor());
        editor.reset();
    }
    check (reopened->phraseNote (2).velocity == 42, "closing the editor keeps the phrase");
    reopened->prepareToPlay (48000.0, 128);
    reopened->setPhrasePlaying (true);
    juce::MidiBuffer silent;
    reopened->processBlock (small, silent);
    bool finite = true;
    for (int i = 0; i < 128; ++i)
        finite = finite && std::isfinite (small.getSample (0, i));
    check (finite, "playback after the editor closes stays finite");

    auto decoded = vox::preset::decode (
        std::span (static_cast<const std::byte*> (saved.getData()), saved.getSize()),
        vox::preset::makeTag (VOX_SUITE_STATE_TAG), 1);
    vox::preset::StateDocument missing = decoded.document;
    missing.sections.erase (std::remove_if (missing.sections.begin(), missing.sections.end(),
                                            [] (const vox::preset::Section& section) { return section.tag == vox::syn::phraseSectionTag; }),
                            missing.sections.end());
    const auto missingBytes = vox::preset::encode (missing);
    auto migrated = make();
    migrated->setPhraseIndex (4);
    migrated->setStateInformation (missingBytes->data(), static_cast<int> (missingBytes->size()));
    check (migrated->lastStateError().isEmpty(), "missing PHRS still loads");
    check (migrated->phraseIndex() == 0, "missing PHRS rolls the working phrase back to the factory phrase");
    check (migrated->phraseNote (2).velocity != 42 || factoryVelocity == 42, "missing PHRS does not keep the edited velocity");

    auto corrupt = decoded.document;
    for (auto& section : corrupt.sections)
        if (section.tag == vox::syn::phraseSectionTag && ! section.data.empty())
            section.data[0] = std::byte { 9 };
    const auto corruptBytes = vox::preset::encode (corrupt);
    edited->setStateInformation (corruptBytes->data(), static_cast<int> (corruptBytes->size()));
    check (edited->lastStateError().isNotEmpty(), "corrupt PHRS is rejected");
    check (edited->phraseNote (2).velocity == 42, "rejected PHRS leaves the live phrase alone");

    auto truncated = decoded.document;
    for (auto& section : truncated.sections)
        if (section.tag == vox::syn::phraseSectionTag)
            section.data.resize (4);
    const auto truncatedBytes = vox::preset::encode (truncated);
    edited->setStateInformation (truncatedBytes->data(), static_cast<int> (truncatedBytes->size()));
    check (edited->lastStateError().isNotEmpty(), "truncated PHRS is rejected");
    check (edited->phraseNote (2).velocity == 42, "truncated PHRS leaves the live phrase alone");

    auto fresh = make();
    check (migrated->phraseIndex() == fresh->phraseIndex(), "missing PHRS selects the same factory phrase");
    check (migrated->phraseNote (0).note == fresh->phraseNote (0).note
               && migrated->phraseNote (0).velocity == fresh->phraseNote (0).velocity
               && migrated->phraseNote (2).note == fresh->phraseNote (2).note
               && migrated->phraseNote (2).velocity == fresh->phraseNote (2).velocity,
           "missing PHRS restores the factory phrase deterministically");

    auto shared = make();
    setPlain (*shared, "syn.a.engine", 2.0f);
    setPlain (*shared, "syn.b.level", 0.0f);
    shared->prepareToPlay (48000.0, 128);
    shared->setPhraseIndex (0);
    for (int i = 1; i < shared->phraseNoteCount(); ++i)
    {
        auto note = shared->phraseNote (i);
        note.tick = 3000;
        note.slide = false;
        shared->setPhraseNote (i, note);
    }
    auto same = shared->phraseNote (0);
    same.tick = 0;
    same.duration = 960;
    same.note = 60;
    same.velocity = 80;
    same.slide = false;
    same.accent = false;
    shared->setPhraseNote (0, same);
    shared->setPhrasePlaying (true);
    juce::AudioBuffer<float> sharedBuffer (2, 128);
    juce::MidiBuffer sharedMidi;
    sharedMidi.addEvent (juce::MidiMessage::noteOn (1, 60, static_cast<juce::uint8> (100)), 0);
    shared->processBlock (sharedBuffer, sharedMidi);
    check (shared->vaNoteHeld (0, 60), "host and phrase share one key");
    shared->setPhrasePlaying (false);
    juce::MidiBuffer sharedStop;
    for (int i = 0; i < 300; ++i)
        sharedStop.addEvent (juce::MidiMessage::controllerEvent (1, 1, 10), 0);
    shared->processBlock (sharedBuffer, sharedStop);
    check (shared->vaNoteHeld (0, 60), "phrase stop does not release the host-owned same pitch");

    auto acid = make();
    setPlain (*acid, "syn.a.engine", 1.0f);
    setPlain (*acid, "syn.b.level", 0.0f);
    acid->prepareToPlay (48000.0, 128);
    acid->setPhraseIndex (0);
    for (int i = 1; i < acid->phraseNoteCount(); ++i)
    {
        auto note = acid->phraseNote (i);
        note.tick = 3000;
        acid->setPhraseNote (i, note);
    }
    auto acidNote = acid->phraseNote (0);
    acidNote.tick = 0;
    acidNote.duration = 960;
    acidNote.note = 60;
    acidNote.velocity = 70;
    acidNote.slide = false;
    acid->setPhraseNote (0, acidNote);
    acid->setPhrasePlaying (true);
    juce::MidiBuffer acidMidi;
    acidMidi.addEvent (juce::MidiMessage::noteOn (1, 60, static_cast<juce::uint8> (90)), 0);
    acid->processBlock (sharedBuffer, acidMidi);
    check (acid->monoGate (0) && acid->monoNote (0) == 60, "acid holds the shared key");
    acid->setPhrasePlaying (false);
    acid->processBlock (sharedBuffer, sharedStop);
    check (acid->monoGate (0) && acid->monoNote (0) == 60, "acid phrase stop leaves the host key held");

    auto gated = make();
    gated->prepareToPlay (48000.0, 64);
    gated->armPhraseHandoffForTest();
    std::thread reader ([&] {
        juce::AudioBuffer<float> audio (2, 64);
        juce::MidiBuffer none;
        gated->processBlock (audio, none);
    });
    int spins = 0;
    while (gated->phraseHandoffPhase() != 2 && spins < 2000)
    {
        ++spins;
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }
    auto burst = gated->phraseNote (0);
    burst.velocity = 41;
    gated->setPhraseNote (0, burst);
    gated->setPhraseIndex (3);
    gated->releasePhraseHandoffForTest();
    reader.join();
    check (spins < 2000, "reader parked between load and claim");
    check (gated->phraseClaimViolations() == 0, "a slot was not copied while it was being written");

    auto raced = make();
    raced->prepareToPlay (48000.0, 64);
    raced->setPhrasePlaying (true);
    raced->setPhraseNote (0, raced->phraseNote (0));
    std::atomic<bool> run { true };
    std::thread writer ([&] {
        int n = 0;
        while (run.load (std::memory_order_relaxed))
        {
            raced->setPhraseIndex (n % vox::syn::factoryPhraseCount());
            auto note = raced->phraseNote (0);
            note.velocity = 30 + (n % 80);
            note.slide = (n % 2) == 0;
            raced->setPhraseNote (0, note);
            raced->setPhrasePlaying ((n % 5) != 0);
            raced->setPhraseClock ((n % 2) == 0, true, (n % 8) * 0.25, 120.0);
            if ((n % 16) == 0)
            {
                juce::MemoryBlock state;
                raced->getStateInformation (state);
                raced->setStateInformation (state.getData(), static_cast<int> (state.getSize()));
            }
            ++n;
        }
    });
    juce::AudioBuffer<float> race (2, 64);
    bool raceFinite = true;
    for (int i = 0; i < 300; ++i)
    {
        juce::MidiBuffer raceMidi;
        raceMidi.addEvent (juce::MidiMessage::noteOn (1, 50 + (i % 12), static_cast<juce::uint8> (80)), 0);
        raced->processBlock (race, raceMidi);
        for (int sample = 0; sample < 64; ++sample)
            raceFinite = raceFinite && std::isfinite (race.getSample (0, sample)) && std::isfinite (race.getSample (1, sample));
    }
    run.store (false, std::memory_order_relaxed);
    writer.join();
    raced->setPhrasePlaying (false);
    juce::MidiBuffer empty;
    raced->processBlock (race, empty);
    check (raceFinite, "edit, switch, play, stop and restore during processBlock stay finite");
    check (raced->phraseClaimViolations() == 0, "burst did not copy a slot that was being written");
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
    testFactoryContent();
    testPhraseClock();
    testArticulation();
    testHistoricalUbnk();
    testPhrasePlayer();
    testPhraseHardening();
    testStateEdges();
    testIntegrationStress();
    if (failures == 0 && skips == 0)
        std::printf ("synth product tests passed (%d checks, 0 skipped)\n", checks);
    else
        std::printf ("synth product tests failed (%d checks, %d failed, %d skipped)\n", checks, failures, skips);
    return failures == 0 && skips == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
