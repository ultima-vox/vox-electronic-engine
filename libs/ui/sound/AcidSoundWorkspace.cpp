#include "AcidSoundWorkspace.h"

namespace vstengine::ui {

namespace {

constexpr int panelGap = 8;
constexpr int tabStripHeight = 15;

template <std::size_t N>
std::array<juce::Component*, N> asComponents (const std::array<SoundParameterKnob*, N>& knobs)
{
    std::array<juce::Component*, N> out {};
    for (std::size_t i = 0; i < N; ++i)
        out[i] = knobs[i];
    return out;
}

} // namespace

AcidSoundWorkspace::AcidSoundWorkspace()
{
    setIdentity (colours::accentDomain, HeroIdentity::acid);
    buildComposition();
}

void AcidSoundWorkspace::buildComposition()
{
    // --- Top strip ---------------------------------------------------------
    oscillatorPanel = &addPanel ("OSCILLATOR");
    filterPanel = &addPanel ("FILTER");
    envelopePanel = &addPanel ("ENVELOPE");
    drivePanel = &addPanel ("DISTORTION / DRIVE");
    accentPanel = &addPanel ("ACCENT");
    slidePanel = &addPanel ("SLIDE");
    outputPanel = &addPanel ("OUTPUT");

    envelopeTabs = std::make_unique<SoundTabStrip> ();
    envelopeTabs->setTabs ({ "AMP", "FILTER" });
    envelopeTabs->setAccent (identityAccent);

    envelopeGraph = std::make_unique<SoundGraph> (SoundGraphKind::envelope);
    envelopeGraph->setVerticalWeight (0.52f);
    envelopeGraph->setAccent (identityAccent);

    // --- Centre: the sequencer is the identity of this instrument ----------
    sequencerPanel = &addPanel ("STEP SEQUENCER");
    sequencer = std::make_unique<GateAVisualSequencer> ();
    sequencer->setAccent (identityAccent);

    // --- Bottom row --------------------------------------------------------
    modulationPanel = &addPanel ("MODULATION");
    performancePanel = &addPanel ("PERFORMANCE");
    playModePanel = &addPanel ("PLAY MODE");

    modulationTabs = std::make_unique<SoundTabStrip> ();
    modulationTabs->setAccent (identityAccent);

    modulationGraph = std::make_unique<SoundGraph> (SoundGraphKind::modulation);
    modulationGraph->setVerticalWeight (1.0f);
    modulationGraph->setAccent (identityAccent);

    playMode = std::make_unique<GateAVisualOptionList> ();
    playMode->setAccent (identityAccent);
}

SoundParameterKnob& AcidSoundWorkspace::addControl (juce::String requestedId,
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

void AcidSoundWorkspace::buildControls()
{
    // Releases the APVTS attachments created for the previously selected slot.
    // Clear panel content before destroying controls: the knobs are children of
    // their panels, so their layout hooks must go first to avoid dangling
    // pointers.
    clearPanelContent();

    controls.clear();
    oscillatorKnobs.fill (nullptr);
    filterKnobs.fill (nullptr);
    envelopeKnobs.fill (nullptr);
    driveKnobs.fill (nullptr);
    modulationKnobs.fill (nullptr);
    performanceKnobs.fill (nullptr);
    outputKnobs.fill (nullptr);
    accentKnob = nullptr;
    slideKnob = nullptr;

    // Genuine semantic matches against the existing Acid descriptor:
    //   waveform  -> OSCILLATOR / Wave
    //   cutoff    -> FILTER / Cutoff
    //   resonance -> FILTER / Resonance
    //   decay     -> ENVELOPE / D
    //   drive     -> DISTORTION / Drive
    //   accent    -> ACCENT / Amount
    //   slide     -> SLIDE / Time
    //
    // `output` is intentionally requested but will NOT resolve: the Acid
    // descriptor publishes it with preferredMacro == -1, so there is no existing
    // APVTS route for it. It therefore renders preview-only rather than being
    // bound to something unrelated. `envelope` is not requested because the
    // reference panel's A knob is ambiguous against that generic id.

    // Top strip
    oscillatorKnobs[0] = &addControl ({}, "TUNE", "-12 ct", vox::ui::VoxKnob::Size::Small);
    oscillatorKnobs[1] = &addControl ({}, "FINE", "+04 ct", vox::ui::VoxKnob::Size::Small);
    oscillatorKnobs[2] = &addControl ("waveform", "WAVE", "SAW",  vox::ui::VoxKnob::Size::Small);
    oscillatorKnobs[3] = &addControl ({},   "PW",   "50%",   vox::ui::VoxKnob::Size::Small);

    filterKnobs[0] = &addControl ("cutoff",    "CUTOFF",    "640 Hz", vox::ui::VoxKnob::Size::Small);
    filterKnobs[1] = &addControl ("resonance", "RESONANCE", "0.72",   vox::ui::VoxKnob::Size::Small);
    filterKnobs[2] = &addControl ({}, "ENV AMT",   "+0.55",  vox::ui::VoxKnob::Size::Small);
    filterKnobs[3] = &addControl ({},  "KEY TRACK", "0.30",   vox::ui::VoxKnob::Size::Small);

    envelopeKnobs[0] = &addControl ({}, "A", "1 ms",   vox::ui::VoxKnob::Size::Small);
    envelopeKnobs[1] = &addControl ("decay",     "D", "240 ms", vox::ui::VoxKnob::Size::Small);
    envelopeKnobs[2] = &addControl ({}, "S", "0.00",   vox::ui::VoxKnob::Size::Small);
    envelopeKnobs[3] = &addControl ({}, "R", "60 ms",  vox::ui::VoxKnob::Size::Small);

    driveKnobs[0] = &addControl ("drive",     "DRIVE", "0.58", vox::ui::VoxKnob::Size::Small);
    driveKnobs[1] = &addControl ({}, "TONE",  "0.47", vox::ui::VoxKnob::Size::Small);
    driveKnobs[2] = &addControl ({},  "MIX",   "0.82", vox::ui::VoxKnob::Size::Small);

    accentKnob = &addControl ("accent", "AMOUNT", "+0.68", vox::ui::VoxKnob::Size::Normal);
    slideKnob  = &addControl ("slide",  "TIME",   "82 ms",  vox::ui::VoxKnob::Size::Normal);

    outputKnobs[0] = &addControl ("output", "VOLUME", "-3.2 dB", vox::ui::VoxKnob::Size::Small);
    outputKnobs[1] = &addControl ({},    "PAN",    "C",       vox::ui::VoxKnob::Size::Small);

    // Bottom row
    modulationKnobs[0] = &addControl ({},  "AMOUNT",  "0.52", vox::ui::VoxKnob::Size::Small);
    modulationKnobs[1] = &addControl ({},  "ATTACK",  "8 ms",  vox::ui::VoxKnob::Size::Small);
    modulationKnobs[2] = &addControl ({},   "DECAY",   "180 ms", vox::ui::VoxKnob::Size::Small);
    modulationKnobs[3] = &addControl ({}, "SUSTAIN", "0.44",  vox::ui::VoxKnob::Size::Small);
    modulationKnobs[4] = &addControl ({}, "RELEASE", "140 ms", vox::ui::VoxKnob::Size::Small);

    performanceKnobs[0] = &addControl ({}, "PORTAMENTO", "ON",     vox::ui::VoxKnob::Size::Small);
    performanceKnobs[1] = &addControl ({},     "UNISON",     "1",      vox::ui::VoxKnob::Size::Small);
    performanceKnobs[2] = &addControl ({},     "DETUNE",     "0 ct",   vox::ui::VoxKnob::Size::Small);
    performanceKnobs[3] = &addControl ({},     "SPREAD",     "0.30",   vox::ui::VoxKnob::Size::Small);

    populatePanels();
}

void AcidSoundWorkspace::bind (const SoundWorkspaceBinding& binding)
{
    state = &binding.state;
    slotIndex = binding.slotIndex;
    descriptor = binding.descriptor;
    setIdentity (colours::accentDomain, HeroIdentity::acid);
    buildControls();

    // Preview copy for Visual Gate A.
    if (oscillatorPanel != nullptr)
        oscillatorPanel->setHeaderSelector ("SAWTOOTH");
    if (filterPanel != nullptr)
        filterPanel->setHeaderSelector ("LP-24");
    if (drivePanel != nullptr)
        drivePanel->setHeaderSelector ("303");
    if (modulationPanel != nullptr)
        modulationPanel->setHeaderSelector ("ENV 1 > CUTOFF");

    resized();
}

juce::StringArray AcidSoundWorkspace::getGateAVisualOnlyLabels() const
{
    juce::StringArray labels;
    for (const auto& control : controls)
        if (control != nullptr && ! control->isBoundToRealParameter())
            labels.add (control->getLabel());
    labels.add ("STEP SEQUENCER pattern");
    labels.add ("PLAY MODE options");
    labels.add ("MODULATION targets");
    return labels;
}

void AcidSoundWorkspace::populatePanels()
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

    // Top strip: four even voice panels, each a 2x2 control grid.
    if (oscillatorPanel != nullptr)
        addControlStrip (*oscillatorPanel, asVector (oscillatorKnobs), 2);
    if (filterPanel != nullptr)
        addControlStrip (*filterPanel, asVector (filterKnobs), 2);
    if (drivePanel != nullptr)
        addControlStrip (*drivePanel, asVector (driveKnobs), 2);

    // ACCENT and SLIDE hold a single centred control each.
    if (accentPanel != nullptr && accentKnob != nullptr)
        addCentreControl (*accentPanel, *accentKnob);
    if (slidePanel != nullptr && slideKnob != nullptr)
        addCentreControl (*slidePanel, *slideKnob);

    if (outputPanel != nullptr)
        addControlStrip (*outputPanel, asVector (outputKnobs), 2);

    // ENVELOPE: tab strip, then a graph, then a 2x2 control grid.
    if (envelopePanel != nullptr && envelopeTabs != nullptr && envelopeGraph != nullptr)
    {
        auto items = asComponents (envelopeKnobs);
        std::vector<juce::Component*> all { envelopeTabs.get(), envelopeGraph.get() };
        all.insert (all.end(), items.begin(), items.end());

        addPanelContent (*envelopePanel, all, [all] (juce::Rectangle<int> content)
        {
            auto area = content;
            auto tabs = area.removeFromTop (tabStripHeight);
            tabs.removeFromTop (2);
            all[0]->setBounds (tabs);

            const auto knobStrip = juce::jmax (0,
                static_cast<int> (static_cast<float> (area.getHeight()) * 0.42f));
            auto graphArea = area;
            graphArea.setHeight (juce::jmax (12, area.getHeight() - knobStrip - 2));
            all[1]->setBounds (graphArea);

            layoutControlGrid (all.data() + 2, static_cast<int> (all.size()) - 2,
                               juce::Rectangle<int> (area.getX(), area.getBottom() - knobStrip,
                                                     area.getWidth(), knobStrip).reduced (0, 2),
                               2);
        });
    }

    // STEP SEQUENCER fills its panel: it is the centre of this composition.
    if (sequencerPanel != nullptr && sequencer != nullptr)
        addPanelContent (*sequencerPanel, { sequencer.get() },
                         [raw = sequencer.get()] (juce::Rectangle<int> content)
                         {
                             if (raw != nullptr)
                                 raw->setBounds (content);
                         });

    // MODULATION: tab strip, dominant graph, then a 2x2 control grid.
    if (modulationPanel != nullptr && modulationTabs != nullptr && modulationGraph != nullptr)
    {
        auto items = asComponents (modulationKnobs);
        std::vector<juce::Component*> all { modulationTabs.get(), modulationGraph.get() };
        all.insert (all.end(), items.begin(), items.end());

        addPanelContent (*modulationPanel, all, [all] (juce::Rectangle<int> content)
        {
            auto area = content;
            auto tabs = area.removeFromTop (tabStripHeight);
            tabs.removeFromTop (2);
            all[0]->setBounds (tabs);
            area.removeFromTop (tabStripHeight + 2);

            const auto knobStrip = juce::jmax (0,
                static_cast<int> (static_cast<float> (area.getHeight()) * 0.44f));
            auto graphArea = area;
            graphArea.setHeight (juce::jmax (12, area.getHeight() - knobStrip - 2));
            all[1]->setBounds (graphArea);

            layoutControlGrid (all.data() + 2, static_cast<int> (all.size()) - 2,
                               juce::Rectangle<int> (area.getX(), area.getBottom() - knobStrip,
                                                     area.getWidth(), knobStrip),
                               2);
        });
    }

    if (performancePanel != nullptr)
        addControlStrip (*performancePanel, asVector (performanceKnobs), 2);

    // PLAY MODE fills its panel with the inert option list.
    if (playModePanel != nullptr && playMode != nullptr)
        addPanelContent (*playModePanel, { playMode.get() },
                         [raw = playMode.get()] (juce::Rectangle<int> content)
                         {
                             if (raw != nullptr)
                                 raw->setBounds (content);
                         });

    resized();
}

void AcidSoundWorkspace::resized()
{
    // The workspace positions PANELS only. Content is laid out inside each panel
    // in panel-local coordinates, so no content rectangle is computed here.
    auto area = getLocalBounds();

    // The sequencer takes whatever the top strip and bottom row do not need, so
    // it stays dominant at every breakpoint instead of being clipped.
    const int topHeight = juce::jlimit (96, 148, static_cast<int> (area.getHeight() * 0.28f));
    const int bottomHeight = juce::jlimit (84, 128, static_cast<int> (area.getHeight() * 0.26f));

    const auto top = area.removeFromTop (juce::jmin (topHeight, area.getHeight()));
    area.removeFromTop (panelGap);

    const auto bottom = area.removeFromBottom (juce::jmin (bottomHeight, area.getHeight()));
    area.removeFromBottom (panelGap);

    layoutTopStrip (top);

    if (sequencerPanel != nullptr)
        sequencerPanel->setBounds (area);

    layoutBottomRow (bottom);
}

void AcidSoundWorkspace::layoutTopStrip (juce::Rectangle<int> area)
{
    if (area.getHeight() <= 0 || area.getWidth() <= panelGap * 4)
        return;

    // Non-uniform widths taken from the accepted render, not an equal grid.
    const float proportions[] = { 0.22f, 0.21f, 0.22f, 0.13f };
    SoundModulePanel* panels[] = { oscillatorPanel, filterPanel,
                                   envelopePanel, drivePanel };

    const auto usable = area.getWidth() - panelGap * 4;
    auto rest = area;
    for (int i = 0; i < 4; ++i) {
        if (panels[i] == nullptr)
            continue;
        panels[i]->setBounds (rest.removeFromLeft (
            juce::jmax (1, static_cast<int> (static_cast<float> (usable) * proportions[i]))));
        rest.removeFromLeft (panelGap);
    }

    // Right block: ACCENT + SLIDE side by side, OUTPUT spanning beneath them.
    //
    // At very compact widths the remainder can collapse, so the block is dropped
    // rather than laid out with negative or overlapping rectangles.
    const auto rightBlock = rest;
    const auto rightBlockUsable = rightBlock.getWidth() > panelGap
                               && rightBlock.getHeight() > panelGap;
    for (auto* panel : { accentPanel, slidePanel, outputPanel })
        if (panel != nullptr)
            panel->setVisible (rightBlockUsable);

    if (rightBlockUsable) {
        const auto rightHalf = juce::jmax (1, (rightBlock.getWidth() - panelGap) / 2);
        auto rightRest = rightBlock;
        const auto rightTop = rightRest.removeFromTop (
            juce::jmax (1, (rightBlock.getHeight() - panelGap) / 2));
        rightRest.removeFromTop (panelGap);

        // ACCENT and SLIDE sit side by side in the upper half; OUTPUT spans the
        // lower half. SLIDE must be placed INSIDE the block: offsetting it past
        // rightTop.getRight() put it beyond the workspace's right edge, where it
        // was clipped away entirely and never appeared on any capture.
        if (accentPanel != nullptr)
            accentPanel->setBounds (rightTop.withWidth (rightHalf));
        if (slidePanel != nullptr)
            slidePanel->setBounds (rightTop.withTrimmedLeft (rightHalf + panelGap));
        if (outputPanel != nullptr)
            outputPanel->setBounds (rightRest);
    }
}

void AcidSoundWorkspace::layoutBottomRow (juce::Rectangle<int> area)
{
    if (area.getHeight() <= 0 || area.getWidth() <= 0)
        return;

    const auto usable = area.getWidth() - panelGap * 2;
    auto rest = area;

    if (modulationPanel != nullptr)
        modulationPanel->setBounds (rest.removeFromLeft (
            juce::jmax (1, static_cast<int> (static_cast<float> (usable) * 0.52f))));
    rest.removeFromLeft (panelGap);
    if (performancePanel != nullptr)
        performancePanel->setBounds (rest.removeFromLeft (
            juce::jmax (1, static_cast<int> (static_cast<float> (usable) * 0.30f))));
    rest.removeFromLeft (panelGap);
    if (playModePanel != nullptr)
        playModePanel->setBounds (rest);
}

} // namespace vstengine::ui