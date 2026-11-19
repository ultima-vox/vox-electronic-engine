// SYN-01 product tests: ENG1 round-trip, host parameters, both parts, the
// shared voice cap, realtime allocation, and rejection of a bad engine blob.
#include "Processor.h"
#include "Parameters.h"
#include "FactoryBank.h"
#include "FactoryContent.h"
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
        std::vector<std::tuple<int, int, int, bool>> events;
        int previous = -1;
        for (int n = 0; n < phrase.noteCount; ++n)
        {
            const auto& note = phrase.notes[n];
            check (note.tick >= previous && note.note >= 0 && note.note <= 127, "phrase note range");
            check (note.velocity >= 1 && note.velocity <= 127 && note.duration > 0, "phrase gate");
            previous = note.tick;
            pitches += std::to_string (note.tick) + ":" + std::to_string (note.note) + ":"
                     + std::to_string (note.velocity) + ":" + (note.slide ? "s" : "") + (note.accent ? "a" : "") + ";";
            events.push_back ({ note.tick * 25, 1, note.note, note.velocity >= 110 });
            events.push_back ({ (note.tick + note.duration) * 25, 0, note.note, false });
        }
        check (pitchKeys.insert (pitches).second, std::string (phrase.id) + " pitch row is unique without duration salt");
        std::sort (events.begin(), events.end());
        juce::AudioBuffer<float> buffer (2, 256);
        juce::MidiBuffer midi;
        std::size_t at = 0;
        const int end = std::get<0> (events.back()) + 16000;
        float peak = 0.0f;
        bool finite = true;
        bool ordered = true;
        int lastSample = -1;
        for (int origin = 0; origin < end; origin += 256)
        {
            midi.clear();
            while (at < events.size() && std::get<0> (events[at]) < origin + 256)
            {
                const int sample = std::get<0> (events[at]) - origin;
                ordered = ordered && std::get<0> (events[at]) >= lastSample;
                lastSample = std::get<0> (events[at]);
                const int note = std::get<1> (events[at]) == 1 ? std::get<2> (events[at]) : std::get<2> (events[at]);
                if (std::get<1> (events[at]) == 1)
                    midi.addEvent (juce::MidiMessage::noteOn (1, note, static_cast<juce::uint8> (100)), sample);
                else
                    midi.addEvent (juce::MidiMessage::noteOff (1, note), sample);
                ++at;
            }
            synth->processBlock (buffer, midi);
            for (int sample = 0; sample < 256; ++sample)
            {
                const float left = buffer.getSample (0, sample);
                finite = finite && std::isfinite (left);
                peak = std::max (peak, std::abs (left));
            }
        }
        check (ordered && at == events.size(), std::string (phrase.id) + " events stay ordered through note-off");
        check (finite && peak > 1.0e-4f, std::string (phrase.id) + " full phrase is finite and audible");
        juce::MidiBuffer quiet;
        juce::AudioBuffer<float> tail (2, 256);
        for (int block = 0; block < 80 && synth->activeVoices() > 0; ++block)
            synth->processBlock (tail, quiet);
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

    check (synth->saveUserProgram ("Ordinal", static_cast<int> (vox::syn::ProgramCategory::user)), "save legacy user slot");
    juce::MemoryBlock saved;
    synth->getStateInformation (saved);
    auto copy = make();
    copy->setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    check (copy->userProgramCategory (0) == 8, "UBNK still stores user as category 8");
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
    testStateEdges();
    testIntegrationStress();
    if (failures == 0 && skips == 0)
        std::printf ("synth product tests passed (%d checks, 0 skipped)\n", checks);
    else
        std::printf ("synth product tests failed (%d checks, %d failed, %d skipped)\n", checks, failures, skips);
    return failures == 0 && skips == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
