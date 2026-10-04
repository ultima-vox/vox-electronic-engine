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
    addAndMakeVisible (*oscillatorGraph);

    filterGraph = std::make_unique<SoundGraph> (SoundGraphKind::filter);
    filterGraph->setVerticalWeight (0.50f);
    addAndMakeVisible (*filterGraph);

    envelopeGraph = std::make_unique<SoundGraph> (SoundGraphKind::envelope);
    envelopeGraph->setVerticalWeight (0.48f);
    addAndMakeVisible (*envelopeGraph);

    modulationGraph = std::make_unique<SoundGraph> (SoundGraphKind::modulation);
    modulationGraph->setVerticalWeight (1.0f);
    addAndMakeVisible (*modulationGraph);
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
    addAndMakeVisible (knob);
    return knob;
}

void GenericSoundWorkspace::buildControls()
{
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

void GenericSoundWorkspace::resized()
{
    auto area = getLocalBounds();

    const auto topHeight = juce::jlimit (130, 210, static_cast<int> (area.getHeight() * 0.54f));
    auto top = area.removeFromTop (juce::jmin (topHeight, area.getHeight()));
    area.removeFromTop (panelGap);
    const auto bottom = area;

    const auto colWidth = (top.getWidth() - panelGap * 2) / 3;
    auto topRest = top;
    auto oscillatorArea = topRest.removeFromLeft (colWidth);
    topRest.removeFromLeft (panelGap);
    auto filterArea = topRest.removeFromLeft (colWidth);
    topRest.removeFromLeft (panelGap);
    const auto envelopeArea = topRest;

    const auto bottomCol = (bottom.getWidth() - panelGap * 2) / 3;
    auto bottomRest = bottom;
    auto characterArea = bottomRest.removeFromLeft (bottomCol);
    bottomRest.removeFromLeft (panelGap);
    auto performanceArea = bottomRest.removeFromLeft (bottomCol);
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

    const auto layoutGraphOverRow =
        [] (SoundModulePanel& panel, SoundGraph& graph,
            const std::array<juce::Component*, 4>& row, const float weight)
    {
        auto content = panel.getContentBounds();
        const auto graphHeight = juce::jmax (18,
            static_cast<int> (content.getHeight() * weight));
        graph.setBounds (content.removeFromTop (graphHeight));
        layoutControlRow (row.data(), static_cast<int> (row.size()),
                          content.reduced (0, 2));
    };

    if (oscillatorPanel != nullptr && oscillatorGraph != nullptr)
        layoutGraphOverRow (*oscillatorPanel, *oscillatorGraph,
                           asComponents (oscillatorKnobs), 0.45f);
    if (filterPanel != nullptr && filterGraph != nullptr)
        layoutGraphOverRow (*filterPanel, *filterGraph,
                           asComponents (filterKnobs), 0.50f);
    if (envelopePanel != nullptr && envelopeGraph != nullptr)
        layoutGraphOverRow (*envelopePanel, *envelopeGraph,
                           asComponents (envelopeKnobs), 0.48f);

    if (characterPanel != nullptr) {
        const auto row = asComponents (characterKnobs);
        layoutControlRow (row.data(), static_cast<int> (row.size()),
                          characterPanel->getContentBounds());
    }
    if (performancePanel != nullptr) {
        const auto row = asComponents (performanceKnobs);
        layoutControlRow (row.data(), static_cast<int> (row.size()),
                          performancePanel->getContentBounds());
    }
    if (modulationPanel != nullptr) {
        auto content = modulationPanel->getContentBounds();
        if (modulationGraph != nullptr)
            modulationGraph->setBounds (content);
    }
}

} // namespace vstengine::ui