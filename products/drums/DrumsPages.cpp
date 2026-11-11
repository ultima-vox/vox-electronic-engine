#include "DrumsPages.h"

#include "FactoryContent.h"
#include "Presets.h"
#include "Processor.h"
#include "Tokens.h"
#include "Typography.h"

#include <deque>

namespace vox::drums {
namespace {

namespace c = vox::ui::tokens::colour;
namespace sp = vox::ui::tokens::spacing;
using vox::ui::VoxButton;
using vox::ui::VoxComboBox;
using vox::ui::VoxKnob;
using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

std::unique_ptr<juce::Label> makeLabel (const juce::String& text, bool value = false)
{
    auto l = std::make_unique<juce::Label> (text, text);
    l->setColour (juce::Label::textColourId, value ? c::text : c::textSecondary);
    l->setFont (value ? vox::ui::typography::valueText() : vox::ui::typography::controlLabel());
    l->setMinimumHorizontalScale (0.6f);
    return l;
}

juce::String noteName (int note)
{
    static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (names[note % 12]) + juce::String (note / 12 - 2) + " (" + juce::String (note) + ")";
}

juce::String outputName (int output)
{
    return output == 0 ? juce::String ("Main") : "Out " + juce::String (output + 1);
}

// Host-parameter controls of a page, by parameter ID. Each attachment is
// declared after its component, so it is destroyed first.
class HostControls {
public:
    HostControls (DrumsProcessor& processor, juce::Component& owner) : processor_ (processor), owner_ (owner) {}

    VoxKnob& knob (const juce::String& id, const juce::String& label, VoxKnob::Size size = VoxKnob::Size::Small)
    {
        auto& k = knobs_.emplace_back();
        k.id = id;
        k.knob = std::make_unique<VoxKnob> (label, size);
        if (id.endsWith (".pan"))
            k.knob->setStyle (VoxKnob::Style::Bipolar);
        k.attachment = std::make_unique<SliderAttachment> (processor_.state(), id, k.knob->getSlider());
        if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (processor_.state().getParameter (id)))
        {
            k.knob->setDoubleClickResetValue (p->convertFrom0to1 (p->getDefaultValue()));
            k.knob->setTooltip (p->getName (64));
        }
        owner_.addAndMakeVisible (*k.knob);
        return *k.knob;
    }

    VoxButton& toggle (const juce::String& id, const juce::String& text)
    {
        auto& t = toggles_.emplace_back();
        t.id = id;
        t.button = std::make_unique<VoxButton> (text, VoxButton::Type::Toggle);
        t.button->setClickingTogglesState (true);
        t.attachment = std::make_unique<ButtonAttachment> (processor_.state(), id, *t.button);
        if (auto* p = processor_.state().getParameter (id))
            t.button->setTooltip (p->getName (64));
        owner_.addAndMakeVisible (*t.button);
        return *t.button;
    }

    VoxComboBox& combo (const juce::String& id)
    {
        auto& b = combos_.emplace_back();
        b.id = id;
        b.combo = std::make_unique<VoxComboBox>();
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (processor_.state().getParameter (id)))
            b.combo->addItemList (choice->choices, 1);
        b.attachment = std::make_unique<ComboAttachment> (processor_.state(), id, *b.combo);
        owner_.addAndMakeVisible (*b.combo);
        return *b.combo;
    }

    juce::Slider* slider (const juce::String& id)
    {
        for (auto& k : knobs_)
            if (k.id == id)
                return &k.knob->getSlider();
        return nullptr;
    }
    juce::Button* button (const juce::String& id)
    {
        for (auto& t : toggles_)
            if (t.id == id)
                return t.button.get();
        return nullptr;
    }
    juce::ComboBox* comboFor (const juce::String& id)
    {
        for (auto& b : combos_)
            if (b.id == id)
                return b.combo.get();
        return nullptr;
    }

private:
    struct Knob { juce::String id; std::unique_ptr<VoxKnob> knob; std::unique_ptr<SliderAttachment> attachment; };
    struct Toggle { juce::String id; std::unique_ptr<VoxButton> button; std::unique_ptr<ButtonAttachment> attachment; };
    struct Combo { juce::String id; std::unique_ptr<VoxComboBox> combo; std::unique_ptr<ComboAttachment> attachment; };
    DrumsProcessor& processor_;
    juce::Component& owner_;
    std::deque<Knob> knobs_;
    std::deque<Toggle> toggles_;
    std::deque<Combo> combos_;
};

// A peak meter: the level it was last given, falling back between updates.
class Meter final : public juce::Component {
public:
    explicit Meter (juce::Colour accent) : accent_ (accent) {}
    void push (float peak)
    {
        const float next = juce::jmax (peak, level_ * 0.82f);
        if (std::abs (next - level_) > 1.0e-4f)
        {
            level_ = next < 1.0e-4f ? 0.0f : next;
            repaint();
        }
    }
    float level() const noexcept { return level_; }
    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        g.setColour (c::background);
        g.fillRoundedRectangle (r, 2.0f);
        // -60 dB .. 0 dB on the height; red above 0 dB.
        const float db = level_ > 0.0f ? juce::Decibels::gainToDecibels (level_) : -100.0f;
        const float amount = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
        g.setColour (level_ > 1.0f ? juce::Colours::red : accent_);
        g.fillRoundedRectangle (r.withTop (r.getBottom() - r.getHeight() * amount), 2.0f);
    }

private:
    juce::Colour accent_;
    float level_ = 0.0f;
};

//==============================================================================
class MixerPage final : public Page {
public:
    explicit MixerPage (DrumsProcessor& processor) : processor_ (processor), host_ (processor, *this)
    {
        const auto accent = processor.product().accent;
        for (int pad = 0; pad < padCount; ++pad)
        {
            auto& s = strips_[static_cast<std::size_t> (pad)];
            const auto prefix = juce::String (padPrefix (pad));
            s.name = makeLabel (juce::String (pad + 1));
            s.name->setJustificationType (juce::Justification::centred);
            s.meter = std::make_unique<Meter> (accent);
            s.level = &host_.knob (prefix + ".level", "Level");
            s.pan = &host_.knob (prefix + ".pan", "Pan");
            s.send1 = &host_.knob (prefix + ".send1", "Dly");
            s.send2 = &host_.knob (prefix + ".send2", "Rev");
            s.mute = &host_.toggle (prefix + ".mute", "M");
            s.solo = &host_.toggle (prefix + ".solo", "S");
            s.out = makeLabel ("Main", true);
            s.out->setJustificationType (juce::Justification::centred);
            addAndMakeVisible (*s.name);
            addAndMakeVisible (*s.meter);
            addAndMakeVisible (*s.out);
        }
        delayLabel_ = makeLabel ("Delay");
        addAndMakeVisible (*delayLabel_);
        delayTime_ = &host_.combo ("drm.fx.delay.time");
        fx_ = { &host_.knob ("drm.fx.delay.feedback", "Feedback", VoxKnob::Size::Normal),
                &host_.knob ("drm.fx.delay.tone", "Tone", VoxKnob::Size::Normal),
                &host_.knob ("drm.fx.delay.level", "Dly Return", VoxKnob::Size::Normal),
                &host_.knob ("drm.fx.reverb.size", "Rev Size", VoxKnob::Size::Normal),
                &host_.knob ("drm.fx.reverb.damp", "Damp", VoxKnob::Size::Normal),
                &host_.knob ("drm.fx.reverb.level", "Rev Return", VoxKnob::Size::Normal),
                &host_.knob ("drm.kit.volume", "Main", VoxKnob::Size::Normal) };
        mainLabel_ = makeLabel ("Main L/R");
        addAndMakeVisible (*mainLabel_);
        for (auto& m : main_)
        {
            m = std::make_unique<Meter> (accent);
            addAndMakeVisible (*m);
        }
    }

    void update() override
    {
        const auto kit = processor_.kitStore().snapshot();
        const auto active = processor_.activeOutputs();
        for (int pad = 0; pad < padCount; ++pad)
        {
            auto& s = strips_[static_cast<std::size_t> (pad)];
            s.meter->push (processor_.takePadMeter (pad));
            const int output = kit.pads[static_cast<std::size_t> (pad)].output;
            // A pad on an output the host has not enabled plays on Main.
            const auto text = (active & (1u << output)) != 0 ? outputName (output) : outputName (output) + ">Main";
            if (s.out->getText() != text)
                s.out->setText (text, juce::dontSendNotification);
            const auto name = juce::String (pad + 1) + " " + modelName (kit.pads[static_cast<std::size_t> (pad)].model);
            if (s.name->getText() != name)
                s.name->setText (name, juce::dontSendNotification);
        }
        main_[0]->push (processor_.takeMainMeter (0));
        main_[1]->push (processor_.takeMainMeter (1));
    }

    void resized() override
    {
        auto area = getLocalBounds();
        auto fx = area.removeFromBottom (juce::jlimit (76, 96, area.getHeight() / 5));
        area.removeFromBottom (sp::sm);
        const int w = area.getWidth() / padCount;
        const int knobH = juce::jlimit (52, 72, (area.getHeight() - 16 - 26 - 16 - 36) / 4);
        for (int pad = 0; pad < padCount; ++pad)
        {
            auto& s = strips_[static_cast<std::size_t> (pad)];
            auto col = juce::Rectangle<int> (area.getX() + pad * w, area.getY(), w, area.getHeight()).reduced (1, 0);
            s.name->setBounds (col.removeFromTop (16));
            s.out->setBounds (col.removeFromBottom (16));
            auto ms = col.removeFromBottom (26);
            s.mute->setBounds (ms.removeFromLeft (ms.getWidth() / 2).reduced (1, 2));
            s.solo->setBounds (ms.reduced (1, 2));
            for (auto* k : { s.send2, s.send1, s.pan, s.level })
                k->setBounds (col.removeFromBottom (knobH));
            s.meter->setBounds (col.reduced (juce::jmax (2, col.getWidth() / 2 - 6), 3));
        }
        auto time = fx.removeFromLeft (120);
        delayLabel_->setBounds (time.removeFromTop (16));
        delayTime_->setBounds (time.removeFromTop (vox::ui::tokens::size::controlHeight).withTrimmedRight (sp::sm));
        auto meters = fx.removeFromRight (64);
        mainLabel_->setBounds (meters.removeFromTop (16));
        main_[0]->setBounds (meters.removeFromLeft (meters.getWidth() / 2).reduced (8, 2));
        main_[1]->setBounds (meters.reduced (8, 2));
        const int kw = juce::jmin (96, fx.getWidth() / static_cast<int> (fx_.size()));
        for (auto* k : fx_)
            k->setBounds (fx.removeFromLeft (kw));
    }

    juce::Slider* slider (const juce::String& id) override { return host_.slider (id); }
    juce::Button* button (const juce::String& id) override { return host_.button (id); }
    juce::ComboBox* combo (const juce::String& id) override { return host_.comboFor (id); }
    juce::Component* named (const juce::String& name) override
    {
        if (name == "meter.main")
            return main_[0].get();
        const int pad = name.fromFirstOccurrenceOf (".", false, false).getIntValue() - 1;
        if (pad < 0 || pad >= padCount)
            return nullptr;
        if (name.startsWith ("meter."))
            return strips_[static_cast<std::size_t> (pad)].meter.get();
        if (name.startsWith ("out."))
            return strips_[static_cast<std::size_t> (pad)].out.get();
        return nullptr;
    }

private:
    struct Strip {
        std::unique_ptr<juce::Label> name, out;
        std::unique_ptr<Meter> meter;
        VoxKnob* level = nullptr;
        VoxKnob* pan = nullptr;
        VoxKnob* send1 = nullptr;
        VoxKnob* send2 = nullptr;
        VoxButton* mute = nullptr;
        VoxButton* solo = nullptr;
    };
    DrumsProcessor& processor_;
    HostControls host_;
    std::array<Strip, padCount> strips_;
    std::unique_ptr<juce::Label> delayLabel_, mainLabel_;
    VoxComboBox* delayTime_ = nullptr;
    std::vector<VoxKnob*> fx_;
    std::array<std::unique_ptr<Meter>, 2> main_;
};

//==============================================================================
class GeneratePage final : public Page {
public:
    explicit GeneratePage (DrumsProcessor& processor) : processor_ (processor)
    {
        styleLabel_ = makeLabel ("Style");
        addAndMakeVisible (*styleLabel_);
        style_ = std::make_unique<VoxComboBox>();
        for (int i = 0; i < styleCount; ++i)
            style_->addItem (styleName (static_cast<Style> (i)), i + 1);
        style_->onChange = [this] {
            change ([this] (GeneratorSettings& g) { g.style = static_cast<Style> (juce::jmax (0, style_->getSelectedItemIndex())); });
        };
        addAndMakeVisible (*style_);
        static const char* names[8] = { "density", "groove", "syncopation", "swing", "humanize", "complexity", "fills", "variation" };
        static const char* labels[8] = { "Density", "Groove", "Syncopation", "Swing", "Humanize", "Complexity", "Fills", "Variation" };
        for (int i = 0; i < 8; ++i)
        {
            auto& k = amounts_[static_cast<std::size_t> (i)];
            k = std::make_unique<VoxKnob> (labels[i], VoxKnob::Size::Normal);
            k->setName (names[i]);
            auto& s = k->getSlider();
            s.setRange (0.0, 1.0, 0.0);
            s.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + " %"; };
            s.onValueChange = [this, i, &s] {
                change ([i, &s] (GeneratorSettings& g) { *field (g, i) = static_cast<float> (s.getValue()); });
            };
            addAndMakeVisible (*k);
        }
        seed_ = makeLabel ("Seed", true);
        addAndMakeVisible (*seed_);
        lockLabel_ = makeLabel ("Lock lanes (the generator leaves them as they are)");
        mutateLabel_ = makeLabel ("Lanes to mutate");
        addAndMakeVisible (*lockLabel_);
        addAndMakeVisible (*mutateLabel_);
        for (int lane = 0; lane < padCount; ++lane)
        {
            auto& lock = locks_[static_cast<std::size_t> (lane)];
            lock = std::make_unique<VoxButton> (juce::String (lane + 1), VoxButton::Type::Toggle);
            lock->setClickingTogglesState (true);
            lock->onClick = [this, lane] {
                const bool on = locks_[static_cast<std::size_t> (lane)]->getToggleState();
                change ([lane, on] (GeneratorSettings& g) {
                    g.laneLocks = static_cast<std::uint16_t> (on ? g.laneLocks | (1u << lane) : g.laneLocks & ~(1u << lane));
                });
            };
            addAndMakeVisible (*lock);
            auto& m = mutate_[static_cast<std::size_t> (lane)];
            m = std::make_unique<VoxButton> (juce::String (lane + 1), VoxButton::Type::Toggle);
            m->setClickingTogglesState (true);
            m->setToggleState (lane >= 3, juce::dontSendNotification);   // hats and percussion by default
            addAndMakeVisible (*m);
        }
        const auto action = [this] (std::unique_ptr<VoxButton>& b, const juce::String& text, const juce::String& tip,
                                    std::function<void()> run, VoxButton::Type type = VoxButton::Type::Secondary) {
            b = std::make_unique<VoxButton> (text, type);
            b->setTooltip (tip);
            b->onClick = std::move (run);
            addAndMakeVisible (*b);
        };
        using G = DrumsProcessor::Generate;
        action (pattern_, "Generate Pattern", "A new pattern in the selected slot (new seed)",
                [this] { run (G::pattern, "Pattern generated"); }, VoxButton::Type::Primary);
        action (song_, "Generate Song", "New patterns in all five slots, their B variations and the song timeline (new seed)",
                [this] { run (G::song, "Song generated"); }, VoxButton::Type::Primary);
        action (mutateButton_, "Mutate Lanes", "Regenerate only the lanes chosen below (new seed)", [this] { run (G::mutate, "Lanes mutated"); });
        action (fill_, "Fill", "Write a fill into the end of the pattern's last bar (new seed)", [this] { run (G::fill, "Fill written"); });
        action (variation_, "Variation A > B", "Write variation B from pattern A (new seed)", [this] { run (G::variation, "Variation B written"); });
        action (undo_, "Undo", "Undo the last generator or pattern edit", [this] {
            processor_.patternHistory().undo (processor_.patterns());
            status_->setText ("Undone", juce::dontSendNotification);
        });
        action (redo_, "Redo", "Redo", [this] {
            processor_.patternHistory().redo (processor_.patterns());
            status_->setText ("Redone", juce::dontSendNotification);
        });
        status_ = makeLabel ("", true);
        addAndMakeVisible (*status_);
        refresh();
    }

    void update() override
    {
        if (processor_.generator() != shown_)
            refresh();
        undo_->setEnabled (processor_.patternHistory().canUndo());
        redo_->setEnabled (processor_.patternHistory().canRedo());
    }

    void resized() override
    {
        auto area = getLocalBounds();
        auto left = area.removeFromLeft (juce::jlimit (380, 520, area.getWidth() * 45 / 100));
        area.removeFromLeft (sp::xxl);
        auto row = left.removeFromTop (16);
        styleLabel_->setBounds (row);
        row = left.removeFromTop (vox::ui::tokens::size::controlHeight);
        style_->setBounds (row.removeFromLeft (row.getWidth() / 2));
        seed_->setBounds (row.withTrimmedLeft (sp::lg));
        left.removeFromTop (sp::lg);
        const int kh = juce::jlimit (64, 96, (left.getHeight() - sp::sm) / 2);
        for (int r = 0; r < 2; ++r)
        {
            auto knobs = left.removeFromTop (kh);
            const int kw = knobs.getWidth() / 4;
            for (int i = 0; i < 4; ++i)
                amounts_[static_cast<std::size_t> (r * 4 + i)]->setBounds (knobs.removeFromLeft (kw));
            left.removeFromTop (sp::sm);
        }
        const auto lanes = [] (juce::Rectangle<int> r, std::array<std::unique_ptr<VoxButton>, padCount>& buttons) {
            const int w = r.getWidth() / 8, h = r.getHeight() / 2;
            for (int lane = 0; lane < padCount; ++lane)
                buttons[static_cast<std::size_t> (lane)]->setBounds (
                    juce::Rectangle<int> (r.getX() + (lane % 8) * w, r.getY() + (lane / 8) * h, w, h).reduced (2));
        };
        lockLabel_->setBounds (area.removeFromTop (16));
        lanes (area.removeFromTop (64), locks_);
        area.removeFromTop (sp::md);
        mutateLabel_->setBounds (area.removeFromTop (16));
        lanes (area.removeFromTop (64), mutate_);
        area.removeFromTop (sp::lg);
        auto buttons = area.removeFromTop (36);
        const int bw = buttons.getWidth() / 2;
        pattern_->setBounds (buttons.removeFromLeft (bw).reduced (2));
        song_->setBounds (buttons.reduced (2));
        area.removeFromTop (sp::xs);
        buttons = area.removeFromTop (36);
        const int cw = buttons.getWidth() / 3;
        mutateButton_->setBounds (buttons.removeFromLeft (cw).reduced (2));
        fill_->setBounds (buttons.removeFromLeft (cw).reduced (2));
        variation_->setBounds (buttons.reduced (2));
        area.removeFromTop (sp::xs);
        buttons = area.removeFromTop (36);
        undo_->setBounds (buttons.removeFromLeft (cw).reduced (2));
        redo_->setBounds (buttons.removeFromLeft (cw).reduced (2));
        area.removeFromTop (sp::sm);
        status_->setBounds (area.removeFromTop (20));
    }

    juce::Component* named (const juce::String& name) override
    {
        if (name == "style") return style_.get();
        if (name == "seed") return seed_.get();
        if (name == "status") return status_.get();
        if (name == "pattern") return pattern_.get();
        if (name == "song") return song_.get();
        if (name == "mutate") return mutateButton_.get();
        if (name == "fill") return fill_.get();
        if (name == "variation.ab") return variation_.get();
        if (name == "undo") return undo_.get();
        if (name == "redo") return redo_.get();
        for (auto& k : amounts_)
            if (k->getName() == name)
                return &k->getSlider();
        const int lane = name.fromFirstOccurrenceOf (".", false, false).getIntValue() - 1;
        if (lane >= 0 && lane < padCount && name.startsWith ("lock."))
            return locks_[static_cast<std::size_t> (lane)].get();
        if (lane >= 0 && lane < padCount && name.startsWith ("mutate."))
            return mutate_[static_cast<std::size_t> (lane)].get();
        return nullptr;
    }

private:
    static float* field (GeneratorSettings& g, int i)
    {
        float* fields[8] = { &g.density, &g.groove, &g.syncopation, &g.swing, &g.humanize, &g.complexity, &g.fills, &g.variation };
        return fields[i];
    }
    void change (const std::function<void (GeneratorSettings&)>& edit)
    {
        if (refreshing_)
            return;
        auto g = processor_.generator();
        edit (g);
        processor_.setGenerator (g);
        shown_ = g;
    }
    void run (DrumsProcessor::Generate what, const juce::String& done)
    {
        std::uint32_t mask = 0;
        for (int lane = 0; lane < padCount; ++lane)
            if (mutate_[static_cast<std::size_t> (lane)]->getToggleState())
                mask |= 1u << lane;
        const bool changed = processor_.generate (what, mask);
        refresh();
        status_->setText (changed ? done + " (seed " + juce::String (shown_.seed) + ")"
                                  : juce::String ("Nothing changed: every target step or lane is locked"),
                          juce::dontSendNotification);
    }
    void refresh()
    {
        refreshing_ = true;
        shown_ = processor_.generator();
        style_->setSelectedItemIndex (static_cast<int> (shown_.style), juce::dontSendNotification);
        for (int i = 0; i < 8; ++i)
            amounts_[static_cast<std::size_t> (i)]->getSlider().setValue (*field (shown_, i), juce::dontSendNotification);
        for (int lane = 0; lane < padCount; ++lane)
            locks_[static_cast<std::size_t> (lane)]->setToggleState ((shown_.laneLocks & (1u << lane)) != 0, juce::dontSendNotification);
        seed_->setText ("Seed " + juce::String (shown_.seed), juce::dontSendNotification);
        refreshing_ = false;
    }

    DrumsProcessor& processor_;
    GeneratorSettings shown_;
    bool refreshing_ = false;
    std::unique_ptr<juce::Label> styleLabel_, seed_, lockLabel_, mutateLabel_, status_;
    std::unique_ptr<VoxComboBox> style_;
    std::array<std::unique_ptr<VoxKnob>, 8> amounts_;
    std::array<std::unique_ptr<VoxButton>, padCount> locks_, mutate_;
    std::unique_ptr<VoxButton> pattern_, song_, mutateButton_, fill_, variation_, undo_, redo_;
};

//==============================================================================
class RoutingPage final : public Page {
public:
    explicit RoutingPage (DrumsProcessor& processor) : processor_ (processor), host_ (processor, *this)
    {
        juce::StringArray notes, outputs;
        for (int n = 0; n < 128; ++n)
            notes.add (noteName (n));
        for (int o = 0; o <= kit::auxOutputs; ++o)
            outputs.add (outputName (o));
        for (int pad = 0; pad < padCount; ++pad)
        {
            auto& r = rows_[static_cast<std::size_t> (pad)];
            r.name = makeLabel ("");
            r.note = std::make_unique<VoxComboBox>();
            r.note->addItemList (notes, 1);
            r.note->setTooltip ("The MIDI note that plays this pad");
            r.note->onChange = [this, pad] {
                set (pad, [this, pad] (kit::PadSettings& p) {
                    p.note = static_cast<std::uint8_t> (juce::jlimit (0, 127, rows_[static_cast<std::size_t> (pad)].note->getSelectedItemIndex()));
                });
            };
            r.output = std::make_unique<VoxComboBox>();
            r.output->addItemList (outputs, 1);
            r.output->setTooltip ("Where this pad's dry signal goes. Sends always return to Main.");
            r.output->onChange = [this, pad] {
                set (pad, [this, pad] (kit::PadSettings& p) {
                    p.output = static_cast<std::uint8_t> (
                        juce::jlimit (0, kit::auxOutputs, rows_[static_cast<std::size_t> (pad)].output->getSelectedItemIndex()));
                });
            };
            r.state = makeLabel ("", true);
            for (auto* comp : std::initializer_list<juce::Component*> { r.name.get(), r.note.get(), r.output.get(), r.state.get() })
                addAndMakeVisible (*comp);
        }
        modeLabel_ = makeLabel ("Sequencer");
        addAndMakeVisible (*modeLabel_);
        mode_ = &host_.combo ("drm.seq.mode");
        sync_ = makeLabel ("", true);
        addAndMakeVisible (*sync_);
        buses_ = makeLabel ("", true);
        addAndMakeVisible (*buses_);
        refresh();
    }

    void update() override
    {
        if (processor_.kitStore().generation() != generation_)
            refresh();
        const auto active = processor_.activeOutputs();
        const auto kit = processor_.kitStore().snapshot();
        for (int pad = 0; pad < padCount; ++pad)
        {
            const int output = kit.pads[static_cast<std::size_t> (pad)].output;
            const auto text = (active & (1u << output)) != 0 ? juce::String ("active")
                                                             : juce::String ("disabled in the host: plays on Main");
            auto& label = *rows_[static_cast<std::size_t> (pad)].state;
            if (label.getText() != text)
                label.setText (text, juce::dontSendNotification);
        }
        juce::StringArray on;
        for (int bus = 0; bus <= kit::auxOutputs; ++bus)
            if ((active & (1u << bus)) != 0)
                on.add (outputName (bus));
        const auto busText = "Outputs enabled by the host: " + on.joinIntoString (", ");
        if (buses_->getText() != busText)
            buses_->setText (busText, juce::dontSendNotification);
        const auto song = processor_.songBarPlaying();
        const auto syncText = juce::String (processor_.transportPlaying() ? "Host playing" : "Host stopped") + ", "
                            + juce::String (processor_.lastBpm(), 1) + " BPM, "
                            + (song >= 0 ? "Song bar " + juce::String (song + 1) : juce::String ("Pattern mode"));
        if (sync_->getText() != syncText)
            sync_->setText (syncText, juce::dontSendNotification);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        auto bottom = area.removeFromBottom (52);
        area.removeFromBottom (sp::sm);
        const int rowH = juce::jlimit (30, 44, area.getHeight() / 8);
        const int colW = area.getWidth() / 2;
        for (int pad = 0; pad < padCount; ++pad)
        {
            auto& r = rows_[static_cast<std::size_t> (pad)];
            auto row = juce::Rectangle<int> (area.getX() + (pad / 8) * colW, area.getY() + (pad % 8) * rowH, colW, rowH).reduced (sp::xs, 2);
            r.name->setBounds (row.removeFromLeft (96));
            r.note->setBounds (row.removeFromLeft (104).withSizeKeepingCentre (100, juce::jmin (28, rowH - 4)));
            r.output->setBounds (row.removeFromLeft (94).withSizeKeepingCentre (90, juce::jmin (28, rowH - 4)));
            r.state->setBounds (row.withTrimmedLeft (sp::sm));
        }
        auto mode = bottom.removeFromLeft (200);
        modeLabel_->setBounds (mode.removeFromTop (16));
        mode_->setBounds (mode.removeFromTop (vox::ui::tokens::size::controlHeight).withTrimmedRight (sp::lg));
        sync_->setBounds (bottom.removeFromTop (24));
        buses_->setBounds (bottom.removeFromTop (24));
    }

    juce::ComboBox* combo (const juce::String& id) override { return host_.comboFor (id); }
    juce::Component* named (const juce::String& name) override
    {
        if (name == "sync")
            return sync_.get();
        const int pad = name.fromFirstOccurrenceOf (".", false, false).getIntValue() - 1;
        if (pad < 0 || pad >= padCount)
            return nullptr;
        auto& r = rows_[static_cast<std::size_t> (pad)];
        if (name.startsWith ("note.")) return r.note.get();
        if (name.startsWith ("output.")) return r.output.get();
        if (name.startsWith ("state.")) return r.state.get();
        return nullptr;
    }

private:
    void set (int pad, const std::function<void (kit::PadSettings&)>& edit)
    {
        if (refreshing_)
            return;
        auto settings = processor_.kitStore().pad (pad);
        edit (settings);
        processor_.kitStore().setPad (pad, settings);
    }
    void refresh()
    {
        refreshing_ = true;
        generation_ = processor_.kitStore().generation();
        const auto kit = processor_.kitStore().snapshot();
        for (int pad = 0; pad < padCount; ++pad)
        {
            auto& r = rows_[static_cast<std::size_t> (pad)];
            const auto& p = kit.pads[static_cast<std::size_t> (pad)];
            r.name->setText (juce::String (pad + 1) + "  " + modelName (p.model), juce::dontSendNotification);
            r.note->setSelectedItemIndex (p.note, juce::dontSendNotification);
            r.output->setSelectedItemIndex (p.output, juce::dontSendNotification);
        }
        refreshing_ = false;
    }

    struct Row {
        std::unique_ptr<juce::Label> name, state;
        std::unique_ptr<VoxComboBox> note, output;
    };
    DrumsProcessor& processor_;
    HostControls host_;
    std::array<Row, padCount> rows_;
    std::unique_ptr<juce::Label> modeLabel_, sync_, buses_;
    VoxComboBox* mode_ = nullptr;
    std::uint64_t generation_ = 0;
    bool refreshing_ = false;
};

//==============================================================================
class PresetsPage final : public Page, public juce::ListBoxModel {
public:
    PresetsPage (DrumsProcessor& processor, std::function<int()> selectedPad)
        : processor_ (processor), selectedPad_ (std::move (selectedPad))
    {
        typeLabel_ = makeLabel ("Type");
        addAndMakeVisible (*typeLabel_);
        type_ = std::make_unique<VoxComboBox>();
        for (int i = 0; i < 4; ++i)
            type_->addItem (presetTypeName (static_cast<PresetType> (i)), i + 1);
        type_->setSelectedItemIndex (0, juce::dontSendNotification);
        type_->onChange = [this] { rescan(); };
        addAndMakeVisible (*type_);
        list_ = std::make_unique<juce::ListBox> ("presets", this);
        list_->setRowHeight (24);
        list_->setColour (juce::ListBox::backgroundColourId, c::panel);
        addAndMakeVisible (*list_);
        nameLabel_ = makeLabel ("Name for Save");
        addAndMakeVisible (*nameLabel_);
        name_ = std::make_unique<juce::TextEditor> ("name");
        name_->setText ("My Preset", false);
        addAndMakeVisible (*name_);
        const auto action = [this] (std::unique_ptr<VoxButton>& b, const juce::String& text, const juce::String& tip,
                                    std::function<void()> run, VoxButton::Type type = VoxButton::Type::Secondary) {
            b = std::make_unique<VoxButton> (text, type);
            b->setTooltip (tip);
            b->onClick = std::move (run);
            addAndMakeVisible (*b);
        };
        action (load_, "Load", "Load the selected preset (a pad preset loads onto the selected pad, a pattern into the selected slot)",
                [this] { loadSelected(); }, VoxButton::Type::Primary);
        action (save_, "Save", "Save the current kit, selected pad, selected pattern or song under the name above", [this] { saveCurrent(); });
        action (undo_, "Undo Load", "Return to the state before the last preset load", [this] {
            report (processor_.presets().undoLoad(), "Load undone");
        });
        action (import_, "Import Kit...", "Import a portable kit (.voxkit) with its samples", [this] {
            chooser_ = std::make_unique<juce::FileChooser> ("Import a portable kit", juce::File(), "*.voxkit");
            chooser_->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                   [this] (const juce::FileChooser& fc) {
                                       if (fc.getResult().existsAsFile())
                                           report (processor_.presets().importPortableKit (fc.getResult()), "Portable kit imported");
                                   });
        });
        action (export_, "Export Kit...", "Export the current kit and its sample files as one portable kit (.voxkit)", [this] {
            chooser_ = std::make_unique<juce::FileChooser> (
                "Export a portable kit",
                juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (name_->getText() + ".voxkit"), "*.voxkit");
            chooser_->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                       | juce::FileBrowserComponent::warnAboutOverwriting,
                                   [this] (const juce::FileChooser& fc) {
                                       if (fc.getResult() != juce::File())
                                           report (processor_.presets().exportPortableKit (fc.getResult().withFileExtension ("voxkit"),
                                                                                           name_->getText()),
                                                   "Portable kit exported");
                                   });
        });
        status_ = makeLabel ("", true);
        addAndMakeVisible (*status_);
        samples_ = makeLabel ("", true);
        samples_->setJustificationType (juce::Justification::topLeft);
        addAndMakeVisible (*samples_);
        rescan();
    }

    void update() override
    {
        undo_->setEnabled (processor_.presets().canUndoLoad());
        const auto diagnostics = processor_.samples().diagnostics();
        const auto text = diagnostics.isEmpty() ? juce::String ("Samples: all references resolved") : "Samples:\n" + diagnostics;
        if (samples_->getText() != text)
            samples_->setText (text, juce::dontSendNotification);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        auto left = area.removeFromLeft (juce::jlimit (300, 460, area.getWidth() * 42 / 100));
        area.removeFromLeft (sp::xxl);
        typeLabel_->setBounds (left.removeFromTop (16));
        type_->setBounds (left.removeFromTop (vox::ui::tokens::size::controlHeight).withWidth (160));
        left.removeFromTop (sp::sm);
        list_->setBounds (left);
        nameLabel_->setBounds (area.removeFromTop (16));
        name_->setBounds (area.removeFromTop (28).withWidth (juce::jmin (320, area.getWidth())));
        area.removeFromTop (sp::md);
        auto row = area.removeFromTop (36);
        const int bw = juce::jmin (130, row.getWidth() / 3);
        load_->setBounds (row.removeFromLeft (bw).reduced (2));
        save_->setBounds (row.removeFromLeft (bw).reduced (2));
        undo_->setBounds (row.removeFromLeft (bw).reduced (2));
        area.removeFromTop (sp::xs);
        row = area.removeFromTop (36);
        import_->setBounds (row.removeFromLeft (bw).reduced (2));
        export_->setBounds (row.removeFromLeft (bw).reduced (2));
        area.removeFromTop (sp::md);
        status_->setBounds (area.removeFromTop (40));
        area.removeFromTop (sp::sm);
        samples_->setBounds (area);
    }

    juce::Component* named (const juce::String& name) override
    {
        if (name == "type") return type_.get();
        if (name == "list") return list_.get();
        if (name == "name") return name_.get();
        if (name == "load") return load_.get();
        if (name == "save") return save_.get();
        if (name == "undo") return undo_.get();
        if (name == "import") return import_.get();
        if (name == "export") return export_.get();
        if (name == "status") return status_.get();
        return nullptr;
    }

private:
    struct Item {
        juce::String label;
        int factory = -1;    // factory kit index, or -1
        juce::File file;     // user preset
    };
    PresetType type() const { return static_cast<PresetType> (juce::jlimit (0, 3, type_->getSelectedItemIndex())); }
    void rescan()
    {
        items_.clear();
        const auto t = type();
        // Factory content: kits under Kit, each kit's patterns and song under Song.
        if (t == PresetType::kit || t == PresetType::song)
            for (int kit = 0; kit < factoryKitCount; ++kit)
                items_.push_back ({ juce::String ("Factory: ") + factoryKitInfo (kit).name
                                        + (t == PresetType::song ? " patterns and song" : ""),
                                    kit, {} });
        for (const auto& file : processor_.presets().userPresets (t))
            items_.push_back ({ file.getFileNameWithoutExtension(), -1, file });
        list_->updateContent();
        list_->repaint();
    }
    void loadSelected()
    {
        const int row = list_->getSelectedRow();
        if (row < 0 || row >= static_cast<int> (items_.size()))
        {
            status_->setText ("Select a preset in the list first", juce::dontSendNotification);
            return;
        }
        const auto& item = items_[static_cast<std::size_t> (row)];
        auto& presets = processor_.presets();
        if (item.factory >= 0)
            report (type() == PresetType::kit ? presets.loadFactoryKit (item.factory) : presets.loadFactoryPatterns (item.factory),
                    "Loaded " + item.label);
        else
            report (presets.load (item.file, type(), type() == PresetType::pad ? selectedPad_() : -1), "Loaded " + item.label);
    }
    void saveCurrent()
    {
        const auto t = type();
        const auto name = juce::File::createLegalFileName (name_->getText().trim());
        if (name.isEmpty())
        {
            status_->setText ("Enter a name first", juce::dontSendNotification);
            return;
        }
        const auto file = Presets::userFolder (t).getChildFile (name + presetExtension (t));
        const int index = t == PresetType::pad ? selectedPad_() : t == PresetType::pattern ? processor_.currentSlot() : 0;
        report (processor_.presets().save (t, name, file, index), "Saved " + file.getFullPathName());
        rescan();
    }
    void report (const juce::Result& result, const juce::String& done)
    {
        status_->setText (result.wasOk() ? done : "Not loaded, nothing changed: " + result.getErrorMessage(), juce::dontSendNotification);
        status_->setColour (juce::Label::textColourId, result.wasOk() ? c::text : juce::Colours::orange);
    }

    int getNumRows() override { return static_cast<int> (items_.size()); }
    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (row < 0 || row >= static_cast<int> (items_.size()))
            return;
        if (selected)
        {
            g.setColour (processor_.product().accent.withAlpha (0.35f));
            g.fillRect (0, 0, width, height);
        }
        g.setColour (c::text);
        g.setFont (vox::ui::typography::valueText());
        g.drawText (items_[static_cast<std::size_t> (row)].label, 8, 0, width - 16, height, juce::Justification::centredLeft);
    }
    void listBoxItemDoubleClicked (int, const juce::MouseEvent&) override { loadSelected(); }

    DrumsProcessor& processor_;
    std::function<int()> selectedPad_;
    std::vector<Item> items_;
    std::unique_ptr<juce::Label> typeLabel_, nameLabel_, status_, samples_;
    std::unique_ptr<VoxComboBox> type_;
    std::unique_ptr<juce::ListBox> list_;
    std::unique_ptr<juce::TextEditor> name_;
    std::unique_ptr<VoxButton> load_, save_, undo_, import_, export_;
    std::unique_ptr<juce::FileChooser> chooser_;
};

//==============================================================================
class AdvancedPage final : public Page {
public:
    explicit AdvancedPage (DrumsProcessor& processor) : processor_ (processor), host_ (processor, *this)
    {
        voicesLabel_ = makeLabel ("Voice limit");
        addAndMakeVisible (*voicesLabel_);
        voices_ = std::make_unique<VoxComboBox>();
        voices_->addItemList ({ "16 voices", "32 voices", "64 voices" }, 1);
        voices_->setTooltip ("The size of the voice pool. Changing it stops the voices that are sounding.");
        voices_->onChange = [this] {
            if (! refreshing_)
                processor_.kitStore().setVoiceLimit (16 << juce::jlimit (0, 2, voices_->getSelectedItemIndex()));
        };
        addAndMakeVisible (*voices_);
        velocity_ = &host_.knob ("drm.kit.velocity", "Velocity", VoxKnob::Size::Normal);
        choke_ = &host_.toggle ("drm.kit.hat_choke", "Choke groups");
        panic_ = std::make_unique<VoxButton> ("Panic", VoxButton::Type::Danger);
        panic_->setTooltip ("Stop every voice and clear the delay and reverb tails");
        panic_->onClick = [this] { processor_.reset(); };
        addAndMakeVisible (*panic_);
        diagnostics_ = makeLabel ("", true);
        diagnostics_->setJustificationType (juce::Justification::topLeft);
        addAndMakeVisible (*diagnostics_);
        refresh();
    }

    void update() override
    {
        if (processor_.kitStore().generation() != generation_)
            refresh();
        const auto mb = [] (std::size_t bytes) { return juce::String (static_cast<double> (bytes) / (1024.0 * 1024.0), 1) + " MB"; };
        juce::String text;
        text << "Active voices: " << processor_.activeVoices() << "\n"
             << "Dropped MIDI events: " << juce::String (static_cast<juce::int64> (processor_.droppedMidiEvents())) << "\n"
             << "Sample memory: " << mb (processor_.samples().committedBytes()) << " of " << mb (processor_.samples().budget()) << "\n"
             << "Sample rate: " << juce::String (processor_.preparedSampleRate(), 0) << " Hz, block " << processor_.preparedBlockSize() << "\n"
             << "Outputs: Main + Out 2..9 (9 stereo buses); enabled now: "
             << juce::String (juce::BigInteger (static_cast<juce::int64> (processor_.activeOutputs())).countNumberOfSetBits()) << "\n"
             << "Version " << JucePlugin_VersionString << ", state schema " << juce::String (processor_.product().stateSchema)
             << ", sections DPRM DPAD(2) DSMP(2) DSEQ(3) DSNG(1) DGEN(1) DUIS(1)";
        const auto error = processor_.lastStateError();
        if (error.isNotEmpty())
            text << "\nLast rejected state: " << error;
        const auto notice = processor_.lastStateNotice();
        if (notice.isNotEmpty())
            text << "\nState: " << notice;
        if (diagnostics_->getText() != text)
            diagnostics_->setText (text, juce::dontSendNotification);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        auto top = area.removeFromTop (84);
        auto voices = top.removeFromLeft (180);
        voicesLabel_->setBounds (voices.removeFromTop (16));
        voices_->setBounds (voices.removeFromTop (vox::ui::tokens::size::controlHeight).withTrimmedRight (sp::lg));
        velocity_->setBounds (top.removeFromLeft (90));
        top.removeFromLeft (sp::lg);
        choke_->setBounds (top.removeFromLeft (130).withSizeKeepingCentre (126, 30));
        top.removeFromLeft (sp::lg);
        panic_->setBounds (top.removeFromLeft (100).withSizeKeepingCentre (96, 30));
        area.removeFromTop (sp::lg);
        diagnostics_->setBounds (area);
    }

    juce::Slider* slider (const juce::String& id) override { return host_.slider (id); }
    juce::Button* button (const juce::String& id) override { return host_.button (id); }
    juce::Component* named (const juce::String& name) override
    {
        if (name == "voices") return voices_.get();
        if (name == "panic") return panic_.get();
        if (name == "diagnostics") return diagnostics_.get();
        return nullptr;
    }

private:
    void refresh()
    {
        refreshing_ = true;
        generation_ = processor_.kitStore().generation();
        const int limit = processor_.kitStore().snapshot().voiceLimit;
        voices_->setSelectedItemIndex (limit <= 16 ? 0 : limit <= 32 ? 1 : 2, juce::dontSendNotification);
        refreshing_ = false;
    }
    DrumsProcessor& processor_;
    HostControls host_;
    std::unique_ptr<juce::Label> voicesLabel_, diagnostics_;
    std::unique_ptr<VoxComboBox> voices_;
    VoxKnob* velocity_ = nullptr;
    VoxButton* choke_ = nullptr;
    std::unique_ptr<VoxButton> panic_;
    std::uint64_t generation_ = 0;
    bool refreshing_ = false;
};

} // namespace

std::unique_ptr<Page> makeMixerPage (DrumsProcessor& p) { return std::make_unique<MixerPage> (p); }
std::unique_ptr<Page> makeGeneratePage (DrumsProcessor& p) { return std::make_unique<GeneratePage> (p); }
std::unique_ptr<Page> makeRoutingPage (DrumsProcessor& p) { return std::make_unique<RoutingPage> (p); }
std::unique_ptr<Page> makePresetsPage (DrumsProcessor& p, std::function<int()> pad) { return std::make_unique<PresetsPage> (p, std::move (pad)); }
std::unique_ptr<Page> makeAdvancedPage (DrumsProcessor& p) { return std::make_unique<AdvancedPage> (p); }

//==============================================================================
SongStrip::SongStrip (DrumsProcessor& processor, juce::Colour accent) : processor_ (processor), accent_ (accent)
{
    const auto toggle = [this] (std::unique_ptr<VoxButton>& b, const juce::String& text, const juce::String& tip) {
        b = std::make_unique<VoxButton> (text, VoxButton::Type::Toggle);
        b->setClickingTogglesState (true);
        b->setTooltip (tip);
        addAndMakeVisible (*b);
    };
    const auto action = [this] (std::unique_ptr<VoxButton>& b, const juce::String& text, const juce::String& tip, std::function<void()> run) {
        b = std::make_unique<VoxButton> (text);
        b->setTooltip (tip);
        b->onClick = std::move (run);
        addAndMakeVisible (*b);
    };
    const auto combo = [this] (std::unique_ptr<VoxComboBox>& b, const juce::StringArray& items, const juce::String& tip) {
        b = std::make_unique<VoxComboBox>();
        b->addItemList (items, 1);
        b->setSelectedItemIndex (0, juce::dontSendNotification);
        b->setTooltip (tip);
        addAndMakeVisible (*b);
    };
    toggle (mode_, "Song", "On: the sequencer plays the song timeline. Off: it plays the selected pattern.");
    mode_->onClick = [this] {
        const bool on = mode_->getToggleState();
        apply ([on] (SongData& s) { s.songMode = on; });
    };
    combo (length_, { "16 bars", "32 bars", "64 bars" }, "Song length");
    length_->onChange = [this] {
        if (refreshing_)
            return;
        const int length = 16 << juce::jlimit (0, 2, length_->getSelectedItemIndex());
        apply ([length] (SongData& s) {
            s.length = static_cast<std::uint8_t> (length);
            // The loop region stays inside the song.
            s.loopStart = static_cast<std::uint8_t> (juce::jmin<int> (s.loopStart, length - 1));
            s.loopLength = static_cast<std::uint8_t> (juce::jlimit (1, length - s.loopStart, static_cast<int> (s.loopLength)));
        });
        select (selFrom_, selCount_);
        if (auto* parent = getParentComponent())
            parent->resized();
    };
    combo (slot_, { "P1", "P2", "P3", "P4", "P5" }, "The pattern to place");
    combo (variation_, { "A", "B" }, "The variation to place");
    action (place_, "Place", "Place the pattern at the selected bar (one song bar per pattern bar; replaces what is there)", [this] {
        const int at = selFrom_, slot = slot_->getSelectedItemIndex(), variation = variation_->getSelectedItemIndex();
        const auto pattern = std::make_unique<StoredPattern>();
        if (variation != 0)
            processor_.patterns().copyAlternate (slot, *pattern);
        else
            processor_.patterns().copySlot (slot, *pattern);
        const int bars = pattern->bars;
        apply ([=] (SongData& s) { s.place (at, slot, variation, bars); });
    });
    action (insert_, "Ins", "Insert empty bars at the selection (the rest moves right)",
            [this] { const int f = selFrom_, n = selCount_; apply ([=] (SongData& s) { s = songedit::insertEmpty (s, f, n); }); });
    action (delete_, "Del", "Delete the selected bars (the rest moves left)",
            [this] { const int f = selFrom_, n = selCount_; apply ([=] (SongData& s) { s = songedit::removeBars (s, f, n); }); });
    action (dup_, "Dup", "Copy the selected bars after themselves",
            [this] { const int f = selFrom_, n = selCount_; apply ([=] (SongData& s) { s = songedit::duplicate (s, f, n); }); });
    action (clear_, "Clear", "Empty the selected bars",
            [this] { const int f = selFrom_, n = selCount_; apply ([=] (SongData& s) { s = songedit::clear (s, f, n); }); });
    action (copy_, "Copy", "Copy the selected bars", [this] { clipboard_ = songedit::copy (song_, selFrom_, selCount_); refresh(); });
    action (paste_, "Paste", "Paste the copied bars at the selection (replaces)",
            [this] { const int f = selFrom_; const auto bars = clipboard_; apply ([=] (SongData& s) { s = songedit::paste (s, f, bars); }); });
    action (unique_, "Unique", "Make the selected bars independent of their patterns", [this] {
        const int f = selFrom_, n = selCount_;
        const auto all = std::make_shared<PatternStore::Everything>();
        processor_.patterns().copyEverything (*all);
        apply ([=] (SongData& s) { s = songedit::makeUnique (s, f, n, all->slots, all->alternates); });
    });
    toggle (loop_, "Loop", "Loop the region in Song mode");
    loop_->onClick = [this] {
        const bool on = loop_->getToggleState();
        apply ([on] (SongData& s) { s.loopOn = on; });
    };
    action (loopSel_, "Loop=Sel", "Set the loop region to the selected bars", [this] {
        const int f = selFrom_, n = selCount_;
        apply ([=] (SongData& s) {
            s.loopStart = static_cast<std::uint8_t> (f);
            s.loopLength = static_cast<std::uint8_t> (n);
        });
    });
    toggle (edit_, "Edit Bar", "Edit the selected song bar in the step grid (the bar becomes unique when you change it)");
    edit_->onClick = [this] {
        editing_ = edit_->getToggleState();
        if (onEditBar)
            onEditBar (editedBar());
    };
    combo (scope_, { "MIDI: Pattern", "MIDI: Entire Song" }, "What the MIDI button exports");
    scope_->onChange = [this] {
        if (refreshing_)
            return;
        auto ui = processor_.uiState();
        ui.exportScope = static_cast<std::uint8_t> (juce::jlimit (0, 1, scope_->getSelectedItemIndex()));
        processor_.setUiState (ui);
    };
    update();
}

SongStrip::~SongStrip() = default;

void SongStrip::apply (const std::function<void (SongData&)>& change)
{
    if (refreshing_)
        return;
    processor_.editSong (change);
    update();
}

void SongStrip::refresh()
{
    refreshing_ = true;
    mode_->setToggleState (song_.songMode, juce::dontSendNotification);
    loop_->setToggleState (song_.loopOn, juce::dontSendNotification);
    length_->setSelectedItemIndex (song_.length == 64 ? 2 : song_.length == 32 ? 1 : 0, juce::dontSendNotification);
    scope_->setSelectedItemIndex (processor_.uiState().exportScope, juce::dontSendNotification);
    paste_->setEnabled (! clipboard_.empty());
    refreshing_ = false;
}

void SongStrip::update()
{
    bool changed = false;
    if (processor_.patterns().generation() != generation_)
    {
        generation_ = processor_.patterns().generation();
        song_ = processor_.patterns().song();
        selFrom_ = juce::jlimit (0, song_.length - 1, selFrom_);
        selCount_ = juce::jlimit (1, song_.length - selFrom_, selCount_);
        changed = true;
    }
    const int playing = processor_.transportPlaying() ? processor_.songBarPlaying() : -1;
    if (playing != playing_)
    {
        playing_ = playing;
        changed = true;
    }
    if (scope_->getSelectedItemIndex() != processor_.uiState().exportScope)
        changed = true;
    if (changed)
    {
        refresh();
        repaint();
    }
}

int SongStrip::preferredHeight() const
{
    return 30 + sp::xs + (song_.length / 16) * 22;
}

void SongStrip::select (int from, int count)
{
    selFrom_ = juce::jlimit (0, song_.length - 1, from);
    selCount_ = juce::jlimit (1, song_.length - selFrom_, count);
    auto ui = processor_.uiState();
    ui.songBar = static_cast<std::uint8_t> (selFrom_);
    processor_.setUiState (ui);
    if (editing_ && onEditBar)
        onEditBar (selFrom_);
    repaint();
}

juce::Rectangle<int> SongStrip::barBounds (int bar) const
{
    const int rows = juce::jmax (1, song_.length / 16);
    const int w = grid_.getWidth() / 16, h = grid_.getHeight() / rows;
    return { grid_.getX() + (bar % 16) * w, grid_.getY() + (bar / 16) * h, w, h };
}

int SongStrip::barAt (juce::Point<int> p) const
{
    for (int bar = 0; bar < song_.length; ++bar)
        if (barBounds (bar).contains (p))
            return bar;
    return -1;
}

void SongStrip::mouseDown (const juce::MouseEvent& e)
{
    const int bar = barAt (e.getPosition());
    if (bar < 0)
        return;
    if (e.mods.isShiftDown())
        select (juce::jmin (anchor_, bar), std::abs (bar - anchor_) + 1);
    else
    {
        anchor_ = bar;
        select (bar, 1);
    }
}

void SongStrip::mouseDrag (const juce::MouseEvent& e)
{
    const int bar = barAt (e.getPosition());
    if (bar >= 0)
        select (juce::jmin (anchor_, bar), std::abs (bar - anchor_) + 1);
}

void SongStrip::paint (juce::Graphics& g)
{
    static const juce::Colour slots[slotCountForSong] = { juce::Colour (0xff4f8fd9), juce::Colour (0xffef805d), juce::Colour (0xff5fc98a),
                                                         juce::Colour (0xffb07fe0), juce::Colour (0xffe0c050) };
    g.setFont (vox::ui::typography::valueText());
    for (int bar = 0; bar < song_.length; ++bar)
    {
        auto r = barBounds (bar).reduced (1);
        const auto& b = song_.bars[static_cast<std::size_t> (bar)];
        juce::String text = juce::String (bar + 1);
        juce::Colour fill = c::panel;
        if (b.kind == SongBar::Kind::pattern)
        {
            fill = slots[b.slot % slotCountForSong].withAlpha (b.variation != 0 ? 0.75f : 0.5f);
            text << " P" << (b.slot + 1) << (b.variation != 0 ? "B" : "A") << (b.patternBar + 1);
        }
        else if (b.kind == SongBar::Kind::unique)
        {
            fill = c::textSecondary.withAlpha (0.45f);
            text << " U";
        }
        g.setColour (fill);
        g.fillRoundedRectangle (r.toFloat(), 2.0f);
        if (song_.loopOn && bar >= song_.loopStart && bar < song_.loopStart + song_.loopLength)
        {
            g.setColour (accent_);
            g.fillRect (r.removeFromBottom (2));
        }
        if (bar == playing_)
        {
            g.setColour (juce::Colours::white.withAlpha (0.3f));
            g.fillRoundedRectangle (barBounds (bar).reduced (1).toFloat(), 2.0f);
        }
        g.setColour (c::text);
        g.drawText (text, barBounds (bar).reduced (3, 0), juce::Justification::centredLeft);
        if (bar >= selFrom_ && bar < selFrom_ + selCount_)
        {
            g.setColour (accent_);
            g.drawRoundedRectangle (barBounds (bar).reduced (1).toFloat(), 2.0f, 1.5f);
        }
    }
}

void SongStrip::resized()
{
    auto area = getLocalBounds();
    auto top = area.removeFromTop (30);
    const auto put = [&top] (juce::Component& comp, int width) { comp.setBounds (top.removeFromLeft (width).reduced (1)); };
    put (*mode_, 52);
    put (*length_, 84);
    top.removeFromLeft (sp::xs);
    put (*slot_, 62);
    put (*variation_, 54);
    put (*place_, 50);
    top.removeFromLeft (sp::xs);
    for (auto* b : { insert_.get(), delete_.get(), dup_.get() })
        put (*b, 40);
    put (*clear_, 46);
    put (*copy_, 46);
    put (*paste_, 48);
    put (*unique_, 56);
    top.removeFromLeft (sp::xs);
    put (*loop_, 46);
    put (*loopSel_, 66);
    put (*edit_, 64);
    scope_->setBounds (top.removeFromRight (juce::jmin (150, top.getWidth())).reduced (1));
    area.removeFromTop (sp::xs);
    grid_ = area;
}

juce::Button* SongStrip::button (const juce::String& name)
{
    if (name == "mode") return mode_.get();
    if (name == "loop") return loop_.get();
    if (name == "edit") return edit_.get();
    if (name == "place") return place_.get();
    if (name == "insert") return insert_.get();
    if (name == "delete") return delete_.get();
    if (name == "dup") return dup_.get();
    if (name == "clear") return clear_.get();
    if (name == "copy") return copy_.get();
    if (name == "paste") return paste_.get();
    if (name == "unique") return unique_.get();
    if (name == "loopsel") return loopSel_.get();
    return nullptr;
}

juce::ComboBox* SongStrip::combo (const juce::String& name)
{
    if (name == "length") return length_.get();
    if (name == "slot") return slot_.get();
    if (name == "variation") return variation_.get();
    if (name == "scope") return scope_.get();
    return nullptr;
}

} // namespace vox::drums
