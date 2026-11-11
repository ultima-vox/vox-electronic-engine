#include "DrumsEditor.h"

#include "DrumsParameters.h"
#include "Processor.h"
#include "PatternEditor.h"
#include "Tokens.h"
#include "Typography.h"

#include <cmath>

namespace vox::drums {
namespace {

namespace c = vox::ui::tokens::colour;
namespace sp = vox::ui::tokens::spacing;

int knobWidth = 72, knobHeight = 78;   // adapted by resized()

bool bipolarId (const juce::String& id)
{
    return id.endsWith (".pan") || id.endsWith (".tune");
}

juce::String noteText (int note)
{
    return juce::MidiMessage::getMidiNoteName (note, true, true, 3) + " (" + juce::String (note) + ")";
}

} // namespace

//==============================================================================
class DrumsPanel::PadButton final : public juce::Button {
public:
    PadButton (DrumsProcessor& p, int pad, juce::Colour accent)
        : juce::Button ("pad " + juce::String (pad + 1)), processor_ (p), pad_ (pad), accent_ (accent)
    {
        setTriggeredOnMouseDown (true);
    }

    void flash() { level_ = 1.0f; repaint(); }
    void decay()
    {
        if (level_ <= 0.0f)
            return;
        level_ = juce::jmax (0.0f, level_ - 0.12f);
        repaint();
    }
    void setSelected (bool selected) { selected_ = selected; repaint(); }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto settings = processor_.kitStore().pad (pad_);
        const bool muted = processor_.state().getRawParameterValue (juce::String (padPrefix (pad_)) + ".mute")->load() >= 0.5f;
        auto r = getLocalBounds().toFloat().reduced (2.0f);
        g.setColour (c::panelRaised.interpolatedWith (accent_, 0.55f * level_));
        g.fillRoundedRectangle (r, vox::ui::tokens::radius::medium);
        g.setColour (selected_ ? accent_ : (over || down ? c::border.brighter (0.4f) : c::border));
        g.drawRoundedRectangle (r, vox::ui::tokens::radius::medium, selected_ ? 2.0f : 1.0f);
        auto textArea = getLocalBounds().reduced (sp::sm, sp::xs);
        g.setColour (muted ? c::textMuted : c::text);
        g.setFont (vox::ui::typography::controlLabel());
        // The model (short, it can change) over the pad number and its note.
        g.drawFittedText (modelName (settings.model), textArea.removeFromTop (textArea.getHeight() / 2), juce::Justification::bottomLeft, 1, 0.7f);
        g.setColour (c::textMuted);
        g.setFont (vox::ui::typography::valueText());
        g.drawText (juce::String (pad_ + 1) + juce::String::fromUTF8 (" \xc2\xb7 ") + juce::String (settings.note) + (muted ? "  M" : ""), textArea,
                    juce::Justification::topLeft);
    }

private:
    DrumsProcessor& processor_;
    int pad_;
    juce::Colour accent_;
    float level_ = 0.0f;
    bool selected_ = false;
};

//==============================================================================
class DrumsPanel::WaveformView final : public juce::Component {
public:
    WaveformView (DrumsProcessor& p, juce::Colour accent) : processor_ (p), accent_ (accent) {}

    void setSlot (int pad, int player)
    {
        pad_ = pad;
        player_ = player;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (c::graph);
        g.fillRoundedRectangle (r, vox::ui::tokens::radius::small);
        g.setColour (c::borderSubtle);
        g.drawRoundedRectangle (r.reduced (0.5f), vox::ui::tokens::radius::small, 1.0f);
        const auto info = processor_.samples().pad (slotOf (pad_, player_));
        if (info.buffer == nullptr)
        {
            g.setColour (c::textMuted);
            g.setFont (vox::ui::typography::valueText());
            g.drawText (info.reference ? "Sample missing: the pad plays its synthesis" : "Drop an audio file here or Load",
                        getLocalBounds(), juce::Justification::centred);
            return;
        }
        const auto settings = processor_.kitStore().pad (pad_).players[static_cast<std::size_t> (player_)];
        const auto& lo = info.buffer->overviewMin;
        const auto& hi = info.buffer->overviewMax;
        const float mid = r.getCentreY(), half = r.getHeight() * 0.45f;
        const float scale = info.buffer->peak > 0.0f ? 1.0f / info.buffer->peak : 1.0f;
        const auto n = lo.size();
        for (std::size_t i = 0; i < n; ++i)
        {
            const float fraction = static_cast<float> (i) / static_cast<float> (n);
            g.setColour (fraction >= settings.start && fraction <= settings.end ? accent_ : c::textMuted.withAlpha (0.5f));
            g.drawVerticalLine (juce::roundToInt (r.getX() + r.getWidth() * fraction), mid - hi[i] * scale * half,
                                mid - lo[i] * scale * half + 1.0f);
        }
        g.setColour (c::text);
        for (const float f : { settings.start, settings.end })
            g.drawVerticalLine (juce::roundToInt (r.getX() + r.getWidth() * f), r.getY(), r.getBottom());
    }

private:
    DrumsProcessor& processor_;
    juce::Colour accent_;
    int pad_ = 0, player_ = 0;
};

//==============================================================================
DrumsPanel::DrumsPanel (DrumsProcessor& processor) : processor_ (processor)
{
    const auto accent = processor.product().accent;
    for (auto* panel : { &padsPanel_, &padPanel_, &samplePanel_, &kitPanel_, &sequencerPanel_ })
        addAndMakeVisible (*panel);
    kitPanel_.setTitle ("MACROS");
    sequencerPanel_.setTitle ("TRANSPORT");
    {
        std::vector<vox::ui::VoxTabBar::Tab> tabs;
        for (int i = 0; i < pageCount; ++i)
            tabs.push_back ({ juce::String (pageName (i)).toLowerCase(), pageName (i), true });
        tabs_.setTabs (std::move (tabs));
        tabs_.onTabChanged = [this] (int index) { showPage (index); };
        addAndMakeVisible (tabs_);
    }

    for (int pad = 0; pad < padCount; ++pad)
    {
        auto& button = pads_[static_cast<std::size_t> (pad)];
        button = std::make_unique<PadButton> (processor_, pad, accent);
        button->onClick = [this, pad] {
            selectPad (pad);
            processor_.audition (pad, 110);
        };
        addAndMakeVisible (*button);
    }

    const auto label = [] (const juce::String& text) {
        auto l = std::make_unique<juce::Label> (text, text);
        l->setColour (juce::Label::textColourId, c::textSecondary);
        l->setFont (vox::ui::typography::controlLabel());
        return l;
    };

    // Structural controls of the selected pad.
    juce::StringArray models;
    for (int m = 0; m < kit::modelCount; ++m)
        models.add (modelName (static_cast<kit::Model> (m)));
    addCombo ("model", "Model", models, [] (const StructuralContext& c) { return static_cast<float> (c.pad.model); },
              [] (StructuralContext& c, float v) { c.pad.model = static_cast<kit::Model> (juce::roundToInt (v)); });
    juce::StringArray notes;
    for (int n = 0; n < 128; ++n)
        notes.add (noteText (n));
    addCombo ("note", "Note", notes, [] (const StructuralContext& c) { return static_cast<float> (c.pad.note); },
              [] (StructuralContext& c, float v) { c.pad.note = static_cast<std::uint8_t> (juce::roundToInt (v)); });
    addCombo ("choke", "Choke", { "Off", "Group 1", "Group 2", "Group 3", "Group 4", "Group 5", "Group 6", "Group 7", "Group 8" },
              [] (const StructuralContext& c) { return static_cast<float> (c.pad.chokeGroup); },
              [] (StructuralContext& c, float v) { c.pad.chokeGroup = static_cast<std::uint8_t> (juce::roundToInt (v)); });
    addCombo ("poly", "Voices", { "Mono", "2", "3", "4", "5", "6", "7", "8" },
              [] (const StructuralContext& c) { return static_cast<float> (c.pad.polyphony - 1); },
              [] (StructuralContext& c, float v) { c.pad.polyphony = static_cast<std::uint8_t> (juce::roundToInt (v) + 1); });
    addKnob ("fine", "Fine", -100.0, 100.0, 1.0, [] (const StructuralContext& c) { return c.pad.fine; },
             [] (StructuralContext& c, float v) { c.pad.fine = v; }, true);
    addKnob ("character", "Character", 0.0, 1.0, 0.0, [] (const StructuralContext& c) { return c.pad.character; },
             [] (StructuralContext& c, float v) { c.pad.character = v; });
    addCombo ("filter", "Filter", { "Off", "LP 12", "LP 24", "HP 12", "HP 24", "BP" },
              [] (const StructuralContext& c) { return static_cast<float> (c.pad.filter); },
              [] (StructuralContext& c, float v) { c.pad.filter = static_cast<kit::Filter> (juce::roundToInt (v)); });
    addKnob ("resonance", "Resonance", 0.0, 1.0, 0.0, [] (const StructuralContext& c) { return c.pad.resonance; },
             [] (StructuralContext& c, float v) { c.pad.resonance = v; });
    addKnob ("attack", "Attack", -1.0, 1.0, 0.0, [] (const StructuralContext& c) { return c.pad.attack; },
             [] (StructuralContext& c, float v) { c.pad.attack = v; }, true);
    addKnob ("sustain", "Sustain", -1.0, 1.0, 0.0, [] (const StructuralContext& c) { return c.pad.sustain; },
             [] (StructuralContext& c, float v) { c.pad.sustain = v; }, true);
    addKnob ("pitch_amount", "Pitch Env", 0.0, 48.0, 0.0, [] (const StructuralContext& c) { return c.pad.pitchAmount; },
             [] (StructuralContext& c, float v) { c.pad.pitchAmount = v; });
    addKnob ("pitch_decay", "Pitch Decay", 0.005, 0.2, 0.0, [] (const StructuralContext& c) { return c.pad.pitchDecay; },
             [] (StructuralContext& c, float v) { c.pad.pitchDecay = v; }, false, 0.0316);
    addKnob ("drive", "Drive", 1.0, 8.0, 0.0, [] (const StructuralContext& c) { return c.pad.drive; },
             [] (StructuralContext& c, float v) { c.pad.drive = v; });
    addKnob ("sub", "Sub", 0.0, 1.0, 0.0, [] (const StructuralContext& c) { return c.pad.sub; },
             [] (StructuralContext& c, float v) { c.pad.sub = v; });
    addKnob ("tail", "Tail", 0.03, 1.5, 0.0, [] (const StructuralContext& c) { return c.pad.tail; },
             [] (StructuralContext& c, float v) { c.pad.tail = v; }, false, 0.212);
    // Sample: source and players.
    addCombo ("source", "Source", { "Synth", "Sample", "Layer" }, [] (const StructuralContext& c) { return static_cast<float> (c.pad.source); },
              [] (StructuralContext& c, float v) { c.pad.source = static_cast<kit::Source> (juce::roundToInt (v)); });
    addCombo ("sample_mode", "Players", { "A only", "Velocity A/B", "A + B" },
              [] (const StructuralContext& c) { return static_cast<float> (c.pad.sampleMode); },
              [] (StructuralContext& c, float v) { c.pad.sampleMode = static_cast<kit::SampleMode> (juce::roundToInt (v)); });
    addKnob ("split", "Split", 1.0, 127.0, 1.0, [] (const StructuralContext& c) { return static_cast<float> (c.pad.velocitySplit); },
             [] (StructuralContext& c, float v) { c.pad.velocitySplit = static_cast<std::uint8_t> (juce::roundToInt (v)); });
    addKnob ("start", "Start", 0.0, 1.0, 0.0, [] (const StructuralContext& c) { return c.sample().start; },
             [] (StructuralContext& c, float v) { c.sample().start = v; });
    addKnob ("end", "End", 0.0, 1.0, 0.0, [] (const StructuralContext& c) { return c.sample().end; },
             [] (StructuralContext& c, float v) { c.sample().end = v; });
    addKnob ("sample_level", "Level", 0.0, 2.0, 0.0, [] (const StructuralContext& c) { return c.sample().level; },
             [] (StructuralContext& c, float v) { c.sample().level = v; });
    addToggle ("loop", "Loop", [] (const StructuralContext& c) { return c.sample().loop ? 1.0f : 0.0f; },
               [] (StructuralContext& c, float v) { c.sample().loop = v >= 0.5f; });
    addToggle ("reverse", "Reverse", [] (const StructuralContext& c) { return c.sample().reverse ? 1.0f : 0.0f; },
               [] (StructuralContext& c, float v) { c.sample().reverse = v >= 0.5f; });
    addToggle ("normalize", "Normalize", [] (const StructuralContext& c) { return c.sample().normalize ? 1.0f : 0.0f; },
               [] (StructuralContext& c, float v) { c.sample().normalize = v >= 0.5f; });

    // Value text of the structural knobs.
    const auto percent = [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + " %"; };
    const auto signedPercent = [] (double v) {
        const int n = juce::roundToInt (v * 100.0);
        return (n > 0 ? "+" : "") + juce::String (n) + " %";
    };
    const auto seconds = [] (double v) { return v < 1.0 ? juce::String (juce::roundToInt (v * 1000.0)) + " ms" : juce::String (v, 2) + " s"; };
    const std::pair<const char*, std::function<juce::String (double)>> formats[] = {
        { "fine", [] (double v) { const int n = juce::roundToInt (v); return (n > 0 ? "+" : "") + juce::String (n) + " ct"; } },
        { "character", percent },
        { "resonance", percent },
        { "attack", signedPercent },
        { "sustain", signedPercent },
        { "pitch_amount", [] (double v) { return juce::String (v, 1) + " st"; } },
        { "pitch_decay", seconds },
        { "drive", [] (double v) { return juce::String (v, 2) + "x"; } },
        { "sub", percent },
        { "tail", seconds },
        { "split", [] (double v) { return "vel " + juce::String (juce::roundToInt (v)); } },
        { "start", percent },
        { "end", percent },
        { "sample_level", percent },
    };
    for (const auto& [name, format] : formats)
        if (auto* st = findStructural (name))
            st->knob->getSlider().textFromValueFunction = format;

    // Sample slot buttons.
    playerA_ = std::make_unique<vox::ui::VoxButton> ("A", vox::ui::VoxButton::Type::Toggle);
    playerB_ = std::make_unique<vox::ui::VoxButton> ("B", vox::ui::VoxButton::Type::Toggle);
    playerA_->onClick = [this] { selectPlayer (0); };
    playerB_->onClick = [this] { selectPlayer (1); };
    loadButton_ = std::make_unique<vox::ui::VoxButton> ("Load", vox::ui::VoxButton::Type::Primary);
    clearButton_ = std::make_unique<vox::ui::VoxButton> ("Clear");
    libraryButton_ = std::make_unique<vox::ui::VoxButton> ("Library");
    loadButton_->setTooltip ("Load a WAV, AIFF, FLAC or Ogg file into the selected pad's player");
    libraryButton_->setTooltip ("Copy the file into the VOX Drums sample library so the project finds it by content");
    loadButton_->onClick = [this] {
        chooser_ = std::make_unique<juce::FileChooser> ("Load a sample into pad " + juce::String (selectedPad_ + 1)
                                                            + (selectedPlayer_ == 0 ? " A" : " B"),
                                                        juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg");
        const int pad = selectedPad_, player = selectedPlayer_;
        chooser_->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                               [this, pad, player] (const juce::FileChooser& fc) {
                                   if (fc.getResult().existsAsFile())
                                       loadSample (pad, player, fc.getResult());
                               });
    };
    clearButton_->onClick = [this] {
        processor_.samples().clear (slotOf (selectedPad_, selectedPlayer_));
        loadMessage_.clear();
        refreshSample();
    };
    libraryButton_->onClick = [this] {
        const auto result = processor_.samples().copyIntoLibrary (slotOf (selectedPad_, selectedPlayer_));
        loadMessage_ = result.wasOk() ? "Copied into the sample library" : result.getErrorMessage();
        refreshSample();
    };
    for (auto* b : { playerA_.get(), playerB_.get(), loadButton_.get(), clearButton_.get(), libraryButton_.get() })
        addAndMakeVisible (*b);
    sampleStatus_ = label ("");
    sampleStatus_->setFont (vox::ui::typography::valueText());
    sampleStatus_->setMinimumHorizontalScale (0.7f);
    addAndMakeVisible (*sampleStatus_);
    waveform_ = std::make_unique<WaveformView> (processor_, accent);
    addAndMakeVisible (*waveform_);

    // Kit and sequencer (host parameters).
    const auto toggle = [this] (HostToggle& target, const juce::String& id, const juce::String& text) {
        target.id = id;
        target.button = std::make_unique<juce::ToggleButton> (text);
        target.attachment = std::make_unique<ButtonAttachment> (processor_.state(), id, *target.button);
        addAndMakeVisible (*target.button);
    };
    const auto combo = [this, &label] (HostCombo& target, const juce::String& id, const juce::String& text) {
        target.id = id;
        target.label = label (text);
        target.combo = std::make_unique<vox::ui::VoxComboBox>();
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (processor_.state().getParameter (id)))
            target.combo->addItemList (choice->choices, 1);
        target.attachment = std::make_unique<ComboAttachment> (processor_.state(), id, *target.combo);
        addAndMakeVisible (*target.label);
        addAndMakeVisible (*target.combo);
    };
    kitKnobs_.push_back (makeHostKnob ("drm.kit.volume", "Volume"));
    kitKnobs_.push_back (makeHostKnob ("drm.kit.velocity", "Velocity"));
    toggle (choke_, "drm.kit.hat_choke", "Choke");
    combo (seqMode_, "drm.seq.mode", "Mode");
    combo (seqPattern_, patternParameterId, "Pattern");
    seqKnobs_.push_back (makeHostKnob ("drm.seq.swing", "Swing"));
    seqKnobs_.push_back (makeHostKnob ("drm.seq.accent", "Accent"));
    for (int pad = 0; pad < padCount; ++pad)
    {
        lanes_.emplace_back();
        toggle (lanes_.back(), "drm.seq.lane." + juce::String (padPrefix (pad).substr (4)), juce::String (pad + 1));
    }
    steps_ = std::make_unique<PatternEditor> (processor_, accent);
    addAndMakeVisible (*steps_);

    // DRM-07: macros 5-8, the pages, the song timeline, selectors.
    for (const auto& [id, text] : { std::pair { "drm.fx.delay.feedback", "Dly Fbk" }, std::pair { "drm.fx.delay.level", "Delay" },
                                    std::pair { "drm.fx.reverb.size", "Rev Size" }, std::pair { "drm.fx.reverb.level", "Reverb" } })
        fxMacros_.push_back (makeHostKnob (id, text));
    combo (patternSelect_, patternParameterId, "Pattern");
    padSelectLabel_ = label ("Pad");
    addAndMakeVisible (*padSelectLabel_);
    padSelect_ = std::make_unique<vox::ui::VoxComboBox>();
    for (int pad = 0; pad < padCount; ++pad)
        padSelect_->addItem (juce::String (pad + 1) + "  " + padName (pad), pad + 1);
    padSelect_->onChange = [this] {
        if (! refreshing_ && padSelect_->getSelectedItemIndex() != selectedPad_)
            selectPad (padSelect_->getSelectedItemIndex());
    };
    addAndMakeVisible (*padSelect_);
    transport_ = label ("");
    transport_->setFont (vox::ui::typography::valueText());
    transport_->setColour (juce::Label::textColourId, c::text);
    addAndMakeVisible (*transport_);
    songMode_ = std::make_unique<vox::ui::VoxButton> ("Song Mode", vox::ui::VoxButton::Type::Toggle);
    songMode_->setClickingTogglesState (true);
    songMode_->setTooltip ("On: the sequencer plays the song timeline. Off: it plays the selected pattern.");
    songMode_->onClick = [this] {
        const bool on = songMode_->getToggleState();
        processor_.editSong ([on] (SongData& s) { s.songMode = on; });
    };
    addAndMakeVisible (*songMode_);
    variation_ = std::make_unique<vox::ui::VoxButton> ("Swap A/B");
    variation_->setTooltip ("Swap the selected pattern with its variation B");
    variation_->onClick = [this] {
        if (auto* swap = steps_->button ("swap"))
            swap->onClick();
    };
    addAndMakeVisible (*variation_);
    song_ = std::make_unique<SongStrip> (processor_, accent);
    song_->onEditBar = [this] (int bar) { steps_->editSongBar (bar); };
    addAndMakeVisible (*song_);
    pages_[3] = makeMixerPage (processor_);
    pages_[4] = makeGeneratePage (processor_);
    pages_[5] = makeRoutingPage (processor_);
    pages_[6] = makePresetsPage (processor_, [this] { return selectedPad_; });
    pages_[7] = makeAdvancedPage (processor_);
    for (auto& page : pages_)
        if (page != nullptr)
            addChildComponent (*page);

    const auto ui = processor_.uiState();
    page_ = juce::jlimit (0, pageCount - 1, static_cast<int> (ui.page));
    tabs_.setSelectedIndex (page_);
    song_->select (ui.songBar, 1);
    steps_->selectBar (ui.patternBar);
    selectPad (juce::jlimit (0, padCount - 1, static_cast<int> (ui.pad)));
    startTimerHz (30);
}

DrumsPanel::~DrumsPanel()
{
    stopTimer();
}

DrumsPanel::HostKnob DrumsPanel::makeHostKnob (const juce::String& id, const juce::String& label)
{
    HostKnob k;
    k.id = id;
    k.knob = std::make_unique<vox::ui::VoxKnob> (label, vox::ui::VoxKnob::Size::Normal);
    if (bipolarId (id))
        k.knob->setStyle (vox::ui::VoxKnob::Style::Bipolar);
    k.attachment = std::make_unique<SliderAttachment> (processor_.state(), id, k.knob->getSlider());
    if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (processor_.state().getParameter (id)))
    {
        k.knob->setDoubleClickResetValue (p->convertFrom0to1 (p->getDefaultValue()));
        k.knob->setTooltip (p->getName (64));
    }
    addAndMakeVisible (*k.knob);
    return k;
}

DrumsPanel::Structural& DrumsPanel::addKnob (const juce::String& name, const juce::String& label, double lo, double hi, double step,
                                             std::function<float (const StructuralContext&)> get,
                                             std::function<void (StructuralContext&, float)> set, bool bipolar, double skewCentre)
{
    auto s = std::make_unique<Structural>();
    s->name = name;
    s->get = std::move (get);
    s->set = std::move (set);
    s->knob = std::make_unique<vox::ui::VoxKnob> (label, vox::ui::VoxKnob::Size::Normal);
    if (bipolar)
        s->knob->setStyle (vox::ui::VoxKnob::Style::Bipolar);
    auto& slider = s->knob->getSlider();
    slider.setRange (lo, hi, step);
    if (skewCentre > 0.0)
        slider.setSkewFactorFromMidPoint (skewCentre);
    auto* raw = s.get();
    slider.onValueChange = [this, raw] { changeStructure (*raw, static_cast<float> (raw->knob->getSlider().getValue())); };
    addAndMakeVisible (*s->knob);
    structural_.push_back (std::move (s));
    return *structural_.back();
}

DrumsPanel::Structural& DrumsPanel::addCombo (const juce::String& name, const juce::String& label, const juce::StringArray& items,
                                              std::function<float (const StructuralContext&)> get,
                                              std::function<void (StructuralContext&, float)> set)
{
    auto s = std::make_unique<Structural>();
    s->name = name;
    s->get = std::move (get);
    s->set = std::move (set);
    s->label = std::make_unique<juce::Label> (name + ".label", label);
    s->label->setColour (juce::Label::textColourId, c::textSecondary);
    s->label->setFont (vox::ui::typography::controlLabel());
    s->combo = std::make_unique<vox::ui::VoxComboBox>();
    s->combo->addItemList (items, 1);
    auto* raw = s.get();
    s->combo->onChange = [this, raw] { changeStructure (*raw, static_cast<float> (raw->combo->getSelectedItemIndex())); };
    addAndMakeVisible (*s->label);
    addAndMakeVisible (*s->combo);
    structural_.push_back (std::move (s));
    return *structural_.back();
}

DrumsPanel::Structural& DrumsPanel::addToggle (const juce::String& name, const juce::String& label,
                                               std::function<float (const StructuralContext&)> get,
                                               std::function<void (StructuralContext&, float)> set)
{
    auto s = std::make_unique<Structural>();
    s->name = name;
    s->get = std::move (get);
    s->set = std::move (set);
    s->toggle = std::make_unique<juce::ToggleButton> (label);
    auto* raw = s.get();
    s->toggle->onClick = [this, raw] { changeStructure (*raw, raw->toggle->getToggleState() ? 1.0f : 0.0f); };
    addAndMakeVisible (*s->toggle);
    structural_.push_back (std::move (s));
    return *structural_.back();
}

void DrumsPanel::changeStructure (Structural& s, float value)
{
    if (refreshing_)
        return;
    StructuralContext ctx { processor_.kitStore().pad (selectedPad_), selectedPlayer_ };
    s.set (ctx, value);
    processor_.kitStore().setPad (selectedPad_, ctx.pad);
    if (s.name == "model")
        resized();   // the kick body row follows the model
    repaint();
}

DrumsPanel::Structural* DrumsPanel::findStructural (const juce::String& name)
{
    for (auto& s : structural_)
        if (s->name == name)
            return s.get();
    return nullptr;
}

void DrumsPanel::refreshStructure()
{
    shownStructure_ = processor_.kitStore().generation();
    const StructuralContext ctx { processor_.kitStore().pad (selectedPad_), selectedPlayer_ };
    refreshing_ = true;
    for (auto& s : structural_)
    {
        const float v = s->get (ctx);
        if (s->knob != nullptr)
            s->knob->getSlider().setValue (v, juce::dontSendNotification);
        else if (s->combo != nullptr)
            s->combo->setSelectedItemIndex (juce::roundToInt (v), juce::dontSendNotification);
        else if (s->toggle != nullptr)
            s->toggle->setToggleState (v >= 0.5f, juce::dontSendNotification);
    }
    refreshing_ = false;
    const auto name = padName (selectedPad_), model = modelName (ctx.pad.model);
    padPanel_.setTitle ("PAD " + juce::String (selectedPad_ + 1).paddedLeft ('0', 2) + "  " + name.toUpperCase()
                        + (name.equalsIgnoreCase (model) ? juce::String() : juce::String::fromUTF8 ("  \xc2\xb7  ") + model.toUpperCase()));
    if (waveform_ != nullptr)
        waveform_->repaint();
}

void DrumsPanel::selectPad (int pad)
{
    if (pad < 0 || pad >= padCount)
        return;
    selectedPad_ = pad;
    if (padSelect_ != nullptr)
    {
        padSelect_->setSelectedItemIndex (pad, juce::dontSendNotification);
        auto ui = processor_.uiState();
        ui.pad = static_cast<std::uint8_t> (pad);
        processor_.setUiState (ui);
        shownUiPad_ = ui.pad;
    }
    for (int i = 0; i < padCount; ++i)
        pads_[static_cast<std::size_t> (i)]->setSelected (i == pad);
    bindPadControls();
    loadMessage_.clear();
    selectPlayer (selectedPlayer_);
    resized();
    repaint();
}

void DrumsPanel::selectPlayer (int player)
{
    selectedPlayer_ = juce::jlimit (0, 1, player);
    if (playerA_ != nullptr)
    {
        playerA_->setToggleState (selectedPlayer_ == 0, juce::dontSendNotification);
        playerB_->setToggleState (selectedPlayer_ == 1, juce::dontSendNotification);
    }
    if (waveform_ != nullptr)
        waveform_->setSlot (selectedPad_, selectedPlayer_);
    refreshStructure();
    refreshSample();
}

void DrumsPanel::bindPadControls()
{
    // Attachments are destroyed before the components they point at.
    padKnobs_.clear();
    mute_.attachment.reset();
    mute_.button.reset();
    const auto prefix = juce::String (padPrefix (selectedPad_));
    for (const auto& [name, label] : { std::pair { "level", "Level" }, std::pair { "tune", "Tune" }, std::pair { "decay", "Decay" },
                                       std::pair { "pan", "Pan" }, std::pair { "cutoff", "Cutoff" } })
        padKnobs_.push_back (makeHostKnob (prefix + "." + name, label));
    mute_.id = prefix + ".mute";
    mute_.button = std::make_unique<juce::ToggleButton> ("Mute");
    mute_.attachment = std::make_unique<ButtonAttachment> (processor_.state(), mute_.id, *mute_.button);
    addAndMakeVisible (*mute_.button);
}

bool DrumsPanel::bodyVisible() const
{
    return processor_.kitStore().pad (selectedPad_).model == kit::Model::kick;
}

void DrumsPanel::paint (juce::Graphics& g)
{
    g.fillAll (c::background);
}

const char* DrumsPanel::pageName (int page) noexcept
{
    static const char* names[pageCount] = { "PERFORMANCE", "SOUND", "PATTERN/SONG", "MIXER", "GENERATE", "ROUTING", "PRESETS", "ADVANCED" };
    return page >= 0 && page < pageCount ? names[page] : "";
}

void DrumsPanel::showPage (int page)
{
    page_ = juce::jlimit (0, pageCount - 1, page);
    if (tabs_.getSelectedIndex() != page_)
        tabs_.setSelectedIndex (page_);
    auto ui = processor_.uiState();
    ui.page = static_cast<std::uint8_t> (page_);
    processor_.setUiState (ui);
    resized();
    if (auto* p = pages_[static_cast<std::size_t> (page_)].get())
        p->update();
    repaint();
}

// Which controls belong to the page shown. Controls of other pages stay
// alive (their attachments keep following the parameters) but hidden.
void DrumsPanel::applyVisibility()
{
    const bool performance = page_ == 0, sound = page_ == 1, pattern = page_ == 2;
    padsPanel_.setVisible (performance);
    kitPanel_.setVisible (performance);
    sequencerPanel_.setVisible (performance);
    for (auto& p : pads_)
        p->setVisible (performance);
    for (auto* list : { &kitKnobs_, &seqKnobs_, &fxMacros_ })
        for (auto& k : *list)
            k.knob->setVisible (performance);
    choke_.button->setVisible (performance);
    for (auto* target : { &seqMode_, &seqPattern_ })
    {
        target->label->setVisible (performance);
        target->combo->setVisible (performance);
    }
    transport_->setVisible (performance);
    songMode_->setVisible (performance);
    variation_->setVisible (performance);

    padPanel_.setVisible (sound);
    samplePanel_.setVisible (sound);
    padSelect_->setVisible (sound);
    padSelectLabel_->setVisible (sound);
    for (auto& k : padKnobs_)
        k.knob->setVisible (sound);
    if (mute_.button != nullptr)
        mute_.button->setVisible (sound);
    const bool body = bodyVisible();
    for (auto& s : structural_)
    {
        const bool bodyKnob = s->name == "pitch_amount" || s->name == "pitch_decay" || s->name == "drive" || s->name == "sub"
                           || s->name == "tail";
        for (auto* comp : std::initializer_list<juce::Component*> { s->knob.get(), s->label.get(), s->combo.get(), s->toggle.get() })
            if (comp != nullptr)
                comp->setVisible (sound && (! bodyKnob || body));
    }
    for (auto* b : { playerA_.get(), playerB_.get(), loadButton_.get(), clearButton_.get(), libraryButton_.get() })
        b->setVisible (sound);
    sampleStatus_->setVisible (sound);
    waveform_->setVisible (sound);

    steps_->setVisible (pattern);
    song_->setVisible (pattern);
    patternSelect_.label->setVisible (pattern);
    patternSelect_.combo->setVisible (pattern);
    for (auto& lane : lanes_)
        lane.button->setVisible (pattern);

    for (int i = 0; i < pageCount; ++i)
        if (auto* p = pages_[static_cast<std::size_t> (i)].get())
            p->setVisible (i == page_);
}

void DrumsPanel::resized()
{
    if (song_ == nullptr)
        return;   // still constructing
    applyVisibility();
    auto area = getLocalBounds();
    tabs_.setBounds (area.removeFromTop (vox::ui::tokens::size::tabHeight));
    area = area.reduced (sp::xxl, sp::md);
    if (page_ == 0)
        layoutPerformance (area);
    else if (page_ == 1)
        layoutSound (area);
    else if (page_ == 2)
        layoutPattern (area);
    else if (auto* p = pages_[static_cast<std::size_t> (page_)].get())
        p->setBounds (area);
}

void DrumsPanel::layoutPerformance (juce::Rectangle<int> area)
{
    // Pads on the left (4 x 4); macros and transport on the right.
    auto left = area.removeFromLeft (juce::jlimit (420, 640, area.getWidth() * 58 / 100));
    area.removeFromLeft (sp::lg);
    padsPanel_.setBounds (left);
    {
        auto grid = padsPanel_.getContentBounds() + padsPanel_.getPosition();
        const int w = grid.getWidth() / 4, h = grid.getHeight() / 4;
        for (int pad = 0; pad < padCount; ++pad)
            pads_[static_cast<std::size_t> (pad)]->setBounds (grid.getX() + (pad % 4) * w, grid.getY() + (pad / 4) * h, w, h);
    }
    kitPanel_.setBounds (area.removeFromTop (juce::jlimit (210, 260, area.getHeight() / 2)));
    area.removeFromTop (sp::md);
    sequencerPanel_.setBounds (area);
    {
        // Eight macros, 2 x 4: volume, velocity, swing, accent; delay feedback and return, reverb size and return.
        auto macros = kitPanel_.getContentBounds() + kitPanel_.getPosition();
        std::vector<vox::ui::VoxKnob*> knobs;
        for (auto* list : { &kitKnobs_, &seqKnobs_, &fxMacros_ })
            for (auto& k : *list)
                knobs.push_back (k.knob.get());
        const int w = macros.getWidth() / 4, h = macros.getHeight() / 2;
        for (std::size_t i = 0; i < knobs.size(); ++i)
            knobs[i]->setBounds (macros.getX() + static_cast<int> (i % 4) * w, macros.getY() + static_cast<int> (i / 4) * h, w, h);
    }
    {
        auto t = sequencerPanel_.getContentBounds() + sequencerPanel_.getPosition();
        auto row = t.removeFromTop (48);
        for (auto* target : { &seqMode_, &seqPattern_ })
        {
            auto cell = row.removeFromLeft (row.getWidth() / (target == &seqMode_ ? 2 : 1));
            target->label->setBounds (cell.removeFromTop (16));
            target->combo->setBounds (cell.removeFromTop (vox::ui::tokens::size::controlHeight).withTrimmedRight (sp::sm));
        }
        t.removeFromTop (sp::sm);
        row = t.removeFromTop (32);
        const int bw = row.getWidth() / 3;
        songMode_->setBounds (row.removeFromLeft (bw).reduced (2, 0));
        variation_->setBounds (row.removeFromLeft (bw).reduced (2, 0));
        choke_.button->setBounds (row.reduced (4, 2));
        t.removeFromTop (sp::sm);
        transport_->setBounds (t.removeFromTop (juce::jmin (40, t.getHeight())));
    }
}

void DrumsPanel::layoutSound (juce::Rectangle<int> area)
{
    auto select = area.removeFromTop (30);
    padSelectLabel_->setBounds (select.removeFromLeft (40));
    padSelect_->setBounds (select.removeFromLeft (200).reduced (0, 1));
    area.removeFromTop (sp::sm);
    auto sampleArea = area.removeFromRight (juce::jlimit (280, 380, area.getWidth() * 36 / 100));
    area.removeFromRight (sp::lg);
    padPanel_.setBounds (area);
    samplePanel_.setBounds (sampleArea);

    const auto place = [] (Structural* s, juce::Rectangle<int> r) {
        if (s == nullptr)
            return;
        if (s->knob != nullptr)
            s->knob->setBounds (r);
        else if (s->combo != nullptr)
        {
            s->label->setBounds (r.removeFromTop (16));
            s->combo->setBounds (r.removeFromTop (vox::ui::tokens::size::controlHeight).reduced (2, 0));
        }
        else if (s->toggle != nullptr)
            s->toggle->setBounds (r);
    };
    {
        auto pad = padPanel_.getContentBounds() + padPanel_.getPosition();
        const bool body = bodyVisible();
        const int rows = body ? 3 : 2;
        knobHeight = juce::jlimit (60, 96, (pad.getHeight() - 50 - sp::xs * rows) / rows);
        knobWidth = juce::jlimit (56, 96, pad.getWidth() / 6);
        // Row 0: model, note, choke, voices, mute.
        auto row = pad.removeFromTop (48);
        const int cw = row.getWidth() / 5;
        for (const auto* name : { "model", "note", "choke", "poly" })
            place (findStructural (name), row.removeFromLeft (cw));
        mute_.button->setBounds (row.withSizeKeepingCentre (row.getWidth(), 28));
        pad.removeFromTop (sp::xs);
        // Row 1: level, tune, fine, decay, pan, character.
        row = pad.removeFromTop (knobHeight);
        padKnobs_[0].knob->setBounds (row.removeFromLeft (knobWidth));
        padKnobs_[1].knob->setBounds (row.removeFromLeft (knobWidth));
        place (findStructural ("fine"), row.removeFromLeft (knobWidth));
        padKnobs_[2].knob->setBounds (row.removeFromLeft (knobWidth));
        padKnobs_[3].knob->setBounds (row.removeFromLeft (knobWidth));
        place (findStructural ("character"), row.removeFromLeft (knobWidth));
        pad.removeFromTop (sp::xs);
        // Row 2: filter, cutoff, resonance, attack, sustain.
        row = pad.removeFromTop (knobHeight);
        place (findStructural ("filter"), row.removeFromLeft (knobWidth * 2 - 8).withHeight (48));
        padKnobs_[4].knob->setBounds (row.removeFromLeft (knobWidth));
        for (const auto* name : { "resonance", "attack", "sustain" })
            place (findStructural (name), row.removeFromLeft (knobWidth));
        // Row 3: kick body (Kick model only).
        if (body)
        {
            pad.removeFromTop (sp::xs);
            row = pad.removeFromTop (knobHeight);
            for (const auto* name : { "pitch_amount", "pitch_decay", "drive", "sub", "tail" })
                place (findStructural (name), row.removeFromLeft (knobWidth));
        }
    }
    {
        auto sample = samplePanel_.getContentBounds() + samplePanel_.getPosition();
        auto row = sample.removeFromTop (48);
        place (findStructural ("source"), row.removeFromLeft (row.getWidth() / 2));
        place (findStructural ("sample_mode"), row);
        sample.removeFromTop (sp::xs);
        auto buttons = sample.removeFromTop (28);
        const int bw = (buttons.getWidth() - 4 * sp::xs) / 6;
        playerA_->setBounds (buttons.removeFromLeft (bw / 2 + 8));
        buttons.removeFromLeft (sp::xs);
        playerB_->setBounds (buttons.removeFromLeft (bw / 2 + 8));
        buttons.removeFromLeft (sp::xs * 2);
        const int rest = (buttons.getWidth() - 2 * sp::xs) / 3;
        loadButton_->setBounds (buttons.removeFromLeft (rest));
        buttons.removeFromLeft (sp::xs);
        clearButton_->setBounds (buttons.removeFromLeft (rest));
        buttons.removeFromLeft (sp::xs);
        libraryButton_->setBounds (buttons);
        sample.removeFromTop (sp::xs);
        sampleStatus_->setBounds (sample.removeFromTop (16));
        auto toggles = sample.removeFromBottom (24);
        sample.removeFromBottom (sp::xs);
        const int kh = juce::jmin (knobHeight, juce::jmax (60, sample.getHeight() / 2));
        auto knobs = sample.removeFromBottom (kh);
        sample.removeFromBottom (sp::xs);
        waveform_->setBounds (sample);
        const int kw = knobs.getWidth() / 4;
        for (const auto* name : { "start", "end", "sample_level", "split" })
            place (findStructural (name), knobs.removeFromLeft (kw));
        const int tw = toggles.getWidth() / 3;
        for (const auto* name : { "loop", "reverse", "normalize" })
            place (findStructural (name), toggles.removeFromLeft (tw));
    }
}

void DrumsPanel::layoutPattern (juce::Rectangle<int> area)
{
    // The song timeline on top; below it the lane switches and the step grid.
    song_->setBounds (area.removeFromTop (song_->preferredHeight()));
    area.removeFromTop (sp::sm);
    auto left = area.removeFromLeft (124);
    area.removeFromLeft (sp::md);
    patternSelect_.label->setBounds (left.removeFromTop (16));
    patternSelect_.combo->setBounds (left.removeFromTop (vox::ui::tokens::size::controlHeight).withTrimmedRight (sp::xs));
    left.removeFromTop (sp::sm);
    // 16 lane switches, 2 columns x 8.
    const int lw = left.getWidth() / 2, lh = juce::jlimit (18, 30, left.getHeight() / 8);
    for (int pad = 0; pad < padCount; ++pad)
        lanes_[static_cast<std::size_t> (pad)].button->setBounds (left.getX() + (pad / 8) * lw, left.getY() + (pad % 8) * lh, lw, lh);
    steps_->setBounds (area);
}

juce::StringArray DrumsPanel::overlappingControls() const
{
    // Leaf controls of the page: the panel's own visible children except
    // the panels (backgrounds) and, for a page component, its children.
    std::vector<const juce::Component*> leaves;
    const auto isBackground = [this] (const juce::Component* comp) {
        return comp == &padsPanel_ || comp == &padPanel_ || comp == &samplePanel_ || comp == &kitPanel_ || comp == &sequencerPanel_;
    };
    for (const auto* child : getChildren())
    {
        if (! child->isVisible() || isBackground (child))
            continue;
        if (dynamic_cast<const Page*> (child) != nullptr)
            for (const auto* inner : child->getChildren())
            {
                if (inner->isVisible())
                    leaves.push_back (inner);
            }
        else
            leaves.push_back (child);
    }
    juce::StringArray out;
    const auto boundsOf = [this] (const juce::Component* comp) { return getLocalArea (comp->getParentComponent(), comp->getBounds()); };
    for (std::size_t i = 0; i < leaves.size(); ++i)
    {
        const auto a = boundsOf (leaves[i]);
        if (a.isEmpty() || ! getLocalBounds().contains (a))
            out.add ("outside or empty: " + leaves[i]->getName() + " " + a.toString());
        for (std::size_t j = i + 1; j < leaves.size(); ++j)
            if (a.intersects (boundsOf (leaves[j])))
                out.add (leaves[i]->getName() + " " + a.toString() + " x " + leaves[j]->getName() + " " + boundsOf (leaves[j]).toString());
    }
    return out;
}

void DrumsPanel::refreshSample()
{
    const auto info = processor_.samples().pad (slotOf (selectedPad_, selectedPlayer_));
    shownSamples_ = processor_.samples().generation();
    juce::String text;
    if (loadMessage_.isNotEmpty())
        text = loadMessage_;
    else if (info.buffer != nullptr)
        text = info.reference->name + "  " + juce::String (static_cast<double> (info.buffer->frames) / info.buffer->sampleRate, 2) + " s, "
             + juce::String (info.buffer->sampleRate / 1000.0, 1) + " kHz, " + (info.buffer->data.size() > 1 ? "stereo" : "mono");
    else if (info.reference)
        text = "Missing: " + info.reference->name + " (" + info.status + ")";
    else
        text = "No sample";
    if (sampleStatus_ != nullptr)
    {
        sampleStatus_->setText (text, juce::dontSendNotification);
        sampleStatus_->setColour (juce::Label::textColourId, info.reference && info.buffer == nullptr ? c::warning : c::textSecondary);
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
    if (name == "load") return loadButton_.get();
    if (name == "clear") return clearButton_.get();
    if (name == "library") return libraryButton_.get();
    if (name == "A") return playerA_.get();
    if (name == "B") return playerB_.get();
    return nullptr;
}

void DrumsPanel::loadSample (int pad, int player, const juce::File& file)
{
    loadMessage_ = "Loading " + file.getFileName() + " ...";
    refreshSample();
    juce::Component::SafePointer<DrumsPanel> self (this);
    processor_.samples().loadAsync (slotOf (pad, player), file, [self, pad, player] (const juce::String& error) {
        if (self == nullptr)
            return;
        self->loadMessage_ = error;
        if (self->selectedPad_ == pad && self->selectedPlayer_ == player)
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
    loadSample (pad, selectedPlayer_, file);
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
    if (processor_.kitStore().generation() != shownStructure_)
    {
        const bool body = bodyVisible();
        refreshStructure();
        if (page_ == 1 && body != findStructural ("drive")->knob->isVisible())
            resized();
        for (auto& p : pads_)
            p->repaint();
    }
    if (processor_.samples().generation() != shownSamples_)
        refreshSample();
    steps_->update();
    song_->update();
    if (auto* p = pages_[static_cast<std::size_t> (page_)].get())
        p->update();
    // A restored project: its page, pad and song bar.
    const auto ui = processor_.uiState();
    if (ui.page != page_)
        showPage (ui.page);
    if (ui.pad != selectedPad_ && ui.pad != shownUiPad_)
        selectPad (ui.pad);
    shownUiPad_ = ui.pad;
    {
        auto next = ui;
        next.patternBar = static_cast<std::uint8_t> (steps_->bar());
        if (! (next == ui))
            processor_.setUiState (next);
    }
    const bool songOn = processor_.patterns().song().songMode;
    if (songMode_->getToggleState() != songOn)
        songMode_->setToggleState (songOn, juce::dontSendNotification);
    const int bar = processor_.songBarPlaying();
    const auto text = juce::String (processor_.transportPlaying() ? "Playing" : "Stopped") + "  " + juce::String (processor_.lastBpm(), 1)
                    + " BPM  " + (songOn ? "Song" + (bar >= 0 ? " bar " + juce::String (bar + 1) : juce::String()) : juce::String ("Pattern"))
                    + "  voices " + juce::String (processor_.activeVoices());
    if (transport_->getText() != text)
        transport_->setText (text, juce::dontSendNotification);
}

const StoredPattern& DrumsPanel::shownPattern() const
{
    return steps_->shown();
}

juce::Slider* DrumsPanel::sliderFor (const juce::String& id)
{
    for (auto* list : { &padKnobs_, &kitKnobs_, &seqKnobs_, &fxMacros_ })
        for (auto& k : *list)
            if (k.id == id)
                return &k.knob->getSlider();
    for (auto& page : pages_)
        if (page != nullptr)
            if (auto* s = page->slider (id))
                return s;
    return nullptr;
}

juce::Slider* DrumsPanel::selectedPadSlider (const juce::String& name)
{
    for (auto& k : padKnobs_)
        if (k.id.endsWith ("." + name))
            return &k.knob->getSlider();
    return nullptr;
}

juce::Button* DrumsPanel::buttonFor (const juce::String& id)
{
    if (choke_.id == id)
        return choke_.button.get();
    if (mute_.id == id)
        return mute_.button.get();
    for (auto& t : lanes_)
        if (t.id == id)
            return t.button.get();
    for (auto& page : pages_)
        if (page != nullptr)
            if (auto* b = page->button (id))
                return b;
    return nullptr;
}

juce::ComboBox* DrumsPanel::comboFor (const juce::String& id)
{
    for (auto* target : { &seqMode_, &seqPattern_ })
        if (target->id == id)
            return target->combo.get();
    for (auto& page : pages_)
        if (page != nullptr)
            if (auto* b = page->combo (id))
                return b;
    return nullptr;
}

juce::Slider* DrumsPanel::structuralSlider (const juce::String& name)
{
    auto* s = findStructural (name);
    return s != nullptr && s->knob != nullptr ? &s->knob->getSlider() : nullptr;
}

juce::ComboBox* DrumsPanel::structuralCombo (const juce::String& name)
{
    auto* s = findStructural (name);
    return s != nullptr ? s->combo.get() : nullptr;
}

juce::Button* DrumsPanel::structuralToggle (const juce::String& name)
{
    auto* s = findStructural (name);
    return s != nullptr ? s->toggle.get() : nullptr;
}

juce::Button* DrumsPanel::padButton (int pad)
{
    return pad >= 0 && pad < padCount ? pads_[static_cast<std::size_t> (pad)].get() : nullptr;
}

} // namespace vox::drums
