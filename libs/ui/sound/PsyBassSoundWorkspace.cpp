#include "PsyBassSoundWorkspace.h"

#include <array>

namespace vstengine::ui {

namespace {

constexpr int panelGap = 8;
constexpr int tabStripHeight = 15;

// Bridges the typed control arrays to the generic row/grid layout helpers.
template <std::size_t N>
std::array<juce::Component*, N> asComponents (const std::array<SoundParameterKnob*, N>& knobs)
{
    std::array<juce::Component*, N> out {};
    for (std::size_t i = 0; i < N; ++i)
        out[i] = knobs[i];
    return out;
}

// Three-column split with explicit proportions (gaps removed from the row).
std::array<juce::Rectangle<int>, 3> threeColumn (juce::Rectangle<int> row,
                                                const float first, const float second)
{
    const auto usable = row.getWidth() - panelGap * 2;
    auto rest = row;
    auto a = rest.removeFromLeft (juce::jmax (1, static_cast<int> (usable * first)));
    rest.removeFromLeft (panelGap);
    auto b = rest.removeFromLeft (juce::jmax (1, static_cast<int> (usable * second)));
    rest.removeFromLeft (panelGap);
    return { a, b, rest };
}

} // namespace

PsyBassSoundWorkspace::PsyBassSoundWorkspace()
{
    setIdentity (colours::primary, HeroIdentity::psyBass);
    buildComposition();
}

void PsyBassSoundWorkspace::buildComposition()
{
    // --- Row 1 -------------------------------------------------------------
    oscillatorPanel = &addPanel ("OSCILLATOR");
    filterPanel = &addPanel ("FILTER");
    envelopePanel = &addPanel ("AMP ENVELOPE");

    // The reference render shows a saw oscillator, not a generic sine.
    oscillatorGraph = std::make_unique<SoundGraph> (SoundGraphKind::oscillator);
    oscillatorGraph->setWaveShape (PreviewWaveShape::saw);
    oscillatorGraph->setVerticalWeight (0.45f);
    addAndMakeVisible (*oscillatorGraph);

    filterGraph = std::make_unique<SoundGraph> (SoundGraphKind::filter);
    filterGraph->setVerticalWeight (0.52f);
    addAndMakeVisible (*filterGraph);

    envelopeGraph = std::make_unique<SoundGraph> (SoundGraphKind::envelope);
    envelopeGraph->setVerticalWeight (0.48f);
    addAndMakeVisible (*envelopeGraph);

    // --- Row 2 -------------------------------------------------------------
    characterPanel = &addPanel ("DRIVE / CHARACTER");
    accentPanel = &addPanel ("ACCENT");
    performancePanel = &addPanel ("PERFORMANCE");

    // --- Row 3 -------------------------------------------------------------
    modulationPanel = &addPanel ("MODULATION");
    matrixPanel = &addPanel ("MOD MATRIX");

    modulationTabs = std::make_unique<SoundTabStrip>();
    modulationTabs->setAccent (identityAccent);
    addAndMakeVisible (*modulationTabs);

    modulationGraph = std::make_unique<SoundGraph> (SoundGraphKind::modulation);
    modulationGraph->setVerticalWeight (1.0f);
    modulationGraph->setAccent (identityAccent);
    addAndMakeVisible (*modulationGraph);

    matrix = std::make_unique<GateAVisualMatrix>();
    matrix->setAccent (identityAccent);
    addAndMakeVisible (*matrix);
}

SoundParameterKnob& PsyBassSoundWorkspace::addControl (juce::String requestedId,
                                                       juce::String label,
                                                       juce::String previewValue,
                                                       const vox::ui::VoxKnob::Size size)
{
    controls.push_back (std::make_unique<SoundParameterKnob> (
        *state, std::move (requestedId), std::move (label), descriptor, slotIndex,
        size, std::move (previewValue)));
    auto& knob = *controls.back();
    knob.setAccent (identityAccent);
    addAndMakeVisible (knob);
    return knob;
}

void PsyBassSoundWorkspace::buildControls()
{
    // Destroy any previous controls first. This is what releases the APVTS
    // SliderAttachments created for the previously selected slot.
    controls.clear();
    oscillatorKnobs.fill (nullptr);
    filterKnobs.fill (nullptr);
    envelopeKnobs.fill (nullptr);
    characterKnobs.fill (nullptr);
    accentKnobs.fill (nullptr);
    performanceKnobs.fill (nullptr);
    modulationKnobs.fill (nullptr);

    // Real semantic matches only. Everything else stays preview-only because
    // the Psy Bass descriptor does not publish it.
    //
    //   cutoff    -> FILTER / Cutoff
    //   resonance -> FILTER / Resonance
    //   decay     -> AMP ENVELOPE / D
    //   drive     -> DRIVE / Drive
    //   accent    -> ACCENT / Amount
    //
    // `space` and `motion` are intentionally never requested.

    // Row 1 -----------------------------------------------------------------
    oscillatorKnobs[0] = &addControl ({},  "OCTAVE",   "+0",  vox::ui::VoxKnob::Size::Normal);
    oscillatorKnobs[1] = &addControl ({},    "SEMITONE", "+2",  vox::ui::VoxKnob::Size::Normal);
    oscillatorKnobs[2] = &addControl ({},   "BLEND",    "0.34", vox::ui::VoxKnob::Size::Normal);
    oscillatorKnobs[3] = &addControl ({},   "PHASE",    "0.58", vox::ui::VoxKnob::Size::Normal);

    filterKnobs[0] = &addControl ("cutoff",    "CUTOFF",    "412 Hz", vox::ui::VoxKnob::Size::Normal);
    filterKnobs[1] = &addControl ("resonance", "RESONANCE", "0.38",   vox::ui::VoxKnob::Size::Normal);
    filterKnobs[2] = &addControl ({},  "KEY TRACK", "0.62",   vox::ui::VoxKnob::Size::Normal);
    filterKnobs[3] = &addControl ({}, "ENV AMOUNT", "+0.41", vox::ui::VoxKnob::Size::Normal);

    envelopeKnobs[0] = &addControl ({},  "A", "2 ms",  vox::ui::VoxKnob::Size::Normal);
    envelopeKnobs[1] = &addControl ("decay",      "D", "180 ms", vox::ui::VoxKnob::Size::Normal);
    envelopeKnobs[2] = &addControl ({}, "S", "0.72",   vox::ui::VoxKnob::Size::Normal);
    envelopeKnobs[3] = &addControl ({}, "R", "240 ms", vox::ui::VoxKnob::Size::Normal);

    // Row 2 -----------------------------------------------------------------
    characterKnobs[0] = &addControl ("drive",      "DRIVE",    "0.44", vox::ui::VoxKnob::Size::Normal);
    characterKnobs[1] = &addControl ({},  "TONE",     "0.52", vox::ui::VoxKnob::Size::Normal);
    characterKnobs[2] = &addControl ({},  "PRESENCE", "0.61", vox::ui::VoxKnob::Size::Normal);
    characterKnobs[3] = &addControl ({},   "MIX",      "0.78", vox::ui::VoxKnob::Size::Normal);

    accentKnobs[0] = &addControl ("accent",       "AMOUNT",     "+0.55", vox::ui::VoxKnob::Size::Normal);
    accentKnobs[1] = &addControl ({}, "VEL>CUTOFF", "0.40",  vox::ui::VoxKnob::Size::Normal);
    accentKnobs[2] = &addControl ({}, "VEL>DRIVE",  "0.28",  vox::ui::VoxKnob::Size::Normal);
    accentKnobs[3] = &addControl ({},   "ACCENT TONE", "0.66", vox::ui::VoxKnob::Size::Normal);

    performanceKnobs[0] = &addControl ({}, "PORTAMENTO", "24 ms", vox::ui::VoxKnob::Size::Normal);
    performanceKnobs[1] = &addControl ({},     "UNISON",     "3",     vox::ui::VoxKnob::Size::Normal);
    performanceKnobs[2] = &addControl ({},     "DETUNE",     "11 ct", vox::ui::VoxKnob::Size::Normal);
    performanceKnobs[3] = &addControl ({},     "SPREAD",     "0.58",  vox::ui::VoxKnob::Size::Normal);

    // Row 3 -----------------------------------------------------------------
    modulationKnobs[0] = &addControl ({}, "AMOUNT",  "0.46", vox::ui::VoxKnob::Size::Small);
    modulationKnobs[1] = &addControl ({}, "ATTACK",  "14 ms", vox::ui::VoxKnob::Size::Small);
    modulationKnobs[2] = &addControl ({},  "DECAY",   "120 ms", vox::ui::VoxKnob::Size::Small);
    modulationKnobs[3] = &addControl ({}, "RELEASE", "180 ms", vox::ui::VoxKnob::Size::Small);
}

void PsyBassSoundWorkspace::bind (const SoundWorkspaceBinding& binding)
{
    state = &binding.state;
    slotIndex = binding.slotIndex;
    descriptor = binding.descriptor;
    setIdentity (colours::primary, HeroIdentity::psyBass);
    buildControls();

    // Header selectors are preview copy during Visual Gate A.
    if (filterPanel != nullptr)
        filterPanel->setHeaderSelector ("LOW-PASS 24");
    if (characterPanel != nullptr)
        characterPanel->setHeaderSelector ("ANALOG");
    if (performancePanel != nullptr)
        performancePanel->setHeaderSelector ("UNISON 3");
    if (modulationPanel != nullptr)
        modulationPanel->setHeaderSelector ("LFO 1 > CUTOFF");

    resized();
}

juce::StringList PsyBassSoundWorkspace::getGateAVisualOnlyLabels() const
{
    juce::StringList labels;
    for (const auto& control : controls)
        if (control != nullptr && ! control->isBoundToRealParameter())
            labels.add (control->getLabel());
    labels.add ("MOD MATRIX routes");
    labels.add ("MODULATION targets");
    return labels;
}

void PsyBassSoundWorkspace::resized()
{
    auto area = getLocalBounds();

    const int row1Height = juce::jlimit (128, 210, static_cast<int> (area.getHeight() * 0.40f));
    const int row2Height = juce::jlimit (88, 136, static_cast<int> (area.getHeight() * 0.26f));

    auto row1 = area.removeFromTop (juce::jmin (row1Height, area.getHeight()));
    area.removeFromTop (panelGap);
    auto row2 = area.removeFromTop (juce::jmin (row2Height, area.getHeight()));
    area.removeFromTop (panelGap);
    const auto row3 = area;

    // --- Row 1: OSCILLATOR / FILTER / AMP ENVELOPE -------------------------
    if (oscillatorPanel != nullptr && filterPanel != nullptr && envelopePanel != nullptr)
    {
        const auto cells = threeColumn (row1, 0.30f, 0.34f);
        oscillatorPanel->setBounds (cells[0]);
        filterPanel->setBounds (cells[1]);
        envelopePanel->setBounds (cells[2]);

        const auto layoutGraphOverRow =
            [] (SoundModulePanel& panel, SoundGraph& graph,
                const std::array<juce::Component*, 4>& row, const float weight)
        {
            auto content = panel.getContentBounds();
            const auto graphHeight = juce::jmax (20,
                static_cast<int> (content.getHeight() * weight));
            graph.setBounds (content.removeFromTop (graphHeight));
            const auto rowArea = content.reduced (0, 2);
            layoutControlRow (row.data(), static_cast<int> (row.size()), rowArea);
        };

        if (oscillatorGraph != nullptr)
            layoutGraphOverRow (*oscillatorPanel, *oscillatorGraph,
                               asComponents (oscillatorKnobs), 0.45f);
        if (filterGraph != nullptr)
            layoutGraphOverRow (*filterPanel, *filterGraph,
                               asComponents (filterKnobs), 0.52f);
        if (envelopeGraph != nullptr)
            layoutGraphOverRow (*envelopePanel, *envelopeGraph,
                               asComponents (envelopeKnobs), 0.48f);
    }

    // --- Row 2: DRIVE / ACCENT / PERFORMANCE -------------------------------
    if (characterPanel != nullptr && accentPanel != nullptr && performancePanel != nullptr)
    {
        const auto cells = threeColumn (row2, 0.30f, 0.34f);
        characterPanel->setBounds (cells[0]);
        accentPanel->setBounds (cells[1]);
        performancePanel->setBounds (cells[2]);

        const auto characterRow = asComponents (characterKnobs);
        layoutControlRow (characterRow.data(), static_cast<int> (characterRow.size()),
                          characterPanel->getContentBounds());
        const auto accentRow = asComponents (accentKnobs);
        layoutControlRow (accentRow.data(), static_cast<int> (accentRow.size()),
                          accentPanel->getContentBounds());
        const auto performanceRow = asComponents (performanceKnobs);
        layoutControlRow (performanceRow.data(), static_cast<int> (performanceRow.size()),
                          performancePanel->getContentBounds());
    }

    // --- Row 3: MODULATION / MOD MATRIX -----------------------------------
    if (modulationPanel != nullptr && matrixPanel != nullptr)
    {
        const auto usable = row3.getWidth() - panelGap;
        auto rest = row3;
        modulationPanel->setBounds (rest.removeFromLeft (juce::jmax (1,
            static_cast<int> (usable * 0.52f))));
        rest.removeFromLeft (panelGap);
        matrixPanel->setBounds (rest);

        if (modulationTabs != nullptr) {
            auto tabs = modulationPanel->getContentBounds().removeFromTop (tabStripHeight);
            tabs.removeFromTop (3);
            modulationTabs->setBounds (tabs);
        }

        auto content = modulationPanel->getContentBounds();
        content.removeFromTop (tabStripHeight + 3);

        // The modulation graph stays visually dominant; the four ADSR-style
        // controls sit beneath it.
        const auto knobStrip = juce::jmax (0, static_cast<int> (content.getHeight() * 0.40f));
        const auto knobArea = juce::Rectangle<int> (content.getX(),
                                                    content.getBottom() - knobStrip,
                                                    content.getWidth(), knobStrip);
        if (modulationGraph != nullptr) {
            auto graphArea = content;
            graphArea.setHeight (juce::jmax (14, content.getHeight() - knobStrip - 2));
            modulationGraph->setBounds (graphArea);
        }

        const auto rowMod = asComponents (modulationKnobs);
        layoutControlRow (rowMod.data(), static_cast<int> (rowMod.size()),
                          knobArea.reduced (0, 2));

        if (matrix != nullptr)
            matrix->setBounds (matrixPanel->getContentBounds());
    }
}

} // namespace vstengine::ui