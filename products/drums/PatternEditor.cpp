#include "PatternEditor.h"

#include "DrumsParameters.h"
#include "Processor.h"
#include "Tokens.h"
#include "Typography.h"

#include <cmath>

namespace vox::drums {
namespace {

namespace c = vox::ui::tokens::colour;
namespace sp = vox::ui::tokens::spacing;

constexpr int nameWidth = 70;

} // namespace

//==============================================================================
// The step grid of the shown bar.
class PatternEditor::Grid final : public juce::Component {
public:
    explicit Grid (PatternEditor& owner) : owner_ (owner) { setName ("pattern grid"); }

    // Lanes shown: all 16 when each row gets at least 11 px, else a page
    // of 8 (the editor's lane page button).
    int rows() const { return getHeight() >= 16 * 11 ? padCount : padCount / 2; }
    int firstLane() const { return rows() == padCount ? 0 : owner_.lanePage_ * (padCount / 2); }

    juce::Rectangle<int> cell (int lane, int step) const
    {
        const auto& p = owner_.shown_;
        const int row = lane - firstLane();
        if (row < 0 || row >= rows())
            return {};
        const float w = static_cast<float> (getWidth() - nameWidth) / static_cast<float> (p.steps);
        const float h = static_cast<float> (getHeight()) / static_cast<float> (rows());
        return juce::Rectangle<float> (static_cast<float> (nameWidth) + w * static_cast<float> (step), h * static_cast<float> (row), w, h)
            .getSmallestIntegerContainer();
    }

    bool hit (juce::Point<int> at, int& lane, int& step) const
    {
        const auto& p = owner_.shown_;
        if (at.x < nameWidth || at.y < 0 || at.x >= getWidth() || at.y >= getHeight())
            return false;
        step = (at.x - nameWidth) * p.steps / juce::jmax (1, getWidth() - nameWidth);
        lane = firstLane() + at.y * rows() / juce::jmax (1, getHeight());
        return step >= 0 && step < p.steps && lane >= 0 && lane < padCount;
    }

    void paint (juce::Graphics& g) override
    {
        const auto& p = owner_.shown_;
        const float w = static_cast<float> (getWidth() - nameWidth) / static_cast<float> (p.steps);
        const float h = static_cast<float> (getHeight()) / static_cast<float> (rows());
        g.setFont (vox::ui::typography::valueText().withHeight (juce::jlimit (9.0f, 12.0f, h * 0.85f)));
        const int beat = juce::jmax (1, p.steps / 4);
        for (int row = 0; row < rows(); ++row)
        {
            const int lane = firstLane() + row;
            const float y = h * static_cast<float> (row);
            const bool laneOn = owner_.processor_.state().getRawParameterValue ("drm.seq.lane." + juce::String (padPrefix (lane).substr (4)))->load() >= 0.5f;
            g.setColour (lane == owner_.lane_ ? c::text : (laneOn ? c::textSecondary : c::textMuted));
            g.drawText (juce::String (lane + 1) + " " + padName (lane), juce::Rectangle<float> (0.0f, y, nameWidth - 4.0f, h),
                        juce::Justification::centredLeft, true);
            for (int s = 0; s < p.steps; ++s)
            {
                const auto& cellValue = p.cell (lane, owner_.bar_, s);
                auto r = juce::Rectangle<float> (static_cast<float> (nameWidth) + w * static_cast<float> (s), y, w, h).reduced (w > 8.0f ? 1.0f : 0.5f);
                const bool playing = s == owner_.playStep_ && owner_.bar_ == owner_.playBar_;
                g.setColour (playing ? c::panelRaised.brighter (0.35f) : (s % beat == 0 ? c::panelRaised : c::control));
                g.fillRect (r);
                if (cellValue.on)
                {
                    const float alpha = laneOn ? 0.3f + 0.7f * static_cast<float> (cellValue.velocity) / 127.0f : 0.25f;
                    g.setColour (owner_.accent_.withAlpha (alpha));
                    // Probability below 100 %: the step fills only that share.
                    auto fill = r.reduced (1.0f);
                    if (cellValue.probability < 100)
                        fill = fill.removeFromBottom (fill.getHeight() * static_cast<float> (cellValue.probability) / 100.0f);
                    fill = fill.translated (static_cast<float> (cellValue.micro) * 0.01f * w, 0.0f);
                    g.fillRect (fill);
                    if (cellValue.ratchet > 1)
                    {
                        g.setColour (c::background.withAlpha (0.7f));
                        for (int k = 1; k < cellValue.ratchet; ++k)
                        {
                            const float x = fill.getX() + fill.getWidth() * static_cast<float> (k) / static_cast<float> (cellValue.ratchet);
                            g.drawVerticalLine (juce::roundToInt (x), fill.getY(), fill.getBottom());
                        }
                    }
                    if (cellValue.accent)
                    {
                        g.setColour (c::text);
                        g.fillEllipse (r.getCentreX() - 1.5f, r.getCentreY() - 1.5f, 3.0f, 3.0f);
                    }
                }
                if (lane == owner_.lane_ && s == owner_.step_)
                {
                    g.setColour (c::text);
                    g.drawRect (r, 1.5f);
                }
            }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        int lane = 0, step = 0;
        if (! hit (e.getPosition(), lane, step))
            return;
        if (e.mods.isShiftDown() || e.mods.isPopupMenu())
            owner_.selectCell (lane, step);
        else
            owner_.clickCell (lane, step);
    }

private:
    PatternEditor& owner_;
};

//==============================================================================
PatternEditor::PatternEditor (DrumsProcessor& processor, juce::Colour accent) : processor_ (processor), accent_ (accent)
{
    setName ("pattern editor");
    const auto button = [this] (std::unique_ptr<vox::ui::VoxButton>& b, const juce::String& text, const juce::String& tip,
                                vox::ui::VoxButton::Type type = vox::ui::VoxButton::Type::Secondary) {
        b = std::make_unique<vox::ui::VoxButton> (text, type);
        b->setTooltip (tip);
        addAndMakeVisible (*b);
    };
    for (int i = 0; i < kit::maxPatternBars; ++i)
    {
        button (bars_[static_cast<std::size_t> (i)], juce::String (i + 1), "Show bar " + juce::String (i + 1), vox::ui::VoxButton::Type::Toggle);
        bars_[static_cast<std::size_t> (i)]->onClick = [this, i] { selectBar (i); };
    }
    steps_ = std::make_unique<vox::ui::VoxComboBox>();
    steps_->addItemList ({ "16 steps", "32 steps", "64 steps" }, 1);
    steps_->setTooltip ("Steps per bar: 1/16, 1/32 or 1/64 notes");
    steps_->onChange = [this] {
        if (refreshing_)
            return;
        const int steps = 16 << steps_->getSelectedItemIndex();
        edit ("steps", [steps] (StoredPattern& p) { p.resize (steps, p.bars); });
    };
    barCount_ = std::make_unique<vox::ui::VoxComboBox>();
    barCount_->addItemList ({ "1 bar", "2 bars", "3 bars", "4 bars" }, 1);
    barCount_->setTooltip ("Pattern length; added bars repeat bar 1");
    barCount_->onChange = [this] {
        if (refreshing_)
            return;
        const int bars = barCount_->getSelectedItemIndex() + 1;
        edit ("bars", [bars] (StoredPattern& p) { p.resize (p.steps, bars); });
    };
    addAndMakeVisible (*steps_);
    addAndMakeVisible (*barCount_);

    button (lanes_, "1-8", "Show lanes 1-8 or 9-16 (when the editor is too small for all 16)");
    lanes_->onClick = [this] {
        lanePage_ = 1 - lanePage_;
        refreshControls();
        repaint();
    };
    button (dup_, "Dup", "Copy this bar to the next bar");
    dup_->onClick = [this] {
        const int from = bar_;
        edit ("dup", [from] (StoredPattern& p) {
            const int to = (from + 1) % p.bars;
            for (int lane = 0; lane < padCount; ++lane)
                for (int s = 0; s < p.steps; ++s)
                    p.cell (lane, to, s) = p.cell (lane, from, s);
        });
        selectBar ((from + 1) % shown_.bars);
    };
    button (clear_, "Clear", "Clear this bar");
    clear_->onClick = [this] {
        const int at = bar_;
        edit ("clear", [at] (StoredPattern& p) {
            for (int lane = 0; lane < padCount; ++lane)
                for (int s = 0; s < p.steps; ++s)
                    p.cell (lane, at, s) = {};
        });
    };
    button (swap_, "A/B", "Swap this pattern with its variation B");
    swap_->onClick = [this] {
        editBoth ([] (StoredPattern& a, StoredPattern& b) {
            // Member-wise: std::swap would put a ~24 KB temporary on the stack.
            a.cells.swap (b.cells);
            std::swap (a.steps, b.steps);
            std::swap (a.bars, b.bars);
            std::swap (a.seed, b.seed);
        });
    };
    button (copyB_, "A>B", "Copy this pattern to its variation B");
    copyB_->onClick = [this] { editBoth ([] (StoredPattern& a, StoredPattern& b) { b = a; }); };
    button (seed_, "Seed", "New seed: steps below 100 % probability decide differently");
    seed_->onClick = [this] {
        edit ("seed", [] (StoredPattern& p) { p.seed = p.seed * 1664525u + 1013904223u; });
    };
    button (undo_, "Undo", "Undo the last pattern edit");
    undo_->onClick = [this] {
        checkEpoch();
        processor_.patternHistory().undo (processor_.patterns());
        lastKey_.clear();
        update();
    };
    button (redo_, "Redo", "Redo");
    redo_->onClick = [this] {
        checkEpoch();
        processor_.patternHistory().redo (processor_.patterns());
        lastKey_.clear();
        update();
    };
    button (midi_, "MIDI", "Click: save the pattern as a MIDI file. Drag: drop it into your DAW.", vox::ui::VoxButton::Type::Primary);
    midi_->onClick = [this] {
        // Frozen at the click: what is saved is what was there when the
        // button was pressed, whatever changes while the dialog is open.
        const auto frozen = snapshot();
        chooser_ = std::make_unique<juce::FileChooser> ("Save as MIDI", saveAsDefault(), "*.mid");
        chooser_->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                   | juce::FileBrowserComponent::warnAboutOverwriting,
                               [frozen, safe = juce::Component::SafePointer<PatternEditor> (this)] (const juce::FileChooser& fc) {
                                   if (fc.getResult() == juce::File())
                                       return;
                                   const auto file = fc.getResult().withFileExtension ("mid");
                                   const bool ok = writeSnapshot (*frozen, file);
                                   if (safe != nullptr)
                                       safe->exported (ok, file);
                               });
    };
    midi_->addMouseListener (this, false);
    abLabel_ = std::make_unique<juce::Label> ("ab", "");
    abLabel_->setFont (vox::ui::typography::valueText());
    abLabel_->setColour (juce::Label::textColourId, c::textMuted);
    addAndMakeVisible (*abLabel_);

    grid_ = std::make_unique<Grid> (*this);
    addAndMakeVisible (*grid_);

    const auto knob = [this] (std::unique_ptr<vox::ui::VoxKnob>& k, const juce::String& label, double lo, double hi,
                              std::function<juce::String (double)> text, std::function<void (kit::PatternStep&, double)> set) {
        k = std::make_unique<vox::ui::VoxKnob> (label, vox::ui::VoxKnob::Size::Normal);
        auto& s = k->getSlider();
        s.setRange (lo, hi, 1.0);
        s.textFromValueFunction = std::move (text);
        const auto key = "step." + label;
        s.onDragStart = [this] { gesture_ = true; };
        s.onDragEnd = [this] { gesture_ = false; lastKey_.clear(); };
        s.onValueChange = [this, &s, key, set] {
            if (refreshing_)
                return;
            const int lane = lane_, bar = bar_, step = step_;
            const double v = s.getValue();
            edit (key + juce::String (lane) + ":" + juce::String (bar) + ":" + juce::String (step),
                  [lane, bar, step, v, set] (StoredPattern& p) { set (p.cell (lane, bar, step), v); });
        };
        addAndMakeVisible (*k);
    };
    knob (velocity_, "Velocity", 1.0, 127.0, [] (double v) { return juce::String (juce::roundToInt (v)); },
          [] (kit::PatternStep& c, double v) { c.velocity = static_cast<std::uint8_t> (juce::roundToInt (v)); c.on = true; });
    knob (probability_, "Chance", 1.0, 100.0, [] (double v) { return juce::String (juce::roundToInt (v)) + " %"; },
          [] (kit::PatternStep& c, double v) { c.probability = static_cast<std::uint8_t> (juce::roundToInt (v)); });
    knob (ratchet_, "Ratchet", 1.0, 8.0, [] (double v) { return juce::String (juce::roundToInt (v)) + "x"; },
          [] (kit::PatternStep& c, double v) { c.ratchet = static_cast<std::uint8_t> (juce::roundToInt (v)); });
    knob (micro_, "Micro", -50.0, 50.0, [] (double v) {
              const int n = juce::roundToInt (v);
              return (n > 0 ? "+" : "") + juce::String (n) + " %";
          },
          [] (kit::PatternStep& c, double v) { c.micro = static_cast<std::int8_t> (juce::roundToInt (v)); });
    micro_->setStyle (vox::ui::VoxKnob::Style::Bipolar);
    accentButton_ = std::make_unique<juce::ToggleButton> ("Accent");
    accentButton_->onClick = [this] {
        if (refreshing_)
            return;
        const int lane = lane_, bar = bar_, step = step_;
        const bool on = accentButton_->getToggleState();
        edit ("accent", [lane, bar, step, on] (StoredPattern& p) { p.cell (lane, bar, step).accent = on; });
    };
    addAndMakeVisible (*accentButton_);
    lockButton_ = std::make_unique<juce::ToggleButton> ("Lock");
    lockButton_->setTooltip ("A locked step is left as it is by the generator");
    lockButton_->onClick = [this] {
        if (refreshing_)
            return;
        const int lane = lane_, bar = bar_, step = step_;
        const bool on = lockButton_->getToggleState();
        edit ("lock", [lane, bar, step, on] (StoredPattern& p) { p.cell (lane, bar, step).locked = on; });
    };
    addAndMakeVisible (*lockButton_);

    epoch_ = processor_.patternEpoch();
    update (false);
}

PatternEditor::~PatternEditor()
{
    midi_->removeMouseListener (this);
}

void PatternEditor::checkEpoch()
{
    // A project restore replaced the patterns: older history entries would
    // undo into another project.
    if (processor_.patternEpoch() != epoch_)
    {
        epoch_ = processor_.patternEpoch();
        processor_.patternHistory().clear();
        lastKey_.clear();
    }
}

void PatternEditor::edit (const juce::String& key, const std::function<void (StoredPattern&)>& change)
{
    checkEpoch();
    if (slot_ < 0)
        return;
    auto& store = processor_.patterns();
    // On the heap: an entry holds ~100 KB of patterns (small host stacks).
    auto e = std::make_unique<PatternHistory::Entry>();
    if (songBar_ >= 0)
    {
        // The bar as it plays now, changed, becomes the bar's own cells.
        std::uint64_t generation = 0;
        store.copySongBar (songBar_, e->before, generation);
        e->after = e->before;
        change (e->after);
        e->after.resize (e->after.steps, 1);
        if (e->after == e->before)
            return;
        e->slot = -1;
        e->hasSong = true;
        e->songBefore = store.song();
        e->songAfter = e->songBefore;
        auto& bar = e->songAfter.bars[static_cast<std::size_t> (songBar_)];
        bar = {};
        bar.kind = SongBar::Kind::unique;
        bar.cells = songedit::patternToBar (e->after);
        store.setSong (e->songAfter);
        auto& songHistory = processor_.patternHistory();
        const bool amendedSong = gesture_ && key == lastKey_ && songHistory.amendLastSong (e->songAfter);
        if (! amendedSong)
            songHistory.push (std::move (e));
        lastKey_ = gesture_ ? key : juce::String();
        update();
        return;
    }
    e->slot = slot_;
    store.copySlot (slot_, e->before);
    e->after = e->before;
    change (e->after);
    if (e->after == e->before)
        return;
    store.setSlot (slot_, e->after);
    auto& history = processor_.patternHistory();
    // One knob drag is one undo step: later changes of the same drag amend it.
    const bool amended = gesture_ && key == lastKey_ && history.amendLast (slot_, e->after);
    if (! amended)
        history.push (std::move (e));
    lastKey_ = gesture_ ? key : juce::String();
    update();
}

void PatternEditor::editBoth (const std::function<void (StoredPattern& a, StoredPattern& b)>& change)
{
    checkEpoch();
    if (slot_ < 0 || songBar_ >= 0)   // a song bar has no variation
        return;
    auto& store = processor_.patterns();
    auto e = std::make_unique<PatternHistory::Entry>();
    e->slot = slot_;
    e->alternate = true;
    store.copySlot (slot_, e->before);
    store.copyAlternate (slot_, e->beforeB);
    e->after = e->before;
    e->afterB = e->beforeB;
    change (e->after, e->afterB);
    if (e->after == e->before && e->afterB == e->beforeB)
        return;
    store.setSlot (slot_, e->after);
    store.setAlternate (slot_, e->afterB);
    processor_.patternHistory().push (std::move (e));
    lastKey_.clear();
    update();
}

void PatternEditor::update (bool repaintIfChanged)
{
    checkEpoch();
    const int index = std::clamp (static_cast<int> (std::lround (processor_.state().getRawParameterValue (patternParameterId)->load())), 0,
                                  PatternStore::slotCount - 1);
    bool changed = false;
    if (index != slot_ || songBar_ != shownSongBar_ || processor_.patterns().generation() != generation_)
    {
        if (songBar_ >= 0)
            processor_.patterns().copySongBar (songBar_, shown_, generation_);
        else
            processor_.patterns().copySlot (index, shown_, generation_);   // read together (review 6745483562)
        shownSongBar_ = songBar_;
        if (index != slot_)
            lastKey_.clear();
        slot_ = index;
        bar_ = juce::jlimit (0, shown_.bars - 1, bar_);
        step_ = juce::jlimit (0, shown_.steps - 1, step_);
        changed = true;
    }
    const double stepPpq = 4.0 / shown_.steps;
    const auto g = static_cast<long long> (std::floor (processor_.lastPpq() / stepPpq));
    // A song bar shows the playhead only while that bar plays.
    const bool here = songBar_ < 0 || processor_.songBarPlaying() == songBar_;
    const int play = processor_.transportPlaying() && g >= 0 && here ? static_cast<int> (g % shown_.steps) : -1;
    const int playBar = g >= 0 && here ? static_cast<int> ((g / shown_.steps) % shown_.bars) : -1;
    if (play != playStep_ || playBar != playBar_)
    {
        playStep_ = play;
        playBar_ = playBar;
        changed = true;
    }
    if (changed)
    {
        refreshControls();
        if (repaintIfChanged)
            repaint();
    }
}

void PatternEditor::refreshControls()
{
    refreshing_ = true;
    for (int i = 0; i < kit::maxPatternBars; ++i)
    {
        auto& b = *bars_[static_cast<std::size_t> (i)];
        b.setEnabled (i < shown_.bars);
        b.setToggleState (i == bar_, juce::dontSendNotification);
    }
    steps_->setSelectedItemIndex (shown_.steps == 64 ? 2 : shown_.steps == 32 ? 1 : 0, juce::dontSendNotification);
    barCount_->setSelectedItemIndex (shown_.bars - 1, juce::dontSendNotification);
    const auto& c = shown_.cell (lane_, bar_, step_);
    velocity_->getSlider().setValue (c.on ? c.velocity : 100, juce::dontSendNotification);
    probability_->getSlider().setValue (c.probability, juce::dontSendNotification);
    ratchet_->getSlider().setValue (c.ratchet, juce::dontSendNotification);
    micro_->getSlider().setValue (c.micro, juce::dontSendNotification);
    accentButton_->setToggleState (c.accent, juce::dontSendNotification);
    for (auto* k : { velocity_.get(), probability_.get(), ratchet_.get(), micro_.get() })
        k->setEnabled (c.on);
    accentButton_->setEnabled (c.on);
    lockButton_->setToggleState (c.locked, juce::dontSendNotification);
    for (auto* b : { swap_.get(), copyB_.get(), dup_.get() })
        b->setEnabled (songBar_ < 0);
    barCount_->setEnabled (songBar_ < 0);
    lanes_->setButtonText (lanePage_ == 0 ? "1-8" : "9-16");
    lanes_->setVisible (grid_ != nullptr && grid_->rows() < padCount);
    undo_->setEnabled (processor_.patternHistory().canUndo());
    redo_->setEnabled (processor_.patternHistory().canRedo());
    abLabel_->setColour (juce::Label::textColourId, exportMessage_.isEmpty() ? c::textMuted : juce::Colours::orange);
    abLabel_->setText (exportMessage_.isNotEmpty() ? exportMessage_ : ((songBar_ >= 0 ? "Song bar " + juce::String (songBar_ + 1) : "Pattern " + juce::String (slot_ + 1)) + "  " + juce::String (lane_ + 1) + "." + juce::String (bar_ + 1) + "."
                           + juce::String (step_ + 1)),
                       juce::dontSendNotification);
    refreshing_ = false;
}

void PatternEditor::editSongBar (int bar)
{
    songBar_ = bar >= 0 && bar < kit::maxSongBars ? bar : -1;
    lastKey_.clear();
    update();
}

juce::Button* PatternEditor::lockToggle()
{
    return lockButton_.get();
}

void PatternEditor::selectBar (int bar)
{
    bar_ = juce::jlimit (0, shown_.bars - 1, bar);
    refreshControls();
    repaint();
}

void PatternEditor::selectCell (int lane, int step)
{
    lane_ = juce::jlimit (0, padCount - 1, lane);
    lanePage_ = lane_ / (padCount / 2);   // a selected lane is always shown
    step_ = juce::jlimit (0, shown_.steps - 1, step);
    refreshControls();
    repaint();
}

void PatternEditor::clickCell (int lane, int step)
{
    selectCell (lane, step);
    const int l = lane_, s = step_, b = bar_;
    edit ("toggle", [l, s, b] (StoredPattern& p) {
        auto& c = p.cell (l, b, s);
        if (c.on)
            c = {};
        else
        {
            c = {};
            c.on = true;
            c.velocity = 100;
        }
    });
}

juce::Rectangle<int> PatternEditor::cellBounds (int lane, int step) const
{
    return grid_->cell (lane, step) + grid_->getPosition();
}

juce::Slider* PatternEditor::inspector (const juce::String& name)
{
    if (name == "velocity") return &velocity_->getSlider();
    if (name == "probability") return &probability_->getSlider();
    if (name == "ratchet") return &ratchet_->getSlider();
    if (name == "micro") return &micro_->getSlider();
    return nullptr;
}

juce::Button* PatternEditor::accentToggle()
{
    return accentButton_.get();
}

juce::Button* PatternEditor::button (const juce::String& name)
{
    if (name == "dup") return dup_.get();
    if (name == "clear") return clear_.get();
    if (name == "swap") return swap_.get();
    if (name == "copyB") return copyB_.get();
    if (name == "seed") return seed_.get();
    if (name == "undo") return undo_.get();
    if (name == "redo") return redo_.get();
    if (name == "midi") return midi_.get();
    if (name == "lanes") return lanes_.get();
    return nullptr;
}

juce::ComboBox* PatternEditor::combo (const juce::String& name)
{
    if (name == "steps") return steps_.get();
    if (name == "bars") return barCount_.get();
    return nullptr;
}

juce::Button* PatternEditor::barButton (int bar)
{
    return bar >= 0 && bar < kit::maxPatternBars ? bars_[static_cast<std::size_t> (bar)].get() : nullptr;
}

juce::String PatternEditor::fileName() const
{
    if (processor_.uiState().exportScope == 1)
        return "VOX Drums Song.mid";
    return "VOX Drums Pattern " + juce::String (juce::jmax (0, slot_) + 1) + ".mid";
}

juce::File PatternEditor::saveAsDefault() const
{
    // Save-as is the user's own file: suggested in Documents, never the
    // drag files below.
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (fileName());
}

namespace {
juce::File& dragRootOverride()
{
    static juce::File root;
    return root;
}
} // namespace

juce::File PatternEditor::dragRoot()
{
    if (dragRootOverride() != juce::File())
        return dragRootOverride();
    return juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("VOX Drums MIDI drags");
}

void PatternEditor::setDragRootForTesting (const juce::File& root)
{
    dragRootOverride() = root;
}

namespace {

// Drag and instance folders carry their creation time in the name,
// "<ms since epoch>-<...>": file and folder modification times are not
// reliable (directory times change with their content, and setting them
// fails on Windows).
juce::int64 createdMs (const juce::File& folder)
{
    return folder.getFileName().upToFirstOccurrenceOf ("-", false, false).getLargeIntValue();
}

int dragNumber (const juce::File& folder)
{
    return folder.getFileName().fromFirstOccurrenceOf ("-", false, false).upToFirstOccurrenceOf ("-", false, false).getIntValue();
}

} // namespace

juce::File PatternEditor::newDragFile()
{
    // Review 6753052108: every drag gets its own folder, unique per editor
    // instance (and process) and per drag, so no other export, from this
    // or another instance or process, can replace a file a DAW is still
    // importing. The file name stays readable.
    const auto now = juce::Time::getCurrentTime();
    cleanupDragFiles (now);
    const auto folder = dragRoot().getChildFile (instanceId_).getChildFile (
        juce::String (now.toMilliseconds()) + "-" + juce::String (++dragCount_) + "-" + juce::Uuid().toString().substring (0, 12));
    folder.createDirectory();
    return folder.getChildFile (fileName());
}

void PatternEditor::cleanupDragFiles (juce::Time now)
{
    // Bounded: this instance keeps its newest drag and any younger than
    // dragLifetime (time for a DAW to import a drop); folders other
    // instances left behind are removed only when they and all their drags
    // are older than staleLifetime (a crash or a closed host). Never the
    // newest drag of this instance. Ages come from the folder names.
    const auto nowMs = now.toMilliseconds();
    const auto mine = dragRoot().getChildFile (instanceId_);
    auto drags = mine.findChildFiles (juce::File::findDirectories, false);
    std::sort (drags.begin(), drags.end(), [] (const juce::File& a, const juce::File& b) {
        return createdMs (a) != createdMs (b) ? createdMs (a) < createdMs (b) : dragNumber (a) < dragNumber (b);
    });
    for (int i = 0; i + 1 < drags.size(); ++i)
        if (nowMs - createdMs (drags[i]) > dragLifetime.inMilliseconds())
            drags[i].deleteRecursively();
    for (const auto& other : dragRoot().findChildFiles (juce::File::findDirectories, false))
    {
        if (other == mine)
            continue;
        auto newest = createdMs (other);
        for (const auto& drag : other.findChildFiles (juce::File::findDirectories, false))
            newest = std::max (newest, createdMs (drag));
        if (nowMs - newest > staleLifetime.inMilliseconds())
            other.deleteRecursively();
    }
}

std::shared_ptr<const ExportSnapshot> PatternEditor::snapshot() const
{
    return processor_.captureExport (processor_.uiState().exportScope == 1 ? ExportSnapshot::Scope::song : ExportSnapshot::Scope::pattern);
}

bool PatternEditor::writeSnapshot (const ExportSnapshot& snapshot, const juce::File& file)
{
    const auto blob = DrumsProcessor::renderExport (snapshot);
    return blob.getSize() > 0 && file.replaceWithData (blob.getData(), blob.getSize());
}

bool PatternEditor::writeMidi (const juce::File& file)
{
    const bool ok = writeSnapshot (*snapshot(), file);
    exported (ok, file);
    return ok;
}

// A failed export is shown, never silent (and never an empty or partial file
// presented as the song).
void PatternEditor::exported (bool ok, const juce::File& file)
{
    exportMessage_ = ok ? juce::String() : "MIDI export failed: cannot write " + file.getFullPathName();
    refreshControls();
}

juce::File PatternEditor::exportForDrag()
{
    const auto file = newDragFile();
    if (! writeMidi (file))
        return {};
    return file;
}

void PatternEditor::startDrag()
{
    const auto file = exportForDrag();
    if (file == juce::File())
        return;
    if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
        container->performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this);
}

void PatternEditor::mouseDown (const juce::MouseEvent&)
{
    dragging_ = false;
}

void PatternEditor::mouseDrag (const juce::MouseEvent& e)
{
    // A drag that starts on the MIDI button exports the pattern to a file
    // and drops it (into the DAW's arrangement, a folder, ...).
    if (e.eventComponent == midi_.get() && ! dragging_ && e.getDistanceFromDragStart() > 6)
    {
        dragging_ = true;
        midi_->setState (juce::Button::buttonNormal);
        startDrag();
    }
}

void PatternEditor::paint (juce::Graphics&) {}

void PatternEditor::resized()
{
    auto area = getLocalBounds();
    // Top: bar selector, grid size, bar actions, position.
    auto top = area.removeFromTop (28);
    for (auto& b : bars_)
        b->setBounds (top.removeFromLeft (28).reduced (1));
    top.removeFromLeft (sp::sm);
    steps_->setBounds (top.removeFromLeft (100).reduced (1));
    barCount_->setBounds (top.removeFromLeft (84).reduced (1));
    top.removeFromLeft (sp::sm);
    for (auto* b : { dup_.get(), clear_.get(), seed_.get() })
        b->setBounds (top.removeFromLeft (60).reduced (1));
    lanes_->setBounds (top.removeFromRight (56).reduced (1));
    top.removeFromLeft (sp::sm);
    abLabel_->setBounds (top);
    area.removeFromTop (sp::xs);
    // Bottom: the selected step, then A/B, history and export.
    auto bottom = area.removeFromBottom (juce::jlimit (56, 78, area.getHeight() / 4));
    const int kw = juce::jlimit (48, 66, bottom.getWidth() / 11);
    for (auto* k : { velocity_.get(), probability_.get(), ratchet_.get(), micro_.get() })
        k->setBounds (bottom.removeFromLeft (kw));
    {
        auto toggles = bottom.removeFromLeft (84);
        accentButton_->setBounds (toggles.removeFromTop (toggles.getHeight() / 2).withSizeKeepingCentre (80, 24));
        lockButton_->setBounds (toggles.withSizeKeepingCentre (80, 24));
    }
    midi_->setBounds (bottom.removeFromRight (64).withSizeKeepingCentre (62, 30));
    for (auto* b : { redo_.get(), undo_.get(), copyB_.get(), swap_.get() })
        b->setBounds (bottom.removeFromRight (56).withSizeKeepingCentre (54, 30));
    area.removeFromBottom (sp::xs);
    grid_->setBounds (area);
    refreshControls();   // the lane page button depends on the grid height
}

} // namespace vox::drums
