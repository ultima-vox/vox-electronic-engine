// SYN-01 product tests: ENG1 round-trip, host parameters, both parts, the
// shared voice cap, realtime allocation, and rejection of a bad engine blob.
#include "Processor.h"
#include "Parameters.h"
#include "StateContainer.h"

#ifdef VOX_TEST_INTERPOSE_MALLOC
#include "AllocationProbe.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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

} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juce;
    testPartsAndCap();
    testState();
    testOutputAndRealtime();
    if (failures == 0 && skips == 0)
        std::printf ("synth product tests passed (%d checks, 0 skipped)\n", checks);
    else
        std::printf ("synth product tests failed (%d checks, %d failed, %d skipped)\n", checks, failures, skips);
    return failures == 0 && skips == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
