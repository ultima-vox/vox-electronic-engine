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

constexpr int knobHeight = 80;
int knobWidth = 76;   // narrowed by resized() at small editor sizes

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
        g.drawText (getWidth() >= 140 ? juce::MidiMessage::getMidiNoteName (note, true, true, 3) + " / " + juce::String (note)
                                      : juce::String (note),
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
// The selected pad's sample: overview from the decoded peak table (built off
// the audio thread at load), the start/end region and the pad status.
class DrumsPanel::WaveformView final : public juce::Component {
public:
    WaveformView (DrumsProcessor& p, juce::Colour accent) : processor_ (p), accent_ (accent) {}

    void setPad (int pad) { pad_ = pad; repaint(); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (c::graph);
        g.fillRoundedRectangle (r, vox::ui::tokens::radius::small);
        g.setColour (c::borderSubtle);
        g.drawRoundedRectangle (r.reduced (0.5f), vox::ui::tokens::radius::small, 1.0f);

        const auto info = processor_.samples().pad (pad_);
        const auto prefix = juce::String (padPrefix (pad_));
        if (info.buffer == nullptr)
        {
            g.setColour (c::textMuted);
            g.setFont (vox::ui::typography::valueText());
            g.drawText (info.reference ? "Sample missing: the pad plays its synthesis" : "Drop an audio file here or Load",
                        getLocalBounds(), juce::Justification::centred);
            return;
        }
        const float start = processor_.state().getRawParameterValue (prefix + ".sample_start")->load();
        const float end = processor_.state().getRawParameterValue (prefix + ".sample_end")->load();
        const auto& lo = info.buffer->overviewMin;
        const auto& hi = info.buffer->overviewMax;
        const float mid = r.getCentreY(), half = r.getHeight() * 0.45f;
        const float scale = info.buffer->peak > 0.0f ? 1.0f / info.buffer->peak : 1.0f;
        const auto n = lo.size();
        for (std::size_t i = 0; i < n; ++i)
        {
            const float x = r.getX() + r.getWidth() * static_cast<float> (i) / static_cast<float> (n);
            const float fraction = static_cast<float> (i) / static_cast<float> (n);
            const bool inside = fraction >= start && fraction <= end;
            g.setColour (inside ? accent_ : c::textMuted.withAlpha (0.5f));
            g.drawVerticalLine (juce::roundToInt (x), mid - hi[i] * scale * half, mid - lo[i] * scale * half + 1.0f);
        }
        g.setColour (c::text);
        for (const float f : { start, end })
            g.drawVerticalLine (juce::roundToInt (r.getX() + r.getWidth() * f), r.getY(), r.getBottom());
    }

private:
    DrumsProcessor& processor_;
    juce::Colour accent_;
    int pad_ = 0;
};

//==============================================================================
DrumsPanel::DrumsPanel (DrumsProcessor& processor) : processor_ (processor)
{
    const auto accent = processor.product().accent;
    for (auto* panel : { &padsPanel_, &padPanel_, &samplePanel_, &kitPanel_, &sequencerPanel_ })
        addAndMakeVisible (*panel);

    loadButton_ = std::make_unique<vox::ui::VoxButton> ("Load", vox::ui::VoxButton::Type::Primary);
    clearButton_ = std::make_unique<vox::ui::VoxButton> ("Clear");
    libraryButton_ = std::make_unique<vox::ui::VoxButton> ("To library");
    loadButton_->setTooltip ("Load a WAV, AIFF, FLAC or Ogg file into the selected pad");
    libraryButton_->setTooltip ("Copy the file into the VOX Drums sample library so the project finds it by content");
    loadButton_->onClick = [this] {
        chooser_ = std::make_unique<juce::FileChooser> ("Load a sample into pad " + juce::String (selectedPad_ + 1),
                                                        juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg");
        const int pad = selectedPad_;
        chooser_->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                               [this, pad] (const juce::FileChooser& fc) {
                                   if (fc.getResult().existsAsFile())
                                       loadSample (pad, fc.getResult());
                               });
    };
    clearButton_->onClick = [this] {
        processor_.samples().clear (selectedPad_);
        loadMessage_.clear();
        refreshSample();
    };
    libraryButton_->onClick = [this] {
        const auto result = processor_.samples().copyIntoLibrary (selectedPad_);
        loadMessage_ = result.wasOk() ? "Copied into the sample library" : result.getErrorMessage();
        refreshSample();
    };
    for (auto* b : { loadButton_.get(), clearButton_.get(), libraryButton_.get() })
        addAndMakeVisible (*b);
    sampleStatus_ = std::make_unique<juce::Label> ("sample.status", "");
    sampleStatus_->setColour (juce::Label::textColourId, c::textSecondary);
    sampleStatus_->setFont (vox::ui::typography::valueText());
    sampleStatus_->setMinimumHorizontalScale (0.7f);
    addAndMakeVisible (*sampleStatus_);
    waveform_ = std::make_unique<WaveformView> (processor_, processor.product().accent);
    addAndMakeVisible (*waveform_);

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

    kitKnobs_.push_back (makeKnob ("drm.kit.volume", "Volume"));
    kitKnobs_.push_back (makeKnob ("drm.kit.velocity", "Velocity"));
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
    sampleKnobs_.clear();
    sampleToggles_.clear();
    padSource_.attachment.reset();
    padSource_.combo.reset();
    padSource_.label.reset();

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

    padSource_.id = prefix + ".source";
    padSource_.label = std::make_unique<juce::Label> ("source.label", "Source");
    padSource_.label->setColour (juce::Label::textColourId, c::textSecondary);
    padSource_.label->setFont (vox::ui::typography::controlLabel());
    padSource_.combo = std::make_unique<vox::ui::VoxComboBox>();
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (processor_.state().getParameter (padSource_.id)))
        padSource_.combo->addItemList (choice->choices, 1);
    padSource_.attachment = std::make_unique<ComboAttachment> (processor_.state(), padSource_.id, *padSource_.combo);
    addAndMakeVisible (*padSource_.label);
    addAndMakeVisible (*padSource_.combo);
    for (const auto& [name, label] : { std::pair { "sample_start", "Start" }, std::pair { "sample_end", "End" },
                                       std::pair { "sample_level", "Level" } })
        sampleKnobs_.push_back (makeKnob (prefix + "." + name, label));
    for (const auto& [name, label] : { std::pair { "sample_loop", "Loop" }, std::pair { "sample_reverse", "Reverse" },
                                       std::pair { "sample_normalize", "Normalize" } })
    {
        BoundToggle t;
        t.id = prefix + "." + name;
        t.button = std::make_unique<juce::ToggleButton> (label);
        t.attachment = std::make_unique<ButtonAttachment> (processor_.state(), t.id, *t.button);
        addAndMakeVisible (*t.button);
        sampleToggles_.push_back (std::move (t));
    }
    if (waveform_ != nullptr)
        waveform_->setPad (selectedPad_);
    loadMessage_.clear();
    refreshSample();

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
    const int contentWidth = top.getWidth();
    auto left = top.removeFromLeft (juce::jlimit (250, 360, contentWidth / 4));
    auto sampleArea = top.removeFromRight (juce::jlimit (250, 340, contentWidth * 27 / 100));
    top.removeFromRight (sp::lg);
    samplePanel_.setBounds (sampleArea);
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
        knobWidth = juce::jlimit (56, 76, pad.getWidth() / 6);
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

    {
        auto sample = samplePanel_.getContentBounds() + samplePanel_.getPosition();
        if (padSource_.label != nullptr)
        {
            auto row = sample.removeFromTop (16 + vox::ui::tokens::size::controlHeight);
            padSource_.label->setBounds (row.removeFromTop (16));
            padSource_.combo->setBounds (row);
        }
        sample.removeFromTop (sp::xs + 2);
        auto buttons = sample.removeFromTop (28);
        const int bw = (buttons.getWidth() - 2 * sp::xs) / 3;
        loadButton_->setBounds (buttons.removeFromLeft (bw));
        buttons.removeFromLeft (sp::xs);
        clearButton_->setBounds (buttons.removeFromLeft (bw));
        buttons.removeFromLeft (sp::xs);
        libraryButton_->setBounds (buttons);
        sample.removeFromTop (sp::xs);
        sampleStatus_->setBounds (sample.removeFromTop (16));
        auto toggles = sample.removeFromBottom (24);
        sample.removeFromBottom (sp::xs);
        auto knobs = sample.removeFromBottom (knobHeight);
        sample.removeFromBottom (sp::xs);
        waveform_->setBounds (sample);
        const int kw = juce::jmin (76, knobs.getWidth() / 3);
        for (auto& k : sampleKnobs_)
            k.knob->setBounds (knobs.removeFromLeft (kw));
        const int tw = toggles.getWidth() / 3;
        for (auto& t : sampleToggles_)
            t.button->setBounds (toggles.removeFromLeft (tw));
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

void DrumsPanel::refreshSample()
{
    const auto info = processor_.samples().pad (selectedPad_);
    shownGeneration_ = processor_.samples().generation();
    juce::String text;
    if (loadMessage_.isNotEmpty())
        text = loadMessage_;
    else if (info.buffer != nullptr)
        text = info.reference->name + "  " + juce::String (static_cast<double> (info.buffer->frames) / info.buffer->sampleRate, 2)
             + " s, " + juce::String (info.buffer->sampleRate / 1000.0, 1) + " kHz, "
             + (info.buffer->data.size() > 1 ? "stereo" : "mono");
    else if (info.reference)
        text = "Missing: " + info.reference->name + " (" + info.status + ")";
    else
        text = "No sample";
    if (sampleStatus_ != nullptr)
    {
        sampleStatus_->setText (text, juce::dontSendNotification);
        sampleStatus_->setColour (juce::Label::textColourId,
                                  info.reference && info.buffer == nullptr ? c::warning : c::textSecondary);
    }
    if (clearButton_ != nullptr)
    {
        clearButton_->setEnabled (info.reference.has_value());
        libraryButton_->setEnabled (info.buffer != nullptr);
    }
    if (waveform_ != nullptr)
        waveform_->repaint();
}

juce::String DrumsPanel::sampleStatus() const
{
    return sampleStatus_ != nullptr ? sampleStatus_->getText() : juce::String();
}

juce::Button* DrumsPanel::sampleButton (const juce::String& name)
{
    if (name == "load")
        return loadButton_.get();
    if (name == "clear")
        return clearButton_.get();
    if (name == "library")
        return libraryButton_.get();
    return nullptr;
}

void DrumsPanel::loadSample (int pad, const juce::File& file)
{
    loadMessage_ = "Loading " + file.getFileName() + " ...";
    refreshSample();
    juce::Component::SafePointer<DrumsPanel> self (this);
    processor_.samples().loadAsync (pad, file, [self, pad] (const juce::String& error) {
        if (self == nullptr)
            return;
        self->loadMessage_ = error;
        if (self->selectedPad_ == pad)
            self->refreshSample();
    });
}

bool DrumsPanel::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;ogg"))
            return true;
    return false;
}

void DrumsPanel::filesDropped (const juce::StringArray& files, int x, int y)
{
    juce::File file;
    for (const auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;ogg"))
        {
            file = juce::File (f);
            break;
        }
    if (! file.existsAsFile())
        return;
    int pad = selectedPad_;
    for (int i = 0; i < padCount; ++i)
        if (pads_[static_cast<std::size_t> (i)]->getBounds().contains (x, y))
            pad = i;
    selectPad (pad);
    loadSample (pad, file);
}

void DrumsPanel::timerCallback()
{
    if (processor_.samples().generation() != shownGeneration_)
        refreshSample();
    {
        const auto prefix = juce::String (padPrefix (selectedPad_));
        const float start = processor_.state().getRawParameterValue (prefix + ".sample_start")->load();
        const float end = processor_.state().getRawParameterValue (prefix + ".sample_end")->load();
        if (! juce::exactlyEqual (start, shownStart_) || ! juce::exactlyEqual (end, shownEnd_))
        {
            shownStart_ = start;
            shownEnd_ = end;
            waveform_->repaint();
        }
    }
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
    for (auto* list : { &padKnobs_, &kickKnobs_, &sampleKnobs_, &kitKnobs_, &seqKnobs_ })
        for (auto& k : *list)
            if (k.id == id)
                return &k.knob->getSlider();
    return nullptr;
}

juce::Button* DrumsPanel::buttonFor (const juce::String& id)
{
    if (hatChoke_.id == id)
        return hatChoke_.button.get();
    for (auto* list : { &lanes_, &sampleToggles_ })
        for (auto& t : *list)
            if (t.id == id)
                return t.button.get();
    return nullptr;
}

juce::ComboBox* DrumsPanel::comboFor (const juce::String& id)
{
    for (auto* target : { &padFilter_, &padSource_, &seqMode_, &seqPattern_ })
        if (target->id == id)
            return target->combo.get();
    return nullptr;
}

juce::Button* DrumsPanel::padButton (int pad)
{
    return pad >= 0 && pad < padCount ? pads_[static_cast<std::size_t> (pad)].get() : nullptr;
}

} // namespace vox::drums
