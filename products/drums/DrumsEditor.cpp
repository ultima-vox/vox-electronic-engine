#include "DrumsEditor.h"

#include "DrumsParameters.h"
#include "Processor.h"
#include "Tokens.h"
#include "Typography.h"

#include <cmath>

namespace vox::drums {
namespace {

namespace c = vox::ui::tokens::colour;
namespace sp = vox::ui::tokens::spacing;

constexpr int knobWidth = 76, knobHeight = 80;

const EngineParameter* engineParameterFor (const juce::String& id)
{
    for (const auto& entry : pluginParameters())
        if (entry.id == id.toStdString())
            return entry.engine;
    return nullptr;
}

// "Kick Pitch Env" -> "Pitch Env": the pad name is already the section title.
juce::String shortLabel (const juce::String& id)
{
    const auto* p = engineParameterFor (id);
    if (p == nullptr)
        return id;
    juce::String name (p->name);
    for (const auto& pad : DrumEngine::padNames())
        if (name.startsWith (juce::String (pad) + " "))
            return name.fromFirstOccurrenceOf (juce::String (pad) + " ", false, false);
    for (const auto* prefix : { "Kit ", "Seq " })
        if (name.startsWith (prefix))
            return name.fromFirstOccurrenceOf (prefix, false, false);
    return name;
}

bool bipolar (const juce::String& id)
{
    return id.endsWith (".pan") || id.endsWith (".tune") || id.endsWith (".fine")
        || id.endsWith (".transient_attack") || id.endsWith (".transient_sustain");
}

} // namespace

//==============================================================================
class DrumsPanel::PadButton final : public juce::Button {
public:
    PadButton (int pad, juce::Colour accent)
        : juce::Button (juce::String (DrumEngine::padNames()[static_cast<std::size_t> (pad)])),
          pad_ (pad), accent_ (accent)
    {
        setClickingTogglesState (false);
        setTriggeredOnMouseDown (true);
    }

    void flash() { level_ = 1.0f; repaint(); }
    bool decay()
    {
        if (level_ <= 0.0f)
            return false;
        level_ = juce::jmax (0.0f, level_ - 0.12f);
        repaint();
        return true;
    }
    void setSelected (bool selected) { selected_ = selected; repaint(); }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced (2.0f);
        g.setColour (c::panelRaised.interpolatedWith (accent_, 0.55f * level_));
        g.fillRoundedRectangle (r, vox::ui::tokens::radius::medium);
        g.setColour (selected_ ? accent_ : (over || down ? c::border.brighter (0.4f) : c::border));
        g.drawRoundedRectangle (r, vox::ui::tokens::radius::medium, selected_ ? 2.0f : 1.0f);

        auto textArea = getLocalBounds().reduced (sp::sm);
        g.setColour (c::text);
        g.setFont (vox::ui::typography::controlLabel());
        g.drawText (getName(), textArea, juce::Justification::centredLeft);
        g.setColour (c::textMuted);
        g.setFont (vox::ui::typography::valueText());
        const auto note = DrumEngine::padNotes()[static_cast<std::size_t> (pad_)];
        g.drawText (juce::MidiMessage::getMidiNoteName (note, true, true, 3) + " / " + juce::String (note),
                    textArea, juce::Justification::centredRight);
    }

private:
    int pad_;
    juce::Colour accent_;
    float level_ = 0.0f;
    bool selected_ = false;
};

//==============================================================================
// Read-only view of the selected factory pattern (bar of the playhead) with
// the step under the playhead; pattern editing is DRM-04.
class DrumsPanel::StepView final : public juce::Component {
public:
    StepView (DrumsProcessor& p, juce::Colour accent) : processor_ (p), accent_ (accent) {}

    void update (bool repaintIfChanged = true)
    {
        const auto* pattern = processor_.state().getRawParameterValue (patternParameterId);
        const int index = pattern != nullptr ? static_cast<int> (std::lround (pattern->load())) : 0;
        const bool playing = processor_.transportPlaying();
        const double ppq = processor_.lastPpq();
        const auto g = static_cast<long long> (std::floor (ppq / 0.25));
        const int step = playing && g >= 0 ? static_cast<int> (g % patternSteps) : -1;
        const int bar = g >= 0 ? static_cast<int> ((g / patternSteps) % patternBars) : 0;
        if (index != pattern_ || step != step_ || bar != bar_)
        {
            pattern_ = index;
            step_ = step;
            bar_ = bar;
            if (repaintIfChanged)
                repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        update (false);
        auto area = getLocalBounds();
        auto header = area.removeFromTop (16);
        g.setColour (c::textMuted);
        g.setFont (vox::ui::typography::valueText());
        g.drawText ("Bar " + juce::String (bar_ + 1) + " / " + juce::String (patternBars),
                    header, juce::Justification::centredLeft);

        const int nameWidth = 78;
        const float cellW = static_cast<float> (area.getWidth() - nameWidth) / patternSteps;
        const float cellH = static_cast<float> (area.getHeight()) / padCount;
        for (int lane = 0; lane < padCount; ++lane)
        {
            const float y = static_cast<float> (area.getY()) + cellH * static_cast<float> (lane);
            g.setColour (c::textSecondary);
            g.drawText (juce::String (DrumEngine::padNames()[static_cast<std::size_t> (lane)]),
                        juce::Rectangle<float> (static_cast<float> (area.getX()), y, nameWidth - 4.0f, cellH),
                        juce::Justification::centredLeft);
            for (int s = 0; s < patternSteps; ++s)
            {
                const auto cell = DrumEngine::factoryStep (pattern_, lane, bar_, s);
                juce::Rectangle<float> r (static_cast<float> (area.getX() + nameWidth) + cellW * static_cast<float> (s),
                                          y, cellW, cellH);
                r = r.reduced (1.5f);
                const bool beat = (s % 4) == 0;
                g.setColour (s == step_ ? c::panelRaised.brighter (0.3f) : (beat ? c::panelRaised : c::control));
                g.fillRoundedRectangle (r, 2.0f);
                if (cell.on)
                {
                    const float v = static_cast<float> (cell.velocity) / 127.0f;
                    g.setColour (accent_.withAlpha (0.35f + 0.65f * v));
                    g.fillRoundedRectangle (r.reduced (2.0f), 2.0f);
                    if (cell.accent)
                    {
                        g.setColour (c::text);
                        g.fillEllipse (r.getCentreX() - 2.0f, r.getCentreY() - 2.0f, 4.0f, 4.0f);
                    }
                }
            }
        }
    }

private:
    DrumsProcessor& processor_;
    juce::Colour accent_;
    int pattern_ = -1, step_ = -1, bar_ = 0;
};

//==============================================================================
DrumsPanel::DrumsPanel (DrumsProcessor& processor) : processor_ (processor)
{
    const auto accent = processor.product().accent;
    for (auto* panel : { &padsPanel_, &padPanel_, &kitPanel_, &sequencerPanel_ })
        addAndMakeVisible (*panel);

    for (int pad = 0; pad < padCount; ++pad)
    {
        auto& button = pads_[static_cast<std::size_t> (pad)];
        button = std::make_unique<PadButton> (pad, accent);
        button->onClick = [this, pad] {
            selectPad (pad);
            processor_.audition (pad, 110);
        };
        addAndMakeVisible (*button);
    }

    const auto combo = [this] (BoundCombo& target, const juce::String& id, const juce::String& label) {
        target.id = id;
        target.label = std::make_unique<juce::Label> (id + ".label", label);
        target.label->setColour (juce::Label::textColourId, c::textSecondary);
        target.label->setFont (vox::ui::typography::controlLabel());
        target.combo = std::make_unique<vox::ui::VoxComboBox>();
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (processor_.state().getParameter (id)))
            target.combo->addItemList (choice->choices, 1);
        target.attachment = std::make_unique<ComboAttachment> (processor_.state(), id, *target.combo);
        addAndMakeVisible (*target.label);
        addAndMakeVisible (*target.combo);
    };
    const auto toggle = [this] (BoundToggle& target, const juce::String& id, const juce::String& label) {
        target.id = id;
        target.button = std::make_unique<juce::ToggleButton> (label);
        target.attachment = std::make_unique<ButtonAttachment> (processor_.state(), id, *target.button);
        addAndMakeVisible (*target.button);
    };

    for (const auto* id : { "drm.kit.volume", "drm.kit.velocity" })
        kitKnobs_.push_back (makeKnob (id, shortLabel (id)));
    toggle (hatChoke_, "drm.kit.hat_choke", "Hat choke");

    combo (seqMode_, "drm.seq.mode", "Mode");
    combo (seqPattern_, patternParameterId, "Pattern");
    for (const auto* id : { "drm.seq.swing", "drm.seq.accent" })
        seqKnobs_.push_back (makeKnob (id, shortLabel (id)));
    for (int pad = 0; pad < padCount; ++pad)
    {
        lanes_.emplace_back();
        toggle (lanes_.back(), "drm.seq.lane." + juce::String (padPrefix (pad).substr (4)),
                juce::String (DrumEngine::padNames()[static_cast<std::size_t> (pad)]));
    }
    steps_ = std::make_unique<StepView> (processor_, accent);
    addAndMakeVisible (*steps_);

    selectPad (0);
    startTimerHz (30);
}

DrumsPanel::~DrumsPanel()
{
    stopTimer();
}

DrumsPanel::BoundKnob DrumsPanel::makeKnob (const juce::String& id, const juce::String& label)
{
    BoundKnob bound;
    bound.id = id;
    bound.knob = std::make_unique<vox::ui::VoxKnob> (label, vox::ui::VoxKnob::Size::Normal);
    if (bipolar (id))
        bound.knob->setStyle (vox::ui::VoxKnob::Style::Bipolar);
    auto& slider = bound.knob->getSlider();
    bound.attachment = std::make_unique<SliderAttachment> (processor_.state(), id, slider);
    if (auto* parameter = processor_.state().getParameter (id))
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter);
        bound.knob->setDoubleClickResetValue (ranged->convertFrom0to1 (ranged->getDefaultValue()));
        bound.knob->setTooltip (parameter->getName (64));
    }
    addAndMakeVisible (*bound.knob);
    return bound;
}

void DrumsPanel::selectPad (int pad)
{
    if (pad < 0 || pad >= padCount)
        return;
    selectedPad_ = pad;
    for (int i = 0; i < padCount; ++i)
        pads_[static_cast<std::size_t> (i)]->setSelected (i == pad);
    bindPadControls();
    resized();
    repaint();
}

void DrumsPanel::bindPadControls()
{
    // Attachments first, then the components they point at.
    padKnobs_.clear();
    kickKnobs_.clear();
    padFilter_.attachment.reset();
    padFilter_.combo.reset();
    padFilter_.label.reset();

    const auto prefix = juce::String (padPrefix (selectedPad_));
    for (const auto* name : { "level", "tune", "fine", "decay", "pan", "character",
                              "cutoff", "resonance", "transient_attack", "transient_sustain" })
    {
        const auto id = prefix + "." + name;
        padKnobs_.push_back (makeKnob (id, shortLabel (id)));
    }
    padFilter_.id = prefix + ".filter";
    padFilter_.label = std::make_unique<juce::Label> ("filter.label", "Filter");
    padFilter_.label->setColour (juce::Label::textColourId, c::textSecondary);
    padFilter_.label->setFont (vox::ui::typography::controlLabel());
    padFilter_.combo = std::make_unique<vox::ui::VoxComboBox>();
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (processor_.state().getParameter (padFilter_.id)))
        padFilter_.combo->addItemList (choice->choices, 1);
    padFilter_.attachment = std::make_unique<ComboAttachment> (processor_.state(), padFilter_.id, *padFilter_.combo);
    addAndMakeVisible (*padFilter_.label);
    addAndMakeVisible (*padFilter_.combo);

    if (selectedPad_ == 0)
        for (const auto* name : { "pitch_amount", "pitch_decay", "drive", "sub", "tail" })
        {
            const auto id = prefix + "." + name;
            kickKnobs_.push_back (makeKnob (id, shortLabel (id)));
        }
    padPanel_.setTitle ("PAD " + juce::String (selectedPad_ + 1).paddedLeft ('0', 2) + "  "
                        + juce::String (DrumEngine::padNames()[static_cast<std::size_t> (selectedPad_)]).toUpperCase());
}

void DrumsPanel::layoutKnobRow (std::vector<BoundKnob*> knobs, juce::Rectangle<int> area)
{
    for (auto* k : knobs)
        k->knob->setBounds (area.removeFromLeft (knobWidth).withHeight (knobHeight));
}

void DrumsPanel::paint (juce::Graphics& g)
{
    g.fillAll (c::background);
}

void DrumsPanel::resized()
{
    auto area = getLocalBounds().reduced (sp::xxl, sp::lg);

    // Top: pads (left) and the selected pad (right).
    auto top = area.removeFromTop (juce::jmax (300, area.getHeight() / 2 + 20));
    auto left = top.removeFromLeft (juce::jmin (420, top.getWidth() / 3));
    padsPanel_.setBounds (left.removeFromTop (left.getHeight() - 132));
    left.removeFromTop (sp::md);
    kitPanel_.setBounds (left);
    top.removeFromLeft (sp::lg);
    padPanel_.setBounds (top);

    {
        auto grid = padsPanel_.getContentBounds() + padsPanel_.getPosition();
        const int rows = 4, cols = 2;
        const int w = grid.getWidth() / cols, h = grid.getHeight() / rows;
        for (int pad = 0; pad < padCount; ++pad)
            pads_[static_cast<std::size_t> (pad)]->setBounds (grid.getX() + (pad % cols) * w,
                                                              grid.getY() + (pad / cols) * h, w, h);
    }
    {
        auto kit = kitPanel_.getContentBounds() + kitPanel_.getPosition();
        std::vector<BoundKnob*> row;
        for (auto& k : kitKnobs_)
            row.push_back (&k);
        layoutKnobRow (row, kit.removeFromLeft (knobWidth * 2));
        hatChoke_.button->setBounds (kit.withSizeKeepingCentre (kit.getWidth(), 28));
    }
    {
        auto pad = padPanel_.getContentBounds() + padPanel_.getPosition();
        std::vector<BoundKnob*> first, second;
        for (std::size_t i = 0; i < padKnobs_.size(); ++i)
            (i < 6 ? first : second).push_back (&padKnobs_[i]);
        layoutKnobRow (first, pad.removeFromTop (knobHeight));
        pad.removeFromTop (sp::sm);
        auto row = pad.removeFromTop (knobHeight);
        auto filter = row.removeFromLeft (140);
        if (padFilter_.label != nullptr)
        {
            padFilter_.label->setBounds (filter.removeFromTop (16));
            padFilter_.combo->setBounds (filter.removeFromTop (vox::ui::tokens::size::controlHeight).reduced (0, 0).withWidth (128));
        }
        layoutKnobRow (second, row);
        if (! kickKnobs_.empty())
        {
            pad.removeFromTop (sp::sm);
            std::vector<BoundKnob*> kick;
            for (auto& k : kickKnobs_)
                kick.push_back (&k);
            layoutKnobRow (kick, pad.removeFromTop (knobHeight));
        }
    }

    // Bottom: sequencer.
    area.removeFromTop (sp::md);
    sequencerPanel_.setBounds (area);
    auto seq = sequencerPanel_.getContentBounds() + sequencerPanel_.getPosition();
    auto controls = seq.removeFromLeft (360);
    {
        auto row = controls.removeFromTop (48);
        for (auto* target : { &seqMode_, &seqPattern_ })
        {
            auto cell = row.removeFromLeft (172);
            target->label->setBounds (cell.removeFromTop (16));
            target->combo->setBounds (cell.removeFromTop (vox::ui::tokens::size::controlHeight).withTrimmedRight (sp::sm));
        }
        controls.removeFromTop (sp::sm);
        std::vector<BoundKnob*> knobs;
        for (auto& k : seqKnobs_)
            knobs.push_back (&k);
        auto knobRow = controls.removeFromTop (knobHeight);
        layoutKnobRow (knobs, knobRow.removeFromLeft (knobWidth * 2));
        // Lane switches: two columns next to the knobs.
        const int lw = knobRow.getWidth() / 2;
        const int lh = juce::jmax (18, (knobRow.getHeight() + controls.getHeight()) / 4);
        auto lanesArea = knobRow.withHeight (lh * 4);
        for (int pad = 0; pad < padCount; ++pad)
            lanes_[static_cast<std::size_t> (pad)].button->setBounds (lanesArea.getX() + (pad / 4) * lw,
                                                                      lanesArea.getY() + (pad % 4) * lh, lw, lh);
    }
    seq.removeFromLeft (sp::lg);
    steps_->setBounds (seq);
}

void DrumsPanel::timerCallback()
{
    const auto hits = processor_.takeHitMask();
    for (int pad = 0; pad < padCount; ++pad)
    {
        auto& button = *pads_[static_cast<std::size_t> (pad)];
        if ((hits >> pad) & 1u)
            button.flash();
        else
            button.decay();
    }
    steps_->update();
}

juce::Slider* DrumsPanel::sliderFor (const juce::String& id)
{
    for (auto* list : { &padKnobs_, &kickKnobs_, &kitKnobs_, &seqKnobs_ })
        for (auto& k : *list)
            if (k.id == id)
                return &k.knob->getSlider();
    return nullptr;
}

juce::Button* DrumsPanel::buttonFor (const juce::String& id)
{
    if (hatChoke_.id == id)
        return hatChoke_.button.get();
    for (auto& t : lanes_)
        if (t.id == id)
            return t.button.get();
    return nullptr;
}

juce::ComboBox* DrumsPanel::comboFor (const juce::String& id)
{
    for (auto* target : { &padFilter_, &seqMode_, &seqPattern_ })
        if (target->id == id)
            return target->combo.get();
    return nullptr;
}

juce::Button* DrumsPanel::padButton (int pad)
{
    return pad >= 0 && pad < padCount ? pads_[static_cast<std::size_t> (pad)].get() : nullptr;
}

} // namespace vox::drums
