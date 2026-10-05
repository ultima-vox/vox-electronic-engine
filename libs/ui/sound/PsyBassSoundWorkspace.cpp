#include "PsyBassSoundWorkspace.h"

#include <array>

namespace vstengine::ui {

namespace {

constexpr int panelGap = 8;
constexpr int tabStripHeight = 15;

// Knob array -> component vector, dropping any unbuilt entries.
std::vector<SoundParameterKnob*> asVector (const std::array<SoundParameterKnob*, 4>& knobs)
{
    std::vector<SoundParameterKnob*> out;
    out.reserve (knobs.size());
    for (auto* knob : knobs)
        if (knob != nullptr)
            out.push_back (knob);
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

    filterGraph = std::make_unique<SoundGraph> (SoundGraphKind::filter);
    filterGraph->setVerticalWeight (0.52f);

    envelopeGraph = std::make_unique<SoundGraph> (SoundGraphKind::envelope);
    envelopeGraph->setVerticalWeight (0.48f);

    // --- Row 2 -------------------------------------------------------------
    characterPanel = &addPanel ("DRIVE / CHARACTER");
    accentPanel = &addPanel ("ACCENT");
    performancePanel = &addPanel ("PERFORMANCE");

    // --- Row 3 -------------------------------------------------------------
    modulationPanel = &addPanel ("MODULATION");
    matrixPanel = &addPanel ("MOD MATRIX");

    modulationTabs = std::make_unique<SoundTabStrip>();
    modulationTabs->setAccent (identityAccent);

    modulationGraph = std::make_unique<SoundGraph> (SoundGraphKind::modulation);
    modulationGraph->setVerticalWeight (1.0f);
    modulationGraph->setAccent (identityAccent);

    matrix = std::make_unique<GateAVisualMatrix>();
    matrix->setAccent (identityAccent);
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
    return knob;
}

void PsyBassSoundWorkspace::buildControls()
{
    // Drop panel content BEFORE destroying controls. The knobs are children of
    // their panels, so the panels' content and layout hooks must be released
    // first, otherwise destroying a knob would leave a dangling pointer behind
    // for the next clearPanelContent() to dereference.
    clearPanelContent();

    // Destroy any previous controls. This releases the APVTS SliderAttachments
    // created for the previously selected slot.
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

    populatePanels();
}

void PsyBassSoundWorkspace::populatePanels()
{
    // Every module component becomes a child of its panel, so all of the layout
    // below happens in PANEL-LOCAL coordinates. The workspace only decides the
    // arrangement; the panel owns where its content lives.
    //

    // A graph over a control strip, repeated for the three voice modules.
    const auto graphOverControls =
        [] (SoundModulePanel& panel, SoundGraph& graph,
            const std::array<SoundParameterKnob*, 4>& knobs, const float weight)
        {
            std::vector<juce::Component*> items { &graph };
            for (auto* knob : knobs)
                if (knob != nullptr)
                    items.push_back (knob);

            // Captured by value: the hook never dereferences the workspace.
            addPanelContent (panel, items, [items, weight] (juce::Rectangle<int> content)
            {
                auto area = content;
                const auto graphHeight = juce::jmax (20,
                    static_cast<int> (static_cast<float> (area.getHeight()) * weight));
                items[0]->setBounds (area.removeFromTop (graphHeight));
                layoutControlRow (items.data() + 1,
                                  static_cast<int> (items.size()) - 1,
                                  area.reduced (0, 2));
            });
        };

    if (oscillatorPanel != nullptr && oscillatorGraph != nullptr)
        graphOverControls (*oscillatorPanel, *oscillatorGraph, oscillatorKnobs, 0.45f);
    if (filterPanel != nullptr && filterGraph != nullptr)
        graphOverControls (*filterPanel, *filterGraph, filterKnobs, 0.52f);
    if (envelopePanel != nullptr && envelopeGraph != nullptr)
        graphOverControls (*envelopePanel, *envelopeGraph, envelopeKnobs, 0.48f);

    // Control-only panels.
    if (characterPanel != nullptr)
        addControlStrip (*characterPanel, asVector (characterKnobs));
    if (accentPanel != nullptr)
        addControlStrip (*accentPanel, asVector (accentKnobs));
    if (performancePanel != nullptr)
        addControlStrip (*performancePanel, asVector (performanceKnobs));

    // MODULATION: tab strip, dominant graph, then a compact control strip.
    if (modulationPanel != nullptr && modulationTabs != nullptr && modulationGraph != nullptr)
    {
        std::vector<juce::Component*> items { modulationTabs.get(), modulationGraph.get() };
        for (auto* knob : modulationKnobs)
            if (knob != nullptr)
                items.push_back (knob);

        addPanelContent (*modulationPanel, items, [items] (juce::Rectangle<int> content)
        {
            auto area = content;
            auto tabs = area.removeFromTop (tabStripHeight);
            tabs.removeFromTop (3);
            items[0]->setBounds (tabs);
            area.removeFromTop (3);

            const auto knobStrip = juce::jmax (0,
                static_cast<int> (static_cast<float> (area.getHeight()) * 0.40f));
            auto graphArea = area;
            graphArea.setHeight (juce::jmax (14, area.getHeight() - knobStrip - 2));
            items[1]->setBounds (graphArea);

            layoutControlRow (items.data() + 2, static_cast<int> (items.size()) - 2,
                              juce::Rectangle<int> (area.getX(), area.getBottom() - knobStrip,
                                                    area.getWidth(), knobStrip).reduced (0, 2));
        });
    }

    // MOD MATRIX fills its panel.
    if (matrixPanel != nullptr && matrix != nullptr)
        addPanelContent (*matrixPanel, { matrix.get() },
                         [raw = matrix.get()] (juce::Rectangle<int> content)
                         {
                             // Single child: give it the whole content rectangle.
                             if (raw != nullptr)
                                 raw->setBounds (content);
                         });

    resized();
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

juce::StringArray PsyBassSoundWorkspace::getGateAVisualOnlyLabels() const
{
    juce::StringArray labels;
    for (const auto& control : controls)
        if (control != nullptr && ! control->isBoundToRealParameter())
            labels.add (control->getLabel());
    labels.add ("MOD MATRIX routes");
    labels.add ("MODULATION targets");
    return labels;
}

void PsyBassSoundWorkspace::resized()
{
    // The workspace only positions PANELS. Every graph, knob, tab strip and
    // matrix is a child of its panel and is laid out by that panel in
    // panel-local coordinates, so no content rectangle is computed here.
    auto area = getLocalBounds();

    const int row1Height = juce::jlimit (128, 210, static_cast<int> (area.getHeight() * 0.40f));
    const int row2Height = juce::jlimit (88, 136, static_cast<int> (area.getHeight() * 0.26f));

    const auto row1 = area.removeFromTop (juce::jmin (row1Height, area.getHeight()));
    area.removeFromTop (panelGap);
    const auto row2 = area.removeFromTop (juce::jmin (row2Height, area.getHeight()));
    area.removeFromTop (panelGap);
    const auto row3 = area;

    // --- Row 1: OSCILLATOR / FILTER / AMP ENVELOPE -------------------------
    if (oscillatorPanel != nullptr && filterPanel != nullptr && envelopePanel != nullptr)
    {
        const auto cells = threeColumn (row1, 0.30f, 0.34f);
        oscillatorPanel->setBounds (cells[0]);
        filterPanel->setBounds (cells[1]);
        envelopePanel->setBounds (cells[2]);
    }

    // --- Row 2: DRIVE / ACCENT / PERFORMANCE -------------------------------
    if (characterPanel != nullptr && accentPanel != nullptr && performancePanel != nullptr)
    {
        const auto cells = threeColumn (row2, 0.30f, 0.34f);
        characterPanel->setBounds (cells[0]);
        accentPanel->setBounds (cells[1]);
        performancePanel->setBounds (cells[2]);
    }

    // --- Row 3: MODULATION / MOD MATRIX -----------------------------------
    if (modulationPanel != nullptr && matrixPanel != nullptr)
    {
        const auto usable = row3.getWidth() - panelGap;
        auto rest = row3;
        modulationPanel->setBounds (rest.removeFromLeft (juce::jmax (1,
            static_cast<int> (static_cast<float> (usable) * 0.52f))));
        rest.removeFromLeft (panelGap);
        matrixPanel->setBounds (rest);
    }
}

} // namespace vstengine::ui