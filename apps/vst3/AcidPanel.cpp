#include "AcidPanel.h"
#include "PluginProcessor.h"
#include "acid/AcidEngine.h"
#include "acid/AcidParameterBridge.h"
#include "instrument/HostParameterSchema.h"
#include "ui/common/UiComponents.h"
#include <algorithm>
#include <cmath>

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
                    note, true, true, 4) : "--";
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
        changeChoice("mod_target", modulationChoice_.getSelectedItemIndex(), 6);
    };
    for (auto* button : { &ampButton_, &filterButton_, &generate_, &mutate_, &clear_ }) {
        vstengine::ui::styleButton(*button);
        addAndMakeVisible(*button);
    }
    ampButton_.onClick = [this] { filterEnvelope_ = false; updateEnvelopeAttachments(); };
    filterButton_.onClick = [this] { filterEnvelope_ = true; updateEnvelopeAttachments(); };
    for (int i = 0; i < 4; ++i) addAndMakeVisible(flags_[static_cast<std::size_t>(i)]);
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
AcidPanel::~AcidPanel() { stopTimer(); flagAttachments_.clear(); for (auto& c : knobs_) c.attach.reset(); }
std::size_t AcidPanel::attachedControlCount() const noexcept {
    std::size_t count = flagAttachments_.size();
    for (const auto& knob : knobs_)
        if (knob.attach != nullptr) ++count;
    return count;
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
void AcidPanel::switchEnvelopeForTesting(bool filter) {
    filterEnvelope_ = filter;
    updateEnvelopeAttachments();
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
    for (auto& knob : knobs_) knob.attach.reset();
    flagAttachments_.clear();
    attachKnobs();
    constexpr std::array<const char*, 4> flags { "legato", "retrigger",
                                                   "vel_to_accent", "slide_on_overlap" };
    constexpr std::array<const char*, 4> captions { "Legato", "Retrigger",
                                                     "Vel -> Accent", "Slide overlap" };
    for (std::size_t i = 0; i < flags.size(); ++i) {
        flags_[i].setButtonText(captions[i]);
        flagAttachments_.push_back(std::make_unique<
            juce::AudioProcessorValueTreeState::ButtonAttachment>(
                processor_.parameters(),
                vstengine::acid::hostParameterId(slot_, flags[i]), flags_[i]));
    }
    const auto& seq = processor_.sequence();
    lengthChoice_.setText(juce::String(seq.size()), juce::dontSendNotification);
    timingChoice_.setSelectedId(static_cast<int>(seq.getTimingMode()) + 1,
                                juce::dontSendNotification);
    refreshChoices();
    steps_.repaint();
}
void AcidPanel::attachKnobs() {
    for (auto& k : knobs_) {
        if (k.envelopeStage) continue;
        auto id = hostId(k.semantic);
        k.attach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            processor_.parameters(), id, k.widget->getSlider());
        if (auto* param = processor_.parameters().getParameter(id.c_str()))
            k.widget->setDoubleClickResetValue(param->getDefaultValue());
    }
    updateEnvelopeAttachments();
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
        const auto id = hostId(k.semantic);
        k.attach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            processor_.parameters(), id, k.widget->getSlider());
        if (auto* param = processor_.parameters().getParameter(id.c_str()))
            k.widget->setDoubleClickResetValue(param->getDefaultValue());
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
    refresh(modulationChoice_, "mod_target", 6);
}
void AcidPanel::timerCallback() {
    if (!isVisible()) return;
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
    for (const auto& k : knobs_) if (k.group == group) ++count;
    if (count <= 0) return;
    const int width = std::max(1, box.getWidth() / count);
    int index = 0;
    for (auto& k : knobs_) if (k.group == group) {
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
    modulationChoice_.setBounds(section(modulation).reduced(6).removeFromTop(22)
                               .translated(0, 23));
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
    layoutKnobs(modulation, controlArea(modulation, 25));
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
        "ACCENT", "SLIDE", "OUTPUT", "MODULATION (ENV 1)",
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
