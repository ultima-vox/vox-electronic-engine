#include "PadControlSurface.h"
#include "PadControlBindings.h"
#include "Processor.h"
#include "Tokens.h"
#include "Typography.h"
#include "VoxComponents.h"
#include "DrumsDial.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace vox::drums {
namespace {
namespace c = vox::ui::tokens::colour;
using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;

class SampleWaveform final : public juce::Component {
public:
    explicit SampleWaveform (DrumsProcessor& p) : processor (p) {}
    void setContext (int newPad, int newPlayer) { pad = newPad; player = newPlayer; repaint(); }
    bool dragging = false;
    void paint (juce::Graphics& g) override
    {
        g.fillAll (c::graph);
        const auto info = processor.samples().pad (slotOf (pad, player));
        if (info.buffer == nullptr)
        {
            g.setColour (c::textMuted);
            g.drawFittedText (info.reference ? "Sample missing / synthesis fallback" : "Drop an audio file or Load",
                             getLocalBounds().reduced (8), juce::Justification::centred, 2);
            return;
        }
        const auto settings = processor.kitStore().pad (pad).players[static_cast<std::size_t> (player)];
        const auto& low = info.buffer->overviewMin;
        const auto& high = info.buffer->overviewMax;
        const float height = static_cast<float> (getHeight()), width = static_cast<float> (getWidth());
        const float scale = info.buffer->peak > 0 ? height * 0.45f / info.buffer->peak : 0;
        for (std::size_t n = 0; n < low.size(); ++n)
        {
            const float fraction = static_cast<float> (n) / static_cast<float> (low.size());
            g.setColour (fraction >= settings.start && fraction <= settings.end ? c::accent : c::textMuted);
            g.drawVerticalLine (juce::roundToInt (width * fraction), height * 0.5f - high[n] * scale, height * 0.5f - low[n] * scale + 1);
        }
        g.setColour (c::text);
        for (float position : { settings.start, settings.end })
        {
            const float x = juce::jlimit (1.0f, std::max (1.0f, width - 2), width * position);
            g.fillRect (x - 1, 0.0f, 2.0f, height);
            g.fillRect (juce::jlimit (0.0f, std::max (0.0f, width - 8), x - 4), 0.0f, 8.0f, 12.0f);
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto info = processor.samples().pad (slotOf (pad, player));
        if (info.buffer == nullptr || getWidth() <= 0) return;
        const auto settings = processor.kitStore().pad (pad).players[static_cast<std::size_t> (player)];
        const float x = static_cast<float> (e.x) / static_cast<float> (getWidth());
        trimStart = std::abs (x - settings.start) <= std::abs (x - settings.end);
        dragging = true;
        mouseDrag (e);
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging || getWidth() <= 0) return;
        auto settings = processor.kitStore().pad (pad);
        auto& sample = settings.players[static_cast<std::size_t> (player)];
        const auto info = processor.samples().pad (slotOf (pad, player));
        const float gap = info.buffer != nullptr ? 1.0f / static_cast<float> (std::max<std::uint64_t> (1, info.buffer->frames)) : 0.001f;
        const float fraction = static_cast<float> (e.x) / static_cast<float> (getWidth());
        if (trimStart) sample.start = juce::jlimit (0.0f, std::max (0.0f, sample.end - gap), fraction);
        else sample.end = juce::jlimit (std::min (1.0f, sample.start + gap), 1.0f, fraction);
        processor.kitStore().setPad (pad, settings);
        repaint();
    }
    void mouseUp (const juce::MouseEvent&) override { dragging = false; }
private:
    DrumsProcessor& processor;
    int pad = 0, player = 0;
    bool trimStart = false;
};
} // namespace

struct PadControlSurface::Impl {
    struct Control {
        juce::String name;
        std::function<float (const StructuralContext&)> get;
        std::function<void (StructuralContext&, float)> set;
        std::unique_ptr<DrumsDial> knob;
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<vox::ui::VoxComboBox> combo;
        std::unique_ptr<juce::ToggleButton> toggle;
        bool dragging = false;
    };
    struct Host {
        juce::String suffix;
        std::unique_ptr<DrumsDial> knob;
        std::unique_ptr<Attachment> attachment;
        bool dragging = false;
    };

    Impl (PadControlSurface& view, DrumsProcessor& p, Group which) : owner (view), processor (p), group (which), wave (p)
    {
        owner.addAndMakeVisible (title);
        if (group == Group::sound)
        {
            populateSoundBindings (*this);
            for (const auto& [name, label] : { std::pair { "level", "Level" }, { "tune", "Tune" }, { "decay", "Decay" },
                                              { "pan", "Pan" }, { "cutoff", "Cutoff" } })
            {
                auto h = std::make_unique<Host>(); h->suffix = name;
                h->knob = std::make_unique<DrumsDial> (label);
                if (h->suffix == "pan" || h->suffix == "tune") h->knob->setStyle (vox::ui::VoxKnob::Style::Bipolar);
                auto* raw = h.get();
                h->knob->getSlider().onDragStart = [raw] { raw->dragging = true; };
                h->knob->getSlider().onDragEnd = [raw] { raw->dragging = false; };
                owner.addAndMakeVisible (*h->knob); hosts.push_back (std::move (h));
            }
        }
        else
        {
            populateSampleBindings (*this);
            for (auto* component : std::initializer_list<juce::Component*> { &wave, &status, &a, &b, &load, &clear, &library })
                owner.addAndMakeVisible (*component);
            a.setButtonText ("A"); b.setButtonText ("B");
            a.onClick = [this] { choosePlayer (0); }; b.onClick = [this] { choosePlayer (1); };
            load.setButtonText ("Load"); clear.setButtonText ("Clear"); library.setButtonText ("Library");
            clear.onClick = [this] { processor.samples().clear (slotOf (pad, player)); message.clear(); owner.update(); };
            library.onClick = [this] {
                const auto result = processor.samples().copyIntoLibrary (slotOf (pad, player));
                message = result.wasOk() ? "Copied into sample library" : result.getErrorMessage(); owner.update();
            };
            load.onClick = [this] {
                chooser = std::make_unique<juce::FileChooser> ("Load sample", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg");
                const int targetPad = pad, targetPlayer = player;
                juce::Component::SafePointer<PadControlSurface> self (&owner);
                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                    [self, targetPad, targetPlayer] (const juce::FileChooser& fc) {
                        if (self != nullptr && fc.getResult().existsAsFile()) self->impl_->loadFile (fc.getResult(), targetPad, targetPlayer);
                    });
            };
        }
        configurePadValueText (*this);
        context (0, 0, false);
    }
    void choosePlayer (int selected)
    {
        context (pad, selected);
        if (owner.onPlayerSelected) owner.onPlayerSelected (selected);
    }
    bool busy() const
    {
        if (wave.dragging) return true;
        for (const auto& h : hosts) if (h->dragging) return true;
        for (const auto& s : controls) if (s->dragging) return true;
        return false;
    }
    void context (int nextPad, int nextPlayer, bool relayout = true)
    {
        requestedPad = nextPad; requestedPlayer = nextPlayer;
        if (busy() || (pad == nextPad && player == nextPlayer)) return;
        pad = nextPad; player = nextPlayer; message.clear();
        for (auto& h : hosts)
        {
            h->attachment.reset();
            const auto id = juce::String (padPrefix (pad)) + "." + h->suffix;
            auto* parameter = processor.state().getParameter (id);
            h->knob->getSlider().setName (parameter->getName (64));
            h->knob->setDoubleClickResetValue (parameter->convertFrom0to1 (parameter->getDefaultValue()));
            h->attachment = std::make_unique<Attachment> (processor.state(), id, h->knob->getSlider());
        }
        wave.setContext (pad, player);
        if (relayout) owner.resized();
    }
    void change (Control& s, float value)
    {
        StructuralContext ctx { processor.kitStore().pad (pad), player };
        s.set (ctx, value); processor.kitStore().setPad (pad, ctx.pad);
        if (s.name == "model") owner.resized();
        owner.repaint();
    }
    Control& addKnob (const juce::String& name, const juce::String& label, double lo, double hi, double step,
                     std::function<float (const StructuralContext&)> get, std::function<void (StructuralContext&, float)> set,
                     bool bipolar = false, double skew = 0)
    {
        auto s = std::make_unique<Control>(); s->name = name; s->get = std::move (get); s->set = std::move (set);
        s->knob = std::make_unique<DrumsDial> (label);
        if (bipolar) s->knob->setStyle (vox::ui::VoxKnob::Style::Bipolar);
        auto& slider = s->knob->getSlider(); slider.setRange (lo, hi, step);
        slider.setName (label);
        if (skew > 0) slider.setSkewFactorFromMidPoint (skew);
        auto* raw = s.get();
        slider.onValueChange = [this, raw] { change (*raw, static_cast<float> (raw->knob->getSlider().getValue())); };
        slider.onDragStart = [raw] { raw->dragging = true; }; slider.onDragEnd = [raw] { raw->dragging = false; };
        owner.addAndMakeVisible (*s->knob); controls.push_back (std::move (s)); return *controls.back();
    }
    Control& addCombo (const juce::String& name, const juce::String& label, const juce::StringArray& items,
                      std::function<float (const StructuralContext&)> get, std::function<void (StructuralContext&, float)> set)
    {
        auto s = std::make_unique<Control>(); s->name = name; s->get = std::move (get); s->set = std::move (set);
        s->combo = std::make_unique<vox::ui::VoxComboBox>(); s->combo->addItemList (items, 1);
        s->combo->setName (label);
        s->label = std::make_unique<juce::Label> (name, label);
        auto* raw = s.get();
        s->combo->onChange = [this, raw] { change (*raw, static_cast<float> (raw->combo->getSelectedItemIndex())); };
        owner.addAndMakeVisible (*s->label); owner.addAndMakeVisible (*s->combo);
        controls.push_back (std::move (s)); return *controls.back();
    }
    Control& addToggle (const juce::String& name, const juce::String& label,
                       std::function<float (const StructuralContext&)> get, std::function<void (StructuralContext&, float)> set)
    {
        auto s = std::make_unique<Control>(); s->name = name; s->get = std::move (get); s->set = std::move (set);
        s->toggle = std::make_unique<juce::ToggleButton> (label); auto* raw = s.get();
        s->toggle->onClick = [this, raw] { change (*raw, raw->toggle->getToggleState() ? 1.0f : 0.0f); };
        owner.addAndMakeVisible (*s->toggle); controls.push_back (std::move (s)); return *controls.back();
    }
    Control* findStructural (const juce::String& name)
    {
        for (const auto& s : controls) if (s->name == name) return s.get();
        return nullptr;
    }
    void loadFile (const juce::File& file, int targetPad, int targetPlayer)
    {
        if (pad == targetPad && player == targetPlayer) message = "Loading " + file.getFileName();
        juce::Component::SafePointer<PadControlSurface> self (&owner);
        processor.samples().loadAsync (slotOf (targetPad, targetPlayer), file,
            [self, targetPad, targetPlayer] (const juce::String& error) {
                if (self != nullptr && self->impl_->pad == targetPad && self->impl_->player == targetPlayer)
                { self->impl_->message = error; self->update(); }
            });
    }
    PadControlSurface& owner;
    DrumsProcessor& processor;
    Group group;
    kit::Model shownModel = kit::Model::kick;
    int pad = -1, player = -1, requestedPad = 0, requestedPlayer = 0;
    std::vector<std::unique_ptr<Control>> controls;
    std::vector<std::unique_ptr<Host>> hosts;
    juce::Label title, status;
    SampleWaveform wave;
    vox::ui::VoxButton a, b, load, clear, library;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String message;
};

PadControlSurface::PadControlSurface (DrumsProcessor& p, Group group) : impl_ (std::make_unique<Impl> (*this, p, group)) { update(); }
PadControlSurface::~PadControlSurface() = default;
void PadControlSurface::setContext (int pad, int player)
{
    if (pad < 0 || pad >= 16 || player < 0 || player > 1) return;
    impl_->context (pad, player); update();
}
void PadControlSurface::update()
{
    auto& i = *impl_; i.context (i.requestedPad, i.requestedPlayer);
    const StructuralContext ctx { i.processor.kitStore().pad (i.pad), i.player };
    if (i.shownModel != ctx.pad.model)
    {
        i.shownModel = ctx.pad.model;
        resized();
    }
    for (auto& s : i.controls)
    {
        const auto v = s->get (ctx);
        if (s->knob && ! s->dragging) s->knob->getSlider().setValue (v, juce::dontSendNotification);
        if (s->combo) s->combo->setSelectedItemIndex (juce::roundToInt (v), juce::dontSendNotification);
        if (s->toggle) s->toggle->setToggleState (v >= 0.5f, juce::dontSendNotification);
    }
    i.title.setText ((i.group == Group::sound ? "SOUND / PAD " : "SAMPLE / PAD ") + juce::String (i.pad + 1), juce::dontSendNotification);
    if (i.group == Group::sample)
    {
        const auto info = i.processor.samples().pad (slotOf (i.pad, i.player));
        const auto text = i.message.isNotEmpty() ? i.message : info.buffer != nullptr ? info.reference->name
                          + " / " + juce::String (info.buffer->sampleRate / 1000, 1) + " kHz"
                          : info.reference ? "Missing: " + info.reference->name + " / synthesis fallback" : "No sample";
        i.status.setText (text, juce::dontSendNotification);
        i.a.setToggleState (i.player == 0, juce::dontSendNotification); i.b.setToggleState (i.player == 1, juce::dontSendNotification);
        i.clear.setEnabled (info.reference.has_value()); i.library.setEnabled (info.buffer != nullptr); i.wave.repaint();
    }
}
void PadControlSurface::paint (juce::Graphics& g) { g.fillAll (c::panel); }
void PadControlSurface::resized()
{
    if (! impl_) return;
    auto& i = *impl_;
    auto r = getLocalBounds().reduced (8); i.title.setBounds (r.removeFromTop (24));
    if (i.group == Group::sample)
    {
        auto toolbar = r.removeFromTop (30);
        for (auto* button : { &i.a, &i.b, &i.load, &i.clear, &i.library }) button->setBounds (toolbar.removeFromLeft (82).reduced (2));
        i.status.setBounds (r.removeFromTop (24)); i.wave.setBounds (r.removeFromTop (150)); r.removeFromTop (8);
    }
    if (! i.hosts.empty())
    {
        auto row = r.removeFromTop (86); const int width = row.getWidth() / static_cast<int> (i.hosts.size());
        for (auto& h : i.hosts) h->knob->setBounds (row.removeFromLeft (width));
    }
    const int columns = std::max (1, r.getWidth() / 110), cellWidth = r.getWidth() / columns;
    int item = 0;
    for (auto& s : i.controls)
    {
        const bool body = s->name == "pitch_amount" || s->name == "pitch_decay" || s->name == "drive" || s->name == "sub" || s->name == "tail";
        const bool visible = ! body || i.processor.kitStore().pad (i.pad).model == kit::Model::kick;
        if (s->knob) s->knob->setVisible (visible);
        if (s->combo) s->combo->setVisible (visible);
        if (s->label) s->label->setVisible (visible);
        if (s->toggle) s->toggle->setVisible (visible);
        if (! visible) continue;
        auto cell = juce::Rectangle<int> (r.getX() + item % columns * cellWidth, r.getY() + item / columns * 96, cellWidth, 92).reduced (3);
        if (s->knob) s->knob->setBounds (cell);
        if (s->combo) { s->label->setBounds (cell.removeFromTop (20)); s->combo->setBounds (cell.removeFromTop (30)); }
        if (s->toggle) s->toggle->setBounds (cell.removeFromTop (30));
        ++item;
    }
}
juce::Slider* PadControlSurface::structuralSlider (const juce::String& name)
{
    auto* s = impl_->findStructural (name); return s && s->knob ? &s->knob->getSlider() : nullptr;
}
juce::ComboBox* PadControlSurface::structuralCombo (const juce::String& name)
{
    auto* s = impl_->findStructural (name); return s ? s->combo.get() : nullptr;
}
juce::Button* PadControlSurface::structuralToggle (const juce::String& name)
{
    auto* s = impl_->findStructural (name); return s ? s->toggle.get() : nullptr;
}
juce::Slider* PadControlSurface::hostSlider (const juce::String& suffix)
{
    for (auto& h : impl_->hosts) if (h->suffix == suffix) return &h->knob->getSlider();
    return nullptr;
}
juce::Button* PadControlSurface::sampleButton (const juce::String& name)
{
    if (name == "A") return &impl_->a;
    if (name == "B") return &impl_->b;
    if (name == "load") return &impl_->load;
    if (name == "clear") return &impl_->clear;
    if (name == "library") return &impl_->library;
    return nullptr;
}
juce::String PadControlSurface::sampleStatus() const { return impl_->status.getText(); }
juce::Component& PadControlSurface::waveform() { return impl_->wave; }
void PadControlSurface::loadSample (const juce::File& file) { impl_->loadFile (file, impl_->pad, impl_->player); }
bool PadControlSurface::isInterestedInFileDrag (const juce::StringArray& files)
{
    if (impl_->group != Group::sample) return false;
    for (const auto& file : files) if (juce::File (file).hasFileExtension ("wav;aif;aiff;flac;ogg")) return true;
    return false;
}
void PadControlSurface::filesDropped (const juce::StringArray& files, int, int)
{
    if (! isInterestedInFileDrag (files)) return;
    for (const auto& file : files) if (juce::File (file).hasFileExtension ("wav;aif;aiff;flac;ogg")) { loadSample (juce::File (file)); break; }
}
} // namespace vox::drums
