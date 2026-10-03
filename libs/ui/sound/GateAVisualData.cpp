#include "GateAVisualData.h"

#include <cmath>

#include "SoundGraphs.h"

namespace vstengine::ui {

namespace {

constexpr std::array<const char*, 5> matrixHeaders { "#", "SOURCE", "AMOUNT", "DESTINATION", "" };

const char* laneName (const PreviewSequencerLane lane)
{
    switch (lane) {
        case PreviewSequencerLane::note:   return "NOTE";
        case PreviewSequencerLane::accent: return "ACCENT";
        case PreviewSequencerLane::slide:  return "SLIDE";
        case PreviewSequencerLane::gate:   return "GATE";
        case PreviewSequencerLane::octave: return "OCTAVE";
        case PreviewSequencerLane::count:  break;
    }
    return "";
}

constexpr std::array<const char*, 12> sequencerToolbar {
    "PLAY", "STOP", "16 STEPS", "GENERATE", "MUTATE", "CLEAR",
    "PATTERN", "SWING", "RESOLUTION", "DENSITY", "VARIATION", "LENGTH"
};

} // namespace

// --- GateAVisualMatrix -----------------------------------------------------

GateAVisualMatrix::GateAVisualMatrix()
    : routes (previewDefaults())
{
    setInterceptsMouseClicks (false, false);
}

std::vector<PreviewModRoute> GateAVisualMatrix::previewDefaults()
{
    return {
        { "MOD WHEEL",  "FILTER CUTOFF", 0.42f, true  },
        { "AFTERTOUCH", "DRIVE AMOUNT",  0.28f, true  },
        { "LFO 1",      "PITCH",         0.15f, true  },
        { "ENV 2",      "RESONANCE",     0.61f, true  },
        { "VELOCITY",   "AMP ENV",       0.33f, false }
    };
}

void GateAVisualMatrix::setAccent (juce::Colour newAccent)
{
    accent = newAccent;
    repaint();
}

void GateAVisualMatrix::setRoutes (std::vector<PreviewModRoute> newRoutes)
{
    routes = std::move (newRoutes);
    repaint();
}

void GateAVisualMatrix::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    if (area.getWidth() < 40.0f || area.getHeight() < 30.0f)
        return;

    SoundGraph::paintSurface (g, area);

    const auto headerHeight = juce::jmin (13.0f, area.getHeight() * 0.16f);
    auto header = area.withHeight (headerHeight);
    auto table = area.withTrimmedTop (headerHeight);

    // Column proportions: number, source, amount, destination.
    const auto numberW = juce::jmin (20.0f, table.getWidth() * 0.08f);
    const auto amountW = juce::jmin (46.0f, table.getWidth() * 0.20f);
    const auto sourceW = table.getWidth() * 0.34f;

    g.setColour (colours::mutedText);
    g.setFont (7.5f);
    g.drawText (matrixHeaders[0], header.removeFromLeft (numberW),
                juce::Justification::centredLeft, true);
    g.drawText (matrixHeaders[1], header.removeFromLeft (sourceW),
                juce::Justification::centredLeft, true);
    g.drawText (matrixHeaders[2], header.removeFromLeft (amountW),
                juce::Justification::centredLeft, true);
    g.drawText (matrixHeaders[3], header, juce::Justification::centredLeft, true);

    const auto rowCount = juce::jmax (1, static_cast<int> (routes.size()));
    const auto rowHeight = table.getHeight() / static_cast<float> (rowCount);

    for (std::size_t row = 0; row < routes.size(); ++row) {
        const auto& route = routes[row];
        const auto y = table.getY() + rowHeight * static_cast<float> (row);
        auto rowArea = juce::Rectangle<float> (table.getX(), y, table.getWidth(), rowHeight);

        // Active routes read stronger than unused rows, but colour is never the
        // only signal: unused rows are also dimmed in text.
        const auto strength = route.enabled ? 1.0f : 0.55f;

        g.setColour (colours::borderSubtle.withAlpha (0.6f));
        g.drawHorizontalLine (static_cast<int> (rowArea.getBottom()),
                              table.getX(), table.getRight());

        if (rowArea.getHeight() < 9.0f)
            continue;

        g.setFont (8.5f);

        g.setColour (colours::mutedText.withAlpha (strength));
        g.drawText (juce::String (static_cast<int> (row + 1)),
                    rowArea.removeFromLeft (numberW),
                    juce::Justification::centredLeft, true);

        g.setColour (colours::textSecondary.withAlpha (strength));
        g.drawText (route.source, rowArea.removeFromLeft (sourceW),
                    juce::Justification::centredLeft, true);

        auto amountArea = rowArea.removeFromLeft (amountW);
        const auto magnitude = juce::jlimit (0.0f, 1.0f, std::abs (route.amount));
        g.setColour (route.enabled ? accent.withAlpha (0.92f)
                                   : colours::mutedText.withAlpha (0.7f));
        const auto percent = static_cast<int> (std::lround (route.amount * 100.0f));
        g.drawText ((percent >= 0 ? "+" : "") + juce::String (percent),
                    amountArea.removeFromLeft (amountArea.getWidth() * 0.42f),
                    juce::Justification::centredLeft, true);

        // Bipolar amount field: bar grows from the centre.
        auto barArea = amountArea.reduced (1.0f, rowArea.getHeight() * 0.30f);
        if (barArea.getWidth() > 6.0f) {
            const auto centreX = barArea.getCentreX();
            g.setColour (colours::control);
            g.fillRoundedRectangle (barArea, 1.5f);
            const auto filled = barArea.withWidth (barArea.getWidth() * 0.5f * magnitude);
            g.setColour (accent.withAlpha (0.72f));
            if (route.amount >= 0.0f)
                g.fillRoundedRectangle (filled.withX (centreX), 1.5f);
            else
                g.fillRoundedRectangle (filled, 1.5f);
        }

        g.setColour (colours::text.withAlpha (strength));
        g.drawText (route.destination, rowArea, juce::Justification::centredLeft, true);
    }
}

// --- GateAVisualSequencer ---------------------------------------------------

GateAVisualSequencer::GateAVisualSequencer()
    : pattern (previewPattern())
{
    setInterceptsMouseClicks (false, false);
}

std::vector<PreviewSequencerStep> GateAVisualSequencer::previewPattern()
{
    std::vector<PreviewSequencerStep> steps (static_cast<std::size_t> (stepCount));

    static constexpr std::array<const char*, 4> notes { "C3", "C3", "Eb3", "G3" };
    for (int s = 0; s < stepCount; ++s) {
        auto& step = steps[static_cast<std::size_t> (s)];
        const auto t = static_cast<float> (s);
        const auto index = static_cast<std::size_t> ((s * 5 + 2) % 4);
        step.noteOn = ((s * 7 + 3) % 11) < 8;
        step.noteName = notes[index];
        step.accent = (s % 8) == 0;
        step.slide = (s % 8) == 6;
        step.octaveOffset = ((s * 3) % 7) == 0 ? 1 : 0;
        step.gate = 0.30f + 0.62f * std::abs (std::sin (t * 0.9f));
    }
    return steps;
}

void GateAVisualSequencer::setAccent (juce::Colour newAccent)
{
    accent = newAccent;
    repaint();
}

void GateAVisualSequencer::setPattern (std::vector<PreviewSequencerStep> newPattern)
{
    pattern = std::move (newPattern);
    repaint();
}

void GateAVisualSequencer::paintToolbar (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto itemWidth = area.getWidth()
                             / static_cast<float> (sequencerToolbar.size());

    for (std::size_t i = 0; i < sequencerToolbar.size(); ++i) {
        auto cell = juce::Rectangle<float> (area.getX() + itemWidth * static_cast<float> (i),
                                            area.getY(), itemWidth - 2.0f, area.getHeight());

        // Play/Stop read as actions; the rest read as selectors/density tools.
        const auto isAction = i == 0 || i == 1;
        g.setColour (isAction ? accent.withAlpha (0.16f) : colours::control);
        g.fillRoundedRectangle (cell, 2.5f);
        g.setColour (isAction ? accent.withAlpha (0.62f) : colours::borderSubtle);
        g.drawRoundedRectangle (cell, 2.5f, 1.0f);

        g.setColour (isAction ? accent.withAlpha (0.95f) : colours::textSecondary);
        g.setFont (7.5f);
        g.drawText (sequencerToolbar[i], cell.reduced (3.0f, 0.0f),
                    juce::Justification::centred, true);
    }
}

void GateAVisualSequencer::paintGrid (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto laneCount = static_cast<int> (PreviewSequencerLane::count);

    const auto labelWidth = juce::jmin (54.0f, area.getWidth() * 0.09f);
    const auto headerHeight = juce::jmin (15.0f, area.getHeight() * 0.13f);
    auto steps = area.withTrimmedLeft (labelWidth);
    auto stepHeader = steps.removeFromTop (headerHeight);
    auto lanes = steps;

    // Step numbers.
    const auto cellWidth = lanes.getWidth() / static_cast<float> (stepCount);
    for (int s = 0; s < stepCount; ++s) {
        g.setColour (s % 4 == 0 ? accent.withAlpha (0.78f) : colours::mutedText);
        g.setFont (7.5f);
        g.drawText (juce::String (s + 1),
                    juce::Rectangle<float> (stepHeader.getX() + cellWidth * static_cast<float> (s),
                                            stepHeader.getY(), cellWidth,
                                            stepHeader.getHeight()),
                    juce::Justification::centred, true);
    }

    const auto rowHeight = lanes.getHeight() / static_cast<float> (laneCount);

    // Lane labels and separators.
    for (int r = 0; r < laneCount; ++r) {
        const auto y = lanes.getY() + rowHeight * static_cast<float> (r);
        g.setColour (colours::mutedText);
        g.setFont (7.8f);
        g.drawText (laneName (static_cast<PreviewSequencerLane> (r)),
                    juce::Rectangle<float> (area.getX(), y, labelWidth - 5.0f, rowHeight),
                    juce::Justification::centredLeft, true);
        g.setColour (colours::border.withAlpha (0.55f));
        g.drawHorizontalLine (static_cast<int> (y + rowHeight), lanes.getX(), lanes.getRight());
    }

    // Beat-grouped vertical lines keep 16 steps readable.
    for (int s = 0; s <= stepCount; ++s) {
        const auto x = lanes.getX() + cellWidth * static_cast<float> (s);
        g.setColour (colours::border.withAlpha (s % 4 == 0 ? 0.85f : 0.34f));
        g.drawVerticalLine (static_cast<int> (x), lanes.getY(), lanes.getBottom());
    }

    // Lane cells.
    for (int s = 0; s < stepCount && s < static_cast<int> (pattern.size()); ++s) {
        const auto& step = pattern[static_cast<std::size_t> (s)];
        const auto x = lanes.getX() + cellWidth * static_cast<float> (s);
        const auto cell = juce::Rectangle<float> (x + 2.0f, lanes.getY() + 2.0f,
                                                   cellWidth - 4.0f, rowHeight - 4.0f);

        auto laneRow = [&](const int lane) {
            return juce::Rectangle<float> (cell.getX(),
                                           cell.getY() + rowHeight * static_cast<float> (lane),
                                           cell.getWidth(),
                                           juce::jmax (3.0f, rowHeight - 3.0f));
        };

        if (step.noteOn) {
            auto noteCell = laneRow (0);
            g.setColour (accent.withAlpha (0.76f));
            g.fillRoundedRectangle (noteCell, 2.0f);
            g.setColour (colours::graphSurface.withAlpha (0.92f));
            g.setFont (7.5f);
            g.drawText (step.noteName, noteCell, juce::Justification::centred, true);
        }

        if (step.accent) {
            auto accentCell = laneRow (1).reduced (cell.getWidth() * 0.18f,
                                                   cell.getHeight() * 0.18f);
            g.setColour (accent.withAlpha (0.95f));
            g.fillRoundedRectangle (accentCell, 1.5f);
        }

        if (step.slide) {
            auto slideCell = laneRow (2).reduced (cell.getWidth() * 0.14f,
                                                  cell.getHeight() * 0.24f);
            g.setColour (accent.withAlpha (0.70f));
            g.drawRoundedRectangle (slideCell, 2.0f, 1.2f);
        }

        {
            auto gateRow = laneRow (3);
            const auto magnitude = juce::jlimit (0.08f, 1.0f, step.gate);
            auto gateBar = gateRow.withHeight (gateRow.getHeight() * magnitude);
            g.setColour (accent.withAlpha (0.55f));
            g.fillRoundedRectangle (gateBar.withX (gateRow.getCentreX()
                                                    - juce::jmax (1.0f,
                                                                  cell.getWidth() * 0.12f)),
                                    1.0f);
        }

        if (step.octaveOffset != 0) {
            auto octaveCell = laneRow (4);
            g.setColour (colours::textSecondary.withAlpha (0.9f));
            g.setFont (7.5f);
            g.drawText (step.octaveOffset > 0 ? "+1" : "-1", octaveCell,
                        juce::Justification::centred, true);
        }
    }
}

void GateAVisualSequencer::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    if (area.getWidth() < 80.0f || area.getHeight() < 40.0f)
        return;

    SoundGraph::paintSurface (g, area);

    const auto toolbarHeight = juce::jmin (20.0f, area.getHeight() * 0.16f);
    paintToolbar (g, area.removeFromTop (toolbarHeight));
    area.removeFromTop (3.0f);

    paintGrid (g, area);
}

// --- GateAVisualOptionList --------------------------------------------------

GateAVisualOptionList::GateAVisualOptionList()
    : options { "LEGATO", "RETRIGGER", "VEL > ACCENT", "SLIDE ON OVERLAP" }
{
    setInterceptsMouseClicks (false, false);
}

void GateAVisualOptionList::setOptions (std::vector<juce::String> newOptions)
{
    options = std::move (newOptions);
    selectedIndex = 0;
    repaint();
}

void GateAVisualOptionList::setSelectedIndex (const int index)
{
    if (selectedIndex == index)
        return;
    selectedIndex = index;
    repaint();
}

void GateAVisualOptionList::setAccent (juce::Colour newAccent)
{
    accent = newAccent;
    repaint();
}

void GateAVisualOptionList::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    if (options.empty() || area.getWidth() < 24.0f || area.getHeight() < 12.0f)
        return;

    // Header strip so the list reads as a labelled selector group.
    const auto headerHeight = juce::jmin (12.0f, area.getHeight() * 0.22f);
    g.setColour (colours::mutedText);
    g.setFont (7.5f);
    g.drawText ("MODE", area.removeFromTop (headerHeight),
                juce::Justification::centredLeft, true);

    const auto rowHeight = area.getHeight() / static_cast<float> (options.size());
    for (std::size_t i = 0; i < options.size(); ++i) {
        const auto y = area.getY() + rowHeight * static_cast<float> (i);
        const auto row = juce::Rectangle<float> (area.getX(), y, area.getWidth(), rowHeight);
        const auto selected = static_cast<int> (i) == selectedIndex;

        // Radio marker plus text. Selection is conveyed by the marker and by
        // text weight, never by colour alone.
        const auto marker = juce::Rectangle<float> (row.getX() + 1.0f,
                                                    row.getCentreY() - 3.0f, 6.0f, 6.0f);
        if (selected) {
            g.setColour (accent.withAlpha (0.92f));
            g.fillEllipse (marker);
            g.setColour (colours::graphSurface);
            g.fillEllipse (marker.reduced (2.2f, 2.2f));
        } else {
            g.setColour (colours::border);
            g.drawEllipse (marker, 1.0f);
        }

        g.setColour (selected ? colours::text : colours::textSecondary.withAlpha (0.85f));
        g.setFont (selected ? juce::Font (8.0f, juce::Font::bold) : juce::Font (8.0f));
        g.drawText (options[i], row.withTrimmedLeft (11).reduced (1.0f, 0.0f),
                    juce::Justification::centredLeft, true);
    }
}

// --- SoundTabStrip ----------------------------------------------------------

SoundTabStrip::SoundTabStrip()
    : tabs { "ENV 1", "ENV 2", "LFO 1", "LFO 2", "LFO 3", "STEP" }
{
    setInterceptsMouseClicks (false, false);
}

void SoundTabStrip::setTabs (std::vector<juce::String> newTabs)
{
    tabs = std::move (newTabs);
    selectedIndex = 0;
    repaint();
}

void SoundTabStrip::setSelectedIndex (const int index)
{
    if (selectedIndex == index)
        return;
    selectedIndex = index;
    repaint();
}

void SoundTabStrip::setAccent (juce::Colour newAccent)
{
    accent = newAccent;
    repaint();
}

void SoundTabStrip::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    if (tabs.empty() || area.getWidth() < 30.0f)
        return;

    const auto tabWidth = area.getWidth() / static_cast<float> (tabs.size());

    for (std::size_t i = 0; i < tabs.size(); ++i) {
        auto cell = juce::Rectangle<float> (area.getX() + tabWidth * static_cast<float> (i),
                                            area.getY(), tabWidth - 2.0f, area.getHeight());
        const auto selected = static_cast<int> (i) == selectedIndex;

        g.setColour (selected ? accent.withAlpha (0.20f) : colours::control);
        g.fillRoundedRectangle (cell, 2.5f);
        g.setColour (selected ? accent.withAlpha (0.75f) : colours::borderSubtle);
        g.drawRoundedRectangle (cell, 2.5f, selected ? 1.2f : 1.0f);

        g.setColour (selected ? accent.withAlpha (0.95f) : colours::textSecondary);
        g.setFont (7.8f);
        g.drawText (tabs[i], cell.reduced (2.0f, 0.0f), juce::Justification::centred, true);
    }
}

} // namespace vstengine::ui