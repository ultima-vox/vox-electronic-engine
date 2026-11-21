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
#include "DrumsRolesPanel.h"
#include "WorkspaceLibrary.h"
#include "DrumsDockZone.h"
#include "VoxLookAndFeel.h"

#include <cmath>
#include <vector>

namespace vox::drums {
namespace {
namespace c = vox::ui::tokens::colour;
using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

juce::Rectangle<int> rectangle (workspace::Box b) { return { b.x, b.y, b.w, b.h }; }

void putLayoutRecord (layout::Document& document, layout::Type type, layout::Bytes bytes)
{
    const auto id = static_cast<layout::Byte> (type);
    const auto at = std::lower_bound (document.records.begin(), document.records.end(), id,
                                     [] (const auto& r, auto value) { return r.type < value; });
    if (at != document.records.end() && at->type == id) at->payload = std::move (bytes);
    else document.records.insert (at, { id, std::move (bytes) });
}
void layoutWord (layout::Bytes& bytes, std::size_t at, int value)
{
    bytes[at] = static_cast<layout::Byte> (value); bytes[at + 1] = static_cast<layout::Byte> (value >> 8);
}
int layoutWord (const layout::Bytes& bytes, std::size_t at)
{
    return static_cast<int> (bytes[at]) | (static_cast<int> (bytes[at + 1]) << 8);
}

struct Knob {
    bool dragging = false;
    juce::String parameterId;
    std::unique_ptr<DrumsDial> control;
    std::unique_ptr<SliderAttachment> attachment;
    void bind (juce::Component& parent, DrumsProcessor& p, const juce::String& id, const juce::String& label,
               vox::ui::VoxKnob::Size size = vox::ui::VoxKnob::Size::Small)
    {
        parameterId = id;
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
        attachment = std::make_unique<SliderAttachment> (p.state(), id, control->getSlider());
        control->setTooltip (parameter->getName (64));
        control->getSlider().setName (parameter->getName (64));
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

class StepInspector final : public juce::Component {
public:
    explicit StepInspector (DrumsProcessor& processor) : processor_ (processor)
    {
        addAndMakeVisible (description_); description_.setJustificationType (juce::Justification::topLeft);
        static constexpr const char* labels[] { "Velocity", "Chance", "Ratchet", "Micro" };
        static constexpr double low[] { 0, 1, 1, -50 }, high[] { 127, 100, 8, 50 };
        for (unsigned i = 0; i < knobs_.size(); ++i)
        {
            knobs_[i] = std::make_unique<DrumsDial> (labels[i]); addAndMakeVisible (*knobs_[i]);
            if (i == 3) knobs_[i]->setStyle (vox::ui::VoxKnob::Style::Bipolar);
            auto& slider = knobs_[i]->getSlider(); slider.setRange (low[i], high[i], 1);
            slider.textFromValueFunction = [this, i] (double value) {
                if (mixed_[i]) return juce::String ("Mixed");
                const auto n = juce::roundToInt (value);
                return juce::String (n) + (i == 1 || i == 3 ? " %" : i == 2 ? "x" : "");
            };
            slider.valueFromTextFunction = [&slider] (const juce::String& value) {
                return value.trim().equalsIgnoreCase ("Mixed") ? slider.getValue() : value.getDoubleValue();
            };
            slider.onDragStart = [this, i] {
                dragging_ = abandoned_ = amended_ = false; activeField_ = -1;
                update (true); dragging_ = true; activeField_ = static_cast<int> (i);
            };
            slider.onDragEnd = [this] { dragging_ = abandoned_ = amended_ = false; activeField_ = -1; update (true); };
            slider.onValueChange = [this, i, &slider] { apply (static_cast<StepField> (i), slider.getValue()); };
        }
        const std::array fields { StepField::on, StepField::accent, StepField::locked };
        for (unsigned i = 0; i < toggles_.size(); ++i)
        {
            addAndMakeVisible (toggles_[i]);
            toggles_[i].onClick = [this, i, field = fields[i]] { apply (field, toggles_[i].getToggleState() ? 1 : 0); };
        }
    }
    void setTarget (std::optional<StepTarget> target) { requested_ = std::move (target); update(); }
    const std::optional<StepTarget>& target() const noexcept { return bound_; }
    bool available() const noexcept { return values_ && ! values_->cells.empty() && ! abandoned_; }
    bool dragging() const noexcept { return dragging_; }
    void abandon() { if (dragging_) { abandoned_ = true; description_.setText ("Gesture canceled after context changed", juce::dontSendNotification); } }
    void update (bool force = false)
    {
        if (dragging_)
        {
            if (! values_ || values_->epoch != processor_.patternEpoch() || values_->generation != processor_.patterns().generation()
                || processor_.restoringMusic()) abandon();
            return;
        }
        const auto generation = processor_.patterns().generation(), epoch = processor_.patternEpoch();
        const auto restoring = processor_.restoringMusic();
        if (! force && bound_ == requested_ && generation == lastGeneration_ && epoch == lastEpoch_ && restoring == lastRestoring_) return;
        bound_ = requested_; lastGeneration_ = generation; lastEpoch_ = epoch; lastRestoring_ = restoring;
        values_ = bound_ ? inspectSteps (processor_, *bound_) : std::optional<StepValues> {};
        refresh();
    }
    juce::Slider* slider (const juce::String& name)
    {
        const std::array<const char*, 4> names { "velocity", "probability", "ratchet", "micro" };
        for (unsigned i = 0; i < names.size(); ++i) if (name == names[i]) return &knobs_[i]->getSlider();
        return nullptr;
    }
    juce::Button* button (const juce::String& name)
    {
        if (name == "stepOn") return &toggles_[0];
        if (name == "accent") return &toggles_[1];
        if (name == "locked") return &toggles_[2];
        return nullptr;
    }
    void resized() override
    {
        auto r = getLocalBounds(); description_.setBounds (r.removeFromTop (44));
        for (int row = 0; row < 2; ++row)
        {
            auto line = r.removeFromTop (86);
            knobs_[static_cast<unsigned> (row * 2)]->setBounds (line.removeFromLeft (line.getWidth() / 2));
            knobs_[static_cast<unsigned> (row * 2 + 1)]->setBounds (line);
        }
        auto line = r.removeFromTop (26);
        toggles_[0].setBounds (line.removeFromLeft (line.getWidth() / 2)); toggles_[1].setBounds (line);
        toggles_[2].setBounds (r.removeFromTop (26));
    }
private:
    void apply (StepField field, double value)
    {
        if (refreshing_ || abandoned_ || ! bound_ || ! values_
            || (dragging_ && activeField_ != static_cast<int> (field))) return;
        if (editSteps (processor_, *bound_, field, value, *values_, dragging_ && amended_))
        {
            if (dragging_) amended_ = true;
            values_ = inspectSteps (processor_, *bound_);
            lastGeneration_ = values_ ? values_->generation : ~std::uint64_t {};
            lastEpoch_ = values_ ? values_->epoch : ~std::uint64_t {};
            if (dragging_ && ! values_) abandon();
            else refresh();
        }
        else if (dragging_) update();
        else update (true);
    }
    void refresh()
    {
        const juce::ScopedValueSetter<bool> guard (refreshing_, true);
        const bool available = values_ && ! values_->cells.empty();
        bool allOn = available;
        if (available) for (const auto& cell : values_->cells) allOn = allOn && cell.on;
        for (unsigned field = 0; field < knobs_.size(); ++field)
        {
            mixed_[field] = false;
            if (available)
            {
                const int value = stepValue (static_cast<StepField> (field), values_->cells[0], values_->ticks[0]);
                for (unsigned n = 1; n < values_->cells.size(); ++n)
                    mixed_[field] = mixed_[field] || value != stepValue (static_cast<StepField> (field), values_->cells[n], values_->ticks[n]);
                knobs_[field]->getSlider().setValue (value, juce::dontSendNotification);
            }
            knobs_[field]->setEnabled (available && allOn && (! dragging_ || activeField_ == static_cast<int> (field)));
            knobs_[field]->getSlider().updateText();
        }
        const std::array fields { StepField::on, StepField::accent, StepField::locked };
        static constexpr const char* names[] { "On", "Accent", "Lock" };
        for (unsigned i = 0; i < toggles_.size(); ++i)
        {
            bool mixed = false; int value = 0;
            if (available)
            {
                value = stepValue (fields[i], values_->cells[0], values_->ticks[0]);
                for (unsigned n = 1; n < values_->cells.size(); ++n)
                    mixed = mixed || value != stepValue (fields[i], values_->cells[n], values_->ticks[n]);
            }
            toggles_[i].setButtonText (juce::String (names[i]) + (mixed ? " (mixed)" : ""));
            toggles_[i].setToggleState (value != 0, juce::dontSendNotification);
            toggles_[i].setEnabled (available && (i != 1 || allOn) && ! dragging_);
        }
        juce::String text = "Steps unavailable; unpin or select a valid object";
        if (available)
        {
            const auto& t = *bound_;
            text = t.song >= 0 ? "Song bar " + juce::String (t.song + 1) : "P" + juce::String (t.slot + 1) + (t.variation ? " B" : " A") + " bar " + juce::String (t.bar + 1);
            text << " / Pad " << t.lane + 1 << "\nStep " << t.first + 1;
            if (t.last != t.first) text << "-" << t.last + 1;
        }
        description_.setText (text, juce::dontSendNotification);
    }
    DrumsProcessor& processor_;
    std::optional<StepTarget> requested_, bound_;
    std::optional<StepValues> values_;
    std::array<std::unique_ptr<DrumsDial>, 4> knobs_;
    std::array<bool, 4> mixed_ {};
    std::array<juce::ToggleButton, 3> toggles_;
    juce::Label description_;
    std::uint64_t lastGeneration_ = ~std::uint64_t {}, lastEpoch_ = ~std::uint64_t {};
    int activeField_ = -1;
    bool lastRestoring_ = false, refreshing_ = false, dragging_ = false, abandoned_ = false, amended_ = false;
};

class Inspector final : public juce::Component {
public:
    explicit Inspector (DrumsProcessor& p) : processor_ (p), stepControls_ (p)
    {
        addChildComponent (stepControls_);
        for (auto* component : std::initializer_list<juce::Component*> { &songDescription_, &editSource_, &editBar_ }) addChildComponent (*component);
        editSource_.setButtonText ("Edit Source Pattern"); editBar_.setButtonText ("Edit This Bar");
        editSource_.onClick = [this] { if (editSource_.isEnabled() && onEditSong) onEditSong (songBar_, true); };
        editBar_.onClick = [this] { if (editBar_.isEnabled() && onEditSong) onEditSong (songBar_, false); };
        addAndMakeVisible (title); addAndMakeVisible (pin); addAndMakeVisible (mute); addAndMakeVisible (solo);
        addAndMakeVisible (openSound);
        openSound.setButtonText ("Open Sound"); openSound.setTooltip ("Open Sound Editor for this Inspector's pad");
        for (auto* combo : { &model, &note, &output }) addAndMakeVisible (*combo);
        for (int i = 0; i < kit::modelCount; ++i) model.addItem (modelName (static_cast<kit::Model> (i)), i + 1);
        for (int i = 0; i < 128; ++i) note.addItem ("Note " + juce::String (i), i + 1);
        for (int i = 0; i < 9; ++i) output.addItem (i == 0 ? "Main" : "Out " + juce::String (i + 1), i + 1);
        model.setTooltip ("Pad synthesis model"); note.setTooltip ("Pad MIDI note");
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
        if (stepMode_ && stepControls_.dragging()) return;
        const bool changed = stepMode_ || songMode_;
        songMode_ = false; songVisible (false);
        stepMode_ = false; stepControls_.setTarget ({}); stepControls_.setVisible (false); padVisible (true); openSound.setEnabled (true); pin.setEnabled (true);
        pin.setButtonText ("Pin pad");
        bind (pad);
        title.setText ("INSPECTOR / PAD " + juce::String (pad_ + 1) + (pinned ? " / PINNED" : " / FOLLOW"), juce::dontSendNotification);
        pin.setToggleState (pinned, juce::dontSendNotification);
        const auto settings = processor_.kitStore().pad (pad_);
        model.setSelectedId (static_cast<int> (settings.model) + 1, juce::dontSendNotification);
        note.setSelectedId (settings.note + 1, juce::dontSendNotification);
        output.setSelectedId (settings.output + 1, juce::dontSendNotification);
        if (changed) resized();
    }
    void showSteps (std::optional<StepTarget> target, bool pinned)
    {
        for (const auto& knob : knobs) if (knob.dragging) return;
        const bool changed = ! stepMode_ || songMode_;
        songMode_ = false; songVisible (false);
        stepMode_ = true; padVisible (false); stepControls_.setVisible (true); stepControls_.setTarget (target);
        const bool available = stepControls_.available();
        title.setText (available ? "INSPECTOR / STEPS" : pinned ? "PINNED / UNAVAILABLE" : "STEPS / UNAVAILABLE", juce::dontSendNotification);
        pin.setToggleState (pinned, juce::dontSendNotification);
        pin.setButtonText (pinned ? "Unpin" : target && target->song >= 0 ? "Pin unavailable" : "Pin steps");
        pin.setEnabled (pinned || (available && target && target->song < 0));
        openSound.setEnabled (available);
        if (changed) resized();
    }
    bool showingSteps() const noexcept { return stepMode_; }
    bool showingSong() const noexcept { return songMode_; }
    int songTarget() const noexcept { return songBar_; }
    std::function<void (int, bool)> onEditSong;
    juce::Button* songButton (const juce::String& name)
    {
        if (! songMode_) return nullptr;
        if (name == "editSource") return &editSource_;
        if (name == "editThisBar") return &editBar_;
        return nullptr;
    }
    void showSong (int bar, bool pinned)
    {
        for (const auto& knob : knobs) if (knob.dragging) return;
        if (stepControls_.dragging()) return;
        const bool changed = ! songMode_;
        songMode_ = true; stepMode_ = false; stepControls_.setTarget ({}); stepControls_.setVisible (false);
        padVisible (false); songVisible (true); songBar_ = bar;
        const auto song = processor_.patterns().song();
        const bool valid = ! processor_.restoringMusic() && bar >= 0 && bar < song.length;
        title.setText (valid ? "INSPECTOR / SONG BAR " + juce::String (bar + 1) : "SONG / UNAVAILABLE", juce::dontSendNotification);
        pin.setButtonText (pinned ? "Unpin" : "Pin Song bar"); pin.setEnabled (pinned || valid);
        pin.setToggleState (pinned, juce::dontSendNotification); openSound.setEnabled (false);
        juce::String description = "This bar is unavailable. Unpin or select a bar in the current Song.";
        bool linked = false;
        if (valid)
        {
            const auto& target = song.bars[static_cast<std::size_t> (bar)]; linked = target.kind == SongBar::Kind::pattern;
            description = linked ? "Linked: P" + juce::String (target.slot + 1) + (target.variation ? " B" : " A") + " / source bar " + juce::String (target.patternBar + 1)
                          : target.kind == SongBar::Kind::unique ? "Unique bar / independent cells" : "Empty bar";
            description << "\n" << (song.songMode ? "Song playback" : "Pattern playback") << " / " << song.length << " bars\n";
            description << (song.loopOn ? "Loop " + juce::String (song.loopStart + 1) + "-" + juce::String (song.loopStart + song.loopLength) : "Loop off");
            description << "\n\nSource: all linked occurrences follow.\nThis bar: stays linked until the first real edit.";
            if (linked) description << "\nReference bars wrap to the source length.";
        }
        songDescription_.setText (description, juce::dontSendNotification);
        editSource_.setEnabled (valid && linked); editBar_.setEnabled (valid);
        if (changed) resized();
    }
    bool stepAvailable() const noexcept { return stepControls_.available(); }
    const std::optional<StepTarget>& stepTarget() const noexcept { return stepControls_.target(); }
    int padTarget() const noexcept { return pad_; }
    juce::Slider* stepSlider (const juce::String& name) { return stepMode_ ? stepControls_.slider (name) : nullptr; }
    juce::Button* stepButton (const juce::String& name) { return stepMode_ ? stepControls_.button (name) : nullptr; }
    void paint (juce::Graphics& g) override { g.fillAll (c::panel); }
    void resized() override
    {
        auto r = getLocalBounds().reduced (8);
        title.setBounds (r.removeFromTop (24));
        auto actions = r.removeFromTop (24);
        pin.setBounds (actions.removeFromLeft (actions.getWidth() / 2)); openSound.setBounds (actions);
        r.removeFromTop (8);
        if (songMode_)
        {
            songDescription_.setBounds (r.removeFromTop (176));
            editSource_.setBounds (r.removeFromTop (30)); editBar_.setBounds (r.removeFromTop (30)); return;
        }
        if (stepMode_) { stepControls_.setBounds (r); return; }
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
    juce::TextButton openSound;
    juce::ComboBox model, note, output;
private:
    void songVisible (bool visible) { for (auto* component : std::initializer_list<juce::Component*> { &songDescription_, &editSource_, &editBar_ }) component->setVisible (visible); }
    void padVisible (bool visible)
    {
        for (auto& knob : knobs) { knob.control->setVisible (visible); knob.control->setEnabled (visible); }
        for (auto* control : std::initializer_list<juce::Component*> { &mute, &solo, &model, &note, &output })
        { control->setVisible (visible); control->setEnabled (visible); }
    }
    DrumsProcessor& processor_;
    StepInspector stepControls_;
    juce::Label songDescription_;
    juce::TextButton editSource_, editBar_;
    int songBar_ = -1;
    bool songMode_ = false;
    bool stepMode_ = false;
    int pad_ = -1;
    std::unique_ptr<ButtonAttachment> muteAttachment, soloAttachment;
};

class MixerStrip final : public juce::Component {
public:
    juce::Slider* slider (const juce::String& suffix)
    {
        if (suffix == "level") return &level_;
        if (suffix == "pan") return &pan_.control->getSlider();
        if (suffix == "send1") return &send1_.control->getSlider();
        if (suffix == "send2") return &send2_.control->getSlider();
        return nullptr;
    }
    juce::Button* button (const juce::String& suffix)
    {
        if (suffix == "mute") return &mute_;
        if (suffix == "solo") return &solo_;
        return nullptr;
    }
    MixerStrip (DrumsProcessor& p, int pad) : processor_ (p), pad_ (pad), peak_ (p.product().accent)
    {
        const auto id = juce::String (padPrefix (pad));
        addAndMakeVisible (title_); addAndMakeVisible (output_); addAndMakeVisible (level_);
        addAndMakeVisible (peak_); addAndMakeVisible (mute_); addAndMakeVisible (solo_);
        level_.setSliderStyle (juce::Slider::LinearVertical); level_.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        levelAttachment_ = std::make_unique<SliderAttachment> (p.state(), id + ".level", level_);
        level_.setName ("Pad " + juce::String (pad + 1) + " level");
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
        for (auto* component : std::initializer_list<juce::Component*> { &leftDock, &rightDock, &top, &list, &pin,
                 &workspaceMenu, &maximise, &split, &mixerSize, &mixerView, &mixerBank, &mode, &pattern, &primary, &secondary, &mixerTitle, &mixerViewport, &mainLeft, &mainRight })
            owner.addAndMakeVisible (*component);
        workspaceMenu.setButtonText ("Workspace");
        workspaceMenu.onClick = [&owner] { owner.showWorkspaceMenu(); };
        maximise.setName ("Focus central editor"); maximise.setButtonText ("Focus");
        maximise.setTooltip ("Focus the primary editor, or restore the split");
        maximise.onClick = [&owner, this] { layout.maximized = layout.maximized == 255 ? 0 : 255; owner.setLayout (layout); };
        const std::array<juce::Component*, 4> editors { &steps, &song, &sound, &sample };
        for (std::size_t n = 0; n < editors.size(); ++n)
        {
            editorViews[n].setViewedComponent (editors[n], false);
            owner.addAndMakeVisible (editorViews[n]);
        }
        sound.onPlayerSelected = sample.onPlayerSelected = [this] (int player) { selectedPlayer = player; };
        sound.onLayoutChanged = sample.onLayoutChanged = song.onLayoutChanged = [&owner] { owner.resized(); };
        steps.useFixedPatternExportScope();
        steps.useContextInspector();
        song.enableMidiExport ([this] { steps.saveExport (processor.captureExport (ExportSnapshot::Scope::song)); },
                               [this] { steps.dragExport (processor.captureExport (ExportSnapshot::Scope::song), song); },
                               [this] { return steps.exportMessage(); });
        toolPages[0] = makeGeneratorDock (p);
        toolPages[1] = makePresetsPage (p, [this] { return selection.pad(); });
        toolPages[2] = makeRoutingPage (p);
        toolPages[3] = makeAdvancedPage (p);
        for (auto& page : toolPages) page->onDockSizeChanged = [&owner] { owner.resized(); };
        const auto combo = [&p] (juce::ComboBox& box, const juce::String& id) {
            auto* parameter = dynamic_cast<juce::AudioParameterChoice*> (p.state().getParameter (id));
            jassert (parameter != nullptr);
            box.addItemList (parameter->choices, 1); box.setTooltip (parameter->getName (64));
            return std::make_unique<ComboAttachment> (p.state(), id, box);
        };
        modeAttachment = combo (mode, "drm.seq.mode"); patternAttachment = combo (pattern, patternParameterId);
        returnContent.addAndMakeVisible (delayTime);
        delayAttachment = combo (delayTime, "drm.fx.delay.time");
        static const std::array<const char*, 8> ids { "drm.fx.delay.feedback", "drm.fx.delay.tone", "drm.fx.delay.level",
            "drm.fx.reverb.size", "drm.fx.reverb.damp", "drm.fx.reverb.level", "drm.seq.swing", "drm.seq.accent" };
        static const std::array<const char*, 8> labels { "Delay FB", "Delay tone", "Delay return", "Reverb size", "Damp", "Reverb return", "Swing", "Accent" };
        for (std::size_t n = 0; n < returns.size(); ++n)
            returns[n].bind (returnContent, p, ids[n], labels[n], vox::ui::VoxKnob::Size::Normal);
        mixerView.addItemList ({ "Pads", "Returns / FX" }, 1); mixerView.setSelectedId (1, juce::dontSendNotification);
        mixerView.setName ("Mixer section");
        mixerBank.addItemList ({ "Pads 1-8", "Pads 9-16" }, 1); mixerBank.setSelectedId (1, juce::dontSendNotification);
        mixerBank.setName ("Mixer bank"); mixerBank.setTooltip ("Focus this pad bank in the single Mixer");
        mixerBank.onChange = [&owner, this] {
            if (applyingLayout) return;
            auto document = processor.layoutState().document;
            const auto* current = layout::find (document, layout::Type::mixer);
            auto mixer = current ? current->payload : layout::find (layout::factory (layout::Preset::beatmaking), layout::Type::mixer)->payload;
            mixer[5] = static_cast<layout::Byte> (mixerBank.getSelectedId() == 2);
            putLayoutRecord (document, layout::Type::mixer, std::move (mixer)); owner.commitDockDocument (std::move (document));
        };
        mixerView.onChange = [&owner, this] {
            owner.resized();
            if (mixerView.getSelectedId() == 1) mixerViewport.setViewPosition (mixerBank.getSelectedId() == 2 ? 8 * 84 : 0, 0);
        };
        rackViewport.setViewedComponent (&rackContent, false);
        rackViewport.setScrollBarsShown (true, false);
        top.setJustificationType (juce::Justification::centredLeft);
        list.setButtonText ("List"); list.setClickingTogglesState (true);
        list.onClick = [&owner, this] { owner.setListView (list.getToggleState()); };
        pin.setButtonText ("Pin"); pin.setClickingTogglesState (true);
        pin.onClick = [&owner, this] { owner.pinInspector (pin.getToggleState()); };
        inspector.pin.onClick = [&owner, this] { owner.pinInspector (inspector.pin.getToggleState()); };
        inspector.openSound.onClick = [&owner, this] {
            if (unsupportedPin) return;
            const auto target = inspector.stepTarget();
            owner.selectPad (inspector.showingSteps() && target ? target->lane : inspector.padTarget());
            auto next = layout;
            if (next.secondary == workspace::Editor::sound) next.secondary = next.primary;
            next.primary = workspace::Editor::sound;
            next.centralVisible = true;
            owner.setLayout (next);
        };
        primary.addItemList ({ "Step", "Song", "Sound", "Sample" }, 1); primary.setSelectedId (1, juce::dontSendNotification);
        secondary.addItemList ({ "Step", "Song", "Sound", "Sample" }, 1); secondary.setSelectedId (2, juce::dontSendNotification);
        primary.setTooltip ("Primary central editor"); secondary.setTooltip ("Secondary central editor / available in split layouts");
        primary.onChange = [&owner, this] {
            const auto next = static_cast<workspace::Editor> (primary.getSelectedId() - 1);
            if (next == layout.secondary) layout.secondary = layout.primary;
            layout.primary = next;
            secondary.setSelectedId (static_cast<int> (layout.secondary) + 1, juce::dontSendNotification);
            owner.setLayout (layout);
        };
        secondary.onChange = [&owner, this] {
            const auto next = static_cast<workspace::Editor> (secondary.getSelectedId() - 1);
            if (next == layout.primary) layout.primary = layout.secondary;
            layout.secondary = next;
            primary.setSelectedId (static_cast<int> (layout.primary) + 1, juce::dontSendNotification);
            owner.setLayout (layout);
        };
        split.addItemList ({ "One editor", "Split across", "Split below" }, 1); split.setSelectedId (1, juce::dontSendNotification);
        split.onChange = [&owner, this] {
            layout.split = static_cast<workspace::Split> (split.getSelectedId() - 1); layout.maximized = 255; owner.setLayout (layout);
        };
        mixerSize.addItemList ({ "Mixer closed", "Mixer compact", "Mixer expanded" }, 1); mixerSize.setSelectedId (2, juce::dontSendNotification);
        mixerSize.onChange = [&owner, this] { layout.mixer = static_cast<workspace::MixerSize> (mixerSize.getSelectedId() - 1); owner.setLayout (layout); };
        song.onEditBar = [&owner, this] (int bar) {
            steps.setEnabled (true);
            steps.editSongBar (bar);
            if (followedSteps)
            {
                followedSteps = StepTarget { steps.slot(), steps.variation(), steps.bar(), steps.selectedLane(), steps.selectionFirst(), steps.selectionLast(), steps.songBar() };
                owner.refreshInspector();
            }
        };
        song.onEditSource = [&owner, this] (int bar) {
            const auto current = processor.patterns().song();
            if (bar < 0 || bar >= current.length || current.bars[static_cast<std::size_t> (bar)].kind != SongBar::Kind::pattern)
            { steps.setEnabled (false); followedSteps.reset(); followedSong = bar; owner.refreshInspector(); return; }
            const auto& target = current.bars[static_cast<std::size_t> (bar)];
            steps.setEnabled (true); steps.editSourcePattern (target.slot, target.variation, target.patternBar);
        };
        song.enableSourceEditing();
        inspector.onEditSong = [&owner, this] (int bar, bool source) {
            song.editTarget (bar, source);
            auto next = layout;
            if (next.secondary == workspace::Editor::step) next.secondary = next.primary;
            next.primary = workspace::Editor::step; next.centralVisible = true; owner.setLayout (next);
        };
        song.onSelectionChanged = [&owner, this] {
            layoutGeneration = processor.layoutState().generation;
            followedSteps.reset(); followedSong = song.selectionStart(); owner.refreshInspector();
        };
        steps.onSelectionChanged = [&owner, this] {
            if (applyingLayout) return;
            if (! selectingPad)
            {
                const juce::ScopedValueSetter<bool> sync (syncingStep, true);
                owner.selectPad (steps.selectedLane());
                followedSong = -1;
                followedSteps = StepTarget { steps.slot(), steps.variation(), steps.bar(), steps.selectedLane(), steps.selectionFirst(), steps.selectionLast(), steps.songBar() };
                owner.refreshInspector();
            }
            auto ui = processor.uiState();
            ui.pad = static_cast<std::uint8_t> (steps.selectedLane());
            ui.patternBar = static_cast<std::uint8_t> (steps.bar());
            processor.setUiState (ui);
            layoutGeneration = processor.layoutState().generation;
        };
        steps.onParameterLanesChanged = [&owner, this] {
            if (applyingLayout) return;
            auto document = processor.layoutState().document;
            layout::Bytes lanes { static_cast<layout::Byte> (steps.parameterLanes().size()) };
            for (const auto kind : steps.parameterLanes()) lanes.push_back (static_cast<layout::Byte> (kind));
            putLayoutRecord (document, layout::Type::paramLanes, std::move (lanes));
            owner.commitDockDocument (std::move (document));
        };
        sound.onSectionsChanged = [&owner, this] {
            if (applyingLayout) return;
            auto document = processor.layoutState().document;
            putLayoutRecord (document, layout::Type::soundSections, { sound.collapsedSections() });
            owner.commitDockDocument (std::move (document));
        };
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
        leftDock.clear(); rightDock.clear();
        rackViewport.setViewedComponent (nullptr, false);
    }
    DrumsProcessor& processor;
    vox::ui::VoxLookAndFeel lookAndFeel;
    workspace::Selection selection;
    workspace::Layout layout, committedLayout;
    enum class Divider { none, left, right, across, below, mixer };
    Divider drag = Divider::none;
    Divider dividerAt (int width, int height, juce::Point<int> point) const
    {
        const auto r = workspace::arrange (width, height, layout);
        if (layout.mixer != workspace::MixerSize::collapsed && point.y >= r.mixer.y - 6 && point.y < r.mixer.y) return Divider::mixer;
        if (point.y < r.left.y || point.y >= r.left.y + r.left.h) return Divider::none;
        const int centralX = ! r.primary.empty() ? r.primary.x : ! r.secondary.empty() ? r.secondary.x : r.left.w + 6;
        if (layout.leftVisible && ! layout.leftRail && point.x >= r.left.w && point.x < centralX) return Divider::left;
        if (layout.rightVisible && ! layout.rightRail && point.x >= r.right.x - 6 && point.x < r.right.x) return Divider::right;
        if (! r.primary.empty() && ! r.secondary.empty() && layout.split == workspace::Split::horizontal
            && point.x >= r.primary.x + r.primary.w && point.x < r.secondary.x) return Divider::across;
        if (! r.primary.empty() && ! r.secondary.empty() && layout.split == workspace::Split::vertical
            && point.x >= r.primary.x && point.x < r.primary.x + r.primary.w
            && point.y >= r.primary.y + r.primary.h && point.y < r.secondary.y) return Divider::below;
        return Divider::none;
    }
    Telemetry telemetry;
    bool listView = false, auditionRefused = false, applyingLayout = false;
    bool committedListView = false, unsupportedPin = false, selectingPad = false, syncingStep = false;
    std::optional<StepTarget> followedSteps;
    int followedSong = -1;
    layout::Bytes pinPayload { 0 }, committedPinPayload { 0 };
    std::uint64_t layoutGeneration = 0;
    std::uint64_t layoutEpoch = 0;
    int selectedPlayer = 0;
    Inspector inspector;
    PatternEditor steps;
    SongStrip song;
    PadControlSurface sound, sample;
    std::array<std::unique_ptr<PadCell>, 16> pads;
    std::array<std::unique_ptr<MixerStrip>, 16> strips;
    juce::Label top, mixerTitle;
    juce::TextButton list, pin, workspaceMenu, maximise;
    juce::ComboBox split, mixerSize, mixerView, mixerBank, primary, secondary, mode, pattern, delayTime;
    std::unique_ptr<ComboAttachment> modeAttachment, patternAttachment, delayAttachment;
    std::array<std::unique_ptr<Page>, 4> toolPages;
    DrumsDockZone leftDock { layout::Type::left }, rightDock { layout::Type::right };
    juce::Viewport mixerViewport;
    juce::Component mixerContent;
    juce::Component returnContent;
    std::array<Knob, 8> returns;
    std::array<juce::Viewport, 4> editorViews;
    juce::Viewport rackViewport;
    juce::Component rackContent;
    Knob kitLevel;
    Peak mainLeft, mainRight;
};

DrumsWorkspace::DrumsWorkspace (DrumsProcessor& p) : impl_ (std::make_unique<Impl> (*this, p))
{
    p.layoutChanges().addChangeListener (this);
    readLayout();
    timerCallback(); startTimerHz (30);
}
DrumsWorkspace::~DrumsWorkspace()
{
    stopTimer(); impl_->processor.layoutChanges().removeChangeListener (this); setLookAndFeel (nullptr);
}
void DrumsWorkspace::paint (juce::Graphics& g) { g.fillAll (c::background); }
const workspace::Selection& DrumsWorkspace::selection() const noexcept { return impl_->selection; }
const DrumsWorkspace::Telemetry& DrumsWorkspace::telemetry() const noexcept { return impl_->telemetry; }
void DrumsWorkspace::selectPad (int pad)
{
    const juce::ScopedValueSetter<bool> selecting (impl_->selectingPad, true);
    if (! impl_->selection.selectPad (pad)) return;
    if (impl_->steps.selectedLane() != pad
        || (! impl_->syncingStep && (! impl_->applyingLayout || impl_->layoutEpoch != impl_->processor.patternEpoch())))
        impl_->steps.selectCell (pad, impl_->steps.selectedStep());
    if (! impl_->syncingStep && ! impl_->applyingLayout) { impl_->followedSteps.reset(); impl_->followedSong = -1; }
    refreshInspector();
    impl_->sound.setContext (pad, impl_->selectedPlayer);
    impl_->sample.setContext (pad, impl_->selectedPlayer);
    // Do not consume telemetry merely because selection changed.
    for (int i = 0; i < 16; ++i)
    {
        impl_->pads[static_cast<std::size_t> (i)]->select.setToggleState (i == pad, juce::dontSendNotification);
        impl_->pads[static_cast<std::size_t> (i)]->hit.setToggleState (i == pad, juce::dontSendNotification);
    }
    if (! impl_->applyingLayout)
    {
        auto ui = impl_->processor.uiState(); ui.pad = static_cast<std::uint8_t> (pad);
        impl_->processor.setUiState (ui);
        impl_->layoutGeneration = impl_->processor.layoutState().generation;
    }
}
void DrumsWorkspace::pinInspector (bool pin)
{
    auto& i = *impl_;
    if (pin && ! i.selection.pinned())
    {
        int pad = i.inspector.padTarget();
        if (i.inspector.showingSong())
        {
            if (! i.inspector.pin.isEnabled()) return;
            i.pinPayload = { 3, static_cast<layout::Byte> (i.inspector.songTarget()) };
        }
        else if (i.inspector.showingSteps())
        {
            const auto target = i.inspector.stepTarget();
            if (! target || target->song >= 0 || ! i.inspector.stepAvailable()) { refreshInspector(); return; }
            const auto& t = *target;
            pad = t.lane;
            i.pinPayload = { 2, static_cast<layout::Byte> (t.slot), static_cast<layout::Byte> (t.variation), static_cast<layout::Byte> (t.bar),
                             static_cast<layout::Byte> (t.lane), static_cast<layout::Byte> (t.first), static_cast<layout::Byte> (t.last) };
        }
        else i.pinPayload = { 1, static_cast<layout::Byte> (pad) };
        const int selected = i.selection.pad();
        i.selection.selectPad (pad); i.selection.setPinned (true); i.selection.selectPad (selected);
    }
    else if (! pin) { i.selection.setPinned (false); i.pinPayload = { 0 }; }
    refreshInspector();
    commitLayout();
}
void DrumsWorkspace::refreshInspector()
{
    auto& i = *impl_;
    i.inspector.setEnabled (true);
    if (i.selection.pinned() && i.pinPayload[0] == 2)
    {
        const auto& p = i.pinPayload;
        i.inspector.showSteps (StepTarget { p[1], p[2], p[3], p[4], p[5], p[6], -1 }, true);
    }
    else if (i.selection.pinned() && i.pinPayload[0] == 3) i.inspector.showSong (i.pinPayload[1], true);
    else if (! i.selection.pinned() && i.followedSong >= 0) i.inspector.showSong (i.followedSong, false);
    else if (! i.selection.pinned() && i.followedSteps) i.inspector.showSteps (i.followedSteps, false);
    else i.inspector.update (i.selection.inspectorPad(), i.selection.pinned());
    i.unsupportedPin = i.inspector.showingSteps() && ! i.inspector.stepAvailable();
    i.pin.setToggleState (i.selection.pinned(), juce::dontSendNotification);
    i.pin.setEnabled (i.inspector.pin.isEnabled());
    i.pin.setTooltip (i.selection.pinned() ? "Unpin Inspector" : i.inspector.showingSong() ? "Pin this Song bar"
                     : i.inspector.showingSteps() ? "Pin these steps" : "Pin this pad");
}
void DrumsWorkspace::setListView (bool list)
{
    impl_->listView = list; impl_->list.setToggleState (list, juce::dontSendNotification); resized();
    commitLayout();
}
void DrumsWorkspace::setLayout (workspace::Layout layout)
{
    layout.leftWidth = juce::jlimit (196, 640, layout.leftWidth);
    layout.rightWidth = juce::jlimit (196, 640, layout.rightWidth);
    layout.ratio = std::isfinite (layout.ratio) ? juce::jlimit (0.15f, 0.85f, layout.ratio) : 0.5f;
    layout.compactHeight = juce::jlimit (120, 220, layout.compactHeight);
    layout.expandedHeight = juce::jlimit (280, 560, layout.expandedHeight);
    if (layout.maximized != 0 && layout.maximized != 1) layout.maximized = 255;
    impl_->layout = layout;
    impl_->split.setSelectedId (static_cast<int> (layout.split) + 1, juce::dontSendNotification);
    impl_->mixerSize.setSelectedId (static_cast<int> (layout.mixer) + 1, juce::dontSendNotification);
    if (impl_->layout.primary == impl_->layout.secondary)
        impl_->layout.secondary = static_cast<workspace::Editor> ((static_cast<int> (impl_->layout.primary) + 1) % 4);
    impl_->primary.setSelectedId (static_cast<int> (impl_->layout.primary) + 1, juce::dontSendNotification);
    impl_->secondary.setSelectedId (static_cast<int> (impl_->layout.secondary) + 1, juce::dontSendNotification);
    impl_->maximise.setButtonText (impl_->layout.maximized == 255 ? "Focus" : "Restore");
    impl_->maximise.setEnabled (impl_->layout.split != workspace::Split::single);
    resized();
    commitLayout();
}
workspace::Layout DrumsWorkspace::layout() const noexcept { return impl_->layout; }
juce::Button& DrumsWorkspace::padSelectButton (int i) { return impl_->pads.at (static_cast<std::size_t> (i))->select; }
juce::Button& DrumsWorkspace::padHitButton (int i) { return impl_->pads.at (static_cast<std::size_t> (i))->hit; }
juce::Slider* DrumsWorkspace::inspectorSlider (const juce::String& suffix)
{
    if (auto* slider = impl_->inspector.stepSlider (suffix)) return slider;
    for (std::size_t i = 0; i < impl_->inspector.knobs.size(); ++i)
        if (suffix == Inspector::names[i]) return &impl_->inspector.knobs[i].control->getSlider();
    return nullptr;
}
juce::Button* DrumsWorkspace::inspectorButton (const juce::String& suffix)
{
    if (auto* button = impl_->inspector.stepButton (suffix)) return button;
    if (auto* button = impl_->inspector.songButton (suffix)) return button;
    if (suffix == "mute") return &impl_->inspector.mute;
    if (suffix == "solo") return &impl_->inspector.solo;
    if (suffix == "openSound") return &impl_->inspector.openSound;
    return nullptr;
}
juce::ComboBox* DrumsWorkspace::inspectorCombo (const juce::String& name)
{
    if (name == "model") return &impl_->inspector.model;
    if (name == "note") return &impl_->inspector.note;
    if (name == "output") return &impl_->inspector.output;
    return nullptr;
}
Page* DrumsWorkspace::toolPage (workspace::DockTool tool)
{
    const auto index = static_cast<int> (tool) - 1;
    return index >= 0 && index < 4 ? impl_->toolPages[static_cast<std::size_t> (index)].get() : nullptr;
}
juce::ComboBox* DrumsWorkspace::hostCombo (const juce::String& id)
{
    if (id == "drm.seq.mode") return &impl_->mode;
    if (id == patternParameterId) return &impl_->pattern;
    if (id == "drm.fx.delay.time") return &impl_->delayTime;
    return nullptr;
}
juce::Slider* DrumsWorkspace::returnSlider (const juce::String& id)
{
    for (auto& knob : impl_->returns)
        if (knob.parameterId == id) return &knob.control->getSlider();
    return nullptr;
}
void DrumsWorkspace::pollForTesting() { timerCallback(); }
juce::Slider* DrumsWorkspace::sliderFor (const juce::String& id)
{
    auto& i = *impl_;
    if (id == "drm.kit.volume") return &i.kitLevel.control->getSlider();
    if (auto* slider = returnSlider (id)) return slider;
    if (auto* slider = i.toolPages[3]->slider (id)) return slider;
    for (int pad = 0; pad < 16; ++pad)
    {
        const auto prefix = juce::String (padPrefix (pad)) + ".";
        if (! id.startsWith (prefix)) continue;
        const auto suffix = id.substring (prefix.length());
        if (auto* slider = i.strips[static_cast<std::size_t> (pad)]->slider (suffix)) return slider;
        return pad == i.selection.pad() ? i.sound.hostSlider (suffix) : nullptr;
    }
    return nullptr;
}
juce::Button* DrumsWorkspace::buttonFor (const juce::String& id)
{
    auto& i = *impl_;
    if (auto* button = i.toolPages[2]->button (id)) return button;
    if (auto* button = i.toolPages[3]->button (id)) return button;
    for (int pad = 0; pad < 16; ++pad)
    {
        const auto prefix = juce::String (padPrefix (pad)) + ".";
        if (id.startsWith (prefix)) return i.strips[static_cast<std::size_t> (pad)]->button (id.substring (prefix.length()));
    }
    return nullptr;
}

void DrumsWorkspace::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (impl_->layoutGeneration != impl_->processor.layoutState().generation) readLayout();
}

void DrumsWorkspace::readLayout()
{
    auto& i = *impl_;
    const auto snapshot = i.processor.layoutState();
    i.applyingLayout = true;
    i.steps.update(); i.song.update();
    i.drag = Impl::Divider::none;
    i.leftDock.clear(); i.rightDock.clear();
    const auto defaults = layout::factory (layout::Preset::beatmaking);
    const auto bytes = [&] (layout::Type type) -> const layout::Bytes& {
        const auto* record = layout::find (snapshot.document, type);
        return (record ? record : layout::find (defaults, type))->payload;
    };
    auto next = workspace::Layout {};
    const auto leftDock = layout::dock (snapshot.document, layout::Type::left);
    const auto rightDock = layout::dock (snapshot.document, layout::Type::right);
    next.leftVisible = leftDock.has_value(); next.rightVisible = rightDock.has_value();
    next.leftRail = leftDock && leftDock->rail; next.rightRail = rightDock && rightDock->rail;
    if (leftDock) next.leftWidth = static_cast<int> (leftDock->width);
    if (rightDock) next.rightWidth = static_cast<int> (rightDock->width);
    switch (rightDock ? rightDock->groups[0].tabs[rightDock->groups[0].active] : 3)
    {
        case 2: next.rightTool = workspace::DockTool::library; break;
        case 4: next.rightTool = workspace::DockTool::generator; break;
        case 5: next.rightTool = workspace::DockTool::routing; break;
        case 6: next.rightTool = workspace::DockTool::diagnostics; break;
        default: next.rightTool = workspace::DockTool::inspector; break;
    }
    const auto& central = bytes (layout::Type::central);
    next.centralVisible = layout::find (snapshot.document, layout::Type::central) != nullptr;
    next.primary = static_cast<workspace::Editor> (central[4] - 16);
    next.secondary = central[0] == 2 ? static_cast<workspace::Editor> (central[5] - 16)
        : static_cast<workspace::Editor> ((static_cast<int> (next.primary) + 1) % 4);
    next.split = central[0] == 1 ? workspace::Split::single
        : central[1] == 0 ? workspace::Split::vertical : workspace::Split::horizontal;
    next.ratio = static_cast<float> (layoutWord (central, 2)) / 1000.0f;
    next.maximized = central.back();
    const auto& mixer = bytes (layout::Type::mixer);
    next.mixer = static_cast<workspace::MixerSize> (mixer[0]);
    next.compactHeight = layoutWord (mixer, 1); next.expandedHeight = layoutWord (mixer, 3);
    i.mixerBank.setSelectedId (mixer[5] + 1, juce::dontSendNotification);
    setLayout (next);
    setListView (bytes (layout::Type::padRack)[0] != 0);
    const auto& selected = bytes (layout::Type::selection);
    i.selection.setPinned (false);
    const auto& pin = bytes (layout::Type::inspectorPin);
    i.pinPayload = pin;
    if (i.layoutEpoch != i.processor.patternEpoch())
    {
        i.followedSteps.reset(); i.followedSong = -1; i.song.clearEditing(); i.steps.editSongBar (-1); i.steps.setEnabled (true);
    }
    if (pin[0] == 1) { i.selection.selectPad (pin[1]); i.selection.setPinned (true); }
    if (pin[0] == 2) { i.selection.selectPad (pin[4]); i.selection.setPinned (true); }
    if (pin[0] == 3) i.selection.setPinned (true);
    selectPad (selected[0]);
    if (i.steps.bar() != selected[2] || i.layoutEpoch != i.processor.patternEpoch()) i.steps.selectBar (selected[2]);
    const auto& lanes = bytes (layout::Type::paramLanes);
    i.steps.setParameterLanes ({ lanes.begin() + 1, lanes.end() }, false);
    i.sound.setCollapsedSections (bytes (layout::Type::soundSections)[0], false);
    if (i.song.selectionStart() != selected[1] || i.layoutEpoch != i.processor.patternEpoch()) i.song.restoreSelection (selected[1]);
    if (i.steps.songBar() != i.song.editedBar()) i.steps.editSongBar (i.song.editedBar());
    if (i.followedSteps)
        i.followedSteps = StepTarget { i.steps.slot(), i.steps.variation(), i.steps.bar(), i.steps.selectedLane(), i.steps.selectionFirst(), i.steps.selectionLast(), i.steps.songBar() };
    i.layoutEpoch = i.processor.patternEpoch();
    i.pin.setToggleState (pin[0] != 0, juce::dontSendNotification);
    refreshInspector();
    i.layoutGeneration = snapshot.generation;
    i.committedLayout = i.layout; i.committedListView = i.listView;
    i.committedPinPayload = i.pinPayload;
    const auto panel = [&i] (layout::Byte id) -> juce::Component* {
        switch (id)
        {
            case 1: return &i.rackViewport; case 2: return i.toolPages[1].get(); case 3: return &i.inspector;
            case 4: return i.toolPages[0].get(); case 5: return i.toolPages[2].get(); case 6: return i.toolPages[3].get();
            default: return nullptr;
        }
    };
    const auto height = [&i] (layout::Byte id) {
        if (id == 1) return 0;
        if (id == 3) return 328;
        const int index = id == 2 ? 1 : id == 4 ? 0 : id == 5 ? 2 : 3;
        const std::array defaultHeights { 1200, 900, 1660, 620 };
        const auto requested = i.toolPages[static_cast<std::size_t> (index)]->dockHeight();
        return requested > 0 ? requested : defaultHeights[static_cast<std::size_t> (index)];
    };
    const auto configure = [&] (DrumsDockZone& zone, layout::Type side, const std::optional<layout::Dock>& dock) {
        zone.configure (dock, panel, height, [this, side, expected = dock, epoch = i.processor.patternEpoch()] (const layout::Dock& nextDock) {
            const auto current = impl_->processor.layoutState();
            auto live = layout::dock (current.document, side);
            if (! expected || ! live || impl_->processor.patternEpoch() != epoch) { readLayout(); return; }
            const auto width = live->width;
            live->width = expected->width; // A dock tab does not undo a newer width gesture.
            if (*live != *expected) { readLayout(); return; }
            auto committed = nextDock; committed.width = width;
            commitDockDocument (layout::setDock (current.document, side, committed));
        }, [this, side] (unsigned group, layout::Byte id, juce::Component& target) { showDockMenu (side, group, id, target); });
    };
    configure (i.leftDock, layout::Type::left, leftDock); configure (i.rightDock, layout::Type::right, rightDock);
    i.applyingLayout = false;
    resized();
    if (i.mixerView.getSelectedId() == 1) i.mixerViewport.setViewPosition (mixer[5] != 0 ? 8 * 84 : 0, 0);
}

void DrumsWorkspace::commitLayout()
{
    auto& i = *impl_;
    if (i.applyingLayout) return;
    const auto snapshot = i.processor.layoutState();
    if (snapshot.generation != i.layoutGeneration) { readLayout(); return; }
    auto document = snapshot.document;
    const auto before = document;
    const auto defaults = layout::factory (layout::Preset::beatmaking);
    const auto bytes = [&] (layout::Type type) {
        const auto* record = layout::find (document, type);
        return (record ? record : layout::find (defaults, type))->payload;
    };
    if (i.layout.leftWidth != i.committedLayout.leftWidth)
    {
        auto left = bytes (layout::Type::left); layoutWord (left, 0, i.layout.leftWidth);
        putLayoutRecord (document, layout::Type::left, std::move (left));
    }
    if (i.layout.rightWidth != i.committedLayout.rightWidth)
    {
        auto right = bytes (layout::Type::right); layoutWord (right, 0, i.layout.rightWidth);
        putLayoutRecord (document, layout::Type::right, std::move (right));
    }
    if (i.layout.rightTool != i.committedLayout.rightTool)
    {
        const std::array<layout::Byte, 5> ids { 3, 4, 2, 5, 6 };
        const auto moved = layout::movePanel (document, ids[static_cast<std::size_t> (i.layout.rightTool)], layout::Type::right);
        if (! moved) { readLayout(); return; }
        document = *moved;
    }
    const auto& a = i.layout; const auto& b = i.committedLayout;
    const bool reparent = a.rightTool != b.rightTool;
    if (a.primary != b.primary || a.secondary != b.secondary || a.split != b.split || a.ratio != b.ratio || a.maximized != b.maximized
        || a.centralVisible != b.centralVisible)
    {
        if (! a.centralVisible)
            std::erase_if (document.records, [] (const auto& record) { return record.type == static_cast<layout::Byte> (layout::Type::central); });
        else
        {
            layout::Bytes central { static_cast<layout::Byte> (a.split == workspace::Split::single ? 1 : 2),
                                static_cast<layout::Byte> (a.split == workspace::Split::horizontal ? 1 : 0), 0, 0,
                                static_cast<layout::Byte> (16 + static_cast<int> (a.primary)) };
            layoutWord (central, 2, juce::roundToInt (a.ratio * 1000.0f));
            if (a.split != workspace::Split::single) central.push_back (static_cast<layout::Byte> (16 + static_cast<int> (a.secondary)));
            central.push_back (static_cast<layout::Byte> (a.maximized == 1 && a.split == workspace::Split::single ? 255 : a.maximized));
            putLayoutRecord (document, layout::Type::central, std::move (central));
        }
    }
    if (a.mixer != b.mixer || a.compactHeight != b.compactHeight || a.expandedHeight != b.expandedHeight)
    {
        auto mixer = bytes (layout::Type::mixer); mixer[0] = static_cast<layout::Byte> (a.mixer);
        layoutWord (mixer, 1, a.compactHeight); layoutWord (mixer, 3, a.expandedHeight);
        putLayoutRecord (document, layout::Type::mixer, std::move (mixer));
    }
    if (i.listView != i.committedListView) putLayoutRecord (document, layout::Type::padRack, { static_cast<layout::Byte> (i.listView) });
    if (i.pinPayload != i.committedPinPayload) putLayoutRecord (document, layout::Type::inspectorPin, i.pinPayload);
    if (document == before) return;
    auto workspace = bytes (layout::Type::workspace); workspace[1] = 1;
    putLayoutRecord (document, layout::Type::workspace, std::move (workspace));
    if (! i.processor.commitLayout (std::move (document))) { readLayout(); return; }
    i.layoutGeneration = i.processor.layoutState().generation;
    i.committedLayout = i.layout; i.committedListView = i.listView;
    i.committedPinPayload = i.pinPayload;
    if (reparent) readLayout();
}

bool DrumsWorkspace::applyWorkspace (const layout::Document& document, bool confirmedReset)
{
    if (! impl_->processor.commitLayout (document, confirmedReset)) return false;
    readLayout();
    return true;
}

bool DrumsWorkspace::commitDockDocument (std::optional<layout::Document> document)
{
    if (! document) return false;
    const auto* workspace = layout::find (*document, layout::Type::workspace);
    const layout::Byte preset = workspace ? workspace->payload[0] : 0;
    putLayoutRecord (*document, layout::Type::workspace, { preset, 1 });
    return applyWorkspace (*document);
}
bool DrumsWorkspace::moveDockPanel (layout::Byte panel, layout::Type side, unsigned group, bool newGroup)
{
    return commitDockDocument (layout::movePanel (impl_->processor.layoutState().document, panel, side, group, newGroup));
}
bool DrumsWorkspace::hideDockPanel (layout::Byte panel)
{
    return commitDockDocument (layout::hidePanel (impl_->processor.layoutState().document, panel));
}
bool DrumsWorkspace::setDockRail (layout::Type side, bool rail)
{
    const auto document = impl_->processor.layoutState().document;
    auto dock = layout::dock (document, side);
    if (! dock) return false;
    dock->rail = rail;
    return commitDockDocument (layout::setDock (document, side, dock));
}
DrumsDockZone& DrumsWorkspace::dockZone (layout::Type side)
{
    jassert (side == layout::Type::left || side == layout::Type::right);
    return side == layout::Type::left ? impl_->leftDock : impl_->rightDock;
}
PatternEditor& DrumsWorkspace::patternEditor() { return impl_->steps; }
SongStrip& DrumsWorkspace::songEditor() { return impl_->song; }
void DrumsWorkspace::showDockMenu (layout::Type side, unsigned sourceGroup, layout::Byte panel, juce::Component& target)
{
    juce::ignoreUnused (sourceGroup);
    const auto snapshot = impl_->processor.layoutState();
    juce::PopupMenu menu;
    menu.addSectionHeader (DrumsDockZone::panelName (panel));
    for (const auto destination : { layout::Type::left, layout::Type::right })
    {
        juce::PopupMenu groups;
        const auto zone = layout::dock (snapshot.document, destination);
        const unsigned count = zone ? static_cast<unsigned> (zone->groups.size()) : 1;
        const int first = destination == layout::Type::left ? 10 : 20;
        for (unsigned n = 0; n < count; ++n) groups.addItem (first + static_cast<int> (n), "Group " + juce::String (n + 1));
        const auto hidden = layout::hidePanel (snapshot.document, panel);
        const auto afterRemoval = hidden ? layout::dock (*hidden, destination) : std::nullopt;
        groups.addItem (destination == layout::Type::left ? 30 : 31, "New group", ! afterRemoval || afterRemoval->groups.size() < 3);
        menu.addSubMenu (destination == layout::Type::left ? "Move to left dock" : "Move to right dock", groups);
    }
    menu.addItem (40, "Hide panel"); menu.addItem (50, "Collapse dock to rail");
    const auto weak = juce::Component::SafePointer<DrumsWorkspace> (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target),
        [weak, generation = snapshot.generation, panel, side] (int id) {
            if (! weak || generation != weak->impl_->processor.layoutState().generation) return;
            if (id >= 10 && id <= 12) weak->moveDockPanel (panel, layout::Type::left, static_cast<unsigned> (id - 10));
            else if (id >= 20 && id <= 22) weak->moveDockPanel (panel, layout::Type::right, static_cast<unsigned> (id - 20));
            else if (id == 30 || id == 31) weak->moveDockPanel (panel, id == 30 ? layout::Type::left : layout::Type::right, 0, true);
            else if (id == 40) weak->hideDockPanel (panel);
            else if (id == 50) weak->setDockRail (side, true);
        });
}

void DrumsWorkspace::showWorkspaceMenu()
{
    WorkspaceLibrary menuLibrary;
    const auto read = menuLibrary.read();
    juce::PopupMenu menu;
    menu.addSectionHeader ("Factory workspaces");
    menu.addItem (1, "Beatmaking"); menu.addItem (2, "Composition");
    menu.addItem (3, "Sound Design"); menu.addItem (4, "Mixing");
    menu.addSeparator();
    for (std::size_t n = 0; n < read.entries.size(); ++n)
        menu.addItem (100 + static_cast<int> (n), read.entries[n].name);
    menu.addItem (10, "Save Workspace As...");
    if (! read.entries.empty())
    {
        juce::PopupMenu remove;
        for (std::size_t n = 0; n < read.entries.size(); ++n)
            remove.addItem (200 + static_cast<int> (n), read.entries[n].name);
        menu.addSubMenu ("Delete user workspace", remove);
    }
    menu.addItem (11, "Reset layout...");
    juce::PopupMenu open;
    for (layout::Byte panel = 1; panel <= 6; ++panel) open.addItem (300 + panel, DrumsDockZone::panelName (panel));
    menu.addSubMenu ("Open panel", open);
    const auto snapshot = impl_->processor.layoutState();
    if (snapshot.status == layout::Status::future)
        menu.addSectionHeader ("Newer layout preserved; session changes are not saved");
    if (read.diagnostic.isNotEmpty()) menu.addSectionHeader (read.diagnostic);
    const auto weak = juce::Component::SafePointer<DrumsWorkspace> (this);
    const auto generation = snapshot.generation;
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&impl_->workspaceMenu),
        [weak, generation, entries = read.entries] (int id) {
            if (! weak || id == 0 || generation != weak->impl_->processor.layoutState().generation) return;
            if (id >= 301 && id <= 306)
                weak->moveDockPanel (static_cast<layout::Byte> (id - 300), id == 301 ? layout::Type::left : layout::Type::right);
            else if (id >= 1 && id <= 4)
                weak->applyWorkspace (layout::factory (static_cast<layout::Preset> (id - 1)));
            else if (id >= 100 && id < 100 + static_cast<int> (entries.size()))
            {
                auto document = entries[static_cast<std::size_t> (id - 100)].layout;
                const auto* old = layout::find (document, layout::Type::workspace);
                putLayoutRecord (document, layout::Type::workspace, { old ? old->payload[0] : layout::Byte { 0 }, 1 });
                weak->applyWorkspace (document);
            }
            else if (id >= 200 && id < 200 + static_cast<int> (entries.size()))
            {
                WorkspaceLibrary library;
                auto current = library.read().entries;
                const auto name = entries[static_cast<std::size_t> (id - 200)].name;
                std::erase_if (current, [&] (const auto& entry) { return entry.name.equalsIgnoreCase (name); });
                const auto result = library.write (current);
                if (result.failed()) juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Workspace", result.getErrorMessage());
            }
            else if (id == 10)
            {
                auto* dialog = new juce::AlertWindow ("Save Workspace As", "Name this workspace (1-64 characters).", juce::MessageBoxIconType::NoIcon);
                dialog->addTextEditor ("name", {}, "Name");
                dialog->addButton ("Save", 1); dialog->addButton ("Cancel", 0);
                const auto alert = juce::Component::SafePointer<juce::AlertWindow> (dialog);
                dialog->enterModalState (true, juce::ModalCallbackFunction::create ([weak, generation, alert] (int result) {
                    if (result != 1 || ! weak || ! alert || generation != weak->impl_->processor.layoutState().generation) return;
                    WorkspaceLibrary library;
                    auto current = library.read().entries;
                    current.push_back ({ alert->getTextEditorContents ("name"), weak->impl_->processor.layoutState().document });
                    const auto saved = library.write (current);
                    if (saved.failed()) juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Workspace", saved.getErrorMessage());
                }), true);
            }
            else if (id == 11)
            {
                if (weak->impl_->processor.layoutState().status != layout::Status::future)
                    weak->applyWorkspace (layout::factory (layout::Preset::beatmaking), true);
                else
                    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::WarningIcon, "Reset newer layout?",
                        "Replace the newer layout with this version's layout? The newer layout will be lost.", "Replace", "Cancel", nullptr,
                        juce::ModalCallbackFunction::create ([weak, generation] (int result) {
                            if (result != 0 && weak && generation == weak->impl_->processor.layoutState().generation)
                                weak->applyWorkspace (layout::factory (layout::Preset::beatmaking), true);
                        }));
            }
        });
}
void DrumsWorkspace::mouseDown (const juce::MouseEvent& e)
{
    impl_->drag = impl_->dividerAt (getWidth(), getHeight(), e.getPosition());
}
void DrumsWorkspace::mouseUp (const juce::MouseEvent&)
{
    if (impl_->drag == Impl::Divider::none) return;
    impl_->drag = Impl::Divider::none;
    commitLayout();
}
void DrumsWorkspace::mouseMove (const juce::MouseEvent& e)
{
    const auto target = impl_->dividerAt (getWidth(), getHeight(), e.getPosition());
    setMouseCursor (target == Impl::Divider::none ? juce::MouseCursor::NormalCursor
                   : target == Impl::Divider::below || target == Impl::Divider::mixer
                       ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::LeftRightResizeCursor);
}
void DrumsWorkspace::mouseDrag (const juce::MouseEvent& e)
{
    auto& i = *impl_;
    const auto r = workspace::arrange (getWidth(), getHeight(), i.layout);
    switch (i.drag)
    {
        case Impl::Divider::left: i.layout.leftWidth = juce::jlimit (196, 640, e.x); break;
        case Impl::Divider::right: i.layout.rightWidth = juce::jlimit (196, 640, getWidth() - e.x); break;
        case Impl::Divider::across:
            i.layout.ratio = juce::jlimit (0.15f, 0.85f, static_cast<float> (e.x - r.primary.x) / static_cast<float> (r.primary.w + r.secondary.w)); break;
        case Impl::Divider::below:
            i.layout.ratio = juce::jlimit (0.15f, 0.85f, static_cast<float> (e.y - r.primary.y) / static_cast<float> (r.primary.h + r.secondary.h)); break;
        case Impl::Divider::mixer:
            if (i.layout.mixer == workspace::MixerSize::compact) i.layout.compactHeight = juce::jlimit (120, 220, getHeight() - e.y);
            else i.layout.expandedHeight = juce::jlimit (280, 560, getHeight() - e.y);
            break;
        case Impl::Divider::none: return;
    }
    resized();
}
void DrumsWorkspace::timerCallback()
{
    auto& i = *impl_;
    if (i.processor.restoringMusic()) return;
    // Apply a completed project epoch before delivering subsequent UI actions,
    // even when its asynchronous change notification is still queued.
    if (i.layoutEpoch != i.processor.patternEpoch()) readLayout();
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
    if (i.followedSteps)
        i.followedSteps = StepTarget { i.steps.slot(), i.steps.variation(), i.steps.bar(), i.steps.selectedLane(), i.steps.selectionFirst(), i.steps.selectionLast(), i.steps.songBar() };
    if (i.steps.selectedLane() != i.selection.pad()) selectPad (i.steps.selectedLane());
    refreshInspector();
    i.sound.setContext (i.selection.pad(), i.selectedPlayer);
    i.sample.setContext (i.selection.pad(), i.selectedPlayer);
    for (auto& page : i.toolPages) if (page->isVisible() && page->getParentComponent()) page->update();
    i.top.setText (juce::String (i.processor.transportPlaying() ? "HOST PLAYING / " : "HOST STOPPED / ")
                   + juce::String (i.processor.lastBpm(), 1) + " BPM" + (i.auditionRefused ? " / audition queue full" : ""), juce::dontSendNotification);
}
void DrumsWorkspace::resized()
{
    auto& i = *impl_;
    const auto boxes = workspace::arrange (getWidth(), getHeight(), i.layout);
    auto top = rectangle (boxes.top);
    i.workspaceMenu.setBounds (top.removeFromLeft (104).reduced (2));
    i.pattern.setBounds (top.removeFromRight (104).reduced (2));
    i.mode.setBounds (top.removeFromRight (120).reduced (2));
    i.split.setBounds (top.removeFromRight (108).reduced (2));
    i.maximise.setBounds (top.removeFromRight (56).reduced (2));
    i.primary.setBounds (top.removeFromRight (80).reduced (2));
    i.secondary.setVisible (i.layout.split != workspace::Split::single);
    if (i.secondary.isVisible()) i.secondary.setBounds (top.removeFromRight (80).reduced (2));
    i.pin.setBounds (top.removeFromRight (44).reduced (2));
    i.list.setBounds (top.removeFromRight (44).reduced (2)); i.top.setBounds (top);
    i.leftDock.setBounds (rectangle (boxes.left)); i.rightDock.setBounds (rectangle (boxes.right));
    i.leftDock.resized(); i.rightDock.resized();
    i.rackContent.setSize (std::max (0, i.rackViewport.getWidth() - i.rackViewport.getScrollBarThickness()),
                           std::max (i.rackViewport.getHeight(), i.listView ? 16 * 32 : 4 * 70));
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
    const std::array<juce::Component*, 4> editors { &i.steps, &i.song, &i.sound, &i.sample };
    for (std::size_t n = 0; n < editors.size(); ++n)
    {
        const auto editor = static_cast<workspace::Editor> (n);
        const bool isPrimary = editor == i.layout.primary && ! boxes.primary.empty();
        const bool isSecondary = editor == i.layout.secondary && ! boxes.secondary.empty();
        auto& viewport = i.editorViews[n];
        viewport.setVisible (isPrimary || isSecondary);
        if (! viewport.isVisible()) continue;
        viewport.setBounds (rectangle (isPrimary ? boxes.primary : boxes.secondary));
        const int width = std::max (360, viewport.getWidth() - viewport.getScrollBarThickness());
        const std::array preferredHeights { 320, i.song.preferredHeightForWidth (width),
                                           i.sound.preferredHeight (width), i.sample.preferredHeight (width) };
        editors[n]->setSize (width, std::max (preferredHeights[n], viewport.getHeight() - viewport.getScrollBarThickness()));
    }
    auto mixer = rectangle (boxes.mixer);
    i.mixerTitle.setText ("MIXER / 16 PADS / 9 OUTPUTS / Main L-R", juce::dontSendNotification);
    auto mixerHeader = mixer.removeFromTop (24);
    const bool open = i.layout.mixer != workspace::MixerSize::collapsed;
    i.mixerSize.setBounds (mixerHeader.removeFromRight (146).reduced (1));
    i.mixerView.setBounds (mixerHeader.removeFromRight (118).reduced (1));
    i.mixerBank.setVisible (open && i.mixerView.getSelectedId() == 1);
    if (i.mixerBank.isVisible()) i.mixerBank.setBounds (mixerHeader.removeFromRight (108).reduced (1));
    i.mixerTitle.setBounds (mixerHeader);
    i.mixerViewport.setVisible (open); i.kitLevel.control->setVisible (open);
    i.mainLeft.setVisible (open); i.mainRight.setVisible (open);
    i.mainRight.setBounds (mixer.removeFromRight (8).reduced (1));
    i.mainLeft.setBounds (mixer.removeFromRight (8).reduced (1));
    i.kitLevel.control->setBounds (mixer.removeFromRight (78));
    i.mixerViewport.setBounds (mixer);
    auto* mixerPage = i.mixerView.getSelectedId() == 2 ? &i.returnContent : &i.mixerContent;
    if (i.mixerViewport.getViewedComponent() != mixerPage) i.mixerViewport.setViewedComponent (mixerPage, false);
    i.mixerContent.setSize (16 * 84, std::max (0, mixer.getHeight() - i.mixerViewport.getScrollBarThickness()));
    for (int pad = 0; pad < 16; ++pad)
        i.strips[static_cast<std::size_t> (pad)]->setBounds (pad * 84, 0, 84, i.mixerContent.getHeight());
    i.returnContent.setSize (924, i.mixerContent.getHeight());
    i.delayTime.setBounds (4, 6, 116, 30);
    for (std::size_t n = 0; n < i.returns.size(); ++n)
        i.returns[n].control->setBounds (124 + static_cast<int> (n) * 100, 0, 100, 96);
}
} // namespace vox::drums
