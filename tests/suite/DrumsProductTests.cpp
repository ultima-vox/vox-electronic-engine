// VOX Drums product tests (DRM-01..03b), against the plugin's shared code.
//
//  - parameter contract: the APVTS layout matches products/drums/parameters.csv
//    (VOX_GOLDEN_UPDATE=1 rewrites it), 119 unique IDs, host defaults are
//    the default kit's values;
//  - editor: every plugin parameter is reachable from a bound control, and
//    controls and parameters move together both ways; every structural
//    control moves the kit store and follows it;
//  - sound: every pad sounds from its MIDI note and from the editor
//    audition, the internal sequencer plays each factory pattern only while
//    the host transport runs, Internal mode ignores host notes, Host mode
//    ignores the transport, huge blocks and MIDI floods are safe;
//  - state: parameter values and pad structure (DPAD) round-trip exactly, a
//    document without the parameter section restores defaults, a DRM-01/02
//    document migrates its former structural parameters onto pads 1-8,
//    malformed sections are rejected without touching the live state,
//    unknown IDs are ignored;
//  - realtime: processBlock allocates nothing (allocation probe), with
//    transport, sequencer, MIDI, automation and audition all active.
#include "Processor.h"
#include "DrumsEditor.h"
#include "DrumsParameters.h"
#include "KitStore.h"
#include "PatternExport.h"
#include "PatternStore.h"
#include "PatternEditor.h"

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
#include <array>
#include <memory>
#include <random>
#include <set>
#include <sstream>
#include <tuple>
#include <vector>
#include <string>

namespace {

using vox::drums::DrumsProcessor;
namespace kit = vox::drums::kit;
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
    const auto& kitDefaults = vox::drums::defaultKit();
    int perPad[padCount] {};
    for (const auto& entry : vox::drums::pluginParameters())
    {
        check (ids.insert (entry.id).second, "parameter ID is unique: " + juce::String (entry.id));
        check (juce::String (entry.id).startsWith ("drm."), "parameter ID uses the drm. namespace: " + juce::String (entry.id));
        check (p.state().getParameter (entry.id) != nullptr, "layout has " + juce::String (entry.id));
        if (entry.pad >= 0 && entry.field != vox::drums::Field::seqLane)
            ++perPad[entry.pad];
        auto& param = parameter (p, entry.id);
        const float def = param.convertFrom0to1 (param.getDefaultValue());
        float expected = def;
        const auto* pad = entry.pad >= 0 ? &kitDefaults.pads[static_cast<std::size_t> (entry.pad)] : nullptr;
        using vox::drums::Field;
        switch (entry.field)
        {
            case Field::level: expected = pad->level; break;
            case Field::tune: expected = pad->tune; break;
            case Field::decay: expected = pad->decay; break;
            case Field::pan: expected = pad->pan; break;
            case Field::cutoff: expected = pad->cutoff; break;
            case Field::mute: expected = 0.0f; break;
            case Field::kitVolume: expected = kitDefaults.volume; break;
            case Field::kitVelocity: expected = kitDefaults.velocitySensitivity; break;
            case Field::kitChoke: expected = kitDefaults.chokeEnabled ? 1.0f : 0.0f; break;
            case Field::seqMode: expected = 0.0f; break;
            case Field::seqPattern: expected = 0.0f; break;
            case Field::seqSwing: expected = kitDefaults.swing; break;
            case Field::seqAccent: expected = kitDefaults.accent; break;
            case Field::seqLane: expected = 1.0f; break;
        }
        check (std::abs (def - expected) <= 1.0e-4f * std::max (1.0f, std::abs (def)),
               "host default equals the default kit: " + juce::String (entry.id));
    }
    check (static_cast<int> (ids.size()) == p.getParameters().size(), "every host parameter is in the table");
    check (ids.size() == 119, "119 plugin parameters (got " + juce::String (static_cast<int> (ids.size())) + ")");
    for (int pad = 0; pad < padCount; ++pad)
        check (perPad[pad] == 6, "pad " + juce::String (pad + 1) + " has level, tune, decay, pan, cutoff and mute");
    // DRM-01/02 host IDs of pads 1-8, kit and sequencer keep their identity.
    for (const auto* id : { "drm.p01.level", "drm.p08.cutoff", "drm.kit.volume", "drm.kit.velocity", "drm.kit.hat_choke",
                            "drm.seq.mode", "drm.seq.pattern", "drm.seq.swing", "drm.seq.accent", "drm.seq.lane.p08" })
        check (ids.count (id) == 1, juce::String ("DRM-01 ID kept: ") + id);
    check (parameter (p, "drm.p01.level").getName (64) == "Pad 1 Level" && parameter (p, "drm.p16.mute").getName (64) == "Pad 16 Mute",
           "host names are pad-numbered");

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

// Writes one structural setting by its editor name (the test's own
// mapping, independent of the editor's).
struct StructuralContextSetter {
    static void set (kit::PadSettings& ps, int player, const juce::String& name, float v)
    {
        auto& pl = ps.players[static_cast<std::size_t> (player)];
        const int i = juce::roundToInt (v);
        if (name == "model") ps.model = static_cast<kit::Model> (i);
        else if (name == "note") ps.note = static_cast<std::uint8_t> (i);
        else if (name == "choke") ps.chokeGroup = static_cast<std::uint8_t> (i);
        else if (name == "poly") ps.polyphony = static_cast<std::uint8_t> (i + 1);
        else if (name == "filter") ps.filter = static_cast<kit::Filter> (i);
        else if (name == "source") ps.source = static_cast<kit::Source> (i);
        else if (name == "sample_mode") ps.sampleMode = static_cast<kit::SampleMode> (i);
        else if (name == "fine") ps.fine = v;
        else if (name == "character") ps.character = v;
        else if (name == "resonance") ps.resonance = v;
        else if (name == "attack") ps.attack = v;
        else if (name == "sustain") ps.sustain = v;
        else if (name == "pitch_amount") ps.pitchAmount = v;
        else if (name == "pitch_decay") ps.pitchDecay = v;
        else if (name == "drive") ps.drive = v;
        else if (name == "sub") ps.sub = v;
        else if (name == "tail") ps.tail = v;
        else if (name == "split") ps.velocitySplit = static_cast<std::uint8_t> (i);
        else if (name == "start") pl.start = v;
        else if (name == "end") pl.end = v;
        else if (name == "sample_level") pl.level = v;
        else if (name == "loop") pl.loop = v >= 0.5f;
        else if (name == "reverse") pl.reverse = v >= 0.5f;
        else if (name == "normalize") pl.normalize = v >= 0.5f;
    }
};

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

    // Structural controls: each moves the selected pad's (player's) setting
    // in the kit store, and follows a change made elsewhere (restore,
    // another view) on the next editor tick.
    {
        const auto read = [&] (const juce::String& name, int pad, int player) -> float {
            const auto ps = p->kitStore().pad (pad);
            const auto& pl = ps.players[static_cast<std::size_t> (player)];
            if (name == "model") return static_cast<float> (ps.model);
            if (name == "note") return ps.note;
            if (name == "choke") return ps.chokeGroup;
            if (name == "poly") return static_cast<float> (ps.polyphony - 1);
            if (name == "filter") return static_cast<float> (ps.filter);
            if (name == "source") return static_cast<float> (ps.source);
            if (name == "sample_mode") return static_cast<float> (ps.sampleMode);
            if (name == "fine") return ps.fine;
            if (name == "character") return ps.character;
            if (name == "resonance") return ps.resonance;
            if (name == "attack") return ps.attack;
            if (name == "sustain") return ps.sustain;
            if (name == "pitch_amount") return ps.pitchAmount;
            if (name == "pitch_decay") return ps.pitchDecay;
            if (name == "drive") return ps.drive;
            if (name == "sub") return ps.sub;
            if (name == "tail") return ps.tail;
            if (name == "split") return ps.velocitySplit;
            if (name == "start") return pl.start;
            if (name == "end") return pl.end;
            if (name == "sample_level") return pl.level;
            if (name == "loop") return pl.loop ? 1.0f : 0.0f;
            if (name == "reverse") return pl.reverse ? 1.0f : 0.0f;
            if (name == "normalize") return pl.normalize ? 1.0f : 0.0f;
            return -12345.0f;
        };
        const char* sliders[] = { "fine", "character", "resonance", "attack", "sustain", "pitch_amount", "pitch_decay",
                                  "drive", "sub", "tail", "split", "start", "end", "sample_level" };
        const char* combos[] = { "model", "note", "choke", "poly", "filter", "source", "sample_mode" };
        const char* toggles[] = { "loop", "reverse", "normalize" };
        for (const int pad : { 0, 6, 13 })
            for (const int player : { 0, 1 })
            {
                panel->selectPad (pad);
                panel->selectPlayer (player);
                const juce::String where = " (pad " + juce::String (pad + 1) + (player == 0 ? " A)" : " B)");
                const auto other = 1 - player;
                for (const auto* name : sliders)
                {
                    auto* slider = panel->structuralSlider (name);
                    check (slider != nullptr, juce::String ("structural knob ") + name);
                    if (slider == nullptr)
                        continue;
                    const double target = slider->proportionOfLengthToValue (0.37);
                    const float otherBefore = read (name, pad, other);
                    slider->setValue (target, juce::sendNotificationSync);
                    const double shown = slider->getValue();
                    check (std::abs (read (name, pad, player) - shown) <= 1.0e-5 * std::max (1.0, std::abs (shown)),
                           juce::String ("knob moves the store: ") + name + where);
                    const bool perPlayer = juce::String (name) == "start" || juce::String (name) == "end" || juce::String (name) == "sample_level";
                    if (perPlayer)
                        check (juce::exactlyEqual (read (name, pad, other), otherBefore), juce::String ("the other player is untouched: ") + name + where);
                    // The store moves the knob.
                    auto ps = p->kitStore().pad (pad);
                    const double back = slider->proportionOfLengthToValue (0.81);
                    StructuralContextSetter::set (ps, player, name, static_cast<float> (slider->snapValue (back, juce::Slider::notDragging)));
                    p->kitStore().setPad (pad, ps);
                    panel->pollForTesting();
                    check (std::abs (slider->getValue() - read (name, pad, player)) <= 1.0e-5 * std::max (1.0, std::abs (slider->getValue())),
                           juce::String ("the store moves the knob: ") + name + where);
                }
                for (const auto* name : combos)
                {
                    auto* combo = panel->structuralCombo (name);
                    check (combo != nullptr, juce::String ("structural combo ") + name);
                    if (combo == nullptr)
                        continue;
                    const int last = combo->getNumItems() - 1;
                    combo->setSelectedItemIndex (last, juce::sendNotificationSync);
                    check (juce::roundToInt (read (name, pad, player)) == last, juce::String ("combo moves the store: ") + name + where);
                    auto ps = p->kitStore().pad (pad);
                    StructuralContextSetter::set (ps, player, name, 1.0f);
                    p->kitStore().setPad (pad, ps);
                    panel->pollForTesting();
                    check (combo->getSelectedItemIndex() == 1, juce::String ("the store moves the combo: ") + name + where);
                }
                for (const auto* name : toggles)
                {
                    auto* toggle = panel->structuralToggle (name);
                    check (toggle != nullptr, juce::String ("structural toggle ") + name);
                    if (toggle == nullptr)
                        continue;
                    const float otherBefore = read (name, pad, other);
                    toggle->setToggleState (! toggle->getToggleState(), juce::sendNotificationSync);
                    check ((read (name, pad, player) >= 0.5f) == toggle->getToggleState(), juce::String ("toggle moves the store: ") + name + where);
                    check (juce::exactlyEqual (read (name, pad, other), otherBefore), juce::String ("the other player is untouched: ") + name + where);
                    auto ps = p->kitStore().pad (pad);
                    StructuralContextSetter::set (ps, player, name, toggle->getToggleState() ? 0.0f : 1.0f);
                    p->kitStore().setPad (pad, ps);
                    panel->pollForTesting();
                    check ((read (name, pad, player) >= 0.5f) == toggle->getToggleState(), juce::String ("the store moves the toggle: ") + name + where);
                }
            }
        // The kick body row shows only for the Kick model (the knob holding
        // the slider is what hides).
        const auto shown = [panel] (juce::Component* c) {
            for (; c != nullptr && c != panel; c = c->getParentComponent())
                if (! c->isVisible())
                    return false;
            return c == panel;
        };
        p->kitStore().setAll (vox::drums::defaultKit());
        panel->selectPad (0);
        panel->pollForTesting();
        check (panel->bodyVisible() && shown (panel->structuralSlider ("drive")), "kick body shows for a Kick pad");
        panel->structuralCombo ("model")->setSelectedItemIndex (static_cast<int> (kit::Model::tom), juce::sendNotificationSync);
        check (! panel->bodyVisible(), "a Tom pad has no kick body");
        check (! shown (panel->structuralSlider ("drive")), "kick body hides for a Tom pad");
        p->kitStore().setAll (vox::drums::defaultKit());
        panel->pollForTesting();
        check (shown (panel->structuralSlider ("drive")), "kick body returns when a restore makes the pad a Kick again");
        // Host parameters bind to the selected pad.
        panel->selectPad (11);
        check (panel->sliderFor ("drm.p12.level") != nullptr && panel->sliderFor ("drm.p01.level") == nullptr
                   && panel->buttonFor ("drm.p12.mute") != nullptr,
               "pad knobs follow the selected pad");
    }

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
    check (p->takeHitMask() == 0xffffu, "every auditioned pad is reported as hit");

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

juce::MemoryBlock stateOf (DrumsProcessor& p);

//==============================================================================
void testSound()
{
    std::array<std::uint8_t, padCount> notes {};
    for (int pad = 0; pad < padCount; ++pad)
        notes[static_cast<std::size_t> (pad)] = vox::drums::defaultKit().pads[static_cast<std::size_t> (pad)].note;
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
        // Its mute parameter silences it.
        auto m = makeProcessor();
        setPlain (*m, juce::String (vox::drums::padPrefix (pad)) + ".mute", 1.0f);
        const auto mutedByMute = render (*m, 40, 256, nullptr, 120.0, [&] (int b, juce::MidiBuffer& mb) {
            if (b == 0)
                mb.addEvent (juce::MidiMessage::noteOn (1, notes[static_cast<std::size_t> (pad)], static_cast<juce::uint8> (110)), 17);
        });
        check (mutedByMute.peak <= 0.0 && m->takeHitMask() == 0, "pad " + juce::String (pad + 1) + " mute is silent");
    }

    // Every model sounds on every kind of pad slot, and a reassigned note
    // follows the kit store.
    for (int model = 0; model < kit::modelCount; ++model)
        for (const int pad : { 2, 12 })
        {
            auto p = makeProcessor();
            auto ps = p->kitStore().pad (pad);
            ps.model = static_cast<kit::Model> (model);
            ps.note = 72;
            p->kitStore().setPad (pad, ps);
            const auto r = render (*p, 60, 256, nullptr, 120.0, [] (int b, juce::MidiBuffer& mb) {
                if (b == 0)
                    mb.addEvent (juce::MidiMessage::noteOn (1, 72, static_cast<juce::uint8> (120)), 0);
            });
            check (r.finite && r.peak > 1.0e-3 && p->takeHitMask() == (1u << pad),
                   "model " + vox::drums::modelName (static_cast<kit::Model> (model)) + " sounds on pad " + juce::String (pad + 1)
                       + " from its reassigned note");
        }

    // Owner decision (#120): pads 9-16 are full instruments with the
    // factory defaults Perc on notes 50-57: each sounds from the editor
    // audition and from its host note, alone, and keeps its settings over a
    // save and restore.
    for (int pad = 8; pad < padCount; ++pad)
    {
        const auto& d = vox::drums::defaultKit().pads[static_cast<std::size_t> (pad)];
        const juce::String name = "pad " + juce::String (pad + 1);
        check (d.model == kit::Model::perc && d.note == 50 + (pad - 8) && vox::drums::padName (pad) == "Perc " + juce::String (pad - 7),
               name + ": factory default Perc on note " + juce::String (50 + pad - 8));
        auto p = makeProcessor();
        p->audition (pad, 110);
        const auto heard = render (*p, 30, 256, nullptr, 120.0, noMidi);
        check (heard.finite && heard.peak > 1.0e-3 && p->takeHitMask() == (1u << pad), name + ": audition sounds and lights only it");
        auto q = makeProcessor();
        const auto fromNote = render (*q, 30, 256, nullptr, 120.0, [&] (int b, juce::MidiBuffer& m) {
            if (b == 0)
                m.addEvent (juce::MidiMessage::noteOn (1, d.note, static_cast<juce::uint8> (110)), 0);
        });
        check (fromNote.peak > 1.0e-3 && q->takeHitMask() == (1u << pad), name + ": its host note plays it alone");
    }
    {
        // Distinct settings on pads 9-16 survive save and restore and play.
        auto source = makeProcessor();
        for (int pad = 8; pad < padCount; ++pad)
        {
            auto ps = source->kitStore().pad (pad);
            ps.model = static_cast<kit::Model> (pad % kit::modelCount);
            ps.note = static_cast<std::uint8_t> (70 + pad);
            ps.character = 0.1f * static_cast<float> (pad - 7);
            ps.polyphony = static_cast<std::uint8_t> (1 + pad % 8);
            source->kitStore().setPad (pad, ps);
            setPlain (*source, juce::String (vox::drums::padPrefix (pad)) + ".tune", static_cast<float> (pad - 12));
            setPlain (*source, juce::String (vox::drums::padPrefix (pad)) + ".level", 0.5f + 0.03f * static_cast<float> (pad - 8));
        }
        const auto blob = stateOf (*source);
        auto target = makeProcessor();
        target->setStateInformation (blob.getData(), static_cast<int> (blob.getSize()));
        bool same = target->lastStateError().isEmpty();
        for (int pad = 8; pad < padCount; ++pad)
        {
            const auto a = source->kitStore().pad (pad), b = target->kitStore().pad (pad);
            same = same && a.model == b.model && a.note == b.note && juce::exactlyEqual (a.character, b.character) && a.polyphony == b.polyphony;
            for (const auto* field : { ".tune", ".level" })
            {
                const auto id = juce::String (vox::drums::padPrefix (pad)) + field;
                same = same && std::abs (plainOf (*source, id) - plainOf (*target, id)) < 1.0e-5f;
            }
        }
        check (same, "pads 9-16: structure and host values survive save and restore");
        bool play = true;
        for (int pad = 8; pad < padCount; ++pad)
        {
            target->reset();
            target->takeHitMask();
            const auto r = render (*target, 20, 256, nullptr, 120.0, [&] (int b, juce::MidiBuffer& m) {
                if (b == 0)
                    m.addEvent (juce::MidiMessage::noteOn (1, 70 + pad, static_cast<juce::uint8> (110)), 0);
            });
            play = play && r.peak > 1.0e-3 && target->takeHitMask() == (1u << pad);
        }
        check (play, "pads 9-16: each restored pad plays from its restored note");
    }

    // Review 6743099574: an editor audition plays the pad it names, also
    // when two pads share a note and in Internal mode; external MIDI on the
    // shared note follows the lowest-pad rule.
    {
        // Pads 10 and 11 on note 60, different models; `silent` sets one
        // pad's level to 0 (its contribution removed) for the comparison.
        const auto make = [] (int silent, float mode) {
            auto p = makeProcessor();
            auto a = p->kitStore().pad (9), b = p->kitStore().pad (10);
            a.model = kit::Model::kick;
            b.model = kit::Model::cymbal;
            a.note = b.note = 60;
            p->kitStore().setPad (9, a);
            p->kitStore().setPad (10, b);
            setPlain (*p, "drm.seq.mode", mode);
            if (silent >= 0)
                setPlain (*p, juce::String (vox::drums::padPrefix (silent)) + ".level", 0.0f);
            return p;
        };
        const auto take = [] (DrumsProcessor& p, int auditionPad, bool externalNote) {
            std::vector<float> out;
            juce::AudioBuffer<float> buffer (2, 256);
            for (int blk = 0; blk < 30; ++blk)
            {
                juce::MidiBuffer midi;
                if (blk == 0 && auditionPad >= 0)
                    p.audition (auditionPad, 110);
                if (blk == 0 && externalNote)
                    midi.addEvent (juce::MidiMessage::noteOn (1, 60, static_cast<juce::uint8> (110)), 0);
                buffer.clear();
                p.processBlock (buffer, midi);
                for (int ch = 0; ch < 2; ++ch)
                    out.insert (out.end(), buffer.getReadPointer (ch), buffer.getReadPointer (ch) + 256);
            }
            return out;
        };
        const auto peakOf = [] (const std::vector<float>& v) {
            float m = 0.0f;
            for (float x : v)
                m = std::max (m, std::abs (x));
            return m;
        };
        for (const float mode : { 0.0f, 1.0f })   // Host MIDI, Internal (transport stopped)
        {
            const juce::String m = juce::exactlyEqual (mode, 0.0f) ? " (Host MIDI)" : " (Internal)";
            for (const int pad : { 9, 10 })
            {
                const int other = pad == 9 ? 10 : 9;
                auto p = make (-1, mode);
                const auto heard = take (*p, pad, false);
                check (p->takeHitMask() == (1u << pad), "audition of pad " + juce::String (pad + 1) + " lights only that pad" + m);
                auto only = make (other, mode);   // the other pad silenced
                const auto expected = take (*only, pad, false);
                check (peakOf (heard) > 1.0e-3 && heard == expected,
                       "audition of pad " + juce::String (pad + 1) + " on a shared note plays only that pad" + m);
            }
        }
        // External note 60: the lowest pad (10) plays, pad 11 does not.
        {
            auto p = make (-1, 0.0f);
            const auto heard = take (*p, -1, true);
            check (p->takeHitMask() == (1u << 9), "external shared note lights the lowest pad");
            auto only = make (10, 0.0f);
            check (peakOf (heard) > 1.0e-3 && heard == take (*only, -1, true), "external shared note plays the lowest pad only");
            auto lowSilent = make (9, 0.0f);
            check (peakOf (take (*lowSilent, -1, true)) <= 0.0f, "external shared note does not reach the higher pad");
        }
        // Internal mode still ignores external notes, auditions or not.
        {
            auto p = make (-1, 1.0f);
            check (peakOf (take (*p, -1, true)) <= 0.0f, "Internal mode ignores an external note on the shared note");
        }
    }

    // Pads sharing a choke group cut each other (Closed/Open Hat style) only
    // while Choke Groups is on.
    for (const bool chokeOn : { true, false })
    {
        auto p = makeProcessor();
        setPlain (*p, "drm.kit.hat_choke", chokeOn ? 1.0f : 0.0f);
        auto a = p->kitStore().pad (9), b = p->kitStore().pad (10);
        a.model = kit::Model::cymbal;
        a.chokeGroup = b.chokeGroup = 5;
        p->kitStore().setPad (9, a);
        p->kitStore().setPad (10, b);
        setPlain (*p, juce::String (vox::drums::padPrefix (9)) + ".decay", 6.0f);
        setPlain (*p, juce::String (vox::drums::padPrefix (10)) + ".level", 0.0f);   // the choker itself is silent
        const auto r = render (*p, 80, 256, nullptr, 120.0, [&] (int blk, juce::MidiBuffer& mb) {
            if (blk == 0)
                mb.addEvent (juce::MidiMessage::noteOn (1, notes[9], static_cast<juce::uint8> (120)), 0);
            if (blk == 20)
                mb.addEvent (juce::MidiMessage::noteOn (1, notes[10], static_cast<juce::uint8> (120)), 0);
        });
        const auto tail = render (*p, 10, 256, nullptr, 120.0, noMidi);
        if (chokeOn)
            check (r.peak > 1.0e-3 && tail.peak < 1.0e-5, "a choke group member cuts the other's ring");
        else
            check (tail.peak > 1.0e-4, "with Choke Groups off the ring continues");
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

    // Pad structure (DPAD): a randomised kit round-trips exactly and saves
    // back byte-identically; the engine plays the restored structure.
    {
        auto kitSource = makeProcessor();
        auto k = kitSource->kitStore().snapshot();
        std::mt19937 r2 (77);
        std::uniform_real_distribution<float> u (0.0f, 1.0f);
        for (int pad = 0; pad < padCount; ++pad)
        {
            auto& ps = k.pads[static_cast<std::size_t> (pad)];
            ps.model = static_cast<kit::Model> (r2() % kit::modelCount);
            ps.note = static_cast<std::uint8_t> (30 + pad * 3);
            ps.chokeGroup = static_cast<std::uint8_t> (r2() % 9);
            ps.polyphony = static_cast<std::uint8_t> (1 + r2() % kit::maxVoicesPerPad);
            ps.filter = static_cast<kit::Filter> (r2() % 6);
            ps.source = static_cast<kit::Source> (r2() % 3);
            ps.sampleMode = static_cast<kit::SampleMode> (r2() % 3);
            ps.velocitySplit = static_cast<std::uint8_t> (1 + r2() % 127);
            ps.fine = -100.0f + 200.0f * u (r2);
            ps.character = u (r2);
            ps.resonance = u (r2);
            ps.attack = -1.0f + 2.0f * u (r2);
            ps.sustain = -1.0f + 2.0f * u (r2);
            ps.pitchAmount = 48.0f * u (r2);
            ps.pitchDecay = 0.005f + 0.195f * u (r2);
            ps.drive = 1.0f + 7.0f * u (r2);
            ps.sub = u (r2);
            ps.tail = 0.03f + 1.47f * u (r2);
            for (auto& pl : ps.players)
            {
                pl.start = 0.5f * u (r2);
                pl.end = 0.5f + 0.5f * u (r2);
                pl.level = 2.0f * u (r2);
                pl.loop = r2() % 2 != 0;
                pl.reverse = r2() % 2 != 0;
                pl.normalize = r2() % 2 != 0;
            }
        }
        kitSource->kitStore().setAll (k);
        const auto kitSaved = stateOf (*kitSource);
        auto kitTarget = makeProcessor();
        kitTarget->setStateInformation (kitSaved.getData(), static_cast<int> (kitSaved.getSize()));
        check (kitTarget->lastStateError().isEmpty(), "pad structure accepted");
        const auto a = vox::drums::KitStore::encode (k), b = vox::drums::KitStore::encode (kitTarget->kitStore().snapshot());
        check (a.data == b.data, "every pad structure field is restored");
        check (stateOf (*kitTarget) == kitSaved, "restored pad structure saves back byte-identically");
        // The restored note map plays: pad 16's note sounds pad 16.
        const auto r = render (*kitTarget, 40, 256, nullptr, 120.0, [&] (int blk, juce::MidiBuffer& m) {
            if (blk == 0)
                m.addEvent (juce::MidiMessage::noteOn (1, k.pads[15].note, static_cast<juce::uint8> (120)), 0);
        });
        check ((kitTarget->takeHitMask() & (1u << 15)) != 0 && r.finite, "a restored note assignment plays its pad");
    }

    // A DRM-01/02 document (no DPAD, structural values as parameters)
    // migrates them onto pads 1-8 and player A; host values stay.
    {
        auto document = vox::preset::decode (std::span (static_cast<const std::byte*> (saved.getData()), saved.getSize()),
                                             target->product().stateTag, 1).document;
        std::erase_if (document.sections, [] (const auto& sec) { return sec.tag == vox::drums::padSectionTag; });
        std::vector<std::pair<std::string, float>> old = {
            { "drm.p01.level", 0.4f }, { "drm.p01.character", 0.9f }, { "drm.p01.fine", -33.0f }, { "drm.p01.drive", 5.5f },
            { "drm.p01.pitch_amount", 12.0f }, { "drm.p01.tail", 1.2f }, { "drm.p03.filter", 3.0f },
            { "drm.p03.resonance", 0.7f }, { "drm.p04.transient_attack", -0.5f }, { "drm.p05.transient_sustain", 0.25f },
            { "drm.p06.source", 2.0f }, { "drm.p06.sample_start", 0.125f }, { "drm.p06.sample_end", 0.75f },
            { "drm.p06.sample_level", 1.5f }, { "drm.p07.sample_loop", 1.0f }, { "drm.p08.sample_reverse", 1.0f },
            { "drm.p08.sample_normalize", 1.0f }, { "drm.seq.lane.p08", 0.0f },
        };
        std::vector<std::byte> data;
        const auto put = [&] (std::uint32_t v) {
            for (int sh = 0; sh < 32; sh += 8)
                data.push_back (static_cast<std::byte> ((v >> sh) & 0xffu));
        };
        put (static_cast<std::uint32_t> (old.size()));
        for (const auto& [id, value] : old)
        {
            put (static_cast<std::uint32_t> (id.size()));
            for (char c : id)
                data.push_back (static_cast<std::byte> (c));
            std::uint32_t bits;
            std::memcpy (&bits, &value, 4);
            put (bits);
        }
        for (auto& sec : document.sections)
            if (sec.tag == vox::drums::parameterSectionTag)
                sec.data = data;
        const auto bytes = vox::preset::encode (document);
        auto migrated = makeProcessor();
        migrated->setStateInformation (bytes->data(), static_cast<int> (bytes->size()));
        check (migrated->lastStateError().isEmpty(), "a DRM-01/02 document is accepted");
        const auto k = migrated->kitStore().snapshot();
        const auto& d = vox::drums::defaultKit();
        check (juce::exactlyEqual (k.pads[0].character, 0.9f) && juce::exactlyEqual (k.pads[0].fine, -33.0f)
                   && juce::exactlyEqual (k.pads[0].drive, 5.5f) && juce::exactlyEqual (k.pads[0].pitchAmount, 12.0f)
                   && juce::exactlyEqual (k.pads[0].tail, 1.2f),
               "kick character, fine and body migrate");
        check (k.pads[2].filter == kit::Filter::hp12 && juce::exactlyEqual (k.pads[2].resonance, 0.7f)
                   && juce::exactlyEqual (k.pads[3].attack, -0.5f) && juce::exactlyEqual (k.pads[4].sustain, 0.25f),
               "filter, resonance and transient migrate");
        check (k.pads[5].source == kit::Source::layer && juce::exactlyEqual (k.pads[5].players[0].start, 0.125f)
                   && juce::exactlyEqual (k.pads[5].players[0].end, 0.75f) && juce::exactlyEqual (k.pads[5].players[0].level, 1.5f)
                   && k.pads[6].players[0].loop && k.pads[7].players[0].reverse && k.pads[7].players[0].normalize,
               "sample source and player A settings migrate");
        check (juce::exactlyEqual (k.pads[5].players[1].start, d.pads[5].players[1].start) && k.pads[12].model == d.pads[12].model
                   && k.pads[12].note == d.pads[12].note,
               "player B and pads 9-16 keep their defaults");
        check (std::abs (plainOf (*migrated, "drm.p01.level") - 0.4f) < 1.0e-6f && plainOf (*migrated, "drm.seq.lane.p08") < 0.5f,
               "host values of a DRM-01/02 document are restored");
        // Saved again it carries DPAD and loads identically.
        const auto again = stateOf (*migrated);
        auto reloaded = makeProcessor();
        reloaded->setStateInformation (again.getData(), static_cast<int> (again.getSize()));
        check (vox::drums::KitStore::encode (reloaded->kitStore().snapshot()).data == vox::drums::KitStore::encode (k).data,
               "a migrated project saves and reloads its structure");
    }

    // Malformed or invalid DPAD sections are rejected; nothing changes.
    {
        auto holder = makeProcessor();
        holder->setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
        const auto before = stateOf (*holder);
        const auto withPads = [&] (std::vector<std::byte> data) {
            auto document = vox::preset::decode (std::span (static_cast<const std::byte*> (saved.getData()), saved.getSize()),
                                                 holder->product().stateTag, 1).document;
            for (auto& sec : document.sections)
                if (sec.tag == vox::drums::padSectionTag)
                    sec.data = std::move (data);
            const auto bytes = vox::preset::encode (document);
            return juce::MemoryBlock (bytes->data(), bytes->size());
        };
        const auto good = vox::drums::KitStore::encode (vox::drums::defaultKit()).data;
        const auto bad = [&] (std::vector<std::byte> data, const juce::String& what) {
            const auto blob = withPads (std::move (data));
            holder->setStateInformation (blob.getData(), static_cast<int> (blob.getSize()));
            check (holder->lastStateError().isNotEmpty(), "DPAD " + what + ": rejected");
            check (stateOf (*holder) == before, "DPAD " + what + ": live state unchanged");
        };
        bad ({}, "empty");
        {
            auto d = good;
            d[0] = std::byte { 2 };
            bad (d, "unknown version");
        }
        {
            auto d = good;
            d[4] = std::byte { 8 };
            bad (d, "8 pads");
        }
        {
            auto d = good;
            d.pop_back();
            bad (d, "truncated");
        }
        {
            auto d = good;
            d.push_back (std::byte { 0 });
            bad (d, "trailing byte");
        }
        {
            auto d = good;
            d[8] = std::byte { 8 };   // pad 1 model
            bad (d, "unknown model");
        }
        {
            auto d = good;
            d[11] = std::byte { 9 };   // pad 1 polyphony
            bad (d, "polyphony 9");
        }
        {
            auto d = good;
            const float nan = std::numeric_limits<float>::quiet_NaN();
            std::memcpy (&d[16], &nan, 4);   // pad 1 fine
            bad (d, "NaN fine");
        }
        {
            auto d = good;
            const float big = 3.0f;
            std::memcpy (&d[20], &big, 4);   // pad 1 character
            bad (d, "character above its range");
        }
        // Sanity: the unmodified section is accepted.
        const auto ok = withPads (good);
        holder->setStateInformation (ok.getData(), static_cast<int> (ok.getSize()));
        check (holder->lastStateError().isEmpty(), "the default DPAD section is accepted");
    }

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
        check (std::abs (plainOf (*target, "drm.p02.decay") - 10.0f) < 1.0e-5f, "out-of-range value clamps to the range");
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
            auto edited = vox::drums::factorySlot (1);
            edited.resize (64, 4);
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
                if (b % 9 == 0)   // pattern edits and slot changes between blocks
                {
                    edited.cell (b % padCount, 0, b % edited.steps).on ^= true;
                    edited.cell (b % padCount, 0, b % edited.steps).velocity = 100;
                    edited.cell (b % padCount, 0, b % edited.steps).ratchet = static_cast<std::uint8_t> (1 + b % 8);
                    p->patterns().setSlot (b % vox::drums::PatternStore::slotCount, edited);
                    setPlain (*p, vox::drums::patternParameterId, static_cast<float> (b % vox::drums::PatternStore::slotCount));
                }
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


//==============================================================================
// DRM-04: user patterns (DSEQ) and MIDI export.
vox::drums::StoredPattern randomPattern (std::mt19937& random, int steps, int bars)
{
    vox::drums::StoredPattern p;
    p.steps = static_cast<std::uint8_t> (steps);
    p.bars = static_cast<std::uint8_t> (bars);
    p.seed = static_cast<std::uint32_t> (random());
    for (int lane = 0; lane < padCount; ++lane)
        for (int bar = 0; bar < bars; ++bar)
            for (int st = 0; st < steps; ++st)
                if (random() % 6 == 0)
                {
                    auto& c = p.cell (lane, bar, st);
                    c.on = true;
                    c.accent = random() % 3 == 0;
                    c.velocity = static_cast<std::uint8_t> (1 + random() % 127);
                    c.probability = static_cast<std::uint8_t> (random() % 2 ? 100 : 1 + random() % 100);
                    c.ratchet = static_cast<std::uint8_t> (1 + random() % 8);
                    c.micro = static_cast<std::int8_t> (static_cast<int> (random() % 101) - 50);
                }
    return p;
}

void testPatterns()
{
    using vox::drums::PatternStore;
    using vox::drums::StoredPattern;
    // A new instrument holds the factory patterns.
    {
        auto p = makeProcessor();
        bool factory = true;
        for (int i = 0; i < PatternStore::slotCount; ++i)
            factory = factory && p->patterns().slot (i) == vox::drums::factorySlot (i);
        check (factory, "new instrument: slots hold the factory patterns");
    }
    // DSEQ round trip, byte-identical save back.
    std::mt19937 random (404);
    auto source = makeProcessor();
    const int shapes[5][2] = { { 16, 1 }, { 32, 2 }, { 64, 4 }, { 16, 4 }, { 64, 1 } };
    for (int i = 0; i < PatternStore::slotCount; ++i)
        source->patterns().setSlot (i, randomPattern (random, shapes[i][0], shapes[i][1]));
    const auto saved = stateOf (*source);
    auto target = makeProcessor();
    target->setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    check (target->lastStateError().isEmpty(), "pattern state accepted");
    bool equal = true;
    for (int i = 0; i < PatternStore::slotCount; ++i)
        equal = equal && target->patterns().slot (i) == source->patterns().slot (i);
    check (equal, "every pattern slot is restored (16/32/64 steps, 1-4 bars, all step fields, seed)");
    check (stateOf (*target) == saved, "restored patterns save back byte-identically");

    // A document without DSEQ (DRM-01..03) restores the factory patterns.
    {
        auto document = vox::preset::decode (std::span (static_cast<const std::byte*> (saved.getData()), saved.getSize()),
                                             target->product().stateTag, 1).document;
        std::erase_if (document.sections, [] (const auto& sec) { return sec.tag == vox::drums::sequenceSectionTag; });
        const auto bytes = vox::preset::encode (document);
        target->setStateInformation (bytes->data(), static_cast<int> (bytes->size()));
        check (target->lastStateError().isEmpty() && target->patterns().slot (2) == vox::drums::factorySlot (2),
               "a document without patterns restores the factory patterns");
    }

    // Malformed DSEQ sections are rejected; nothing changes.
    {
        target->setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
        const auto before = stateOf (*target);
        const auto good = PatternStore::encode (source->patterns().all(), source->patterns().alternates()).data;
        const auto with = [&] (std::vector<std::byte> data) {
            auto document = vox::preset::decode (std::span (static_cast<const std::byte*> (saved.getData()), saved.getSize()),
                                                 target->product().stateTag, 1).document;
            for (auto& sec : document.sections)
                if (sec.tag == vox::drums::sequenceSectionTag)
                    sec.data = std::move (data);
            const auto bytes = vox::preset::encode (document);
            return juce::MemoryBlock (bytes->data(), bytes->size());
        };
        const auto bad = [&] (std::vector<std::byte> data, const juce::String& what) {
            const auto blob = with (std::move (data));
            target->setStateInformation (blob.getData(), static_cast<int> (blob.getSize()));
            check (target->lastStateError().isNotEmpty(), "DSEQ " + what + ": rejected");
            check (stateOf (*target) == before, "DSEQ " + what + ": live state unchanged");
        };
        const auto edit = [&] (std::size_t at, std::uint8_t v) {
            auto d = good;
            d[at] = std::byte { v };
            return d;
        };
        // Slot 1 header at 8: steps, bars, seed (4); first cell at 14.
        bad ({}, "empty");
        bad (edit (0, 3), "unknown version");
        bad (edit (4, 4), "4 slots");
        bad (edit (8, 24), "24 steps");
        bad (edit (9, 0), "0 bars");
        bad (edit (9, 5), "5 bars");
        bad (edit (14, 4), "unknown flag");
        bad (edit (15, 128), "velocity 128");
        bad (edit (16, 0), "probability 0");
        bad (edit (16, 101), "probability 101");
        bad (edit (17, 0), "ratchet 0");
        bad (edit (17, 9), "ratchet 9");
        bad (edit (18, 51), "micro +51");
        bad (edit (18, static_cast<std::uint8_t> (-51)), "micro -51");
        {
            auto d = good;
            d.pop_back();
            bad (d, "truncated");
        }
        {
            auto d = good;
            d.push_back (std::byte {});
            bad (d, "trailing byte");
        }
    }

    // The sequencer plays the edited slot: an empty slot is silent, one hit
    // sounds, and an edit made while playing is heard.
    {
        auto p = makeProcessor();
        TestPlayHead head;
        head.set (true, 140.0, 0.0);
        p->setPlayHead (&head);
        setPlain (*p, "drm.seq.mode", 1.0f);
        setPlain (*p, vox::drums::patternParameterId, 3.0f);
        StoredPattern empty;
        empty.steps = 16;
        empty.bars = 1;
        p->patterns().setSlot (3, empty);
        check (render (*p, 200, 256, &head, 140.0, noMidi).peak <= 0.0, "an empty pattern slot is silent");
        auto one = empty;
        one.cell (12, 0, 0) = { true, false, 120 };   // pad 13 on beat 1
        p->patterns().setSlot (3, one);
        p->takeHitMask();
        const auto r = render (*p, 200, 256, &head, 140.0, noMidi);
        check (r.peak > 1.0e-3 && p->takeHitMask() == (1u << 12), "an edit while playing is heard on its pad");
        p->setPlayHead (nullptr);
    }

    // MIDI export equals the canonical hit list (what plays from 0).
    {
        auto p = makeProcessor();
        auto pattern = randomPattern (random, 32, 2);
        p->patterns().setSlot (1, pattern);
        setPlain (*p, "drm.seq.swing", 0.25f);
        setPlain (*p, "drm.seq.accent", 0.6f);
        setPlain (*p, "drm.seq.lane.p05", 0.0f);
        auto ps = p->kitStore().pad (12);
        ps.note = 77;
        p->kitStore().setPad (12, ps);
        const auto blob = p->exportPatternMidi (1, 3, 141.0);
        juce::MidiFile file;
        juce::MemoryInputStream in (blob, false);
        check (file.readFrom (in) && file.getNumTracks() == 1 && file.getTimeFormat() == 960, "the export is a readable SMF, 960 ticks per quarter");
        if (file.getNumTracks() == 1)
        {
            const auto& track = *file.getTrack (0);
            double tempo = 0.0;
            std::vector<std::tuple<long long, int, int>> ons;
            std::map<int, long long> openSince;
            bool overlaps = false;
            for (int i = 0; i < track.getNumEvents(); ++i)
            {
                const auto& m = track.getEventPointer (i)->message;
                const auto tick = static_cast<long long> (std::llround (m.getTimeStamp()));
                if (m.isTempoMetaEvent())
                    tempo = 60.0 / m.getTempoSecondsPerQuarterNote();
                if (m.isNoteOn())
                {
                    overlaps = overlaps || openSince.count (m.getNoteNumber()) != 0;
                    openSince[m.getNoteNumber()] = tick;
                    check (m.getChannel() == 10, "notes on channel 10");
                    ons.emplace_back (tick, m.getNoteNumber(), m.getVelocity());
                }
                else if (m.isNoteOff())
                    openSince.erase (m.getNoteNumber());
            }
            check (std::abs (tempo - 141.0) < 0.01, "the export carries the tempo");
            check (! overlaps && openSince.empty(), "every note ends before the same note starts again, and all end");
            // Canonical list, 3 passes.
            vox::drums::kit::StepContext ctx;
            ctx.swing = 0.25;
            ctx.accent = plainOf (*p, "drm.seq.accent");
            ctx.laneMask = 0xffffu & ~(1u << 4);
            std::vector<std::tuple<long long, int, int>> expected;
            const auto view = pattern.view();
            std::array<vox::drums::kit::ScheduledHit, vox::drums::kit::padCount * vox::drums::kit::maxRatchet> out {};
            std::map<std::pair<long long, int>, int> merged;
            for (std::int64_t g = 0; g < 32 * 2 * 3; ++g)
            {
                const int n = vox::drums::kit::stepHits (view, ctx, g, out.data());
                for (int k = 0; k < n; ++k)
                {
                    const auto& h = out[static_cast<std::size_t> (k)];
                    if (h.ppq < 0.0)
                        continue;
                    const int noteNumber = p->kitStore().pad (h.pad).note;
                    auto& v = merged[{ std::llround (h.ppq * 960.0), noteNumber }];
                    v = std::max (v, static_cast<int> (h.velocity));
                }
            }
            for (const auto& [key, v] : merged)
                expected.emplace_back (key.first, key.second, v);
            auto got = ons;
            std::sort (got.begin(), got.end());
            check (expected.size() > 100, "the exported pattern has notes (" + juce::String (static_cast<int> (expected.size())) + ")");
            check (got == expected, "exported Note Ons equal the canonical hits (tick, note, velocity): "
                                        + juce::String (static_cast<int> (got.size())) + " vs " + juce::String (static_cast<int> (expected.size())));
            bool pad13 = false, lane5 = false;
            for (const auto& e : got)
            {
                pad13 = pad13 || std::get<1> (e) == 77;
                lane5 = lane5 || std::get<1> (e) == vox::drums::defaultKit().pads[4].note;
            }
            check (pad13, "a pad's reassigned note is exported");
            check (! lane5 || vox::drums::defaultKit().pads[4].note == 77, "a disabled lane is not exported");
        }
    }

    // Review 6745483562: the editor never caches a generation newer than the
    // cells it shows. The editor polls in the window between a write's start
    // and its commit; the next poll must show the committed pattern.
    {
        auto p = makeProcessor();
        std::unique_ptr<juce::AudioProcessorEditor> editor (p->createEditorAndMakeActive());
        auto* frame = dynamic_cast<vox::suite::SuiteEditor*> (editor.get());
        auto* panel = frame != nullptr ? dynamic_cast<vox::drums::DrumsPanel*> (frame->content()) : nullptr;
        check (panel != nullptr, "Drums panel for the pattern view");
        if (panel != nullptr)
        {
            setPlain (*p, vox::drums::patternParameterId, 2.0f);
            panel->pollForTesting();
            check (panel->shownPattern() == p->patterns().slot (2), "the step view shows the selected slot");
            auto edited = randomPattern (random, 32, 2);
            p->patterns().beforeCommitForTesting = [panel] { panel->pollForTesting(); };
            p->patterns().setSlot (2, edited);
            p->patterns().beforeCommitForTesting = nullptr;
            panel->pollForTesting();
            check (panel->shownPattern() == edited, "an edit committed after a poll in its window is shown on the next poll (setSlot)");
            auto all = PatternStore::factorySlots();
            all[2] = randomPattern (random, 64, 1);
            p->patterns().beforeCommitForTesting = [panel] { panel->pollForTesting(); };
            p->patterns().setAll (all);
            p->patterns().beforeCommitForTesting = nullptr;
            panel->pollForTesting();
            check (panel->shownPattern() == all[2], "a restore committed after a poll in its window is shown on the next poll (setAll)");
        }
        p->editorBeingDeleted (editor.get());
    }
    // Concurrent writers and readers: a slot read with its generation always
    // holds the cells committed with that generation (the writer stores the
    // generation it expects in the seed).
    {
        PatternStore store;
        std::atomic<bool> stop { false };
        std::atomic<int> torn { 0 }, reads { 0 };
        std::thread writer ([&] {
            StoredPattern p;
            p.steps = 16;
            p.bars = 1;
            for (int i = 0; i < 20000; ++i)
            {
                p.seed = static_cast<std::uint32_t> (store.generation() + 1);   // single writer: the next generation
                p.cell (i % padCount, 0, i % 16).on ^= true;
                if (i % 7 == 0)
                {
                    auto all = store.all();
                    for (auto& slot : all)
                        slot = p;
                    store.setAll (all);
                }
                else
                    store.setSlot (1, p);
            }
            stop = true;
        });
        std::thread reader ([&] {
            while (! stop)
            {
                std::uint64_t g = 0;
                const auto p = store.slot (1, g);
                if (g > 1 && p.seed != g)
                    ++torn;
                ++reads;
            }
        });
        writer.join();
        reader.join();
        check (torn == 0 && reads > 0, "slot and generation are read together (" + juce::String (torn.load()) + " torn of "
                                           + juce::String (reads.load()) + ")");
    }

    // Owner decision (#120): pads 9-16 export like pads 1-8. One hit per
    // lane 9-16 at its own step; the export carries each pad's note (50-57
    // by default) and nothing else, and those hits play (sequencer lights
    // the same pads).
    {
        auto p = makeProcessor();
        StoredPattern upper;
        upper.steps = 16;
        upper.bars = 1;
        for (int pad = 8; pad < padCount; ++pad)
            upper.cell (pad, 0, pad - 8) = { true, false, static_cast<std::uint8_t> (60 + pad) };
        p->patterns().setSlot (4, upper);
        const auto blob = p->exportPatternMidi (4, 1, 120.0);
        juce::MidiFile file;
        juce::MemoryInputStream in (blob, false);
        std::vector<std::tuple<long long, int, int>> ons;
        if (file.readFrom (in) && file.getNumTracks() == 1)
            for (int i = 0; i < file.getTrack (0)->getNumEvents(); ++i)
            {
                const auto& m = file.getTrack (0)->getEventPointer (i)->message;
                if (m.isNoteOn())
                    ons.emplace_back (std::llround (m.getTimeStamp()), m.getNoteNumber(), m.getVelocity());
            }
        std::vector<std::tuple<long long, int, int>> expected;
        for (int pad = 8; pad < padCount; ++pad)
            expected.emplace_back (static_cast<long long> ((pad - 8) * 240), 50 + (pad - 8), 60 + pad);
        check (ons == expected, "pads 9-16: each lane exports one note on its pad's note (50-57) at its step");
        TestPlayHead head;
        head.set (true, 120.0, 0.0);
        p->setPlayHead (&head);
        setPlain (*p, "drm.seq.mode", 1.0f);
        setPlain (*p, vox::drums::patternParameterId, 4.0f);
        p->takeHitMask();
        const auto r = render (*p, 200, 256, &head, 120.0, noMidi);   // ~1.07 s: steps 0-7 (0.875 s) at 120 BPM, within one bar
        check (r.peak > 1.0e-3 && p->takeHitMask() == 0xff00u, "pads 9-16: the exported lanes play on pads 9-16");
        p->setPlayHead (nullptr);
    }

    // resize keeps what fits on the new grid.
    {
        StoredPattern p;
        p.steps = 16;
        p.bars = 2;
        p.cell (0, 0, 4) = { true, true, 99 };
        p.cell (1, 1, 15) = { true, false, 50 };
        auto q = p;
        q.resize (64, 4);
        check (q.steps == 64 && q.bars == 4 && q.cell (0, 0, 16).on && q.cell (0, 0, 16).velocity == 99 && q.cell (1, 1, 60).on
                   && q.cell (0, 2, 16).on && ! q.cell (1, 2, 60).on,
               "16 -> 64 steps keeps positions; added bars repeat bar 1");
        q.resize (16, 1);
        check (q.steps == 16 && q.bars == 1 && q.cell (0, 0, 4).on && ! q.cell (1, 0, 15).on, "back to 16 steps x 1 bar");
    }
}


//==============================================================================
// Lead decision (#121, 6749717308): end-to-end parity of Internal playback
// and the exported Standard MIDI File, on one saved and restored pattern and
// kit. Playback is observed as the triggers the processor's engine actually
// scheduled in each processBlock (not a second call of stepHits); the SMF
// is parsed here byte by byte, independently of juce::MidiFile.
//
// Tolerance: a hit at musical position t plays at frame
// ceil((t - blockPpq) * samplesPerPpq - 1e-6) of its block, i.e. up to one
// frame late; the export writes round(t * 960). One frame is at most
// 960 * bpm / 60 / rate = 0.06 tick here (133 BPM, 44.1 kHz), the tick
// rounding 0.5 tick, and the block position (accumulated in double) adds
// < 1e-6 tick: a played hit must be within 1 tick of its exported Note On.
struct SmfEvent {
    long long tick;
    int status, data1, data2;
};

bool parseSmf (const juce::MemoryBlock& blob, int& division, std::vector<SmfEvent>& out, double& bpm)
{
    const auto* d = static_cast<const std::uint8_t*> (blob.getData());
    const std::size_t n = blob.getSize();
    std::size_t at = 0;
    const auto be = [&] (int bytes) {
        std::uint32_t v = 0;
        for (int i = 0; i < bytes; ++i)
            v = (v << 8) | (at < n ? d[at++] : 0u);
        return v;
    };
    if (n < 22 || std::memcmp (d, "MThd", 4) != 0)
        return false;
    at = 4;
    if (be (4) != 6 || be (2) != 0 || be (2) != 1)
        return false;   // format 0, one track
    division = static_cast<int> (be (2));
    if (std::memcmp (d + at, "MTrk", 4) != 0)
        return false;
    at += 4;
    const std::size_t end = at + be (4);
    if (end != n)
        return false;
    long long tick = 0;
    bool ended = false;
    while (at < end)
    {
        std::uint32_t delta = 0;
        for (int k = 0; k < 4; ++k)
        {
            const auto b = d[at++];
            delta = (delta << 7) | (b & 0x7fu);
            if ((b & 0x80u) == 0)
                break;
        }
        tick += delta;
        const int status = d[at++];
        if (status == 0xff)
        {
            const int type = d[at++];
            const int len = d[at++];
            if (type == 0x51 && len == 3)
                bpm = 60000000.0 / static_cast<double> ((d[at] << 16) | (d[at + 1] << 8) | d[at + 2]);
            if (type == 0x2f)
                ended = true;
            at += static_cast<std::size_t> (len);
            continue;
        }
        if ((status & 0xf0) != 0x80 && (status & 0xf0) != 0x90)
            return false;   // only notes are expected
        out.push_back ({ tick, status, d[at], d[at + 1] });
        at += 2;
    }
    return ended && at == end;
}

void testExportPlaybackParity()
{
    using vox::drums::StoredPattern;
    std::mt19937 random (6749717);
    // The saved project: a 32-step, 2-bar pattern with every step feature,
    // swing, accent, a disabled lane and remapped notes on all 16 pads.
    juce::MemoryBlock project;
    StoredPattern pattern;
    {
        auto source = makeProcessor();
        pattern.steps = 32;
        pattern.bars = 2;
        pattern.seed = 4242;
        bool microPlus = false, microMinus = false, ratchets = false, chance = false, accents = false;
        for (int lane = 0; lane < padCount; ++lane)
            for (int bar = 0; bar < 2; ++bar)
                for (int st = 0; st < 32; ++st)
                    if (random() % 5 == 0)
                    {
                        auto& c = pattern.cell (lane, bar, st);
                        c.on = true;
                        c.velocity = static_cast<std::uint8_t> (20 + random() % 100);
                        c.accent = random() % 4 == 0;
                        c.probability = static_cast<std::uint8_t> (random() % 3 == 0 ? 20 + random() % 70 : 100);
                        c.ratchet = static_cast<std::uint8_t> (random() % 4 == 0 ? 2 + random() % 7 : 1);
                        c.micro = static_cast<std::int8_t> (random() % 3 == 0 ? static_cast<int> (random() % 101) - 50 : 0);
                        microPlus = microPlus || c.micro > 0;
                        microMinus = microMinus || c.micro < 0;
                        ratchets = ratchets || c.ratchet > 1;
                        chance = chance || c.probability < 100;
                        accents = accents || c.accent;
                    }
        check (microPlus && microMinus && ratchets && chance && accents, "parity pattern uses +/- micro, ratchets, probability and accents");
        source->patterns().setSlot (3, pattern);
        for (int pad = 0; pad < padCount; ++pad)
        {
            auto ps = source->kitStore().pad (pad);
            ps.note = static_cast<std::uint8_t> (90 - pad * 2);   // remapped, unique
            ps.polyphony = 8;
            source->kitStore().setPad (pad, ps);
        }
        setPlain (*source, "drm.seq.swing", 0.27f);
        setPlain (*source, "drm.seq.accent", 0.65f);
        setPlain (*source, "drm.seq.lane.p06", 0.0f);   // disabled lane
        setPlain (*source, "drm.seq.mode", 1.0f);       // Internal
        setPlain (*source, vox::drums::patternParameterId, 3.0f);
        project = stateOf (*source);
    }
    const double bpm = 133.0;
    const int passes = 2;
    const double patternPpq = 2 * 4.0;
    bool audible = false;
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
        for (const int block : { 64, 333, 512 })
        {
            const juce::String label = juce::String (static_cast<int> (rate)) + " Hz / " + juce::String (block) + ": ";
            auto p = makeProcessor (rate, block);
            p->setStateInformation (project.getData(), static_cast<int> (project.getSize()));
            check (p->lastStateError().isEmpty() && p->patterns().slot (3) == pattern, label + "the saved pattern is restored");
            // Export from the restored instrument.
            int division = 0;
            double fileBpm = 0.0;
            std::vector<SmfEvent> smf;
            const bool parsed = parseSmf (p->exportPatternMidi (3, passes, bpm), division, smf, fileBpm);
            check (parsed && division == 960 && std::abs (fileBpm - bpm) < 0.01, label + "the export is a format-0 SMF, 960 tpq, at the tempo");
            if (! parsed)
                continue;
            // SMF order and note pairing.
            bool ordered = true, overlap = false, offsFirst = true, channel = true;
            std::map<int, long long> open;
            for (std::size_t i = 0; i < smf.size(); ++i)
            {
                const auto& e = smf[i];
                ordered = ordered && (i == 0 || smf[i - 1].tick <= e.tick);
                channel = channel && (e.status & 0x0f) == 9;
                const bool on = (e.status & 0xf0) == 0x90 && e.data2 > 0;
                if (on)
                {
                    overlap = overlap || open.count (e.data1) != 0;
                    open[e.data1] = e.tick;
                }
                else
                {
                    offsFirst = offsFirst && ! (i > 0 && smf[i - 1].tick == e.tick && (smf[i - 1].status & 0xf0) == 0x90);
                    open.erase (e.data1);
                }
            }
            check (ordered && offsFirst && channel, label + "events in time order, Note Offs before Note Ons at a tick, channel 10");
            check (! overlap && open.empty(), label + "no overlapping same-note pairs; every note ends");
            // Exported Note Ons as (tick, pad, velocity).
            std::map<int, int> padOfNote;
            for (int pad = 0; pad < padCount; ++pad)
                padOfNote[p->kitStore().pad (pad).note] = pad;
            std::vector<std::tuple<long long, int, int>> exported;
            for (const auto& e : smf)
                if ((e.status & 0xf0) == 0x90 && e.data2 > 0)
                    exported.emplace_back (e.tick, padOfNote.count (e.data1) ? padOfNote[e.data1] : -1, e.data2);

            // Play: passes from 0, then a restart (loop back to 0) for one
            // more pass; the restart must replay pass 1 identically.
            TestPlayHead head;
            p->setPlayHead (&head);
            const double samplesPerPpq = 60.0 * rate / bpm;
            const auto playSegment = [&] (double fromPpq, double lengthPpq, double& peak) {
                std::map<std::pair<long long, int>, int> hits;   // (frame from fromPpq, pad) -> velocity
                juce::AudioBuffer<float> buffer (2, block);
                double ppq = fromPpq;
                long long frame = 0;
                while (ppq < fromPpq + lengthPpq)
                {
                    head.set (true, bpm, ppq);
                    juce::MidiBuffer midi;
                    buffer.clear();
                    p->processBlock (buffer, midi);
                    peak = std::max (peak, static_cast<double> (buffer.getMagnitude (0, block)));
                    const auto& engine = p->engineForTesting();
                    for (int k = 0; k < engine.scheduledCount(); ++k)
                    {
                        std::uint32_t offset;
                        std::uint8_t pad, velocity;
                        engine.scheduled (k, offset, pad, velocity);
                        // One trigger per pad and frame (the engine merges coincident hits).
                        auto& v = hits[{ frame + offset, pad }];
                        v = std::max (v, static_cast<int> (velocity));
                    }
                    ppq += block / samplesPerPpq;
                    frame += block;
                }
                return hits;
            };
            double peak = 0.0;
            const auto first = playSegment (0.0, passes * patternPpq, peak);
            const auto again = playSegment (0.0, patternPpq, peak);
            p->setPlayHead (nullptr);
            audible = audible || peak > 1.0e-3;

            // Compare with the export: every played trigger within 1 tick of
            // a Note On of the same pad and velocity, one to one.
            const auto compare = [&] (const std::map<std::pair<long long, int>, int>& played, double lengthPpq, const juce::String& what) {
                // The window: both sides up to 2 ticks before the end (the
                // last block runs past the end into the next pass, which an
                // export of N passes does not contain).
                const double limit = lengthPpq * 960.0 - 2.0;
                std::vector<std::tuple<long long, int, int>> want;
                for (const auto& e : exported)
                    if (static_cast<double> (std::get<0> (e)) < limit)
                        want.push_back (e);
                std::vector<bool> used (want.size(), false);
                int matched = 0, unmatched = 0;
                for (const auto& [key, velocity] : played)
                {
                    const double tick = static_cast<double> (key.first) / samplesPerPpq * 960.0;
                    if (tick >= limit + 1.0)
                        continue;
                    bool found = false;
                    for (std::size_t i = 0; i < want.size() && ! found; ++i)
                        if (! used[i] && std::get<1> (want[i]) == key.second && std::get<2> (want[i]) == velocity
                            && std::abs (static_cast<double> (std::get<0> (want[i])) - tick) <= 1.0)
                        {
                            used[i] = true;
                            found = true;
                        }
                    matched += found ? 1 : 0;
                    unmatched += found ? 0 : 1;
                    if (! found && std::getenv ("VOX_PARITY_DEBUG") != nullptr)
                    {
                        std::printf ("  unmatched %s: tick %.3f pad %d vel %d; exported near:", what.toRawUTF8(), tick, key.second, velocity);
                        for (const auto& w : want)
                            if (std::abs (static_cast<double> (std::get<0> (w)) - tick) < 40.0)
                                std::printf (" (%lld p%d v%d)", std::get<0> (w), std::get<1> (w), std::get<2> (w));
                        std::printf ("\n");
                    }
                }
                const auto missing = std::count (used.begin(), used.end(), false);
                check (matched > 100 && unmatched == 0 && missing == 0,
                       label + what + ": playback triggers equal exported Note Ons (" + juce::String (matched) + " matched, "
                           + juce::String (unmatched) + " played but not exported, " + juce::String (static_cast<int> (missing))
                           + " exported but not played)");
            };
            compare (first, passes * patternPpq, "2 passes from 0");
            compare (again, patternPpq, "restart at 0");
            bool lane6 = false, upper = false;
            for (const auto& e : exported)
            {
                lane6 = lane6 || std::get<1> (e) == 5;
                upper = upper || std::get<1> (e) >= 8;
            }
            check (! lane6 && upper, label + "the disabled lane is absent; pads 9-16 are exported on their remapped notes");
        }
    check (audible, "the parity kit is audible");
}


//==============================================================================
// DRM-04b: the step editor edits the selected slot through undoable steps.
void testPatternEditorUi()
{
    using vox::drums::PatternStore;
    using vox::drums::StoredPattern;
    auto p = makeProcessor();
    std::unique_ptr<juce::AudioProcessorEditor> editor (p->createEditorAndMakeActive());
    auto* frame = dynamic_cast<vox::suite::SuiteEditor*> (editor.get());
    auto* panel = frame != nullptr ? dynamic_cast<vox::drums::DrumsPanel*> (frame->content()) : nullptr;
    check (panel != nullptr, "step editor: Drums panel");
    if (panel == nullptr)
        return;
    auto& ed = panel->patternEditor();
    auto& store = p->patterns();
    auto& history = p->patternHistory();
    setPlain (*p, vox::drums::patternParameterId, 2.0f);
    panel->pollForTesting();
    check (ed.slot() == 2 && ed.shown() == store.slot (2), "step editor: shows the selected slot");
    const auto original = store.slot (2);

    // Toggle a step on and off.
    const bool wasOn = store.slot (2).cell (12, 0, 3).on;
    ed.clickCell (12, 3);
    check (store.slot (2).cell (12, 0, 3).on != wasOn && ed.selectedLane() == 12 && ed.selectedStep() == 3,
           "step editor: a click toggles the step and selects it");
    check (! wasOn && store.slot (2).cell (12, 0, 3).velocity == 100, "step editor: a new step has velocity 100");
    ed.clickCell (12, 3);
    check (store.slot (2).cell (12, 0, 3).on == wasOn, "step editor: a second click toggles it back");
    ed.clickCell (12, 3);

    // Inspector: each value moves the selected step; a drag is one undo step.
    const auto steps = history.size();
    auto* velocity = ed.inspector ("velocity");
    velocity->onDragStart();
    for (int v : { 40, 50, 60, 70 })
        velocity->setValue (v, juce::sendNotificationSync);
    velocity->onDragEnd();
    check (store.slot (2).cell (12, 0, 3).velocity == 70 && history.size() == steps + 1,
           "step editor: a velocity drag sets the step and is one undo step");
    ed.inspector ("probability")->setValue (35, juce::sendNotificationSync);
    ed.inspector ("ratchet")->setValue (4, juce::sendNotificationSync);
    ed.inspector ("micro")->setValue (-20, juce::sendNotificationSync);
    ed.accentToggle()->setToggleState (true, juce::sendNotificationSync);
    {
        const auto c = store.slot (2).cell (12, 0, 3);
        check (c.probability == 35 && c.ratchet == 4 && c.micro == -20 && c.accent, "step editor: chance, ratchet, micro and accent set the step");
    }
    // Undo walks back one edit at a time; redo forward; a new edit clears redo.
    ed.button ("undo")->onClick();
    check (! store.slot (2).cell (12, 0, 3).accent && store.slot (2).cell (12, 0, 3).micro == -20, "step editor: undo reverts the last edit only");
    ed.button ("undo")->onClick();
    ed.button ("undo")->onClick();
    ed.button ("undo")->onClick();
    ed.button ("undo")->onClick();
    check (store.slot (2).cell (12, 0, 3).velocity == 100 && store.slot (2).cell (12, 0, 3).probability == 100,
           "step editor: undo of the velocity drag returns to the value before the drag");
    ed.button ("redo")->onClick();
    check (store.slot (2).cell (12, 0, 3).velocity == 70, "step editor: redo replays the drag");
    ed.inspector ("velocity")->setValue (90, juce::sendNotificationSync);
    check (! history.canRedo(), "step editor: a new edit clears redo");

    // Bars and grid.
    ed.combo ("bars")->setSelectedItemIndex (1, juce::sendNotificationSync);   // 2 bars
    check (store.slot (2).bars == 2, "step editor: bars combo resizes the pattern");
    ed.combo ("steps")->setSelectedItemIndex (1, juce::sendNotificationSync);  // 32 steps
    check (store.slot (2).steps == 32 && store.slot (2).cell (12, 0, 6).velocity == 90,
           "step editor: 32 steps keep each step at its position");
    ed.selectBar (0);
    ed.clickCell (2, 31);
    ed.button ("dup")->onClick();
    {
        const auto q = store.slot (2);
        bool same = true;
        for (int lane = 0; lane < padCount; ++lane)
            for (int st = 0; st < q.steps; ++st)
            {
                const auto& a = q.cell (lane, 0, st);
                const auto& b = q.cell (lane, 1, st);
                same = same && a.on == b.on && a.velocity == b.velocity && a.ratchet == b.ratchet;
            }
        check (same && ed.bar() == 1, "step editor: Dup copies bar 1 to bar 2 and shows bar 2");
    }
    ed.button ("clear")->onClick();
    {
        const auto q = store.slot (2);
        bool empty = true;
        for (int lane = 0; lane < padCount; ++lane)
            for (int st = 0; st < q.steps; ++st)
                empty = empty && ! q.cell (lane, 1, st).on;
        check (empty && q.cell (12, 0, 6).on, "step editor: Clear empties only the shown bar");
    }
    const auto seed = store.slot (2).seed;
    ed.button ("seed")->onClick();
    check (store.slot (2).seed != seed, "step editor: Seed changes the probability seed");

    // A/B.
    const auto a = store.slot (2);
    ed.button ("copyB")->onClick();
    check (store.alternate (2) == a, "step editor: A>B copies the pattern to B");
    ed.clickCell (0, 0);
    const auto edited = store.slot (2);
    ed.button ("swap")->onClick();
    check (store.slot (2) == a && store.alternate (2) == edited, "step editor: A/B swaps the pattern and its variation");
    ed.button ("undo")->onClick();
    check (store.slot (2) == edited && store.alternate (2) == a, "step editor: undo reverts the swap");

    // State: alternates are saved (DSEQ v2) and restored; a v1 section reads
    // B = A; a restore clears the history.
    {
        const auto saved = stateOf (*p);
        auto other = makeProcessor();
        other->setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
        check (other->lastStateError().isEmpty() && other->patterns().slot (2) == edited && other->patterns().alternate (2) == a,
               "step editor: slots and variations survive save and restore");
        // v1: header + the 5 slots only.
        auto v2 = PatternStore::encode (other->patterns().all(), other->patterns().all()).data;
        auto v1 = std::vector<std::byte> (v2.begin(), v2.begin() + static_cast<std::ptrdiff_t> (8 + (v2.size() - 8) / 2));
        v1[0] = std::byte { 1 };
        vox::preset::Section section;
        section.tag = vox::drums::sequenceSectionTag;
        section.data = v1;
        std::array<StoredPattern, PatternStore::slotCount> slots, alternates;
        juce::String error;
        check (PatternStore::decode (section, slots, alternates, error) && slots[2] == edited && alternates[2] == edited,
               "step editor: a DSEQ v1 section reads each variation as a copy of its slot");
        check (history.canUndo(), "step editor: history before a restore");
        p->setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
        panel->pollForTesting();
        check (! history.canUndo() && ! history.canRedo(), "step editor: a project restore clears the undo history");
    }

    // MIDI export of the shown slot (what a drag drops).
    {
        const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("vox-drums-editor-export.mid");
        check (ed.writeMidi (file), "step editor: the MIDI file is written");
        juce::MemoryBlock written;
        file.loadFileAsData (written);
        check (written == p->exportPatternMidi (2, 1, p->lastBpm()), "step editor: the file is the export of the shown slot");
        check (ed.saveAsDefault().getFileName() == "VOX Drums Pattern 3.mid"
                   && ed.saveAsDefault().getParentDirectory() == juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
               "step editor: save-as suggests the pattern's name in Documents");
        file.deleteFile();
    }

    // Review 6753052108: drag files are unique per instance and per drag;
    // another export never replaces a handed-off file; cleanup is bounded
    // and never removes the newest drag.
    {
        const auto loadFile = [] (const juce::File& f) {
            juce::MemoryBlock b;
            f.loadFileAsData (b);
            return b;
        };
        // Instance B: same slot index (3), different notes.
        auto q = makeProcessor();
        std::unique_ptr<juce::AudioProcessorEditor> editorB (q->createEditorAndMakeActive());
        auto* panelB = dynamic_cast<vox::drums::DrumsPanel*> (dynamic_cast<vox::suite::SuiteEditor*> (editorB.get())->content());
        setPlain (*q, vox::drums::patternParameterId, 2.0f);
        StoredPattern other;
        other.steps = 16;
        other.bars = 1;
        other.cell (15, 0, 7) = { true, false, 77 };
        q->patterns().setSlot (2, other);
        panelB->pollForTesting();
        auto& edB = panelB->patternEditor();
        const auto expectA = p->exportPatternMidi (2, 1, p->lastBpm());
        const auto expectB = q->exportPatternMidi (2, 1, q->lastBpm());
        check (expectA != expectB, "drag: the two instances export different content");
        const auto a1 = ed.exportForDrag();
        const auto b1 = edB.exportForDrag();
        const auto a2 = ed.exportForDrag();   // a repeated drag
        check (a1 != juce::File() && b1 != juce::File() && a2 != juce::File() && a1 != b1 && a1 != a2 && b1 != a2,
               "drag: every drag gets its own path (instances and repeats)");
        check (a1.getFileName() == "VOX Drums Pattern 3.mid" && b1.getFileName() == "VOX Drums Pattern 3.mid",
               "drag: the dropped file keeps the readable name");
        check (loadFile (a1) == expectA && loadFile (a2) == expectA && loadFile (b1) == expectB,
               "drag: each handed-off file still holds its own instance's export after the others");
        // Cleanup: old drags of this instance go, the newest stays even if
        // old; another instance's folder goes only when stale.
        const auto now = juce::Time::getCurrentTime();
        const auto old = now - vox::drums::PatternEditor::dragLifetime - juce::RelativeTime::minutes (1);
        a1.getParentDirectory().setLastModificationTime (old);
        a2.getParentDirectory().setLastModificationTime (old + juce::RelativeTime::seconds (1));   // newest, also old
        ed.cleanupDragFiles (now);
        check (! a1.exists() && a2.exists(), "drag: cleanup removes old drags but never the newest");
        check (b1.exists(), "drag: another instance's recent drag is kept");
        const auto bInstance = b1.getParentDirectory().getParentDirectory();
        b1.getParentDirectory().setLastModificationTime (now - vox::drums::PatternEditor::staleLifetime - juce::RelativeTime::hours (1));
        bInstance.setLastModificationTime (now - vox::drums::PatternEditor::staleLifetime - juce::RelativeTime::hours (1));
        ed.cleanupDragFiles (now);
        check (! b1.exists() && a2.exists(), "drag: another instance's stale folder (a day old) is removed");
        q->editorBeingDeleted (editorB.get());
        // Remove this test's drag folder, and only a folder inside the drag
        // root (never anything above it, whatever the paths turned out to be).
        const auto instanceDir = a2.getParentDirectory().getParentDirectory();
        if (instanceDir.isAChildOf (vox::drums::PatternEditor::dragRoot()))
            instanceDir.deleteRecursively();
    }

    // A real mouse click on a cell (lane page 2 when the grid is small).
    {
        ed.selectCell (13, 5);
        const auto r = ed.cellBounds (13, 5);
        check (! r.isEmpty(), "step editor: a selected lane is on the shown page");
        juce::Component* target = nullptr;
        for (auto* child : ed.getChildren())
            if (child->getName() == "pattern grid")
                target = child;
        check (target != nullptr, "step editor: grid component");
        if (target != nullptr)
        {
            const bool before = store.slot (2).cell (13, ed.bar(), 5).on;
            auto source = juce::Desktop::getInstance().getMainMouseSource();
            const auto pos = (r.getCentre() - target->getPosition()).toFloat();
            const juce::MouseEvent e (source, pos, juce::ModifierKeys(), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, target, target,
                                      juce::Time::getCurrentTime(), pos, juce::Time::getCurrentTime(), 1, false);
            target->mouseDown (e);
            check (store.slot (2).cell (13, ed.bar(), 5).on != before, "step editor: a mouse click on the cell toggles exactly that step");
        }
    }
    check (store.slot (2) != original, "step editor: the slot was edited");
    p->editorBeingDeleted (editor.get());
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
    testPatterns();
    testExportPlaybackParity();
    testPatternEditorUi();
    testRealtime();
    if (failures == 0)
        std::printf ("VOX Drums product tests passed\n");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
