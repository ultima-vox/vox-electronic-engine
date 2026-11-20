#include "PatternEditor.h"

#include "DrumsParameters.h"
#include "Processor.h"
#include "Tokens.h"
#include "Typography.h"
#include "DrumsDial.h"

#include <cmath>

namespace vox::drums {
namespace {

namespace c = vox::ui::tokens::colour;
namespace sp = vox::ui::tokens::spacing;

constexpr int nameWidth = 70;

} // namespace

//==============================================================================
// The editor addresses a lane's cells by their order in its bar ("step":
// beat by beat, the active cells of each). With every beat straight in 4/4
// this is the 16th / 32nd / 64th index, as before PR 137 §10.
namespace {
int cellCount (const timing::BarTiming& t) noexcept
{
    int n = 0;
    for (int b = 0; b < t.beats(); ++b)
        n += t.cells (b);
    return n;
}

// Beat and cell of `step` (false when the bar has no such cell).
bool locate (const timing::BarTiming& t, int step, int& beat, int& cell) noexcept
{
    if (step < 0)
        return false;
    for (beat = 0; beat < t.beats(); ++beat)
    {
        if (step < t.cells (beat))
        {
            cell = step;
            return true;
        }
        step -= t.cells (beat);
    }
    return false;
}

int slotOfStep (const timing::BarTiming& t, int step) noexcept
{
    int beat = 0, cell = 0;
    return locate (t, step, beat, cell) ? t.slot (beat, cell) : -1;
}

kit::PatternStep& stepCell (StoredPattern& p, int lane, int bar, int step) noexcept
{
    const int slot = slotOfStep (p.timing (lane, bar), step);
    return p.cell (lane, bar, slot >= 0 ? slot : 0);
}

int stepCellTicks (const StoredPattern& p, int lane, int bar, int step) noexcept
{
    const auto t = p.timing (lane, bar);
    int beat = 0, cell = 0;
    return locate (t, step, beat, cell) ? t.cellTicks (beat) : 0;
}

constexpr timing::Resolution stepResolutions[3] = { timing::Resolution::sixteenth, timing::Resolution::thirtySecond,
                                                    timing::Resolution::sixtyFourth };
constexpr const char* parameterNames[] { "Velocity", "Probability", "Ratchet", "Microtiming" };
int parameterValue (int kind, const kit::PatternStep& c, int ticks)
{
    switch (kind)
    {
        case 0: return c.on ? c.velocity : 0;
        case 1: return c.probability;
        case 2: return c.ratchet;
        default: return juce::roundToInt (100.0 * c.microTicks / juce::jmax (1, ticks));
    }
}
void setStepParameter (int kind, kit::PatternStep& c, double value, int ticks)
{
    const auto n = juce::roundToInt (value);
    switch (kind)
    {
        case 0: c.velocity = static_cast<std::uint8_t> (juce::jlimit (1, 127, n)); c.on = true; break;
        case 1: c.probability = static_cast<std::uint8_t> (juce::jlimit (1, 100, n)); break;
        case 2: c.ratchet = static_cast<std::uint8_t> (juce::jlimit (1, 8, n)); break;
        case 3: c.microTicks = static_cast<std::int16_t> (kit::percentToTicks (juce::jlimit (-50, 50, n), ticks)); break;
        default: break;
    }
}
} // namespace

//==============================================================================
// The step grid of the shown bar.
class PatternEditor::Grid final : public juce::Component {
public:
    explicit Grid (PatternEditor& owner) : owner_ (owner) { setName ("pattern grid"); }

    // Lanes shown: all 16 when each row gets at least 11 px, else a page
    // of 8 (the editor's lane page button).
    int rulerHeight() const { return owner_.parameterLanesEnabled_ ? 24 : 0; }
    int gridHeight() const { return juce::jmax (88, getHeight() - rulerHeight() - static_cast<int> (owner_.parameterLanes_.size()) * 60); }
    int rows() const { return gridHeight() >= 16 * 11 ? padCount : padCount / 2; }
    int firstLane() const { return rows() == padCount ? 0 : owner_.lanePage_ * (padCount / 2); }

    // One time mapping for the ruler, grid and value lanes, independent of
    // which page of pad rows happens to be visible.
    juce::Rectangle<float> timeArea (int lane, int step) const
    {
        const auto& p = owner_.shown_;
        const auto t = p.timing (lane, owner_.bar_);
        int beat = 0, c = 0;
        if (! locate (t, step, beat, c)) return {};
        const float width = static_cast<float> (getWidth() - nameWidth), bar = static_cast<float> (t.barTicks());
        return { static_cast<float> (nameWidth) + width * static_cast<float> (t.cellStartTick (beat, c)) / bar,
                 0.0f, width * static_cast<float> (t.cellTicks (beat)) / bar, 1.0f };
    }
    juce::Rectangle<float> cellArea (int lane, int step) const
    {
        const int row = lane - firstLane();
        if (row < 0 || row >= rows()) return {};
        const auto area = timeArea (lane, step);
        if (area.isEmpty()) return {};
        const float h = static_cast<float> (gridHeight()) / static_cast<float> (rows());
        return area.withY (static_cast<float> (rulerHeight()) + h * static_cast<float> (row)).withHeight (h);
    }

    juce::Rectangle<int> cell (int lane, int step) const { return cellArea (lane, step).getSmallestIntegerContainer(); }

    bool hit (juce::Point<int> at, int& lane, int& step) const
    {
        const auto& p = owner_.shown_;
        if (at.x < nameWidth || at.y < rulerHeight() || at.x >= getWidth() || at.y >= rulerHeight() + gridHeight())
            return false;
        lane = firstLane() + (at.y - rulerHeight()) * rows() / gridHeight();
        if (lane < 0 || lane >= padCount)
            return false;
        const auto t = p.timing (lane, owner_.bar_);
        const int tick = (at.x - nameWidth) * t.barTicks() / juce::jmax (1, getWidth() - nameWidth);
        step = 0;
        for (int beat = 0; beat < t.beats(); ++beat)
            for (int c = 0; c < t.cells (beat); ++c, ++step)
                if (tick >= t.cellStartTick (beat, c) && tick < t.cellStartTick (beat, c) + t.cellTicks (beat))
                    return true;
        return false;
    }

    void paint (juce::Graphics& g) override
    {
        const auto& p = owner_.shown_;
        const float h = static_cast<float> (gridHeight()) / static_cast<float> (rows());
        g.setFont (vox::ui::typography::valueText().withHeight (juce::jlimit (9.0f, 12.0f, h * 0.85f)));
        for (int row = 0; row < rows(); ++row)
        {
            const int lane = firstLane() + row;
            const float y = static_cast<float> (rulerHeight()) + h * static_cast<float> (row);
            const bool laneOn = owner_.processor_.state().getRawParameterValue ("drm.seq.lane." + juce::String (padPrefix (lane).substr (4)))->load() >= 0.5f;
            g.setColour (lane == owner_.lane_ ? c::text : (laneOn ? c::textSecondary : c::textMuted));
            g.drawText (juce::String (lane + 1) + " " + padName (lane), juce::Rectangle<float> (0.0f, y, nameWidth - 4.0f, h),
                        juce::Justification::centredLeft, true);
            const auto t = p.timing (lane, owner_.bar_);
            int s = 0;
            for (int beat = 0; beat < t.beats(); ++beat)
                for (int inBeat = 0; inBeat < t.cells (beat); ++inBeat, ++s)
            {
                const auto& cellValue = p.cell (lane, owner_.bar_, t.slot (beat, inBeat));
                const auto area = cellArea (lane, s);
                const float w = area.getWidth();
                auto r = area.reduced (w > 8.0f ? 1.0f : 0.5f);
                const int start = t.cellStartTick (beat, inBeat);
                const bool playing = owner_.bar_ == owner_.playBar_ && owner_.playStep_ >= start && owner_.playStep_ < start + t.cellTicks (beat);
                g.setColour (playing ? c::panelRaised.brighter (0.35f) : (inBeat == 0 ? c::panelRaised : c::control));
                g.fillRect (r);
                if (cellValue.on)
                {
                    const float alpha = laneOn ? 0.3f + 0.7f * static_cast<float> (cellValue.velocity) / 127.0f : 0.25f;
                    g.setColour (owner_.accent_.withAlpha (alpha));
                    // Probability below 100 %: the step fills only that share.
                    auto fill = r.reduced (1.0f);
                    if (cellValue.probability < 100)
                        fill = fill.removeFromBottom (fill.getHeight() * static_cast<float> (cellValue.probability) / 100.0f);
                    fill = fill.translated (static_cast<float> (cellValue.microTicks) / static_cast<float> (t.cellTicks (beat)) * w, 0.0f);
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
        paintParameterLanes (g);
    }

    juce::Rectangle<int> parameterCell (unsigned lane, int step) const
    {
        if (lane >= owner_.parameterLanes_.size()) return {};
        const auto area = timeArea (owner_.lane_, step);
        if (area.isEmpty()) return {};
        return area.withY (static_cast<float> (rulerHeight() + gridHeight() + static_cast<int> (lane) * 60 + 16))
                   .withHeight (44.0f).getSmallestIntegerContainer();
    }
    void cancelGesture()
    {
        if (! editingLane_) return;
        editingLane_ = false; owner_.gesture_ = false; owner_.lastKey_.clear();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        cancelGesture(); owner_.update();
        const auto index = (e.y - rulerHeight() - gridHeight()) / 60;
        if (e.y >= rulerHeight() + gridHeight() && index >= 0 && index < static_cast<int> (owner_.parameterLanes_.size())
            && e.y >= rulerHeight() + gridHeight() + index * 60 + 16)
        {
            editingLane_ = true; laneIndex_ = static_cast<unsigned> (index);
            pad_ = owner_.lane_; bar_ = owner_.bar_; slot_ = owner_.slot_; song_ = owner_.songBar_;
            epoch_ = owner_.processor_.patternEpoch(); kind_ = owner_.parameterLanes_[laneIndex_];
            expectedGeneration_ = owner_.processor_.patterns().generation();
            timing_ = owner_.shown_.timing (pad_, bar_);
            owner_.gesture_ = true; owner_.lastKey_.clear(); editLane (e.getPosition()); return;
        }
        int lane = 0, step = 0;
        if (! hit (e.getPosition(), lane, step))
            return;
        if (e.mods.isShiftDown() || e.mods.isPopupMenu())
            owner_.selectCell (lane, step);
        else
            owner_.clickCell (lane, step);
    }
    void mouseDrag (const juce::MouseEvent& e) override { if (editingLane_) editLane (e.getPosition()); }
    void mouseUp (const juce::MouseEvent&) override { cancelGesture(); }

private:
    void paintParameterLanes (juce::Graphics& g)
    {
        if (! owner_.parameterLanesEnabled_) return;
        const auto t = owner_.shown_.timing (owner_.lane_, owner_.bar_);
        g.setFont (vox::ui::typography::valueText().withHeight (10.0f));
        for (int step = 0; step < cellCount (t); ++step)
        {
            const auto area = timeArea (owner_.lane_, step);
            g.setColour (c::textMuted);
            g.drawVerticalLine (juce::roundToInt (area.getX()), 0.0f, static_cast<float> (rulerHeight()));
            if (area.getWidth() >= 20.0f)
                g.drawText (juce::String (step + 1), juce::Rectangle<int> { juce::roundToInt (area.getX()) + 2, 0,
                              juce::roundToInt (area.getWidth()) - 2, rulerHeight() }, juce::Justification::centredLeft);
        }
        for (unsigned row = 0; row < owner_.parameterLanes_.size(); ++row)
        {
            const int kind = owner_.parameterLanes_[row];
            const auto first = parameterCell (row, 0);
            g.setColour (c::textSecondary);
            g.drawText (juce::String (parameterNames[kind]) + " / Pad " + juce::String (owner_.lane_ + 1),
                        juce::Rectangle<int> { 2, first.getY() - 16, getWidth() - 4, 16 }, juce::Justification::centredLeft);
            for (int step = 0; step < cellCount (t); ++step)
            {
                const auto& value = stepCell (owner_.shown_, owner_.lane_, owner_.bar_, step);
                auto area = parameterCell (row, step).toFloat().reduced (0.5f);
                g.setColour (c::control); g.fillRect (area);
                const float v = static_cast<float> (parameterValue (kind, value, stepCellTicks (owner_.shown_, owner_.lane_, owner_.bar_, step)));
                g.setColour (owner_.accent_.withAlpha (value.on ? 0.85f : 0.25f));
                const float amount = kind == 0 ? v / 127.0f : kind == 1 ? v / 100.0f : kind == 2 ? v / 8.0f : v / 100.0f;
                if (kind == 3)
                {
                    const auto centre = area.getCentreY();
                    g.fillRect (area.withY (centre - juce::jmax (0.0f, amount) * area.getHeight())
                                    .withHeight (std::abs (amount) * area.getHeight()));
                    g.setColour (c::textMuted); g.drawHorizontalLine (juce::roundToInt (centre), area.getX(), area.getRight());
                }
                else g.fillRect (area.removeFromBottom (juce::jlimit (0.0f, 1.0f, amount) * area.getHeight()));
                if (step == owner_.step_) { g.setColour (c::text); g.drawRect (parameterCell (row, step).toFloat().reduced (0.5f)); }
            }
        }
    }
    void editLane (juce::Point<int> at)
    {
        if (expectedGeneration_ != owner_.processor_.patterns().generation()) { cancelGesture(); return; }
        owner_.update();
        const auto now = owner_.shown_.timing (pad_, bar_);
        if (epoch_ != owner_.processor_.patternEpoch() || pad_ != owner_.lane_ || bar_ != owner_.bar_
            || slot_ != owner_.slot_ || song_ != owner_.songBar_ || laneIndex_ >= owner_.parameterLanes_.size()
            || kind_ != owner_.parameterLanes_[laneIndex_] || now.signature != timing_.signature
            || now.resolution != timing_.resolution || now.alternativeBeats != timing_.alternativeBeats)
        { cancelGesture(); return; }
        int step = -1;
        for (int n = 0; n < cellCount (now); ++n)
        {
            const auto area = parameterCell (laneIndex_, n);
            if (at.x >= area.getX() && at.x < area.getRight()) { step = n; break; }
        }
        if (step < 0) return;
        const auto area = parameterCell (laneIndex_, step);
        const auto fraction = juce::jlimit (0.0, 1.0, static_cast<double> (area.getBottom() - 1 - at.y) / (area.getHeight() - 1));
        const double value = kind_ == 0 ? 1.0 + fraction * 126.0 : kind_ == 1 ? 1.0 + fraction * 99.0
                          : kind_ == 2 ? 1.0 + fraction * 7.0 : -50.0 + fraction * 100.0;
        owner_.selectCell (pad_, step);
        const auto key = "lane." + juce::String (kind_) + ":" + juce::String (pad_) + ":" + juce::String (bar_);
        owner_.edit (key, [kind = kind_, pad = pad_, bar = bar_, step, value] (StoredPattern& pattern) {
            setStepParameter (kind, stepCell (pattern, pad, bar, step), value, stepCellTicks (pattern, pad, bar, step));
        });
        expectedGeneration_ = owner_.processor_.patterns().generation();
    }
    PatternEditor& owner_;
    bool editingLane_ = false;
    unsigned laneIndex_ = 0;
    int pad_ = 0, bar_ = 0, slot_ = 0, song_ = -1, kind_ = 0;
    std::uint64_t epoch_ = 0, expectedGeneration_ = 0;
    timing::BarTiming timing_;
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
    steps_->setTooltip ("Resolution: 1/16, 1/32 or 1/64 notes. Every note keeps its time; a change that cannot keep them is refused");
    steps_->onChange = [this] {
        if (refreshing_)
            return;
        const auto resolution = stepResolutions[juce::jlimit (0, 2, steps_->getSelectedItemIndex())];
        // PR 137 §10.3: lossless or refused (never resampled).
        auto trial = std::make_unique<StoredPattern> (shown_);
        std::string why;
        if (! trial->convertGrid (trial->signature, resolution, &why))
        {
            exportMessage_ = "Resolution not changed: " + juce::String (why);
            refreshControls();
            return;
        }
        exportMessage_.clear();
        edit ("steps", [resolution] (StoredPattern& p) { p.convertGrid (p.signature, resolution); });
    };
    barCount_ = std::make_unique<vox::ui::VoxComboBox>();
    barCount_->addItemList ({ "1 bar", "2 bars", "3 bars", "4 bars" }, 1);
    barCount_->setTooltip ("Pattern length; added bars repeat bar 1");
    barCount_->onChange = [this] {
        if (refreshing_)
            return;
        const int bars = barCount_->getSelectedItemIndex() + 1;
        edit ("bars", [bars] (StoredPattern& p) { p.setBars (bars); });
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
            {
                p.mask (lane, to) = p.mask (lane, from);
                for (int s = 0; s < kit::slotStride; ++s)
                    p.cell (lane, to, s) = p.cell (lane, from, s);
            }
        });
        selectBar ((from + 1) % shown_.bars);
    };
    button (clear_, "Clear", "Clear this bar");
    clear_->onClick = [this] {
        const int at = bar_;
        edit ("clear", [at] (StoredPattern& p) {
            for (int lane = 0; lane < padCount; ++lane)
                for (int s = 0; s < kit::slotStride; ++s)
                    p.cell (lane, at, s) = {};
        });
    };
    button (swap_, "A/B", "Swap this pattern with its variation B");
    swap_->onClick = [this] {
        editBoth ([] (StoredPattern& a, StoredPattern& b) {
            // Member-wise: std::swap would put a ~33 KB temporary on the stack.
            a.cells.swap (b.cells);
            a.masks.swap (b.masks);
            std::swap (a.signature, b.signature);
            std::swap (a.resolution, b.resolution);
            std::swap (a.followHost, b.followHost);
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
    midi_ = std::make_unique<MidiDragButton> ("MIDI", vox::ui::VoxButton::Type::Primary);
    midi_->setTooltip ("Click: save the pattern as a MIDI file. Drag: drop it into your DAW.");
    addAndMakeVisible (*midi_);
    midi_->onClick = [this] { openSaveDialog(); };
    // A drag that starts on the MIDI button exports the pattern to a file
    // and drops it (into the DAW's arrangement, a folder, ...).
    midi_->onDragStart = [this] { startDrag(); };
    abLabel_ = std::make_unique<juce::Label> ("ab", "");
    abLabel_->setFont (vox::ui::typography::valueText());
    abLabel_->setColour (juce::Label::textColourId, c::textMuted);
    addAndMakeVisible (*abLabel_);

    grid_ = std::make_unique<Grid> (*this);
    addAndMakeVisible (*grid_);
    addChildComponent (gridViewport_);
    gridViewport_.setScrollBarsShown (false, true);
    button (parameterLanesButton_, "Lanes", "Show graphical Velocity, Probability, Ratchet or Microtiming lanes");
    parameterLanesButton_->setVisible (false);
    parameterLanesButton_->onClick = [this] { showParameterLanesMenu(); };

    const auto knob = [this] (std::unique_ptr<vox::ui::VoxKnob>& k, const juce::String& label, double lo, double hi,
                              std::function<juce::String (double)> text, std::function<void (kit::PatternStep&, double, int)> set) {
        k = std::make_unique<DrumsDial> (label, vox::ui::VoxKnob::Size::Normal);
        if (lo < 0.0 && hi > 0.0) k->setStyle (vox::ui::VoxKnob::Style::Bipolar);
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
                  [lane, bar, step, v, set] (StoredPattern& p) { set (stepCell (p, lane, bar, step), v, stepCellTicks (p, lane, bar, step)); });
        };
        addAndMakeVisible (*k);
    };
    knob (velocity_, "Velocity", 1.0, 127.0, [] (double v) { return juce::String (juce::roundToInt (v)); },
          [] (kit::PatternStep& c, double v, int ticks) { setStepParameter (0, c, v, ticks); });
    knob (probability_, "Chance", 1.0, 100.0, [] (double v) { return juce::String (juce::roundToInt (v)) + " %"; },
          [] (kit::PatternStep& c, double v, int ticks) { setStepParameter (1, c, v, ticks); });
    knob (ratchet_, "Ratchet", 1.0, 8.0, [] (double v) { return juce::String (juce::roundToInt (v)) + "x"; },
          [] (kit::PatternStep& c, double v, int ticks) { setStepParameter (2, c, v, ticks); });
    knob (micro_, "Micro", -50.0, 50.0, [] (double v) {
              const int n = juce::roundToInt (v);
              return (n > 0 ? "+" : "") + juce::String (n) + " %";
          },
          // Percent of the cell, quantised once to ticks (PR 137 §10.3).
          [] (kit::PatternStep& c, double v, int cellTicks) {
              setStepParameter (3, c, v, cellTicks);
          });
    micro_->setStyle (vox::ui::VoxKnob::Style::Bipolar);
    accentButton_ = std::make_unique<juce::ToggleButton> ("Accent");
    accentButton_->onClick = [this] {
        if (refreshing_)
            return;
        const int lane = lane_, bar = bar_, step = step_;
        const bool on = accentButton_->getToggleState();
        edit ("accent", [lane, bar, step, on] (StoredPattern& p) { stepCell (p, lane, bar, step).accent = on; });
    };
    addAndMakeVisible (*accentButton_);
    lockButton_ = std::make_unique<juce::ToggleButton> ("Lock");
    lockButton_->setTooltip ("A locked step is left as it is by the generator");
    lockButton_->onClick = [this] {
        if (refreshing_)
            return;
        const int lane = lane_, bar = bar_, step = step_;
        const bool on = lockButton_->getToggleState();
        edit ("lock", [lane, bar, step, on] (StoredPattern& p) { stepCell (p, lane, bar, step).locked = on; });
    };
    addAndMakeVisible (*lockButton_);

    epoch_ = processor_.patternEpoch();
    update (false);
}

PatternEditor::~PatternEditor() = default;

bool PatternEditor::setParameterLanes (std::vector<int> lanes, bool notify)
{
    if (lanes.size() > 2 || std::any_of (lanes.begin(), lanes.end(), [] (int kind) { return kind < 0 || kind > 3; })) return false;
    if (parameterLanesEnabled_ && parameterLanes_ == lanes) return true;
    grid_->cancelGesture();
    parameterLanesEnabled_ = true; parameterLanes_ = std::move (lanes);
    parameterLanesButton_->setVisible (true);
    resized(); repaint();
    if (notify && onParameterLanesChanged) onParameterLanesChanged();
    return true;
}

juce::Rectangle<int> PatternEditor::parameterCellBounds (unsigned lane, int step) const
{
    return getLocalArea (grid_.get(), grid_->parameterCell (lane, step));
}

void PatternEditor::showParameterLanesMenu()
{
    juce::PopupMenu menu;
    menu.addItem (1, "Hide parameter lanes", true, parameterLanes_.empty());
    for (int row = 0; row < 2; ++row)
    {
        juce::PopupMenu kinds;
        for (int kind = 0; kind < 4; ++kind)
            kinds.addItem (10 + row * 10 + kind, parameterNames[kind], row == 0 || parameterLanes_.empty() || parameterLanes_[0] != kind,
                           row < static_cast<int> (parameterLanes_.size()) && parameterLanes_[static_cast<std::size_t> (row)] == kind);
        menu.addSubMenu (row == 0 ? "First lane" : "Second lane", kinds, row == 0 || ! parameterLanes_.empty());
    }
    if (parameterLanes_.size() == 2) menu.addItem (2, "Hide second lane");
    const auto weak = juce::Component::SafePointer<PatternEditor> (this);
    const auto epoch = processor_.patternEpoch(), generation = processor_.layoutState().generation;
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (parameterLanesButton_.get()),
        [weak, epoch, generation] (int id) {
            if (! weak || id == 0 || epoch != weak->processor_.patternEpoch() || generation != weak->processor_.layoutState().generation) return;
            auto lanes = weak->parameterLanes_;
            if (id == 1) lanes.clear();
            else if (id == 2 && lanes.size() == 2) lanes.pop_back();
            else if (id >= 10 && id <= 13) { if (lanes.empty()) lanes.push_back (id - 10); else lanes[0] = id - 10; }
            else if (id >= 20 && id <= 23 && ! lanes.empty()) { if (lanes.size() == 1) lanes.push_back (id - 20); else lanes[1] = id - 20; }
            else return;
            weak->setParameterLanes (std::move (lanes));
        });
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
        e->after.setBars (1);
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
        step_ = juce::jlimit (0, cellCount (shown_.timing (lane_, bar_)) - 1, step_);
        changed = true;
    }
    // The playhead: the tick in the bar, on the bar lines the sequencer used
    // in the last block (the host's, or counted from the start).
    const auto tick = static_cast<long long> (std::floor (processor_.lastPpq() * timing::ticksPerQuarter));
    const long long inBar = tick - processor_.lastBarStartTick();
    const long long barIndex = processor_.lastBarIndex();
    // A song bar shows the playhead only while that bar plays.
    const bool here = songBar_ < 0 || processor_.songBarPlaying() == songBar_;
    const bool inside = tick >= 0 && barIndex >= 0 && inBar >= 0 && inBar < processor_.lastBarTicks();
    const int play = processor_.transportPlaying() && inside && here ? static_cast<int> (inBar) : -1;
    const int playBar = inside && here ? static_cast<int> (barIndex % shown_.bars) : -1;
    if (play != playStep_ || playBar != playBar_)
    {
        playStep_ = play;
        playBar_ = playBar;
        changed = true;
    }
    if (changed)
    {
        if (getWidth() > 0) resized();
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
    steps_->setSelectedItemIndex (shown_.resolution == timing::Resolution::sixtyFourth ? 2
                                  : shown_.resolution == timing::Resolution::thirtySecond ? 1 : 0,
                                  juce::dontSendNotification);
    barCount_->setSelectedItemIndex (shown_.bars - 1, juce::dontSendNotification);
    const auto& c = stepCell (shown_, lane_, bar_, step_);
    const int cellTicks = juce::jmax (1, stepCellTicks (shown_, lane_, bar_, step_));
    velocity_->getSlider().setValue (c.on ? c.velocity : 100, juce::dontSendNotification);
    probability_->getSlider().setValue (c.probability, juce::dontSendNotification);
    ratchet_->getSlider().setValue (c.ratchet, juce::dontSendNotification);
    micro_->getSlider().setValue (std::round (100.0 * c.microTicks / cellTicks), juce::dontSendNotification);
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
    if (getWidth() > 0) resized();
    repaint();
    if (onSelectionChanged) onSelectionChanged();
}

void PatternEditor::selectCell (int lane, int step)
{
    lane_ = juce::jlimit (0, padCount - 1, lane);
    lanePage_ = lane_ / (padCount / 2);   // a selected lane is always shown
    step_ = juce::jlimit (0, cellCount (shown_.timing (lane_, bar_)) - 1, step);
    refreshControls();
    repaint();
    if (onSelectionChanged) onSelectionChanged();
}

void PatternEditor::clickCell (int lane, int step)
{
    selectCell (lane, step);
    const int l = lane_, s = step_, b = bar_;
    edit ("toggle", [l, s, b] (StoredPattern& p) {
        auto& c = stepCell (p, l, b, s);
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
    return getLocalArea (grid_.get(), grid_->cell (lane, step));
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
    if (name == "parameters") return parameterLanesButton_.get();
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
    if (! fixedPatternExport_ && processor_.uiState().exportScope == 1)
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

juce::File PatternEditor::newDragFile (const juce::String& name)
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
    return folder.getChildFile (name);
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
    return processor_.captureExport (! fixedPatternExport_ && processor_.uiState().exportScope == 1
                                        ? ExportSnapshot::Scope::song : ExportSnapshot::Scope::pattern);
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
    return exportForDrag (snapshot());
}

namespace {
juce::String exportName (const ExportSnapshot& frozen)
{
    return frozen.scope == ExportSnapshot::Scope::song ? "VOX Drums Song.mid"
        : "VOX Drums Pattern " + juce::String (frozen.slot + 1) + ".mid";
}
}

juce::File PatternEditor::exportForDrag (std::shared_ptr<const ExportSnapshot> frozen)
{
    if (! frozen) return {};
    const auto file = newDragFile (exportName (*frozen));
    const bool ok = writeSnapshot (*frozen, file);
    exported (ok, file);
    if (! ok) return {};
    return file;
}

void PatternEditor::openSaveDialog()
{
    saveExport (snapshot());
}

void PatternEditor::saveExport (std::shared_ptr<const ExportSnapshot> frozen)
{
    // Frozen at the click: what is saved is what was there when the
    // button was pressed, whatever changes while the dialog is open.
    if (! frozen) return;
    if (saveDialogForTesting)
    {
        saveDialogForTesting (frozen);
        return;
    }
    const auto suggested = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (exportName (*frozen));
    chooser_ = std::make_unique<juce::FileChooser> ("Save as MIDI", suggested, "*.mid");
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
}

void PatternEditor::startDrag()
{
    dragExport (snapshot(), *this);
}

void PatternEditor::dragExport (std::shared_ptr<const ExportSnapshot> frozen, juce::Component& source)
{
    // A failed export is shown (exportMessage) and starts no drag.
    const auto file = exportForDrag (std::move (frozen));
    if (file == juce::File())
        return;
    if (externalDragForTesting)
    {
        externalDragForTesting (file);
        return;
    }
    // The OS drag (JUCE): on Windows an OLE DoDragDrop with a copy of the
    // file (CF_HDROP). Not changed by issue 131; see the audit there.
    if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (&source))
        container->performExternalDragDropOfFiles ({ file.getFullPathName() }, false, &source);
}

//==============================================================================
void MidiDragButton::mouseDown (const juce::MouseEvent& e)
{
    // A new gesture: whatever the last one left (a drag whose mouse-up the
    // OS drag took, a failed export) ends here.
    pressed_ = true;
    dragStarted_ = false;
    VoxButton::mouseDown (e);   // the pressed look; no click on mouse-down
}

void MidiDragButton::mouseDrag (const juce::MouseEvent& e)
{
    if (! pressed_ || dragStarted_)
        return;   // a started drag is never re-armed into a click
    if (e.getDistanceFromDragStart() > dragThreshold)
    {
        dragStarted_ = true;
        setState (juce::Button::buttonNormal);
        if (onDragStart)
            onDragStart();   // may run the OS drag; this gesture never clicks
        return;
    }
    VoxButton::mouseDrag (e);
}

void MidiDragButton::mouseUp (const juce::MouseEvent& e)
{
    const bool inside = getLocalBounds().contains (e.getPosition());
    const bool click = pressed_ && ! dragStarted_ && inside && isEnabled();
    pressed_ = false;
    dragStarted_ = false;
    // Button::mouseUp is not called: it would click from the hover state.
    setState (inside ? juce::Button::buttonOver : juce::Button::buttonNormal);
    if (click && onClick)
        onClick();
}

void PatternEditor::paint (juce::Graphics&) {}

void PatternEditor::resized()
{
    if (getWidth() <= 0 || getHeight() <= 0) return;
    auto area = getLocalBounds();
    if (getWidth() < 900)
    {
        int x = 0, y = 0;
        const auto put = [&] (juce::Component& component, int width) {
            if (x > 0 && x + width > getWidth()) { x = 0; y += 28; }
            component.setBounds (juce::Rectangle<int> (x, y, width, 28).reduced (1)); x += width;
        };
        for (auto& bar : bars_) put (*bar, 28);
        put (*steps_, 100); put (*barCount_, 84);
        for (auto* button : { dup_.get(), clear_.get(), seed_.get() }) put (*button, 60);
        if (parameterLanesEnabled_) put (*parameterLanesButton_, 70);
        put (*lanes_, 70); put (*abLabel_, 120);
        area.removeFromTop (y + 28 + sp::xs);
        auto bottom = area.removeFromBottom (118);
        auto actions = bottom.removeFromBottom (30);
        const int knobWidth = juce::jlimit (52, 66, (bottom.getWidth() - 84) / 4);
        for (auto* knob : { velocity_.get(), probability_.get(), ratchet_.get(), micro_.get() })
            knob->setBounds (bottom.removeFromLeft (knobWidth));
        accentButton_->setBounds (bottom.removeFromTop (32).reduced (2));
        lockButton_->setBounds (bottom.removeFromTop (32).reduced (2));
        for (auto* button : { swap_.get(), copyB_.get(), undo_.get(), redo_.get() })
            button->setBounds (actions.removeFromLeft (56).reduced (1));
        midi_->setBounds (actions.removeFromLeft (70).reduced (1));
        area.removeFromBottom (sp::xs); layoutGrid (area); refreshControls();
        return;
    }
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
    if (parameterLanesEnabled_) parameterLanesButton_->setBounds (top.removeFromLeft (70).reduced (1));
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
    layoutGrid (area);
    refreshControls();   // the lane page button depends on the grid height
}

void PatternEditor::layoutGrid (juce::Rectangle<int> area)
{
    if (getWidth() >= 900 && ! parameterLanesEnabled_)
    {
        gridViewport_.setViewedComponent (nullptr, false); gridViewport_.setVisible (false);
        addAndMakeVisible (*grid_); grid_->setBounds (area); return;
    }
    if (gridViewport_.getViewedComponent() != grid_.get()) gridViewport_.setViewedComponent (grid_.get(), false);
    gridViewport_.setScrollBarsShown (parameterLanesEnabled_, true);
    gridViewport_.setVisible (true); gridViewport_.setBounds (area);
    int cells = 1;
    for (int lane = 0; lane < padCount; ++lane) cells = std::max (cells, cellCount (shown_.timing (lane, bar_)));
    const int minimumHeight = 88 + (parameterLanesEnabled_ ? 24 : 0) + static_cast<int> (parameterLanes_.size()) * 60;
    const int barWidth = parameterLanesEnabled_ ? gridViewport_.getScrollBarThickness() : 0;
    const int width = std::max (area.getWidth() - barWidth, nameWidth + cells * 10);
    grid_->setSize (width, std::max (minimumHeight, area.getHeight() - (width > area.getWidth() - barWidth ? gridViewport_.getScrollBarThickness() : 0)));
}

} // namespace vox::drums
