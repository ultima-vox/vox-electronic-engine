#include "DrumsWorkspace.h"
#include "DrumsParameters.h"
#include "DrumsPages.h"
#include "PatternEditor.h"
#include "PadControlSurface.h"
#include "Processor.h"
#include "Tokens.h"
#include "Typography.h"
#include "VoxComponents.h"
#include "DrumsDial.h"
#include "DrumsFader.h"
#include "VoxLookAndFeel.h"

#include <cmath>
#include <vector>

namespace vox::drums {
namespace {
namespace c = vox::ui::tokens::colour;
using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

juce::Rectangle<int> rectangle (workspace::Box b) { return { b.x, b.y, b.w, b.h }; }

struct Knob {
    bool dragging = false;
    std::unique_ptr<DrumsDial> control;
    std::unique_ptr<SliderAttachment> attachment;
    void bind (juce::Component& parent, DrumsProcessor& p, const juce::String& id, const juce::String& label,
               vox::ui::VoxKnob::Size size = vox::ui::VoxKnob::Size::Small)
    {
        attachment.reset();
        if (control == nullptr)
        {
            control = std::make_unique<DrumsDial> (label, size);
            parent.addAndMakeVisible (*control);
            control->getSlider().onDragStart = [this] { dragging = true; };
            control->getSlider().onDragEnd = [this] { dragging = false; };
        }
        control->setStyle (id.endsWith (".pan") || id.endsWith (".tune")
                               ? vox::ui::VoxKnob::Style::Bipolar : vox::ui::VoxKnob::Style::Standard);
        auto* parameter = p.state().getParameter (id);
        jassert (parameter != nullptr);
        control->setDoubleClickResetValue (parameter->convertFrom0to1 (parameter->getDefaultValue()));
        control->setTooltip (id);
        attachment = std::make_unique<SliderAttachment> (p.state(), id, control->getSlider());
    }
};

class Peak final : public juce::Component {
public:
    explicit Peak (juce::Colour accent) : accent_ (accent) {}
    void push (float value) { value_ = std::max (value, value_ * 0.78f); repaint(); }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (c::graph); g.fillRect (r);
        const float normalized = value_ > 0.0f ? juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (value_) + 60.0f) / 60.0f) : 0.0f;
        g.setColour (value_ > 1.0f ? juce::Colours::orangered : accent_);
        g.fillRect (r.removeFromBottom (r.getHeight() * normalized));
    }
private:
    juce::Colour accent_;
    float value_ = 0.0f;
};

class PadCell final : public juce::Component {
public:
    class Face final : public juce::Button {
    public:
        Face() : juce::Button ("Audition pad") { setTriggeredOnMouseDown (true); }
        bool list = false;
        float activity = 0;
        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            const auto bounds = getLocalBounds().toFloat().reduced (1);
            g.setColour (juce::Colours::black.withAlpha (0.6f)); g.fillRoundedRectangle (bounds, 4.0f);
            const auto rubber = bounds.reduced (2).translated (0, down ? 1.0f : -1.0f);
            g.setGradientFill (juce::ColourGradient (over ? c::panelRaised.brighter (0.2f) : c::panelRaised,
                                                    rubber.getTopLeft(), c::control, rubber.getBottomRight(), false));
            g.fillRoundedRectangle (rubber, 4.0f);
            // Binary activity from the real hit mask; never labelled velocity.
            g.setColour (c::accent.withAlpha (activity * 0.2f)); g.fillRoundedRectangle (rubber, 4.0f);
            g.setColour (getToggleState() ? c::accent : c::border);
            g.drawRoundedRectangle (bounds, 4.0f, getToggleState() ? 2.0f : 1.0f);
            g.setColour (c::text);
            g.setFont (vox::ui::typography::valueText());
            g.drawFittedText (getButtonText(), getLocalBounds().reduced (3), juce::Justification::centred, list ? 1 : 3);
        }
    };
    PadCell (DrumsProcessor& p, int index) : processor_ (p), pad_ (index), peak_ (p.product().accent)
    {
        select.setButtonText ("SEL");
        select.setTooltip ("Select pad; does not play audio");
        hit.setTooltip ("Audition at velocity 110; does not change selection");
        addAndMakeVisible (select); addAndMakeVisible (hit); addAndMakeVisible (peak_);
    }
    void update (bool selected, float peak, bool hitNow)
    {
        select.setToggleState (selected, juce::dontSendNotification);
        hit.setToggleState (selected, juce::dontSendNotification);
        refreshLabel();
        peak_.push (peak);
        active_ = hitNow ? 1.0f : active_ * 0.72f;
        hit.activity = active_; hit.repaint();
        repaint();
    }
    void paint (juce::Graphics& g) override
    {
        g.setColour (active_ > 0.1f ? processor_.product().accent.withAlpha (active_ * 0.35f) : c::panel);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
    }
    void resized() override
    {
        hit.list = list;
        refreshLabel();
        auto r = getLocalBounds().reduced (2);
        peak_.setBounds (r.removeFromRight (4)); r.removeFromRight (2);
        if (list)
        {
            select.setBounds (r.removeFromRight (36));
            hit.setBounds (r);
        }
        else
        {
            select.setBounds (r.removeFromBottom (24));
            hit.setBounds (r);
        }
    }
    bool list = false;
    juce::TextButton select;
    Face hit;
private:
    void refreshLabel()
    {
        const auto pad = processor_.kitStore().pad (pad_);
        const auto separator = list ? " " : "\n";
        hit.setButtonText (juce::String (pad_ + 1) + separator + modelName (pad.model) + separator + juce::String (pad.note));
    }
    DrumsProcessor& processor_;
    int pad_;
    Peak peak_;
    float active_ = 0.0f;
};

class Inspector final : public juce::Component {
public:
    explicit Inspector (DrumsProcessor& p) : processor_ (p)
    {
        addAndMakeVisible (title); addAndMakeVisible (pin); addAndMakeVisible (mute); addAndMakeVisible (solo);
        for (auto* combo : { &model, &note, &output }) addAndMakeVisible (*combo);
        for (int i = 0; i < kit::modelCount; ++i) model.addItem (modelName (static_cast<kit::Model> (i)), i + 1);
        for (int i = 0; i < 128; ++i) note.addItem ("Note " + juce::String (i), i + 1);
        for (int i = 0; i < 9; ++i) output.addItem (i == 0 ? "Main" : "Out " + juce::String (i + 1), i + 1);
        model.setTooltip ("Pad synthesis model / KitStore"); note.setTooltip ("Pad MIDI note / KitStore");
        output.setTooltip ("Requested output / disabled buses fall back to Main");
        model.onChange = [this] {
            auto pad = processor_.kitStore().pad (pad_);
            pad.model = static_cast<kit::Model> (model.getSelectedId() - 1);
            processor_.kitStore().setPad (pad_, pad);
        };
        note.onChange = [this] {
            auto pad = processor_.kitStore().pad (pad_);
            pad.note = static_cast<std::uint8_t> (note.getSelectedId() - 1);
            processor_.kitStore().setPad (pad_, pad);
        };
        output.onChange = [this] {
            auto pad = processor_.kitStore().pad (pad_);
            pad.output = static_cast<std::uint8_t> (output.getSelectedId() - 1);
            processor_.kitStore().setPad (pad_, pad);
        };
        pin.setButtonText ("Pin pad"); pin.setClickingTogglesState (true);
        mute.setButtonText ("Mute"); solo.setButtonText ("Solo");
        bind (0);
    }
    void bind (int pad)
    {
        if (pad_ == pad) return;
        // A selection change during an active drag must not redirect that drag
        // to another parameter. Defer the entire rebind until mouse-up.
        for (const auto& k : knobs)
            if (k.dragging) return;
        muteAttachment.reset(); soloAttachment.reset();
        pad_ = pad;
        const auto prefix = juce::String (padPrefix (pad));
        for (std::size_t i = 0; i < knobs.size(); ++i)
            knobs[i].bind (*this, processor_, prefix + "." + names[i], labels[i], vox::ui::VoxKnob::Size::Normal);
        muteAttachment = std::make_unique<ButtonAttachment> (processor_.state(), prefix + ".mute", mute);
        soloAttachment = std::make_unique<ButtonAttachment> (processor_.state(), prefix + ".solo", solo);
    }
    void update (int pad, bool pinned)
    {
        bind (pad);
        title.setText ("INSPECTOR / PAD " + juce::String (pad_ + 1) + (pinned ? " / PINNED" : " / FOLLOW"), juce::dontSendNotification);
        pin.setToggleState (pinned, juce::dontSendNotification);
        const auto settings = processor_.kitStore().pad (pad_);
        model.setSelectedId (static_cast<int> (settings.model) + 1, juce::dontSendNotification);
        note.setSelectedId (settings.note + 1, juce::dontSendNotification);
        output.setSelectedId (settings.output + 1, juce::dontSendNotification);
    }
    void paint (juce::Graphics& g) override { g.fillAll (c::panel); }
    void resized() override
    {
        auto r = getLocalBounds().reduced (8);
        title.setBounds (r.removeFromTop (24)); pin.setBounds (r.removeFromTop (24)); r.removeFromTop (8);
        for (int row = 0; row < 2; ++row)
        {
            auto cells = r.removeFromTop (getHeight() >= 328 ? 86 : 76);
            knobs[static_cast<std::size_t> (row * 2)].control->setBounds (cells.removeFromLeft (cells.getWidth() / 2));
            knobs[static_cast<std::size_t> (row * 2 + 1)].control->setBounds (cells);
        }
        auto buttons = r.removeFromTop (26);
        mute.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2)); solo.setBounds (buttons);
        r.removeFromTop (6);
        model.setBounds (r.removeFromTop (24));
        auto structural = r.removeFromTop (24);
        note.setBounds (structural.removeFromLeft (structural.getWidth() / 2)); output.setBounds (structural);
    }
    static constexpr const char* names[4] { "level", "tune", "decay", "pan" };
    static constexpr const char* labels[4] { "Level", "Tune", "Decay", "Pan" };
    std::array<Knob, 4> knobs;
    juce::Label title;
    juce::ToggleButton pin, mute, solo;
    juce::ComboBox model, note, output;
private:
    DrumsProcessor& processor_;
    int pad_ = -1;
    std::unique_ptr<ButtonAttachment> muteAttachment, soloAttachment;
};

class MixerStrip final : public juce::Component {
public:
    MixerStrip (DrumsProcessor& p, int pad) : processor_ (p), pad_ (pad), peak_ (p.product().accent)
    {
        const auto id = juce::String (padPrefix (pad));
        addAndMakeVisible (title_); addAndMakeVisible (output_); addAndMakeVisible (level_);
        addAndMakeVisible (peak_); addAndMakeVisible (mute_); addAndMakeVisible (solo_);
        level_.setSliderStyle (juce::Slider::LinearVertical); level_.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        levelAttachment_ = std::make_unique<SliderAttachment> (p.state(), id + ".level", level_);
        mute_.setButtonText ("M"); solo_.setButtonText ("S");
        muteAttachment_ = std::make_unique<ButtonAttachment> (p.state(), id + ".mute", mute_);
        soloAttachment_ = std::make_unique<ButtonAttachment> (p.state(), id + ".solo", solo_);
        pan_.bind (*this, p, id + ".pan", "Pan");
        send1_.bind (*this, p, id + ".send1", "Send 1"); send2_.bind (*this, p, id + ".send2", "Send 2");
    }
    void update (float peak, std::uint32_t outputs)
    {
        peak_.push (peak);
        const auto pad = processor_.kitStore().pad (pad_);
        title_.setText (juce::String (pad_ + 1) + " " + modelName (pad.model), juce::dontSendNotification);
        const auto destination = pad.output == 0 ? juce::String ("Main") : "Out " + juce::String (pad.output + 1);
        output_.setText (destination + ((outputs & (1u << pad.output)) != 0 ? "" : ">Main"), juce::dontSendNotification);
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (2);
        title_.setBounds (r.removeFromTop (18)); output_.setBounds (r.removeFromTop (16));
        auto buttons = r.removeFromBottom (22);
        mute_.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2)); solo_.setBounds (buttons);
        if (getHeight() > 160)
        {
            auto sends = r.removeFromBottom (60);
            send1_.control->setBounds (sends.removeFromLeft (sends.getWidth() / 2)); send2_.control->setBounds (sends);
        }
        send1_.control->setVisible (getHeight() > 160); send2_.control->setVisible (getHeight() > 160);
        pan_.control->setBounds (r.removeFromRight (42));
        peak_.setBounds (r.removeFromRight (5)); level_.setBounds (r);
    }
private:
    DrumsProcessor& processor_;
    int pad_;
    juce::Label title_, output_;
    DrumsFader level_;
    juce::ToggleButton mute_, solo_;
    Peak peak_;
    Knob pan_, send1_, send2_;
    std::unique_ptr<SliderAttachment> levelAttachment_;
    std::unique_ptr<ButtonAttachment> muteAttachment_, soloAttachment_;
};
} // namespace

struct DrumsWorkspace::Impl {
    Impl (DrumsWorkspace& owner, DrumsProcessor& p)
        : processor (p), inspector (p), steps (p, p.product().accent), song (p, p.product().accent),
          sound (p, PadControlSurface::Group::sound), sample (p, PadControlSurface::Group::sample),
          mainLeft (p.product().accent), mainRight (p.product().accent)
    {
        owner.setLookAndFeel (&lookAndFeel);
        for (auto* component : std::initializer_list<juce::Component*> { &inspector, &rackViewport, &top, &list, &pin,
                 &split, &mixerSize, &primary, &secondary, &mixerTitle, &mixerViewport, &mainLeft, &mainRight })
            owner.addAndMakeVisible (*component);
        const std::array<juce::Component*, 4> editors { &steps, &song, &sound, &sample };
        for (std::size_t n = 0; n < editors.size(); ++n)
        {
            editorViews[n].setViewedComponent (editors[n], false);
            owner.addAndMakeVisible (editorViews[n]);
        }
        sample.onPlayerSelected = [this] (int player) { selectedPlayer = player; };
        rackViewport.setViewedComponent (&rackContent, false);
        rackViewport.setScrollBarsShown (true, false);
        top.setJustificationType (juce::Justification::centredLeft);
        list.setButtonText ("List"); list.setClickingTogglesState (true);
        list.onClick = [&owner, this] { owner.setListView (list.getToggleState()); };
        pin.setButtonText ("Pin"); pin.setClickingTogglesState (true);
        pin.onClick = [&owner, this] { owner.pinInspector (pin.getToggleState()); };
        inspector.pin.onClick = [&owner, this] { owner.pinInspector (inspector.pin.getToggleState()); };
        primary.addItemList ({ "Step", "Song", "Sound", "Sample" }, 1); primary.setSelectedId (1, juce::dontSendNotification);
        secondary.addItemList ({ "Step", "Song", "Sound", "Sample" }, 1); secondary.setSelectedId (2, juce::dontSendNotification);
        primary.setTooltip ("Primary central editor"); secondary.setTooltip ("Secondary central editor / available in split layouts");
        primary.onChange = [&owner, this] {
            const auto next = static_cast<workspace::Editor> (primary.getSelectedId() - 1);
            if (next == layout.secondary) layout.secondary = layout.primary;
            layout.primary = next;
            secondary.setSelectedId (static_cast<int> (layout.secondary) + 1, juce::dontSendNotification);
            owner.resized();
        };
        secondary.onChange = [&owner, this] {
            const auto next = static_cast<workspace::Editor> (secondary.getSelectedId() - 1);
            if (next == layout.primary) layout.primary = layout.secondary;
            layout.secondary = next;
            primary.setSelectedId (static_cast<int> (layout.primary) + 1, juce::dontSendNotification);
            owner.resized();
        };
        split.addItemList ({ "One editor", "Split across", "Split below" }, 1); split.setSelectedId (1, juce::dontSendNotification);
        split.onChange = [&owner, this] { layout.split = static_cast<workspace::Split> (split.getSelectedId() - 1); owner.resized(); };
        mixerSize.addItemList ({ "Mixer closed", "Mixer compact", "Mixer expanded" }, 1); mixerSize.setSelectedId (2, juce::dontSendNotification);
        mixerSize.onChange = [&owner, this] { layout.mixer = static_cast<workspace::MixerSize> (mixerSize.getSelectedId() - 1); owner.resized(); };
        song.onEditBar = [this] (int bar) { steps.editSongBar (bar); };
        for (int i = 0; i < 16; ++i)
        {
            auto& pad = pads[static_cast<std::size_t> (i)];
            pad = std::make_unique<PadCell> (p, i); rackContent.addAndMakeVisible (*pad);
            pad->select.onClick = [&owner, i] { owner.selectPad (i); };
            pad->hit.onClick = [this, i] { auditionRefused = ! processor.audition (i, 110); };
            auto& strip = strips[static_cast<std::size_t> (i)];
            strip = std::make_unique<MixerStrip> (p, i); mixerContent.addAndMakeVisible (*strip);
        }
        mixerViewport.setViewedComponent (&mixerContent, false);
        mixerViewport.setScrollBarsShown (false, true);
        kitLevel.bind (owner, p, "drm.kit.volume", "Kit Level");
        kitLevel.control->setTooltip ("All outputs / new triggers: pre-routing kit level, not a Main-only fader");
    }
    ~Impl()
    {
        mixerViewport.setViewedComponent (nullptr, false);
        for (auto& viewport : editorViews) viewport.setViewedComponent (nullptr, false);
        rackViewport.setViewedComponent (nullptr, false);
    }
    DrumsProcessor& processor;
    vox::ui::VoxLookAndFeel lookAndFeel;
    workspace::Selection selection;
    workspace::Layout layout;
    enum class Divider { none, left, right, across, below };
    Divider drag = Divider::none;
    Divider dividerAt (int width, int height, juce::Point<int> point) const
    {
        const auto r = workspace::arrange (width, height, layout);
        if (point.y < r.left.y || point.y >= r.left.y + r.left.h) return Divider::none;
        if (point.x >= r.left.w && point.x < r.primary.x) return Divider::left;
        if (point.x >= r.right.x - 6 && point.x < r.right.x) return Divider::right;
        if (! r.secondary.empty() && layout.split == workspace::Split::horizontal
            && point.x >= r.primary.x + r.primary.w && point.x < r.secondary.x) return Divider::across;
        if (! r.secondary.empty() && layout.split == workspace::Split::vertical
            && point.x >= r.primary.x && point.x < r.primary.x + r.primary.w
            && point.y >= r.primary.y + r.primary.h && point.y < r.secondary.y) return Divider::below;
        return Divider::none;
    }
    Telemetry telemetry;
    bool listView = false, auditionRefused = false;
    int selectedPlayer = 0;
    Inspector inspector;
    PatternEditor steps;
    SongStrip song;
    PadControlSurface sound, sample;
    std::array<std::unique_ptr<PadCell>, 16> pads;
    std::array<std::unique_ptr<MixerStrip>, 16> strips;
    juce::Label top, mixerTitle;
    juce::TextButton list, pin;
    juce::ComboBox split, mixerSize, primary, secondary;
    juce::Viewport mixerViewport;
    juce::Component mixerContent;
    std::array<juce::Viewport, 4> editorViews;
    juce::Viewport rackViewport;
    juce::Component rackContent;
    Knob kitLevel;
    Peak mainLeft, mainRight;
};

DrumsWorkspace::DrumsWorkspace (DrumsProcessor& p) : impl_ (std::make_unique<Impl> (*this, p))
{
    timerCallback(); startTimerHz (30);
}
DrumsWorkspace::~DrumsWorkspace() { stopTimer(); setLookAndFeel (nullptr); }
void DrumsWorkspace::paint (juce::Graphics& g) { g.fillAll (c::background); }
const workspace::Selection& DrumsWorkspace::selection() const noexcept { return impl_->selection; }
const DrumsWorkspace::Telemetry& DrumsWorkspace::telemetry() const noexcept { return impl_->telemetry; }
void DrumsWorkspace::selectPad (int pad)
{
    if (! impl_->selection.selectPad (pad)) return;
    impl_->steps.selectCell (pad, impl_->steps.selectedStep());
    impl_->inspector.update (impl_->selection.inspectorPad(), impl_->selection.pinned());
    impl_->sound.setContext (pad, impl_->selectedPlayer);
    impl_->sample.setContext (pad, impl_->selectedPlayer);
    // Do not consume telemetry merely because selection changed.
    for (int i = 0; i < 16; ++i)
    {
        impl_->pads[static_cast<std::size_t> (i)]->select.setToggleState (i == pad, juce::dontSendNotification);
        impl_->pads[static_cast<std::size_t> (i)]->hit.setToggleState (i == pad, juce::dontSendNotification);
    }
}
void DrumsWorkspace::pinInspector (bool pin)
{
    impl_->selection.setPinned (pin); impl_->pin.setToggleState (pin, juce::dontSendNotification);
    impl_->inspector.update (impl_->selection.inspectorPad(), pin);
}
void DrumsWorkspace::setListView (bool list)
{
    impl_->listView = list; impl_->list.setToggleState (list, juce::dontSendNotification); resized();
}
void DrumsWorkspace::setLayout (workspace::Layout layout)
{
    impl_->layout = layout;
    impl_->split.setSelectedId (static_cast<int> (layout.split) + 1, juce::dontSendNotification);
    impl_->mixerSize.setSelectedId (static_cast<int> (layout.mixer) + 1, juce::dontSendNotification);
    if (impl_->layout.primary == impl_->layout.secondary)
        impl_->layout.secondary = static_cast<workspace::Editor> ((static_cast<int> (impl_->layout.primary) + 1) % 4);
    impl_->primary.setSelectedId (static_cast<int> (impl_->layout.primary) + 1, juce::dontSendNotification);
    impl_->secondary.setSelectedId (static_cast<int> (impl_->layout.secondary) + 1, juce::dontSendNotification);
    resized();
}
workspace::Layout DrumsWorkspace::layout() const noexcept { return impl_->layout; }
juce::Button& DrumsWorkspace::padSelectButton (int i) { return impl_->pads.at (static_cast<std::size_t> (i))->select; }
juce::Button& DrumsWorkspace::padHitButton (int i) { return impl_->pads.at (static_cast<std::size_t> (i))->hit; }
juce::Slider* DrumsWorkspace::inspectorSlider (const juce::String& suffix)
{
    for (std::size_t i = 0; i < impl_->inspector.knobs.size(); ++i)
        if (suffix == Inspector::names[i]) return &impl_->inspector.knobs[i].control->getSlider();
    return nullptr;
}
juce::Button* DrumsWorkspace::inspectorButton (const juce::String& suffix)
{
    if (suffix == "mute") return &impl_->inspector.mute;
    if (suffix == "solo") return &impl_->inspector.solo;
    return nullptr;
}
juce::ComboBox* DrumsWorkspace::inspectorCombo (const juce::String& name)
{
    if (name == "model") return &impl_->inspector.model;
    if (name == "note") return &impl_->inspector.note;
    if (name == "output") return &impl_->inspector.output;
    return nullptr;
}
void DrumsWorkspace::pollForTesting() { timerCallback(); }
void DrumsWorkspace::mouseDown (const juce::MouseEvent& e)
{
    impl_->drag = impl_->dividerAt (getWidth(), getHeight(), e.getPosition());
}
void DrumsWorkspace::mouseUp (const juce::MouseEvent&) { impl_->drag = Impl::Divider::none; }
void DrumsWorkspace::mouseMove (const juce::MouseEvent& e)
{
    const auto target = impl_->dividerAt (getWidth(), getHeight(), e.getPosition());
    setMouseCursor (target == Impl::Divider::none ? juce::MouseCursor::NormalCursor
                   : target == Impl::Divider::below ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::LeftRightResizeCursor);
}
void DrumsWorkspace::mouseDrag (const juce::MouseEvent& e)
{
    auto& i = *impl_;
    const auto r = workspace::arrange (getWidth(), getHeight(), i.layout);
    switch (i.drag)
    {
        case Impl::Divider::left: i.layout.leftWidth = juce::jlimit (160, 280, e.x); break;
        case Impl::Divider::right: i.layout.rightWidth = juce::jlimit (200, 300, getWidth() - e.x); break;
        case Impl::Divider::across:
            i.layout.ratio = static_cast<float> (e.x - r.primary.x) / static_cast<float> (r.primary.w + r.secondary.w); break;
        case Impl::Divider::below:
            i.layout.ratio = static_cast<float> (e.y - r.primary.y) / static_cast<float> (r.primary.h + r.secondary.h); break;
        case Impl::Divider::none: return;
    }
    i.layout.ratio = juce::jlimit (0.25f, 0.75f, i.layout.ratio);
    resized();
}
void DrumsWorkspace::timerCallback()
{
    auto& i = *impl_;
    // Exactly one consumer per timer tick, shared by every view. Paint and
    // selection never exchange processor meters. Hit mask carries no velocity.
    i.telemetry.hits = i.processor.takeHitMask();
    i.telemetry.outputs = i.processor.activeOutputs();
    for (int pad = 0; pad < 16; ++pad)
    {
        const auto n = static_cast<std::size_t> (pad);
        i.telemetry.pads[n] = i.processor.takePadMeter (pad);
        i.pads[n]->update (i.selection.pad() == pad, i.telemetry.pads[n], (i.telemetry.hits & (1u << pad)) != 0);
        i.strips[n]->update (i.telemetry.pads[n], i.telemetry.outputs);
    }
    for (int ch = 0; ch < 2; ++ch) i.telemetry.main[static_cast<std::size_t> (ch)] = i.processor.takeMainMeter (ch);
    i.mainLeft.push (i.telemetry.main[0]); i.mainRight.push (i.telemetry.main[1]);
    i.steps.update(); i.song.update();
    if (i.steps.selectedLane() != i.selection.pad()) i.selection.selectPad (i.steps.selectedLane());
    i.inspector.update (i.selection.inspectorPad(), i.selection.pinned());
    i.sound.setContext (i.selection.pad(), i.selectedPlayer);
    i.sample.setContext (i.selection.pad(), i.selectedPlayer);
    i.top.setText (juce::String (i.processor.transportPlaying() ? "HOST PLAYING / " : "HOST STOPPED / ")
                   + juce::String (i.processor.lastBpm(), 1) + " BPM" + (i.auditionRefused ? " / audition queue full" : ""), juce::dontSendNotification);
}
void DrumsWorkspace::resized()
{
    auto& i = *impl_;
    const auto boxes = workspace::arrange (getWidth(), getHeight(), i.layout);
    auto top = rectangle (boxes.top);
    i.mixerSize.setBounds (top.removeFromRight (146).reduced (2));
    i.split.setBounds (top.removeFromRight (132).reduced (2));
    i.primary.setBounds (top.removeFromRight (88).reduced (2));
    i.secondary.setBounds (top.removeFromRight (88).reduced (2));
    i.secondary.setEnabled (i.layout.split != workspace::Split::single);
    i.pin.setBounds (top.removeFromRight (48).reduced (2));
    i.list.setBounds (top.removeFromRight (48).reduced (2)); i.top.setBounds (top);
    i.rackViewport.setBounds (rectangle (boxes.left));
    i.rackContent.setSize (std::max (0, boxes.left.w - i.rackViewport.getScrollBarThickness()),
                           i.listView ? std::max (boxes.left.h, 16 * 32) : boxes.left.h);
    const auto rack = i.rackContent.getLocalBounds();
    for (int pad = 0; pad < 16; ++pad)
    {
        auto& cell = *i.pads[static_cast<std::size_t> (pad)];
        cell.list = i.listView;
        const int columns = i.listView ? 1 : 4, rows = 16 / columns;
        const int x1 = rack.getX() + pad % columns * rack.getWidth() / columns;
        const int x2 = rack.getX() + (pad % columns + 1) * rack.getWidth() / columns;
        const int y1 = rack.getY() + pad / columns * rack.getHeight() / rows;
        const int y2 = rack.getY() + (pad / columns + 1) * rack.getHeight() / rows;
        cell.setBounds (juce::Rectangle<int> (x1, y1, x2 - x1, y2 - y1).reduced (1));
        cell.resized();
    }
    i.inspector.setBounds (rectangle (boxes.right));
    const std::array<juce::Component*, 4> editors { &i.steps, &i.song, &i.sound, &i.sample };
    const std::array widths { 980, 1100, 720, 720 }, heights { 320, 240, 470, 560 };
    for (std::size_t n = 0; n < editors.size(); ++n)
    {
        const auto editor = static_cast<workspace::Editor> (n);
        const bool isPrimary = editor == i.layout.primary, isSecondary = editor == i.layout.secondary && ! boxes.secondary.empty();
        auto& viewport = i.editorViews[n];
        viewport.setVisible (isPrimary || isSecondary);
        if (! viewport.isVisible()) continue;
        viewport.setBounds (rectangle (isPrimary ? boxes.primary : boxes.secondary));
        // Preserve full donor controls in scrollable canvases during the port;
        // no clipping/scaling away of controls or duplicate editor instances.
        editors[n]->setSize (std::max (widths[n], viewport.getWidth() - 14), std::max (heights[n], viewport.getHeight() - 14));
    }
    auto mixer = rectangle (boxes.mixer);
    i.mixerTitle.setText ("MIXER / 16 PADS / 9 OUTPUTS / Main L-R", juce::dontSendNotification);
    i.mixerTitle.setBounds (mixer.removeFromTop (24));
    const bool open = i.layout.mixer != workspace::MixerSize::collapsed;
    i.mixerViewport.setVisible (open); i.kitLevel.control->setVisible (open);
    i.mainLeft.setVisible (open); i.mainRight.setVisible (open);
    i.mainRight.setBounds (mixer.removeFromRight (8).reduced (1));
    i.mainLeft.setBounds (mixer.removeFromRight (8).reduced (1));
    i.kitLevel.control->setBounds (mixer.removeFromRight (78));
    i.mixerViewport.setBounds (mixer);
    i.mixerContent.setSize (16 * 84, std::max (0, mixer.getHeight() - i.mixerViewport.getScrollBarThickness()));
    for (int pad = 0; pad < 16; ++pad)
        i.strips[static_cast<std::size_t> (pad)]->setBounds (pad * 84, 0, 84, i.mixerContent.getHeight());
}
} // namespace vox::drums
