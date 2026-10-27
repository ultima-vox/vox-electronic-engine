#include "AcidPanel.h"
#include "PluginProcessor.h"
#include "acid/AcidEngine.h"
#include "acid/AcidParameterBridge.h"
#include "instrument/HostParameterSchema.h"
#include "ui/common/UiComponents.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
const auto acidAccentColour = juce::Colour::fromRGB(0, 221, 245);
const auto acidGreenColour = juce::Colour::fromRGB(0, 218, 163);
const auto panelFill = juce::Colour::fromRGB(9, 28, 42);
const auto edge = juce::Colour::fromRGB(24, 64, 81);
const auto dimText = juce::Colour::fromRGB(130, 176, 190);

void drawCard(juce::Graphics& g, juce::Rectangle<int> area,
              const juce::String& title) {
    if (area.isEmpty()) return;
    auto rect = area.toFloat().reduced(1.0f);
    g.setColour(panelFill);
    g.fillRoundedRectangle(rect, 5.0f);
    g.setColour(edge);
    g.drawRoundedRectangle(rect, 5.0f, 1.0f);
    g.setColour(acidAccentColour);
    g.fillEllipse(static_cast<float>(area.getX() + 9),
                  static_cast<float>(area.getY() + 12), 5.0f, 5.0f);
    g.setColour(juce::Colours::whitesmoke);
    g.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
    g.drawFittedText(title, area.reduced(20, 2).removeFromTop(22),
                     juce::Justification::centredLeft, 1);
}
std::string macroOrAcidId(std::size_t slot, std::string_view semantic) {
    for (std::size_t i = 0; i < vstengine::instrument::macrosPerSlot; ++i)
        if (vstengine::acid::AcidEngine::parameterIds[i] == semantic)
            return vstengine::instrument::hostparams::macroId(slot, i);
    return vstengine::acid::hostParameterId(slot, semantic);
}
}

AcidPanel::StepGrid::StepGrid(VstEngineAudioProcessor& p) : processor_(p)
{
    // StepGrid does not implement SettableTooltipClient. Keep the grid
    // accessible via its lane labels without a non-existent tooltip API.
}
void AcidPanel::StepGrid::paint(juce::Graphics& g) {
    const auto a = getLocalBounds();
    if (a.getWidth() < 170 || a.getHeight() < 58) return;
    constexpr int gutter = 47;
    constexpr std::array<const char*, 5> names { "Note", "Accent", "Slide", "Gate", "Octave" };
    const int width = std::max(1, (a.getWidth() - gutter - 2) / 16);
    const int height = std::max(1, a.getHeight() / 5);
    const auto& seq = processor_.sequence();
    for (int lane = 0; lane < 5; ++lane) {
        g.setColour(dimText);
        g.setFont(juce::Font(juce::FontOptions(9.0f)));
        g.drawText(names[static_cast<std::size_t>(lane)], 1, lane * height,
                   gutter - 2, height, juce::Justification::centredLeft);
        for (int s = 0; s < 16; ++s) {
            const auto& step = seq[s];
            const bool valid = s < seq.size();
            const bool active = valid && (lane == 0 ? step.gate
                : lane == 1 ? step.accent : lane == 2 ? step.slideDuration > 0.0f
                : lane == 3 ? step.gate : (std::abs(step.noteOffset) >= 12));
            auto cell = juce::Rectangle<int>(gutter + s * width, lane * height,
                                             width - 2, height - 2);
            g.setColour(active ? (lane == 1 || lane == 2 ? acidGreenColour.darker(0.65f)
                                                        : acidAccentColour.darker(0.72f))
                               : juce::Colour::fromRGB(12, 38, 53));
            g.fillRect(cell);
            g.setColour(s == head_ ? acidAccentColour : edge);
            g.drawRect(cell, s == head_ ? 2 : 1);
            if (!valid) continue;
            g.setColour(active ? juce::Colours::white : dimText);
            g.setFont(juce::Font(juce::FontOptions(9.0f)));
            juce::String text;
            if (lane == 0) {
                const auto root = processor_.parameters().getRawParameterValue("rootNote");
                const int rootNote = root != nullptr
                    ? static_cast<int>(std::round(root->load())) : 48;
                const auto note = juce::jlimit(0, 127, rootNote + step.noteOffset);
                text = step.gate ? juce::MidiMessage::getMidiNoteName(
                    note, true, true, 5) : "--";
            } else if (lane == 1) text = step.accent ? "A" : "";
            else if (lane == 2) text = step.slideDuration > 0.0f ? "S" : "";
            else if (lane == 3) text = step.gate ? "|" : "";
            else text = juce::String(step.noteOffset / 12);
            g.drawFittedText(text, cell, juce::Justification::centred, 1);
        }
    }
}
void AcidPanel::StepGrid::mouseDown(const juce::MouseEvent& e) {
    constexpr int gutter = 47;
    if (e.x < gutter || getWidth() < gutter + 16) return;
    const int width = std::max(1, (getWidth() - gutter - 2) / 16);
    const int column = (e.x - gutter) / width;
    const int row = e.y / std::max(1, getHeight() / 5);
    auto& seq = processor_.sequence();
    if (column < 0 || column >= 16 || column >= seq.size()
        || row < 0 || row >= 5) return;
    auto& s = seq[column];
    switch (row) {
        case 0: s.noteOffset = juce::jlimit(-24, 36, s.noteOffset + (e.mods.isShiftDown() ? -1 : 1));
                s.gate = true; break;
        case 1: s.accent = !s.accent; break;
        case 2: s.slideDuration = s.slideDuration > 0.0f ? 0.0f : 0.5f;
                if (s.slideDuration > 0.0f) { s.gate = true; s.gateWidth = 1.0f; }
                break;
        case 3: s.gate = !s.gate; break;
        case 4: s.noteOffset += e.mods.isShiftDown() ? -12 : 12;
                if (s.noteOffset > 36) s.noteOffset = -24;
                if (s.noteOffset < -24) s.noteOffset = 36;
                break;
    }
    processor_.publishSequenceForAudio();
    repaint();
}

AcidPanel::AcidPanel(VstEngineAudioProcessor& processor)
    : processor_(processor), steps_(processor)
{
    // Every sound knob uses an APVTS attachment, not a decorative callback.
    for (const auto& item : std::initializer_list<std::pair<const char*, const char*>> {
        { "osc_tune", "Tune" }, { "osc_fine", "Fine" },
        { "waveform", "Wave" }, { "osc_pw", "PW" }})
        addKnob(osc, item.first, item.second);
    for (const auto& item : std::initializer_list<std::pair<const char*, const char*>> {
        { "cutoff", "Cutoff" }, { "resonance", "Resonance" },
        { "envelope", "Env Amt" }, { "key_tracking", "Key Trk" }})
        addKnob(filter, item.first, item.second);
    for (const auto& item : std::initializer_list<std::pair<const char*, const char*>> {
        { "amp_attack", "Attack" }, { "amp_decay", "Decay" },
        { "amp_sustain", "Sustain" }, { "amp_release", "Release" }})
        addKnob(envelope, item.first, item.second, true);
    for (const auto& item : std::initializer_list<std::pair<const char*, const char*>> {
        { "drive", "Drive" }, { "dist_tone", "Tone" }, { "dist_mix", "Mix" }})
        addKnob(drive, item.first, item.second);
    addKnob(accent, "accent", "Amount");
    addKnob(slide, "slide", "Time");
    addKnob(output, "output", "Level");
    addKnob(output, "pan", "Pan");
    for (const auto& item : std::initializer_list<std::pair<const char*, const char*>> {
        { "mod_amount", "Amount" }, { "mod_attack", "Attack" },
        { "mod_decay", "Decay" }, { "mod_sustain", "Sustain" },
        { "mod_release", "Release" }})
        addKnob(modulation, item.first, item.second);
    for (const auto& item : std::initializer_list<std::pair<const char*, const char*>> {
        { "portamento", "Porta" }, { "unison_voices", "Unison" },
        { "unison_detune", "Detune" }, { "stereo_spread", "Spread" }})
        addKnob(performance, item.first, item.second);

    auto addChoice = [this](juce::ComboBox& box,
                            std::initializer_list<const char*> names) {
        int id = 1;
        for (const auto* name : names) box.addItem(name, id++);
        addAndMakeVisible(box);
    };
    addChoice(filterChoice_, { "LP 24dB", "Diode 18dB", "BP 12dB", "HP 12dB" });
    addChoice(distortionChoice_, { "Tube", "Soft Clip", "Hard Clip", "Wavefold" });
    addChoice(modulationChoice_, { "Cutoff", "Pitch", "PW", "Res", "Drive", "Pan" });
    filterChoice_.onChange = [this] {
        changeChoice("filter_mode", filterChoice_.getSelectedItemIndex(), 4);
    };
    distortionChoice_.onChange = [this] {
        changeChoice("dist_mode", distortionChoice_.getSelectedItemIndex(), 4);
    };
    modulationChoice_.onChange = [this] {
        changeChoice(std::string(selectedModTarget()),
                     modulationChoice_.getSelectedItemIndex(), 6);
    };
    for (auto* button : { &ampButton_, &filterButton_, &env1Button_, &env2Button_, &lfo1Button_,
                          &lfo2Button_, &lfo3Button_, &stepButton_, &previewButton_,
                          &generate_, &mutate_, &clear_ }) {
        vstengine::ui::styleButton(*button);
        addAndMakeVisible(*button);
    }
    addAndMakeVisible(lfoSyncButton_);
    lfoSyncButton_.setVisible(false);
    lfoSyncButton_.setTooltip("Use DAW BPM and PPQ for the selected LFO");
    lfoSyncButton_.onClick = [this] {
        for (auto& k : knobs_)
            if (k.group == modulation && k.semantic.ends_with("_rate"))
                k.widget->getSlider().repaint();
    };
    ampButton_.onClick = [this] { filterEnvelope_ = false; updateEnvelopeAttachments(); };
    filterButton_.onClick = [this] { filterEnvelope_ = true; updateEnvelopeAttachments(); };
    env1Button_.onClick = [this] { switchModulationForTesting(false); };
    env2Button_.onClick = [this] { switchEnv2ForTesting(); };
    lfo1Button_.onClick = [this] { switchLfoForTesting(1); };
    lfo2Button_.onClick = [this] { switchLfoForTesting(2); };
    lfo3Button_.onClick = [this] { switchLfoForTesting(3); };
    stepButton_.onClick = [this] { switchStepForTesting(); };
    for (int i = 0; i < 4; ++i) addAndMakeVisible(flags_[static_cast<std::size_t>(i)]);
    for (std::size_t step = 0; step < stepLevels_.size(); ++step) {
        auto& slider = stepLevels_[step];
        slider.setSliderStyle(juce::Slider::LinearVertical);
        slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        slider.setRange(0.0, 1.0, 0.0001);
        slider.setDoubleClickReturnValue(true, 0.5);
        slider.setColour(juce::Slider::thumbColourId, acidAccentColour);
        slider.setColour(juce::Slider::trackColourId, acidGreenColour);
        slider.setColour(juce::Slider::backgroundColourId, edge);
        slider.setTooltip("STEP modulation level " + juce::String(
            static_cast<int>(step + 1)) + " (center = zero)");
        slider.setVisible(false);
        addChildComponent(slider);
    }
    for (const auto& t : { "Classic 303", "Psy Acid", "Dark Acid",
                            "Forest Acid", "Hi-Tech Acid", "Hypnotic Acid" })
        profileChoice_.addItem(t, profileChoice_.getNumItems() + 1);
    for (const int steps : { 8, 12, 16, 32 })
        lengthChoice_.addItem(juce::String(steps), lengthChoice_.getNumItems() + 1);
    for (const auto* mode : { "1/16", "1/8", "1/32", "Triplet" })
        timingChoice_.addItem(mode, timingChoice_.getNumItems() + 1);
    for (auto* c : { &profileChoice_, &lengthChoice_, &timingChoice_ })
        addAndMakeVisible(*c);
    addAndMakeVisible(steps_);
    addAndMakeVisible(status_);
    vstengine::ui::styleLabel(status_, 10.0f,
                               juce::Justification::centredLeft, dimText);
    previewButton_.setTooltip(
        "Preview the selected Acid sequence while the DAW transport is stopped. "
        "Does not start the DAW or other Rack slots.");
    previewButton_.onClick = [this] {
        const bool enabled = !processor_.acidPreviewEnabled(slot_);
        processor_.setAcidPreview(slot_, enabled);
        previewButton_.setButtonText(enabled ? "Stop" : "Preview");
        status_.setText(enabled ? "Local Acid preview armed" : "Preview stopped",
                        juce::dontSendNotification);
    };
    generate_.onClick = [this] {
        constexpr std::array<const char*, 6> profiles {
            "classic-303", "psy-acid", "dark-acid",
            "forest-acid", "hi-tech-acid", "hypnotic-acid"
        };
        const int p = juce::jlimit(0, 5, profileChoice_.getSelectedItemIndex());
        juce::String message;
        processor_.generateSelectedPattern(profiles[static_cast<std::size_t>(p)], message);
        status_.setText(message.isEmpty() ? "Pattern generated" : message,
                        juce::dontSendNotification);
        steps_.repaint();
    };
    mutate_.onClick = [this] {
        juce::String message;
        processor_.mutateSelectedPattern(false, message);
        status_.setText(message.isEmpty() ? "Pattern mutated" : message,
                        juce::dontSendNotification);
        steps_.repaint();
    };
    clear_.onClick = [this] {
        processor_.sequence().clear();
        processor_.publishSequenceForAudio();
        steps_.repaint();
    };
    lengthChoice_.onChange = [this] {
        const int selected = lengthChoice_.getSelectedItemIndex();
        constexpr std::array<int, 4> sizes { 8, 12, 16, 32 };
        if (selected >= 0 && selected < 4) {
            processor_.sequence().setLength(sizes[static_cast<std::size_t>(selected)]);
            processor_.publishSequenceForAudio();
            steps_.repaint();
        }
    };
    timingChoice_.onChange = [this] {
        const int selected = timingChoice_.getSelectedItemIndex();
        if (selected >= 0 && selected < 4) {
            processor_.sequence().setTimingMode(
                static_cast<vstengine::sequence::TimingMode>(selected));
            processor_.publishSequenceForAudio();
        }
    };
    profileChoice_.setSelectedId(1, juce::dontSendNotification);
    startTimerHz(12);
}
AcidPanel::~AcidPanel() {
    stopTimer();
    for (auto& a : stepLevelAttachments_) a.reset();
    lfoSyncAttachment_.reset();
    flagAttachments_.clear();
    for (auto& k : knobs_) k.attach.reset();
}
std::size_t AcidPanel::attachedControlCount() const noexcept {
    std::size_t count = flagAttachments_.size();
    for (const auto& knob : knobs_)
        if (knob.attach != nullptr) ++count;
    return count;
}
int AcidPanel::modulationKnobDiameterForTesting() const noexcept {
    int minDiameter = std::numeric_limits<int>::max();
    for (const auto& knob : knobs_)
        if (knob.group == modulation && knob.attach && knob.widget->isVisible())
            minDiameter = std::min(minDiameter,
                std::min(knob.widget->getSlider().getWidth(),
                         knob.widget->getSlider().getHeight()));
    return minDiameter == std::numeric_limits<int>::max() ? 0 : minDiameter;
}
juce::String AcidPanel::knobTextForTesting(std::string_view semantic) const {
    for (const auto& knob : knobs_)
        if (knob.semantic == semantic && knob.attach)
            return knob.widget->getSlider().getTextFromValue(
                knob.widget->getSlider().getValue());
    return {};
}
bool AcidPanel::setKnobForTesting(std::string_view semantic, double normalized) {
    for (auto& knob : knobs_)
        if (knob.semantic == semantic && knob.attach) {
            knob.widget->getSlider().setValue(
                juce::jlimit(0.0, 1.0, normalized), juce::sendNotificationSync);
            return true;
        }
    return false;
}
void AcidPanel::setPreviewForTesting(bool enabled) {
    processor_.setAcidPreview(slot_, enabled);
    previewButton_.setButtonText(enabled ? "Stop" : "Preview");
}
bool AcidPanel::previewForTesting() const noexcept {
    return processor_.acidPreviewEnabled(slot_);
}
void AcidPanel::switchEnvelopeForTesting(bool filter) {
    filterEnvelope_ = filter;
    updateEnvelopeAttachments();
}
void AcidPanel::switchModulationForTesting(bool lfo1) {
    modSource_ = lfo1 ? ModSource::lfo1 : ModSource::env1;
    updateModulationAttachments();
    refreshChoices();
}
void AcidPanel::switchEnv2ForTesting() {
    modSource_ = ModSource::env2;
    updateModulationAttachments();
    refreshChoices();
}
void AcidPanel::switchLfoForTesting(int number) {
    if (number < 1 || number > 3) return;
    modSource_ = number == 1 ? ModSource::lfo1
               : number == 2 ? ModSource::lfo2 : ModSource::lfo3;
    updateModulationAttachments();
    refreshChoices();
}
bool AcidPanel::setCurrentLfoSyncForTesting(bool enabled) {
    if (!lfoSyncAttachment_ || !lfoSyncButton_.isVisible())
        return false;
    if (lfoSyncButton_.getToggleState() != enabled) {
        // triggerClick() posts an asynchronous click message. A synchronous
        // editor smoke assertion would observe the old button/APVTS state.
        // Send the same notification as a real toggle change, synchronously.
        lfoSyncButton_.setToggleState(enabled, juce::sendNotificationSync);
    }
    return lfoSyncButton_.getToggleState() == enabled;
}
void AcidPanel::switchStepForTesting() {
    modSource_ = ModSource::step;
    updateModulationAttachments();
    refreshChoices();
}
std::size_t AcidPanel::attachedStepLevelsForTesting() const noexcept {
    return static_cast<std::size_t>(std::count_if(
        stepLevelAttachments_.begin(), stepLevelAttachments_.end(),
        [](const auto& a) { return a != nullptr; }));
}
bool AcidPanel::setStepLevelForTesting(std::size_t index, double normalized) {
    if (index >= stepLevels_.size() || !stepLevelAttachments_[index])
        return false;
    stepLevels_[index].setValue(juce::jlimit(0.0, 1.0, normalized),
                                juce::sendNotificationSync);
    return true;
}
std::string_view AcidPanel::selectedModTarget() const noexcept {
    switch (modSource_) {
        case ModSource::env1: return "mod_target";
        case ModSource::env2: return "env2_target";
        case ModSource::lfo1: return "lfo1_target";
        case ModSource::lfo2: return "lfo2_target";
        case ModSource::lfo3: return "lfo3_target";
        case ModSource::step: return "step_target";
    }
    return "mod_target";
}
void AcidPanel::addKnob(Group group, std::string_view semantic, const char* name, bool stage) {
    Knob k;
    k.group = group;
    k.semantic = std::string(semantic);
    k.envelopeStage = stage;
    k.widget = std::make_unique<vox::ui::VoxKnob>(name, vox::ui::VoxKnob::Size::Small);
    k.widget->setTooltip("Acid " + juce::String(name) + ": " + juce::String(std::string(semantic)));
    // Values displayed by VoxKnob describe the actual normalized DSP map;
    // they are never fictional labels independent of the parameter.
    const auto target = k.semantic;
    k.widget->getSlider().textFromValueFunction = [target](double raw) {
        const auto v = static_cast<float>(juce::jlimit(0.0, 1.0, raw));
        if (target == "cutoff") {
            const auto hz = vstengine::acid::AcidEngine::cutoffNormalizedToHz(v);
            return hz >= 1000.0f ? juce::String(hz / 1000.0f, 1) + "kHz"
                                 : juce::String(hz, 0) + "Hz";
        }
        if (target == "osc_tune")
            return juce::String(static_cast<int>(std::round((v - 0.5f) * 48.0f))) + "st";
        if (target == "osc_fine")
            return juce::String(static_cast<int>(std::round((v - 0.5f) * 200.0f))) + "ct";
        if (target == "unison_voices")
            return juce::String(1 + static_cast<int>(std::round(v * 3.0f)));
        if (target == "output")
            return v <= 0.0f ? juce::String("-inf")
                             : juce::String(20.0f * std::log10(v), 1) + "dB";
        if (target == "pan")
            return std::abs(v - 0.5f) < 0.001f ? juce::String("C")
                             : (v < 0.5f ? juce::String("L") : juce::String("R"))
                                + juce::String(static_cast<int>(
                                     std::round(std::abs(v - 0.5f) * 200.0f)));
        if (target == "amp_attack" || target == "flt_attack")
            return juce::String((0.0005f + v * v * 0.8f) * 1000.0f, 0) + "ms";
        if (target == "amp_decay" || target == "decay")
            return juce::String(0.015f + v * v * 2.2f, 2) + "s";
        if (target == "amp_release")
            return juce::String(0.004f + v * v * 1.8f, 2) + "s";
        if (target == "flt_release")
            return juce::String(0.010f + v * v * 1.5f, 2) + "s";
        if (target == "portamento" || target == "slide")
            return juce::String(6.0f + v * 240.0f, 0) + "ms";
        return juce::String(v * 100.0f, 0) + "%";
    };
    addAndMakeVisible(*k.widget);
    knobs_.push_back(std::move(k));
}
std::string AcidPanel::hostId(std::string_view semantic) const {
    return macroOrAcidId(slot_, semantic);
}
void AcidPanel::bind(std::size_t slot) {
    slot_ = juce::jlimit<std::size_t>(0, vstengine::instrument::maxSlots - 1, slot);
    previewButton_.setButtonText(processor_.acidPreviewEnabled(slot_)
        ? "Stop" : "Preview");
    for (auto& knob : knobs_) knob.attach.reset();
    flagAttachments_.clear();
    lfoSyncAttachment_.reset();
    for (auto& attachment : stepLevelAttachments_) attachment.reset();
    attachKnobs();
    constexpr std::array<const char*, 4> flagNames { "legato", "retrigger",
                                                       "vel_to_accent", "slide_on_overlap" };
    constexpr std::array<const char*, 4> captions { "Legato", "Retrigger",
                                                     "Vel -> Accent", "Slide overlap" };
    for (std::size_t i = 0; i < flagNames.size(); ++i) {
        flags_[i].setButtonText(captions[i]);
        flagAttachments_.push_back(std::make_unique<
            juce::AudioProcessorValueTreeState::ButtonAttachment>(
                processor_.parameters(),
                vstengine::acid::hostParameterId(slot_, flagNames[i]), flags_[i]));
    }
    const auto& seq = processor_.sequence();
    lengthChoice_.setText(juce::String(seq.size()), juce::dontSendNotification);
    timingChoice_.setSelectedId(static_cast<int>(seq.getTimingMode()) + 1,
                                juce::dontSendNotification);
    refreshChoices();
    steps_.repaint();
}
// Single owner for all Acid rotary APVTS bindings. In addition to host
// automation, this preserves Acid's semantic unit captions: JUCE's attachment
// installs its own numeric formatter, which otherwise replaces Hz/dB/note text.
void AcidPanel::attachKnob(Knob& knob) {
    auto& slider = knob.widget->getSlider();
    const auto parameterId = hostId(knob.semantic);
    const auto displayText = slider.textFromValueFunction;
    knob.attach.reset();
    knob.attach = std::make_unique<
        juce::AudioProcessorValueTreeState::SliderAttachment>(
            processor_.parameters(), parameterId, slider);
    if (displayText)
        slider.textFromValueFunction = displayText;
    if (auto* parameter = processor_.parameters().getParameter(parameterId.c_str()))
        knob.widget->setDoubleClickResetValue(parameter->getDefaultValue());
}
void AcidPanel::attachKnobs() {
    for (auto& knob : knobs_)
        if (!knob.envelopeStage && knob.group != modulation)
            attachKnob(knob);
    updateEnvelopeAttachments();
    updateModulationAttachments();
}
void AcidPanel::updateModulationAttachments() {
    constexpr std::array<const char*, 5> env1Ids {
        "mod_amount", "mod_attack", "mod_decay", "mod_sustain", "mod_release"
    };
    constexpr std::array<const char*, 5> env2Ids {
        "env2_amount", "env2_attack", "env2_decay", "env2_sustain", "env2_release"
    };
    constexpr std::array<const char*, 5> envLabels {
        "Amount", "Attack", "Decay", "Sustain", "Release"
    };
    constexpr std::array<const char*, 3> lfoLabels { "Rate", "Depth", "Wave" };
    constexpr std::array<std::array<const char*, 3>, 3> lfoIds {{
        {{ "lfo1_rate", "lfo1_depth", "lfo1_wave" }},
        {{ "lfo2_rate", "lfo2_depth", "lfo2_wave" }},
        {{ "lfo3_rate", "lfo3_depth", "lfo3_wave" }}
    }};
    const bool isStep = modSource_ == ModSource::step;
    const bool isLfo = modSource_ == ModSource::lfo1
                    || modSource_ == ModSource::lfo2
                    || modSource_ == ModSource::lfo3;
    const auto lfoIndex = modSource_ == ModSource::lfo2 ? 1u
                        : modSource_ == ModSource::lfo3 ? 2u : 0u;
    lfoSyncAttachment_.reset();
    lfoSyncButton_.setVisible(isLfo);
    if (isLfo) {
        constexpr std::array<const char*, 3> syncIds {
            "lfo1_sync", "lfo2_sync", "lfo3_sync"
        };
        lfoSyncAttachment_ = std::make_unique<
            juce::AudioProcessorValueTreeState::ButtonAttachment>(
                processor_.parameters(), hostId(syncIds[lfoIndex]),
                lfoSyncButton_);
    }
    std::size_t index = 0;
    for (auto& k : knobs_) {
        if (k.group != modulation) continue;
        // Source tabs do not share APVTS attachments, and hidden controls
        // must never retain an automation gesture for an unrelated source.
        k.attach.reset();
        const bool active = isStep ? index < 3
                          : (!isLfo || index < lfoLabels.size());
        k.widget->setVisible(active);
        if (active) {
            constexpr std::array<const char*, 3> stepIds {
                "step_amount", "step_rate", "step_smooth"
            };
            constexpr std::array<const char*, 3> stepLabels {
                "Depth", "Rate", "Smooth"
            };
            k.semantic = isStep ? stepIds[index]
                       : (isLfo ? lfoIds[lfoIndex][index]
                         : (modSource_ == ModSource::env2
                                ? env2Ids[index] : env1Ids[index]));
            k.widget->setLabel(isStep ? stepLabels[index]
                              : (isLfo ? lfoLabels[index] : envLabels[index]));
            if (isStep) {
                const auto semantic = k.semantic;
                k.widget->getSlider().textFromValueFunction =
                    [semantic](double value) {
                        const auto v = static_cast<float>(juce::jlimit(0.0, 1.0, value));
                        if (semantic == "step_rate") {
                            constexpr std::array<const char*, 4> notes {
                                "1/8", "1/16", "1/32", "1/8T"
                            };
                            return juce::String(notes[juce::jlimit(
                                0, 3, static_cast<int>(std::round(v * 3.0f)))]);
                        }
                        return juce::String(v * 100.0f, 0) + "%";
                    };
            } else if (isLfo) {
                const auto semantic = k.semantic;
                k.widget->getSlider().textFromValueFunction =
                    [this, semantic](double norm) {
                        const float v = static_cast<float>(juce::jlimit(0.0, 1.0, norm));
                        if (semantic.ends_with("_rate")) {
                            if (lfoSyncButton_.getToggleState()) {
                                constexpr std::array<const char*, 8> divisions {
                                    "1/1", "1/2", "1/4", "1/8",
                                    "1/16", "1/8T", "1/16T", "1/32"
                                };
                                return juce::String(divisions[juce::jlimit(
                                    0, 7, static_cast<int>(std::round(v * 7.0f)))]);
                            }
                            return juce::String(0.05f * std::pow(400.0f, v), 2) + " Hz";
                        }
                        if (semantic.ends_with("_wave")) {
                            constexpr std::array<const char*, 4> waves {
                                "Sine", "Triangle", "Square", "Saw"
                            };
                            return juce::String(waves[juce::jlimit(
                                0, 3, static_cast<int>(std::round(v * 3.0f)))]);
                        }
                        return juce::String(v * 100.0f, 0) + "%";
                    };
            } else {
                k.widget->getSlider().textFromValueFunction = [](double normalized) {
                    return juce::String(
                        juce::jlimit(0.0, 100.0, normalized * 100.0), 0) + "%";
                };
            }
            attachKnob(k);
        }
        ++index;
    }
    env1Button_.setToggleState(modSource_ == ModSource::env1,
                                juce::dontSendNotification);
    env2Button_.setToggleState(modSource_ == ModSource::env2,
                                juce::dontSendNotification);
    lfo1Button_.setToggleState(modSource_ == ModSource::lfo1,
                                juce::dontSendNotification);
    lfo2Button_.setToggleState(modSource_ == ModSource::lfo2,
                                juce::dontSendNotification);
    lfo3Button_.setToggleState(modSource_ == ModSource::lfo3,
                                juce::dontSendNotification);
    stepButton_.setToggleState(isStep, juce::dontSendNotification);
    // Editable bar levels are independently automated APVTS parameters.
    // Only STEP has these attachments: inactive source tabs cannot move them.
    for (std::size_t step = 0; step < stepLevels_.size(); ++step) {
        stepLevelAttachments_[step].reset();
        stepLevels_[step].setVisible(isStep);
        if (!isStep) continue;
        const auto semantic = vstengine::acid::AcidEngine::parameterIds[
            vstengine::acid::AcidEngine::step01 + step];
        const auto id = hostId(semantic);
        stepLevelAttachments_[step] = std::make_unique<
            juce::AudioProcessorValueTreeState::SliderAttachment>(
                processor_.parameters(), id, stepLevels_[step]);
    }
    if (!sections_[modulation].isEmpty()) {
        auto area = section(modulation).reduced(3).withTrimmedTop(22);
        if (isStep) area.removeFromBottom(std::min(42, area.getHeight() / 2));
        layoutKnobs(modulation, area);
        resized();
        repaint(sections_[modulation]);
    }
}
void AcidPanel::updateEnvelopeAttachments() {
    constexpr std::array<const char*, 4> ampIds {
        "amp_attack", "amp_decay", "amp_sustain", "amp_release"
    };
    constexpr std::array<const char*, 4> filterIds {
        "flt_attack", "decay", "flt_sustain", "flt_release"
    };
    std::size_t stage = 0;
    for (auto& k : knobs_) {
        if (!k.envelopeStage) continue;
        k.attach.reset();
        k.semantic = filterEnvelope_ ? filterIds[stage] : ampIds[stage];
        const auto stageIndex = stage;
        const bool isFilter = filterEnvelope_;
        k.widget->getSlider().textFromValueFunction =
            [stageIndex, isFilter](double normalized) {
                const auto v = static_cast<float>(juce::jlimit(0.0, 1.0, normalized));
                if (stageIndex == 0)
                    return juce::String((0.0005f + v * v * 0.8f) * 1000.0f, 0) + "ms";
                if (stageIndex == 1) {
                    const float seconds = isFilter ? 0.03f + v * v * 2.2f
                                                   : 0.015f + v * v * 2.2f;
                    return juce::String(seconds, 2) + "s";
                }
                if (stageIndex == 2) return juce::String(v * 100.0f, 0) + "%";
                const float seconds = isFilter ? 0.010f + v * v * 1.5f
                                               : 0.004f + v * v * 1.8f;
                return juce::String(seconds, 2) + "s";
            };
        attachKnob(k);
        ++stage;
    }
    ampButton_.setToggleState(!filterEnvelope_, juce::dontSendNotification);
    filterButton_.setToggleState(filterEnvelope_, juce::dontSendNotification);
    repaint(sections_[envelope]);
}
void AcidPanel::changeChoice(const std::string& name, int index, int count) {
    if (index < 0 || count < 2) return;
    const auto id = hostId(name);
    if (auto* param = processor_.parameters().getParameter(id.c_str()))
        param->setValueNotifyingHost(static_cast<float>(index) /
                                     static_cast<float>(count - 1));
}
void AcidPanel::refreshChoices() {
    auto refresh = [this](juce::ComboBox& box, std::string_view semantic, int n) {
        const auto id = hostId(semantic);
        if (const auto* raw = processor_.parameters().getRawParameterValue(id.c_str())) {
            const int selected = juce::jlimit(0, n - 1,
                static_cast<int>(std::round(raw->load() * static_cast<float>(n - 1))));
            box.setSelectedId(selected + 1, juce::dontSendNotification);
        }
    };
    refresh(filterChoice_, "filter_mode", 4);
    refresh(distortionChoice_, "dist_mode", 4);
    refresh(modulationChoice_, selectedModTarget(), 6);
}
void AcidPanel::timerCallback() {
    if (!isVisible()) return;
    const bool preview = processor_.acidPreviewEnabled(slot_);
    const auto label = preview ? juce::String("Stop") : juce::String("Preview");
    if (previewButton_.getButtonText() != label)
        previewButton_.setButtonText(label);
    refreshChoices();
    steps_.setHead(processor_.getCurrentPlayHeadStep());
    if (lengthChoice_.hasKeyboardFocus(true) || timingChoice_.hasKeyboardFocus(true))
        return;
    const auto& seq = processor_.sequence();
    lengthChoice_.setText(juce::String(seq.size()), juce::dontSendNotification);
    timingChoice_.setSelectedId(static_cast<int>(seq.getTimingMode()) + 1,
                                juce::dontSendNotification);
    repaint(sections_[envelope]);
}
juce::Rectangle<int> AcidPanel::section(Group group) const noexcept {
    return sections_[static_cast<std::size_t>(group)];
}
void AcidPanel::layoutKnobs(Group group, juce::Rectangle<int> box) {
    int count = 0;
    for (const auto& k : knobs_)
        if (k.group == group && k.widget->isVisible()) ++count;
    if (count <= 0) return;
    const int width = std::max(1, box.getWidth() / count);
    int index = 0;
    for (auto& k : knobs_) if (k.group == group && k.widget->isVisible()) {
        k.widget->setBounds(box.getX() + index * width, box.getY(),
                            width, box.getHeight());
        ++index;
    }
}
void AcidPanel::resized() {
    auto area = getLocalBounds().reduced(2);
    const bool compact = area.getHeight() < 475;
    const int heroHeight = compact ? 39 : 81;
    const int topHeight = compact ? 108 : 145;
    const int bottomHeight = compact ? 94 : 126;
    area.removeFromTop(heroHeight);
    auto top = area.removeFromTop(std::min(topHeight, area.getHeight() / 3 + 10));
    area.removeFromTop(5);
    auto bottom = area.removeFromBottom(std::min(bottomHeight, area.getHeight() / 3));
    area.removeFromBottom(5);
    sections_[grid] = area;
    constexpr std::array<int, 7> weights { 18, 18, 18, 14, 9, 9, 14 };
    const int fullWidth = top.getWidth();
    for (int i = 0; i < 7; ++i) {
        const int w = i == 6 ? top.getWidth()
                             : fullWidth * weights[static_cast<std::size_t>(i)] / 100;
        sections_[static_cast<std::size_t>(i)] =
            top.removeFromLeft(juce::jlimit(1, top.getWidth(), w));
    }
    sections_[modulation] = bottom.removeFromLeft(bottom.getWidth() * 52 / 100);
    sections_[performance] = bottom.removeFromLeft(bottom.getWidth() * 65 / 100);
    sections_[playMode] = bottom;
    auto controlArea = [this](Group group, int selectorHeight) {
        auto b = section(group).reduced(3);
        b.removeFromTop(22 + selectorHeight);
        return b;
    };
    filterChoice_.setBounds(section(filter).reduced(6).removeFromTop(24)
                           .translated(0, 23));
    distortionChoice_.setBounds(section(drive).reduced(6).removeFromTop(23)
                               .translated(0, 23));
    const auto modBounds = section(modulation);
    // Keep five actual source tabs and a real target selector in one row.
    // No new vertical strip: 1024x768 must preserve rotary knob hit areas.
    const int tabWidth = 36;
    const int x0 = modBounds.getX() + 78;
    env1Button_.setBounds(x0, modBounds.getY() + 2, tabWidth - 1, 18);
    env2Button_.setBounds(x0 + tabWidth, modBounds.getY() + 2, tabWidth - 1, 18);
    lfo1Button_.setBounds(x0 + tabWidth * 2, modBounds.getY() + 2, tabWidth - 1, 18);
    lfo2Button_.setBounds(x0 + tabWidth * 3, modBounds.getY() + 2, tabWidth - 1, 18);
    lfo3Button_.setBounds(x0 + tabWidth * 4, modBounds.getY() + 2, tabWidth - 1, 18);
    stepButton_.setBounds(x0 + tabWidth * 5, modBounds.getY() + 2, tabWidth - 1, 18);
    modulationChoice_.setBounds(x0 + tabWidth * 6 + 2, modBounds.getY() + 2,
                                std::max(35, modBounds.getRight() -
                                    (x0 + tabWidth * 6 + 7)), 18);
    ampButton_.setBounds(section(envelope).getX() + 41,
                          section(envelope).getY() + 2, 38, 19);
    filterButton_.setBounds(section(envelope).getX() + 82,
                            section(envelope).getY() + 2, 48, 19);
    for (int i = 0; i < 7; ++i) {
        auto group = static_cast<Group>(i);
        auto inner = controlArea(group, group == filter || group == drive ? 25 : 0);
        if (group == envelope)
            inner.removeFromTop(compact ? 24 : 36);
        layoutKnobs(group, inner);
    }
    auto modControlArea = controlArea(modulation, 0);
    const bool lfoTab = modSource_ == ModSource::lfo1
                     || modSource_ == ModSource::lfo2
                     || modSource_ == ModSource::lfo3;
    if (lfoTab) {
        auto toggleArea = modControlArea.removeFromRight(
            std::min(61, modControlArea.getWidth() / 5));
        lfoSyncButton_.setBounds(toggleArea.reduced(2).removeFromTop(29));
    }
    const bool stepTab = modSource_ == ModSource::step;
    if (stepTab) {
        auto barArea = modControlArea.removeFromBottom(
            std::min(42, modControlArea.getHeight() / 2));
        barArea = barArea.reduced(1, 0);
        const int width = std::max(1, barArea.getWidth() / 16);
        for (std::size_t index = 0; index < stepLevels_.size(); ++index)
            stepLevels_[index].setBounds(barArea.getX() + static_cast<int>(index) * width,
                                         barArea.getY(), width - 1, barArea.getHeight());
    }
    layoutKnobs(modulation, modControlArea);
    layoutKnobs(performance, controlArea(performance, 0));
    auto modeArea = section(playMode).reduced(4);
    modeArea.removeFromTop(23);
    const int each = std::max(1, modeArea.getHeight() / 4);
    for (auto& f : flags_) f.setBounds(modeArea.removeFromTop(each));
    auto gridArea = section(grid).reduced(3);
    auto toolbar = gridArea.removeFromTop(compact ? 25 : 30);
    profileChoice_.setBounds(toolbar.removeFromLeft(std::max(70, toolbar.getWidth()/4)).reduced(1));
    lengthChoice_.setBounds(toolbar.removeFromRight(50).reduced(1));
    timingChoice_.setBounds(toolbar.removeFromRight(65).reduced(1));
    clear_.setBounds(toolbar.removeFromRight(48).reduced(1));
    previewButton_.setBounds(toolbar.removeFromLeft(56).reduced(1));
    mutate_.setBounds(toolbar.removeFromRight(55).reduced(1));
    generate_.setBounds(toolbar.removeFromRight(70).reduced(1));
    status_.setBounds(toolbar);
    steps_.setBounds(gridArea);
}
void AcidPanel::paint(juce::Graphics& g) {
    g.fillAll(vstengine::ui::colours::background);
    const auto h = getLocalBounds().reduced(3).removeFromTop(
        getHeight() < 475 ? 37 : 78);
    g.setGradientFill(juce::ColourGradient(
        juce::Colour::fromRGB(4, 74, 81), h.getTopLeft().toFloat(),
        juce::Colour::fromRGB(5, 20, 33), h.getBottomRight().toFloat(), false));
    g.fillRoundedRectangle(h.toFloat(), 5.0f);
    g.setColour(acidGreenColour);
    g.setFont(juce::Font(juce::FontOptions(getHeight() < 475 ? 22.0f : 37.0f,
                                           juce::Font::bold)));
    g.drawText("ACID", h.reduced(15, 3).removeFromLeft(160),
               juce::Justification::centredLeft);
    g.setColour(dimText);
    g.setFont(juce::Font(juce::FontOptions(10.0f)));
    g.drawText("303   /   PSY ACID   /   SOUND DESIGN", h.reduced(170, 5),
               juce::Justification::centredLeft);
    constexpr std::array<const char*, 11> titles {
        "OSCILLATOR", "FILTER", "ENVELOPE", "DISTORTION / DRIVE",
        "ACCENT", "SLIDE", "OUTPUT", "MODULATION",
        "PERFORMANCE", "PLAY MODE", "16-STEP SEQUENCER"
    };
    for (int i = 0; i < groupCount; ++i)
        drawCard(g, sections_[static_cast<std::size_t>(i)], titles[static_cast<std::size_t>(i)]);

    // Display actual, host-automated Amp or Filter ADSR values. The plotted
    // time axes are normalized to the active envelope's duration, never
    // a hard-coded decorative curve that disagrees with the sound controls.
    auto plot = section(envelope).reduced(6);
    plot.removeFromTop(25);
    plot.setHeight(getHeight() < 475 ? 23 : 33);
    if (plot.getWidth() > 20 && plot.getHeight() > 10) {
        auto read = [this](const char* semantic, float fallback) {
            const auto id = hostId(semantic);
            if (const auto* raw = processor_.parameters().getRawParameterValue(id.c_str()))
                return juce::jlimit(0.0f, 1.0f, raw->load());
            return fallback;
        };
        const float a = read(filterEnvelope_ ? "flt_attack" : "amp_attack", 0.0f);
        const float d = read(filterEnvelope_ ? "decay" : "amp_decay", 0.2f);
        const float sustain = read(filterEnvelope_ ? "flt_sustain" : "amp_sustain", 0.8f);
        const float r = read(filterEnvelope_ ? "flt_release" : "amp_release", 0.2f);
        const float attackSeconds = 0.0005f + a * a * 0.8f;
        const float decaySeconds = (filterEnvelope_ ? 0.030f : 0.015f) + d * d * 2.2f;
        const float releaseSeconds = (filterEnvelope_ ? 0.010f : 0.004f)
                                   + r * r * (filterEnvelope_ ? 1.5f : 1.8f);
        const float holdSeconds = 0.22f; // graph convention: illustrative sustain hold
        const float total = attackSeconds + decaySeconds + holdSeconds + releaseSeconds;
        const auto left = static_cast<float>(plot.getX());
        const auto right = static_cast<float>(plot.getRight());
        const auto top = static_cast<float>(plot.getY());
        const auto bottom = static_cast<float>(plot.getBottom());
        const auto width = right - left;
        auto pointX = [left, width, total](float seconds) {
            return left + width * (seconds / total);
        };
        const auto attackX = pointX(attackSeconds);
        const auto decayX = pointX(attackSeconds + decaySeconds);
        const auto releaseX = pointX(attackSeconds + decaySeconds + holdSeconds);
        const auto sustainY = bottom - sustain * (bottom - top);
        juce::Path curve;
        curve.startNewSubPath(left, bottom);
        curve.lineTo(attackX, top);
        curve.lineTo(decayX, sustainY);
        curve.lineTo(releaseX, sustainY);
        curve.lineTo(right, bottom);
        g.setColour(acidAccentColour.withAlpha(0.65f));
        g.strokePath(curve, juce::PathStrokeType(1.6f));
        for (const auto point : { juce::Point<float>(attackX, top),
                                  juce::Point<float>(decayX, sustainY),
                                  juce::Point<float>(releaseX, sustainY) })
            g.fillEllipse(point.x - 2.0f, point.y - 2.0f, 4.0f, 4.0f);
    }
}
