// DRM-01: VOX Drums product tests, against the plugin's shared code.
//
//  - parameter contract: the APVTS layout matches products/drums/parameters.csv
//    (VOX_GOLDEN_UPDATE=1 rewrites it), IDs unique, every engine parameter
//    either exposed or deferred to a named later PR;
//  - editor: every plugin parameter is reachable from a bound control, and
//    controls and parameters move together both ways;
//  - sound: every pad sounds from its MIDI note and from the editor
//    audition, the internal sequencer plays each factory pattern only while
//    the host transport runs, Internal mode ignores host notes, Host mode
//    ignores the transport, huge blocks and MIDI floods are safe;
//  - state: parameter values round-trip exactly, a document without the
//    parameter section restores defaults, malformed sections are rejected
//    without touching the live state, unknown IDs are ignored;
//  - realtime: processBlock allocates nothing (allocation probe), with
//    transport, sequencer, MIDI, automation and audition all active.
#include "Processor.h"
#include "DrumsEditor.h"
#include "DrumsParameters.h"

#ifdef VOX_TEST_INTERPOSE_MALLOC
#include "AllocationProbe.h"
#endif

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <chrono>
#include <fstream>
#include <future>
#include <thread>
#include <atomic>
#include <limits>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <sstream>
#include <string>

namespace {

using vox::drums::DrumsProcessor;
using vox::drums::DrumEngine;
using vox::drums::padCount;

int failures = 0;
std::string parametersCsvPath;

void check (bool condition, const juce::String& what)
{
    if (! condition)
    {
        std::printf ("FAIL [VOX Drums]: %s\n", what.toRawUTF8());
        ++failures;
    }
}

std::unique_ptr<DrumsProcessor> makeProcessor (double rate = 48000.0, int block = 256)
{
    auto p = std::make_unique<DrumsProcessor>();
    p->setRateAndBufferSizeDetails (rate, block);
    p->prepareToPlay (rate, block);
    return p;
}

class TestPlayHead final : public juce::AudioPlayHead {
public:
    juce::Optional<PositionInfo> getPosition() const override { return info; }

    void set (bool playing, double bpm, double ppq)
    {
        info = {};
        info.setIsPlaying (playing);
        info.setBpm (bpm);
        info.setPpqPosition (ppq);
        info.setPpqPositionOfLastBarStart (std::floor (ppq / 4.0) * 4.0);
        info.setTimeSignature (TimeSignature { 4, 4 });
    }

    PositionInfo info;
};

juce::RangedAudioParameter& parameter (DrumsProcessor& p, const juce::String& id)
{
    auto* result = dynamic_cast<juce::RangedAudioParameter*> (p.state().getParameter (id));
    if (result == nullptr)
    {
        std::printf ("FAIL [VOX Drums]: no parameter %s\n", id.toRawUTF8());
        std::exit (EXIT_FAILURE);
    }
    return *result;
}

void setPlain (DrumsProcessor& p, const juce::String& id, float plain)
{
    auto& param = parameter (p, id);
    param.setValueNotifyingHost (param.convertTo0to1 (plain));
}

float plainOf (DrumsProcessor& p, const juce::String& id)
{
    return p.state().getRawParameterValue (id)->load();
}

struct Render {
    double peak = 0.0, energy = 0.0;
    bool finite = true;
};

// Renders `blocks` blocks; `midiAt` adds events to each block.
template <typename MidiFn>
Render render (DrumsProcessor& p, int blocks, int block, TestPlayHead* head, double bpm, MidiFn&& midiAt)
{
    Render r;
    juce::AudioBuffer<float> buffer (2, block);
    double ppq = head != nullptr ? head->info.getPpqPosition().orFallback (0.0) : 0.0;
    for (int b = 0; b < blocks; ++b)
    {
        if (head != nullptr && head->info.getIsPlaying())
            head->set (true, bpm, ppq);
        juce::MidiBuffer midi;
        midiAt (b, midi);
        buffer.clear();
        p.processBlock (buffer, midi);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < block; ++i)
            {
                const double x = buffer.getSample (ch, i);
                r.finite = r.finite && std::isfinite (x);
                r.peak = std::max (r.peak, std::abs (x));
                r.energy += x * x;
            }
        ppq += block / 48000.0 * bpm / 60.0;
    }
    return r;
}

auto noMidi = [] (int, juce::MidiBuffer&) {};

//==============================================================================
void testParameterContract()
{
    DrumsProcessor p;
    const auto text = vox::drums::describeLayout (p.state());

    std::set<std::string> ids;
    for (const auto& entry : vox::drums::pluginParameters())
    {
        check (ids.insert (entry.id).second, "parameter ID is unique: " + juce::String (entry.id));
        check (juce::String (entry.id).startsWith ("drm."), "parameter ID uses the drm. namespace: " + juce::String (entry.id));
        check (p.state().getParameter (entry.id) != nullptr, "layout has " + juce::String (entry.id));
        if (entry.engine != nullptr)
        {
            auto& param = parameter (p, entry.id);
            const float def = param.convertFrom0to1 (param.getDefaultValue());
            check (std::abs (def - entry.engine->defaultValue) <= 1.0e-5f * std::max (1.0f, std::abs (def)),
                   "host default equals the engine default: " + juce::String (entry.id));
        }
    }
    check (static_cast<int> (ids.size()) == p.getParameters().size(), "every host parameter is in the table");

    // Every engine parameter is exposed, or deferred to the PR that adds its
    // feature (source/sample settings: DRM-02; sends/outputs: DRM-06).
    int deferred = 0;
    for (const auto& e : DrumEngine::parameters())
    {
        if (! vox::drums::pluginIdFor (e.legacyId).empty())
            continue;
        const juce::String name (e.legacyId);
        const bool known = name.endsWith (".source") || name.endsWith (".output") || name.endsWith (".send1")
                        || name.endsWith (".send2") || name.contains (".sample_");
        check (known, "engine parameter " + name + " is exposed or explicitly deferred");
        ++deferred;
    }
    check (deferred == 80, "80 deferred engine parameters (got " + juce::String (deferred) + ")");
    check (DrumEngine::parameterCount() == 187, "the engine has 187 parameters");
    check (ids.size() == 108, "108 plugin parameters (got " + juce::String (static_cast<int> (ids.size())) + ")");

    auto* pattern = dynamic_cast<juce::AudioParameterChoice*> (p.state().getParameter (vox::drums::patternParameterId));
    check (pattern != nullptr && pattern->choices.size() == 5, "five factory patterns");
    if (pattern != nullptr)
        check (pattern->choices[0] == "Full-on Basic" && pattern->choices[4] == "Kick & Offbeat", "factory pattern names");

    if (std::getenv ("VOX_GOLDEN_UPDATE") != nullptr)
    {
        std::ofstream (parametersCsvPath, std::ios::binary) << text;
        std::printf ("wrote %s\n", parametersCsvPath.c_str());
        return;
    }
    std::ifstream in (parametersCsvPath, std::ios::binary);
    std::stringstream committed;
    committed << in.rdbuf();
    auto expected = committed.str();
    expected.erase (std::remove (expected.begin(), expected.end(), '\r'), expected.end());
    check (in.good() || ! expected.empty(), "parameters.csv is readable");
    check (expected == text, "the parameter layout matches products/drums/parameters.csv "
                             "(intended changes: VOX_GOLDEN_UPDATE=1 in a reviewed commit)");
}

//==============================================================================
void testEditorBindings()
{
    auto p = makeProcessor();
    std::unique_ptr<juce::AudioProcessorEditor> editor (p->createEditorAndMakeActive());
    auto* frame = dynamic_cast<vox::suite::SuiteEditor*> (editor.get());
    auto* panel = frame != nullptr ? dynamic_cast<vox::drums::DrumsPanel*> (frame->content()) : nullptr;
    check (panel != nullptr, "the Drums editor shows the Drums panel");
    if (panel == nullptr)
        return;

    std::set<std::string> reachable;
    for (int pad = 0; pad < padCount; ++pad)
    {
        panel->selectPad (pad);
        check (panel->selectedPad() == pad, "pad selection");
        for (const auto& entry : vox::drums::pluginParameters())
        {
            const juce::String id (entry.id);
            if (auto* slider = panel->sliderFor (id))
            {
                reachable.insert (entry.id);
                auto& param = parameter (*p, id);
                const auto& range = param.getNormalisableRange();
                const float target = range.convertFrom0to1 (0.73f);
                slider->setValue (target, juce::sendNotificationSync);
                check (std::abs (plainOf (*p, id) - target) <= 1.0e-4f * std::max (1.0f, std::abs (target)),
                       "knob moves " + id);
                setPlain (*p, id, range.convertFrom0to1 (0.21f));
                check (std::abs (slider->getValue() - range.convertFrom0to1 (0.21f)) <= 1.0e-3 * std::max (1.0, std::abs (slider->getValue())),
                       "parameter moves knob " + id);
            }
            else if (auto* button = panel->buttonFor (id))
            {
                reachable.insert (entry.id);
                const bool before = plainOf (*p, id) >= 0.5f;
                button->setToggleState (! before, juce::sendNotificationSync);
                check ((plainOf (*p, id) >= 0.5f) != before, "toggle moves " + id);
                setPlain (*p, id, before ? 1.0f : 0.0f);
                check (button->getToggleState() == before, "parameter moves toggle " + id);
            }
            else if (auto* combo = panel->comboFor (id))
            {
                reachable.insert (entry.id);
                const int last = combo->getNumItems();
                check (last >= 2, "choice " + id + " lists its options");
                combo->setSelectedItemIndex (last - 1, juce::sendNotificationSync);
                check (static_cast<int> (std::lround (plainOf (*p, id))) == last - 1, "combo moves " + id);
                setPlain (*p, id, 0.0f);
                check (combo->getSelectedItemIndex() == 0, "parameter moves combo " + id);
            }
        }
    }
    for (const auto& entry : vox::drums::pluginParameters())
        check (reachable.count (entry.id) == 1, "editor control for " + juce::String (entry.id));

    // Pad buttons audition their pad and light up.
    p->takeHitMask();
    for (int pad = 0; pad < padCount; ++pad)
    {
        auto* button = panel->padButton (pad);
        check (button != nullptr && button->onClick != nullptr, "pad button " + juce::String (pad + 1));
        if (button != nullptr && button->onClick)
            button->onClick();
        check (panel->selectedPad() == pad, "clicking a pad selects it");
    }
    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;
    p->processBlock (buffer, midi);
    check (buffer.getMagnitude (0, 256) > 1.0e-4f, "auditioned pads sound");
    check (p->takeHitMask() == 0xffu, "every auditioned pad is reported as hit");

    juce::Image image (juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true);
    juce::Graphics g (image);
    editor->paintEntireComponent (g, false);
    // VOX_SCREENSHOT_OUT=<dir>: the editor at its default size, for review.
    if (const char* out = std::getenv ("VOX_SCREENSHOT_OUT"))
    {
        auto fresh = makeProcessor();   // default values, not the ones moved above
        std::unique_ptr<juce::AudioProcessorEditor> view (fresh->createEditorAndMakeActive());
        juce::Image shot (juce::Image::ARGB, view->getWidth() * 2, view->getHeight() * 2, true);
        juce::Graphics sg (shot);
        sg.addTransform (juce::AffineTransform::scale (2.0f));
        view->paintEntireComponent (sg, false);
        fresh->editorBeingDeleted (view.get());
        juce::File file = juce::File (juce::String (out)).getChildFile ("vox-drums-editor.png");
        file.deleteFile();
        juce::FileOutputStream stream (file);
        juce::PNGImageFormat().writeImageToStream (shot, stream);
        std::printf ("wrote %s\n", file.getFullPathName().toRawUTF8());
    }
    for (auto size : { std::pair { 1040, 700 }, std::pair { 1500, 920 } })
    {
        editor->setSize (size.first, size.second);
        for (auto* child : panel->getChildren())
            if (child->isVisible())
                check (panel->getLocalBounds().contains (child->getBounds()),
                       "control inside the editor at " + juce::String (size.first) + "x" + juce::String (size.second)
                           + ": " + child->getName() + " " + child->getBounds().toString());
        editor->paintEntireComponent (g, false);
    }
    p->editorBeingDeleted (editor.get());
}

//==============================================================================
void testSound()
{
    const auto& notes = DrumEngine::padNotes();
    for (int pad = 0; pad < padCount; ++pad)
    {
        auto p = makeProcessor();
        const auto r = render (*p, 40, 256, nullptr, 120.0, [&] (int b, juce::MidiBuffer& m) {
            if (b == 0)
                m.addEvent (juce::MidiMessage::noteOn (1, notes[static_cast<std::size_t> (pad)], static_cast<juce::uint8> (110)), 17);
        });
        check (r.finite && r.peak > 1.0e-3, "pad " + juce::String (pad + 1) + " sounds from note " + juce::String (notes[static_cast<std::size_t> (pad)]));
        check (p->takeHitMask() == (1u << pad), "pad " + juce::String (pad + 1) + " hit reported");
        // Its level parameter silences it.
        auto q = makeProcessor();
        setPlain (*q, juce::String (vox::drums::padPrefix (pad)) + ".level", 0.0f);
        const auto muted = render (*q, 40, 256, nullptr, 120.0, [&] (int b, juce::MidiBuffer& m) {
            if (b == 0)
                m.addEvent (juce::MidiMessage::noteOn (1, notes[static_cast<std::size_t> (pad)], static_cast<juce::uint8> (110)), 17);
        });
        check (muted.peak < 1.0e-6, "pad " + juce::String (pad + 1) + " level 0 is silent");
    }

    // Kit volume and a note outside the kit.
    {
        auto p = makeProcessor();
        const auto r = render (*p, 20, 256, nullptr, 120.0, [] (int b, juce::MidiBuffer& m) {
            if (b == 0)
                m.addEvent (juce::MidiMessage::noteOn (1, 60, static_cast<juce::uint8> (110)), 0);
        });
        check (r.peak <= 0.0, "a note outside the kit is silent");
    }

    // Internal sequencer: plays each factory pattern while the transport runs.
    for (int pattern = 0; pattern < 5; ++pattern)
    {
        auto p = makeProcessor();
        TestPlayHead head;
        head.set (true, 145.0, 0.0);
        p->setPlayHead (&head);
        setPlain (*p, "drm.seq.mode", 1.0f);   // Internal
        setPlain (*p, vox::drums::patternParameterId, static_cast<float> (pattern));
        const auto r = render (*p, 400, 256, &head, 145.0, noMidi);   // ~2 s, > 1 bar
        check (r.finite && r.peak > 1.0e-3, "pattern " + juce::String (pattern + 1) + " plays");
        check (p->takeHitMask() != 0, "pattern " + juce::String (pattern + 1) + " hits are reported");

        // Stopped transport: the sequencer is silent.
        auto q = makeProcessor();
        TestPlayHead stopped;
        stopped.set (false, 145.0, 0.0);
        q->setPlayHead (&stopped);
        setPlain (*q, "drm.seq.mode", 1.0f);
        setPlain (*q, vox::drums::patternParameterId, static_cast<float> (pattern));
        check (render (*q, 200, 256, &stopped, 145.0, noMidi).peak <= 0.0, "stopped transport is silent");
        q->setPlayHead (nullptr);
        p->setPlayHead (nullptr);
    }

    // Different patterns sound different.
    {
        double energy[2] {};
        for (int pattern : { 0, 3 })
        {
            auto p = makeProcessor();
            TestPlayHead head;
            head.set (true, 145.0, 0.0);
            p->setPlayHead (&head);
            setPlain (*p, "drm.seq.mode", 1.0f);
            setPlain (*p, vox::drums::patternParameterId, static_cast<float> (pattern));
            energy[pattern == 0 ? 0 : 1] = render (*p, 800, 256, &head, 145.0, noMidi).energy;
            p->setPlayHead (nullptr);
        }
        check (std::abs (energy[0] - energy[1]) > 1.0e-3 * std::max (energy[0], energy[1]), "patterns differ");
    }

    // Host mode (default) ignores the transport; Internal ignores host notes.
    {
        auto p = makeProcessor();
        TestPlayHead head;
        head.set (true, 145.0, 0.0);
        p->setPlayHead (&head);
        check (render (*p, 400, 256, &head, 145.0, noMidi).peak <= 0.0, "Host MIDI mode does not sequence");
        setPlain (*p, "drm.seq.mode", 1.0f);
        head.set (false, 145.0, 0.0);
        const auto r = render (*p, 100, 256, &head, 145.0, [] (int b, juce::MidiBuffer& m) {
            if (b == 0)
                m.addEvent (juce::MidiMessage::noteOn (1, 36, static_cast<juce::uint8> (110)), 0);
        });
        check (r.peak <= 0.0, "Internal mode ignores host notes");
        p->setPlayHead (nullptr);
    }

    // A block larger than the engine's 8192-frame limit, and a MIDI flood.
    {
        auto p = makeProcessor (48000.0, 512);
        juce::AudioBuffer<float> buffer (2, 20000);
        juce::MidiBuffer midi;
        for (int i = 0; i < 6000; ++i)
            midi.addEvent (juce::MidiMessage::noteOn (1, 36 + (i % 14), static_cast<juce::uint8> (100)), (i * 3) % 20000);
        p->processBlock (buffer, midi);
        bool finite = true;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 20000; ++i)
                finite = finite && std::isfinite (buffer.getSample (ch, i));
        check (finite && buffer.getMagnitude (0, 15000, 5000) > 1.0e-4f, "20000-frame block with 6000 events renders to its end");
        check (p->droppedMidiEvents() == 6000 - DrumsProcessor::midiCapacity, "events beyond the per-block capacity are counted as dropped");
    }

    // reset() (host stop / seek) silences ringing voices.
    {
        auto p = makeProcessor();
        render (*p, 2, 256, nullptr, 120.0, [] (int b, juce::MidiBuffer& m) {
            if (b == 0)
                m.addEvent (juce::MidiMessage::noteOn (1, 49, static_cast<juce::uint8> (127)), 0);
        });
        p->reset();
        check (render (*p, 4, 256, nullptr, 120.0, noMidi).peak <= 0.0, "reset() silences the cymbal tail");
    }
}

//==============================================================================
juce::MemoryBlock stateOf (DrumsProcessor& p)
{
    juce::MemoryBlock block;
    p.getStateInformation (block);
    return block;
}

void testState()
{
    std::mt19937 random (1234);
    std::uniform_real_distribution<float> unit (0.0f, 1.0f);

    auto source = makeProcessor();
    std::map<std::string, float> expected;
    for (const auto& entry : vox::drums::pluginParameters())
    {
        auto& param = parameter (*source, entry.id);
        param.setValueNotifyingHost (unit (random));
        expected[entry.id] = plainOf (*source, entry.id);
    }
    const auto saved = stateOf (*source);

    auto target = makeProcessor();
    target->setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    check (target->lastStateError().isEmpty(), "parameter state accepted");
    bool equal = true;
    for (const auto& [id, value] : expected)
        equal = equal && std::abs (plainOf (*target, id) - value) <= 1.0e-5f * std::max (1.0f, std::abs (value));
    check (equal, "every parameter value is restored");
    check (stateOf (*target) == saved, "restored parameters save back byte-identically");

    // A document without the parameter section (FOUND-01 Drums) restores
    // defaults.
    {
        auto document = vox::preset::decode (std::span (static_cast<const std::byte*> (saved.getData()), saved.getSize()),
                                             target->product().stateTag, 1).document;
        std::erase_if (document.sections, [] (const auto& s) { return s.tag == vox::drums::parameterSectionTag; });
        const auto bytes = vox::preset::encode (document);
        target->setStateInformation (bytes->data(), static_cast<int> (bytes->size()));
        check (target->lastStateError().isEmpty(), "state without parameters accepted");
        bool defaults = true;
        for (const auto& entry : vox::drums::pluginParameters())
        {
            auto& param = parameter (*target, entry.id);
            defaults = defaults && std::abs (param.getValue() - param.getDefaultValue()) < 1.0e-6f;
        }
        check (defaults, "missing parameter section restores defaults");
    }

    // Malformed or invalid sections are rejected; the live state is kept.
    target->setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    const auto live = stateOf (*target);
    const auto withSection = [&] (std::vector<std::byte> data) {
        auto document = vox::preset::decode (std::span (static_cast<const std::byte*> (saved.getData()), saved.getSize()),
                                             target->product().stateTag, 1).document;
        for (auto& s : document.sections)
            if (s.tag == vox::drums::parameterSectionTag)
                s.data = std::move (data);
        const auto bytes = vox::preset::encode (document);
        return juce::MemoryBlock (bytes->data(), bytes->size());
    };
    const auto entry = [] (const std::string& id, float value) {
        std::vector<std::byte> out;
        const auto put = [&] (std::uint32_t v) {
            for (int s = 0; s < 32; s += 8)
                out.push_back (static_cast<std::byte> ((v >> s) & 0xffu));
        };
        put (static_cast<std::uint32_t> (id.size()));
        for (char c : id)
            out.push_back (static_cast<std::byte> (c));
        std::uint32_t bits;
        std::memcpy (&bits, &value, 4);
        put (bits);
        return out;
    };
    const auto section = [&] (std::uint32_t count, std::vector<std::vector<std::byte>> entries) {
        std::vector<std::byte> out;
        for (int s = 0; s < 32; s += 8)
            out.push_back (static_cast<std::byte> ((count >> s) & 0xffu));
        for (auto& e : entries)
            out.insert (out.end(), e.begin(), e.end());
        return out;
    };
    const auto rejected = [&] (std::vector<std::byte> data, const juce::String& what) {
        const auto blob = withSection (std::move (data));
        target->setStateInformation (blob.getData(), static_cast<int> (blob.getSize()));
        check (target->lastStateError().isNotEmpty(), what + ": rejected");
        check (stateOf (*target) == live, what + ": live state unchanged");
    };
    rejected ({}, "empty parameter section");
    rejected (section (2, { entry ("drm.p01.level", 0.5f) }), "count larger than the entries");
    rejected (section (1, { entry ("drm.p01.level", std::numeric_limits<float>::quiet_NaN()) }), "NaN value");
    rejected (section (1, { entry ("drm.p01.level", std::numeric_limits<float>::infinity()) }), "infinite value");
    rejected (section (2, { entry ("drm.p01.level", 0.5f), entry ("drm.p01.level", 0.2f) }), "duplicate ID");
    {
        auto data = section (1, { entry ("drm.p01.level", 0.5f) });
        data.push_back (std::byte { 0 });
        rejected (data, "trailing bytes");
    }
    {
        auto data = section (1, { entry ("drm.p01.level", 0.5f) });
        data.resize (data.size() - 2);
        rejected (data, "truncated entry");
    }

    // Unknown IDs (a newer writer) are ignored; out-of-range values clamp.
    {
        const auto blob = withSection (section (3, { entry ("drm.p99.future", 1.0f), entry ("drm.p01.level", 0.25f),
                                                     entry ("drm.p02.decay", 50.0f) }));
        target->setStateInformation (blob.getData(), static_cast<int> (blob.getSize()));
        check (target->lastStateError().isEmpty(), "unknown parameter IDs are ignored");
        check (std::abs (plainOf (*target, "drm.p01.level") - 0.25f) < 1.0e-5f, "known value restored next to an unknown one");
        check (std::abs (plainOf (*target, "drm.p02.decay") - 1.0f) < 1.0e-5f, "out-of-range value clamps to the range");
    }
}

//==============================================================================
// Review 6733006001 P1: no host notification or listener callback runs under
// the state lock, and every document saved while a restore is in flight is
// the old or the new state, never a mix.
struct Doc {
    std::vector<std::byte> ui, params;
};

Doc sectionsOf (const juce::MemoryBlock& blob, vox::preset::Tag tag)
{
    Doc d;
    const auto decoded = vox::preset::decode (std::span (static_cast<const std::byte*> (blob.getData()), blob.getSize()), tag, 1);
    if (! decoded.ok())
        return d;
    if (const auto* ui = decoded.document.find (vox::suite::uiSectionTag))
        d.ui = ui->data;
    if (const auto* params = decoded.document.find (vox::drums::parameterSectionTag))
        d.params = params->data;
    return d;
}

// Two distinct full states (UI size and every parameter differ).
std::pair<juce::MemoryBlock, juce::MemoryBlock> twoStates()
{
    std::mt19937 random (99);
    std::uniform_real_distribution<float> unit (0.05f, 0.95f);
    juce::MemoryBlock states[2];
    for (int k = 0; k < 2; ++k)
    {
        auto p = makeProcessor();
        p->setEditorSize (k == 0 ? 1400 : 1100, k == 0 ? 900 : 720);
        for (const auto& entry : vox::drums::pluginParameters())
            parameter (*p, entry.id).setValueNotifyingHost (k == 0 ? unit (random) : 1.0f - unit (random));
        p->getStateInformation (states[k]);
    }
    return { states[0], states[1] };
}

class SavingListener final : public juce::AudioProcessorParameter::Listener {
public:
    SavingListener (DrumsProcessor& p, bool otherThread) : processor (p), crossThread (otherThread) {}

    void parameterValueChanged (int, float) override
    {
        juce::MemoryBlock blob;
        if (crossThread)
        {
            // The listener waits for another thread that saves: with the
            // state lock held across notifications this deadlocks.
            auto future = std::async (std::launch::async, [this] {
                juce::MemoryBlock b;
                processor.getStateInformation (b);
                return b;
            });
            if (future.wait_for (std::chrono::seconds (10)) != std::future_status::ready)
            {
                std::printf ("FAIL [VOX Drums]: a save from another thread during restore deadlocked\n");
                std::fflush (stdout);
                std::_Exit (EXIT_FAILURE);
            }
            blob = future.get();
        }
        else
        {
            processor.getStateInformation (blob);
        }
        saves.push_back (blob);
    }
    void parameterGestureChanged (int, bool) override {}

    DrumsProcessor& processor;
    bool crossThread;
    std::vector<juce::MemoryBlock> saves;
};

void testStateReentrancy()
{
    const auto [stateA, stateB] = twoStates();
    const auto tag = vox::preset::makeTag (VOX_TEST_STATE_TAG);
    const auto a = sectionsOf (stateA, tag), b = sectionsOf (stateB, tag);
    check (! a.params.empty() && a.params != b.params && a.ui != b.ui, "two distinct states");

    for (const bool crossThread : { false, true })
    {
        const juce::String mode = crossThread ? "listener waits on another thread's save" : "listener saves";
        auto p = makeProcessor();
        p->setStateInformation (stateA.getData(), static_cast<int> (stateA.getSize()));
        SavingListener listener (*p, crossThread);
        std::vector<juce::AudioProcessorParameter*> watched;
        for (const auto* id : { "drm.p01.level", "drm.p05.decay", "drm.seq.pattern" })
        {
            watched.push_back (p->state().getParameter (id));
            watched.back()->addListener (&listener);
        }
        p->setStateInformation (stateB.getData(), static_cast<int> (stateB.getSize()));
        for (auto* w : watched)
            w->removeListener (&listener);

        check (p->lastStateError().isEmpty(), mode + ": restore completes");
        check (listener.saves.size() == watched.size(), mode + ": the listener saved during restore ("
                                                           + juce::String (static_cast<int> (listener.saves.size())) + ")");
        for (const auto& blob : listener.saves)
        {
            const auto d = sectionsOf (blob, tag);
            check (d.ui == b.ui && d.params == b.params, mode + ": a save during restore is the restored state");
        }
        juce::MemoryBlock after;
        p->getStateInformation (after);
        const auto d = sectionsOf (after, tag);
        check (d.ui == b.ui && d.params == b.params, mode + ": after restore the saved state is the restored state");
    }

    // Concurrent restore (A/B) and save: every save is A or B as a whole.
    {
        auto p = makeProcessor();
        p->setStateInformation (stateA.getData(), static_cast<int> (stateA.getSize()));
        std::atomic<bool> done { false };
        std::thread restorer ([&] {
            for (int i = 0; i < 300; ++i)
            {
                const auto& blob = (i & 1) ? stateB : stateA;
                p->setStateInformation (blob.getData(), static_cast<int> (blob.getSize()));
            }
            done = true;
        });
        int saves = 0, torn = 0;
        while (! done)
        {
            juce::MemoryBlock blob;
            p->getStateInformation (blob);
            const auto d = sectionsOf (blob, tag);
            const bool isA = d.ui == a.ui && d.params == a.params;
            const bool isB = d.ui == b.ui && d.params == b.params;
            if (! (isA || isB))
                ++torn;
            ++saves;
        }
        restorer.join();
        check (torn == 0, "concurrent restore/save: " + juce::String (torn) + " of " + juce::String (saves)
                              + " saves mixed two states");
        check (saves > 0, "concurrent restore/save: saves happened");
    }
}

//==============================================================================
void testRealtime()
{
#ifdef VOX_TEST_INTERPOSE_MALLOC
    unsigned long allocations = 0;
    for (double rate : { 44100.0, 48000.0, 96000.0 })
        for (int block : { 32, 128, 512, 2048 })
        {
            auto p = makeProcessor (rate, block);
            TestPlayHead head;
            head.set (true, 145.0, 0.0);
            p->setPlayHead (&head);
            setPlain (*p, "drm.seq.mode", 2.0f);   // Both
            juce::AudioBuffer<float> buffer (2, block);
            juce::MidiBuffer midi;
            midi.ensureSize (8192);
            double ppq = 0.0;
            for (int b = 0; b < 200; ++b)
            {
                head.set (true, 145.0, ppq);
                midi.clear();
                midi.addEvent (juce::MidiMessage::noteOn (1, 36 + (b % 14), static_cast<juce::uint8> (100)), 0);
                midi.addEvent (juce::MidiMessage::allSoundOff (1), block / 2);
                if (b % 7 == 0)
                    p->audition (b % padCount, 90);
                if (b % 5 == 0)
                    setPlain (*p, "drm.p01.decay", 0.1f + 0.01f * static_cast<float> (b % 30));
                voxProbeBegin();
                p->processBlock (buffer, midi);
                allocations += voxProbeEnd();
                ppq += block / rate * 145.0 / 60.0;
            }
            p->setPlayHead (nullptr);
        }
    if (voxProbeAvailable())
        check (allocations == 0, "processBlock allocates nothing (" + juce::String (static_cast<juce::int64> (allocations)) + " calls)");
    else
        std::printf ("allocation probe unavailable on this platform\n");
#endif
}

} // namespace

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf ("usage: %s <products/drums/parameters.csv>\n", argv[0]);
        return EXIT_FAILURE;
    }
    parametersCsvPath = argv[1];
    juce::ScopedJuceInitialiser_GUI juce;
    testParameterContract();
    testEditorBindings();
    testSound();
    testState();
    testStateReentrancy();
    testRealtime();
    if (failures == 0)
        std::printf ("VOX Drums product tests passed\n");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
