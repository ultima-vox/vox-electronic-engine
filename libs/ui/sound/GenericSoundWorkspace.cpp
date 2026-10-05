#include "GenericSoundWorkspace.h"

namespace vstengine::ui {

namespace {

constexpr int panelGap = 8;

template <std::size_t N>
std::array<juce::Component*, N> asComponents (const std::array<SoundParameterKnob*, N>& knobs)
{
    std::array<juce::Component*, N> out {};
    for (std::size_t i = 0; i < N; ++i)
        out[i] = knobs[i];
    return out;
}

} // namespace

GenericSoundWorkspace::GenericSoundWorkspace()
{
    setIdentity (colours::primary, HeroIdentity::generic);
    buildComposition();
}

void GenericSoundWorkspace::buildComposition()
{
    oscillatorPanel = &addPanel ("OSCILLATOR");
    filterPanel = &addPanel ("FILTER");
    envelopePanel = &addPanel ("AMP ENVELOPE");
    characterPanel = &addPanel ("CHARACTER");
    performancePanel = &addPanel ("PERFORMANCE");
    modulationPanel = &addPanel ("MODULATION");

    oscillatorGraph = std::make_unique<SoundGraph> (SoundGraphKind::oscillator);
    oscillatorGraph->setWaveShape (PreviewWaveShape::saw);
    oscillatorGraph->setVerticalWeight (0.45f);

    filterGraph = std::make_unique<SoundGraph> (SoundGraphKind::filter);
    filterGraph->setVerticalWeight (0.50f);

    envelopeGraph = std::make_unique<SoundGraph> (SoundGraphKind::envelope);
    envelopeGraph->setVerticalWeight (0.48f);

    modulationGraph = std::make_unique<SoundGraph> (SoundGraphKind::modulation);
    modulationGraph->setVerticalWeight (1.0f);
}

SoundParameterKnob& GenericSoundWorkspace::addControl (juce::String requestedId,
                                                       juce::String label,
                                                       juce::String previewValue)
{
    controls.push_back (std::make_unique<SoundParameterKnob> (
        *state, std::move (requestedId), std::move (label), descriptor, slotIndex,
        vox::ui::VoxKnob::Size::Normal, std::move (previewValue)));
    auto& knob = *controls.back();
    knob.setAccent (identityAccent);
    return knob;
}

void GenericSoundWorkspace::buildControls()
{
    // Clear panel content before destroying controls: the knobs are children of
    // their panels, so their layout hooks must go first to avoid dangling
    // pointers.
    clearPanelContent();

    controls.clear();
    oscillatorKnobs.fill (nullptr);
    filterKnobs.fill (nullptr);
    envelopeKnobs.fill (nullptr);
    characterKnobs.fill (nullptr);
    performanceKnobs.fill (nullptr);
    modulationKnobs.fill (nullptr);

    // Generic is the fallback for Lead, Atmos, Semantic FX and any future rack
    // instrument, and those providers do NOT share one parameter set. Nothing
    // here can be guaranteed to be a genuine semantic match, so every control
    // requests no host parameter at all and renders Gate-A visual-only.
    //
    // Psy Bass and Acid have dedicated workspaces, which is where their real
    // descriptor ids are requested. Functional Gate B introduces descriptors and
    // then requests the matching ids here; the presentation layer does not
    // change.
    oscillatorKnobs[0] = &addControl ({}, "OCTAVE",  "+0");
    oscillatorKnobs[1] = &addControl ({}, "SEMITONE", "+0");
    oscillatorKnobs[2] = &addControl ({}, "BLEND",  "0.30");
    oscillatorKnobs[3] = &addControl ({}, "SHAPE",  "SAW");

    filterKnobs[0] = &addControl ({}, "CUTOFF",    "520 Hz");
    filterKnobs[1] = &addControl ({}, "RESONANCE", "0.30");
    filterKnobs[2] = &addControl ({}, "ENV AMOUNT", "+0.30");
    filterKnobs[3] = &addControl ({}, "KEY TRACK", "0.40");

    envelopeKnobs[0] = &addControl ({},  "A", "8 ms");
    envelopeKnobs[1] = &addControl ({},  "D", "220 ms");
    envelopeKnobs[2] = &addControl ({}, "S", "0.65");
    envelopeKnobs[3] = &addControl ({}, "R", "260 ms");

    characterKnobs[0] = &addControl ({}, "DRIVE", "0.30");
    characterKnobs[1] = &addControl ({}, "TONE",  "0.50");
    characterKnobs[2] = &addControl ({},  "MIX",   "0.70");

    performanceKnobs[0] = &addControl ({}, "PORTAMENTO", "0 ms");
    performanceKnobs[1] = &addControl ({},     "UNISON",     "1");
    performanceKnobs[2] = &addControl ({},     "DETUNE",     "0 ct");
    performanceKnobs[3] = &addControl ({},     "SPREAD",     "0.50");

    modulationKnobs[0] = &addControl ({},  "AMOUNT",  "0.40");
    modulationKnobs[1] = &addControl ({},  "ATTACK",  "20 ms");
    modulationKnobs[2] = &addControl ({},   "DECAY",   "240 ms");
    modulationKnobs[3] = &addControl ({}, "RELEASE", "200 ms");

    populatePanels();
}

void GenericSoundWorkspace::bind (const SoundWorkspaceBinding& binding)
{
    state = &binding.state;
    slotIndex = binding.slotIndex;
    descriptor = binding.descriptor;
    setIdentity (colours::primary, HeroIdentity::generic);
    buildControls();

    if (filterPanel != nullptr)
        filterPanel->setHeaderSelector ("LOW-PASS");
    resized();
}

juce::StringArray GenericSoundWorkspace::getGateAVisualOnlyLabels() const
{
    juce::StringArray labels;
    for (const auto& control : controls)
        if (control != nullptr && ! control->isBoundToRealParameter())
            labels.add (control->getLabel());
    labels.add ("MODULATION targets");
    return labels;
}

void GenericSoundWorkspace::populatePanels()
{
    // Model A: every module component is a child of its panel and laid out in
    // PANEL-LOCAL coordinates. The workspace positions panels only.
    //
    // Content was already cleared at the top of buildControls(), before the old
    // controls were destroyed, so nothing is cleared again here.
    const auto asVector = [] (auto& knobs)
    {
        std::vector<SoundParameterKnob*> out;
        for (auto* knob : knobs)
            if (knob != nullptr)
                out.push_back (knob);
        return out;
    };

    const auto graphOverControls =
        [this] (SoundModulePanel& panel, SoundGraph& graph,
            const std::array<SoundParameterKnob*, 4>& knobs, const float weight)
        {
            std::vector<juce::Component*> items { &graph };
            for (auto* knob : knobs)
                if (knob != nullptr)
                    items.push_back (knob);

            addPanelContent (panel, items, [items, weight] (juce::Rectangle<int> content)
            {
                auto area = content;
                const auto graphHeight = juce::jmax (18,
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
        graphOverControls (*filterPanel, *filterGraph, filterKnobs, 0.50f);
    if (envelopePanel != nullptr && envelopeGraph != nullptr)
        graphOverControls (*envelopePanel, *envelopeGraph, envelopeKnobs, 0.48f);

    if (characterPanel != nullptr)
        addControlStrip (*characterPanel, asVector (characterKnobs));
    if (performancePanel != nullptr)
        addControlStrip (*performancePanel, asVector (performanceKnobs));

    // MODULATION graph fills its panel; the controls sit beneath it.
    if (modulationPanel != nullptr && modulationGraph != nullptr)
    {
        auto items = asComponents (modulationKnobs);
        std::vector<juce::Component*> all { modulationGraph.get() };
        all.insert (all.end(), items.begin(), items.end());

        addPanelContent (*modulationPanel, all, [all] (juce::Rectangle<int> content)
        {
            auto area = content;
            const auto knobStrip = juce::jmax (0,
                static_cast<int> (static_cast<float> (area.getHeight()) * 0.36f));
            auto graphArea = area;
            graphArea.setHeight (juce::jmax (12, area.getHeight() - knobStrip - 2));
            all[0]->setBounds (graphArea);
            layoutControlRow (all.data() + 1, static_cast<int> (all.size()) - 1,
                              juce::Rectangle<int> (area.getX(), area.getBottom() - knobStrip,
                                                    area.getWidth(), knobStrip).reduced (0, 2));
        });
    }

    resized();
}

void GenericSoundWorkspace::resized()
{
    // The workspace positions PANELS only; content is laid out inside each panel.
    auto area = getLocalBounds();

    const auto topHeight = juce::jlimit (130, 210, static_cast<int> (area.getHeight() * 0.54f));
    const auto top = area.removeFromTop (juce::jmin (topHeight, area.getHeight()));
    area.removeFromTop (panelGap);
    const auto bottom = area;

    const auto colWidth = (top.getWidth() - panelGap * 2) / 3;
    auto topRest = top;
    const auto oscillatorArea = topRest.removeFromLeft (colWidth);
    topRest.removeFromLeft (panelGap);
    const auto filterArea = topRest.removeFromLeft (colWidth);
    topRest.removeFromLeft (panelGap);
    const auto envelopeArea = topRest;

    const auto bottomCol = (bottom.getWidth() - panelGap * 2) / 3;
    auto bottomRest = bottom;
    const auto characterArea = bottomRest.removeFromLeft (bottomCol);
    bottomRest.removeFromLeft (panelGap);
    const auto performanceArea = bottomRest.removeFromLeft (bottomCol);
    bottomRest.removeFromLeft (panelGap);
    const auto modulationArea = bottomRest;

    if (oscillatorPanel != nullptr)
        oscillatorPanel->setBounds (oscillatorArea);
    if (filterPanel != nullptr)
        filterPanel->setBounds (filterArea);
    if (envelopePanel != nullptr)
        envelopePanel->setBounds (envelopeArea);
    if (characterPanel != nullptr)
        characterPanel->setBounds (characterArea);
    if (performancePanel != nullptr)
        performancePanel->setBounds (performanceArea);
    if (modulationPanel != nullptr)
        modulationPanel->setBounds (modulationArea);
}

} // namespace vstengine::ui