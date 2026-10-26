#include "PluginEditor.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace vstengine::vst3 {

namespace {

const juce::Colour kShellBg        { 0xff080b10 };
const juce::Colour kPanelBg        { 0xff0d121a };
const juce::Colour kSlotBg         { 0xff121924 };
const juce::Colour kSlotSelectedBg { 0xff142536 };
const juce::Colour kBorderSubtle   { 0xff1f2c3f };
const juce::Colour kCyanGlow       { 0xff00d2ff };
const juce::Colour kTealWave       { 0xff00f59b };
const juce::Colour kPurpleWave     { 0xffc850ff };
const juce::Colour kPanicRed       { 0xffb8293d };
const juce::Colour kTextPrimary    { 0xffedf2f7 };
const juce::Colour kTextMuted      { 0xff7f8fa6 };

} // namespace

VstEngineAudioProcessorEditor::VstEngineAudioProcessorEditor(
    VstEngineAudioProcessor& processor)
    : AudioProcessorEditor(&processor),
      processorRef_(processor) {
    setLookAndFeel(&lookAndFeel_);

    // Initialize 16 Rack Slots matching the V2 UI Mockup
    for (std::size_t i = 0; i < slots_.size(); ++i) {
        const int num = static_cast<int>(i + 1);
        slots_[i].number = (num < 10 ? "0" : "") + juce::String(num);
        slots_[i].title = "Empty";
        slots_[i].subtitle = "";
        slots_[i].channel = "OFF";
        slots_[i].instrumentId = "";
        slots_[i].waveColour = kTextMuted;
        slots_[i].active = false;
    }

    slots_[0] = { "01", "Psy Bass",        "Ultima Vox 1.0",   "CH1", "com.ultimavox.bass",  kCyanGlow,   true };
    slots_[1] = { "02", "Acid",            "Analog Drive 1.0", "CH2", "com.ultimavox.acid",  kTealWave,   true };
    slots_[2] = { "03", "Lead",            "Surpa Lead 1.0",   "CH3", "com.ultimavox.lead",  kPurpleWave, true };
    slots_[3] = { "04", "Atmos / Texture", "Ethereal 1.0",     "CH4", "com.ultimavox.atmos", kCyanGlow,   true };

    // Top Header Controls
    projectPresetBox_.addItem("Init Project", 1);
    projectPresetBox_.addItem("Trance Template 138 BPM", 2);
    projectPresetBox_.addItem("Acid Goa Session", 3);
    projectPresetBox_.setSelectedId(1, juce::dontSendNotification);
    addAndMakeVisible(projectPresetBox_);

    addAndMakeVisible(saveProjectBtn_);

    seedValueLabel_.setText(juce::String(globalSeed_), juce::dontSendNotification);
    seedValueLabel_.setJustificationType(juce::Justification::centred);
    seedValueLabel_.setColour(juce::Label::backgroundColourId, kSlotBg);
    seedValueLabel_.setColour(juce::Label::textColourId, kTextPrimary);
    addAndMakeVisible(seedValueLabel_);

    randomSeedBtn_.onClick = [this]() { randomizeGlobalSeed(); };
    addAndMakeVisible(randomSeedBtn_);

    masterOutputKnob_.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    masterOutputKnob_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    masterOutputKnob_.setRange(0.0, 1.0, 0.01);
    masterOutputKnob_.setValue(0.66, juce::dontSendNotification);
    addAndMakeVisible(masterOutputKnob_);

    panicButton_.setColour(juce::TextButton::buttonColourId, kPanicRed);
    panicButton_.setColour(juce::TextButton::textColourOffId, kTextPrimary);
    panicButton_.onClick = [this]() { triggerPanic(); };
    addAndMakeVisible(panicButton_);

    addAndMakeVisible(settingsButton_);

    // Instantiate Slot Panels (BassPanel & AcidPanel)
    // Pass &processorRef_.apvts() if your PluginProcessor exposes apvts()
    acidPanel_ = std::make_unique<ui::AcidPanel>(nullptr, 1, globalSeed_);
    acidPanel_->onSequenceChanged([this](const sequence::Sequence& seq) {
        // Forward updated 16-step Acid sequence to processor scheduler for slotIndex
        juce::ignoreUnused(seq);
        // Example hook into processor:
        // processorRef_.publishSlotSequence(selectedSlotIndex_, seq);
    });
    addAndMakeVisible(*acidPanel_);

    // Bottom Keyboard Bar Controls
    keyboardTabBtn_.setColour(juce::TextButton::buttonColourId, kCyanGlow);
    keyboardTabBtn_.setColour(juce::TextButton::textColourOffId, kShellBg);
    addAndMakeVisible(keyboardTabBtn_);
    addAndMakeVisible(chordsTabBtn_);
    addAndMakeVisible(scaleTabBtn_);

    velocityCurveBox_.addItem("Normal", 1);
    velocityCurveBox_.addItem("Soft", 2);
    velocityCurveBox_.addItem("Hard / Accent", 3);
    velocityCurveBox_.setSelectedId(1, juce::dontSendNotification);
    addAndMakeVisible(velocityCurveBox_);
    addAndMakeVisible(midiLearnBtn_);

    // Set initial window size matching the 16:11 high-DPI V2 layout
    setSize(1280, 860);
    selectRackSlot(1); // Open Slot 02 Acid by default
    startTimerHz(30);
}

VstEngineAudioProcessorEditor::~VstEngineAudioProcessorEditor() {
    stopTimer();
    setLookAndFeel(nullptr);
}

void VstEngineAudioProcessorEditor::selectRackSlot(std::size_t slotIndex) {
    if (slotIndex >= slots_.size()) {
        return;
    }
    selectedSlotIndex_ = slotIndex;
    updateActiveSlotView();
    repaint();
}

void VstEngineAudioProcessorEditor::updateActiveSlotView() {
    const auto& slot = slots_[selectedSlotIndex_];

    if (acidPanel_ != nullptr) {
        const bool isAcidSlot =
            (slot.instrumentId == "com.ultimavox.acid" || selectedSlotIndex_ == 1);
        acidPanel_->setVisible(isAcidSlot);
        if (isAcidSlot) {
            acidPanel_->setSlotIndex(selectedSlotIndex_);
            acidPanel_->setBounds(moduleWorkspaceBounds_.toNearestInt());
        }
    }

    if (bassPanel_ != nullptr) {
        const bool isBassSlot = (slot.instrumentId == "com.ultimavox.bass");
        bassPanel_->setVisible(isBassSlot);
        if (isBassSlot) {
            bassPanel_->setBounds(moduleWorkspaceBounds_.toNearestInt());
        }
    }
}

void VstEngineAudioProcessorEditor::randomizeGlobalSeed() {
    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<std::uint32_t> dist(1000u, 99999u);
    globalSeed_ = dist(rng);
    seedValueLabel_.setText(juce::String(globalSeed_), juce::dontSendNotification);

    if (acidPanel_ != nullptr) {
        acidPanel_->setGlobalSeed(globalSeed_);
    }
}

void VstEngineAudioProcessorEditor::triggerPanic() {
    // Example hook into processor:
    // processorRef_.panicAllSlots();
    if (acidPanel_ != nullptr) {
        acidPanel_->setPlayheadStep(-1);
    }
}

void VstEngineAudioProcessorEditor::timerCallback() {
    // Sync playhead step from host transport / GeneratedNoteScheduler if playing
}

void VstEngineAudioProcessorEditor::mouseDown(const juce::MouseEvent& event) {
    const auto pos = event.position;
    for (std::size_t i = 0; i < slotCardBounds_.size(); ++i) {
        if (slotCardBounds_[i].contains(pos)) {
            selectRackSlot(i);
            return;
        }
    }
}

void VstEngineAudioProcessorEditor::drawTopHeaderBar(
    juce::Graphics& g, juce::Rectangle<float> bounds) const {
    g.setColour(kPanelBg);
    g.fillRect(bounds);
    g.setColour(kBorderSubtle);
    g.drawHorizontalLine(static_cast<int>(bounds.getBottom() - 1.0f),
                         bounds.getX(), bounds.getRight());

    // Brand Title: "VOX ELECTRONIC ENGINE" + "CREATE · EVOLVE · TRANSCEND"
    g.setColour(kTextPrimary);
    g.setFont(juce::FontOptions(17.0f, juce::Font::bold));
    g.drawText("VOX ELECTRONIC ENGINE", 48, 6, 250, 20,
               juce::Justification::centredLeft, false);

    g.setColour(kCyanGlow.withAlpha(0.80f));
    g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
    g.drawText("C R E A T E    E V O L V E    T R A N S C E N D", 48, 24, 250, 14,
               juce::Justification::centredLeft, false);

    // Seed & CPU labels
    g.setColour(kTextMuted);
    g.setFont(juce::FontOptions(11.5f));
    g.drawText("Seed", static_cast<int>(bounds.getCentreX() - 20.0f), 10, 36, 24,
               juce::Justification::centredLeft, false);
    g.drawText("MIDI", static_cast<int>(bounds.getRight() - 405.0f), 10, 32, 24,
               juce::Justification::centredLeft, false);
    g.setColour(kCyanGlow);
    g.fillEllipse(bounds.getRight() - 370.0f, 18.0f, 7.0f, 7.0f);

    g.setColour(kTextMuted);
    g.drawText("CPU 4%", static_cast<int>(bounds.getRight() - 350.0f), 10, 52, 24,
               juce::Justification::centredLeft, false);
    g.drawText("Output", static_cast<int>(bounds.getRight() - 250.0f), 10, 44, 24,
               juce::Justification::centredLeft, false);
}

void VstEngineAudioProcessorEditor::drawInstrumentRack(
    juce::Graphics& g, juce::Rectangle<float> bounds) const {
    g.setColour(kPanelBg);
    g.fillRoundedRectangle(bounds, 5.0f);
    g.setColour(kBorderSubtle);
    g.drawRoundedRectangle(bounds, 5.0f, 1.0f);

    g.setColour(kTextPrimary);
    g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    g.drawText("INSTRUMENT RACK",
               bounds.withHeight(28.0f).withTrimmedLeft(10.0f),
               juce::Justification::centredLeft, false);

    g.setColour(kTextMuted);
    g.setFont(juce::FontOptions(11.0f));
    g.drawText("16 slots",
               bounds.withHeight(28.0f).withTrimmedRight(34.0f),
               juce::Justification::centredRight, false);

    for (std::size_t i = 0; i < slots_.size(); ++i) {
        const auto& slot = slots_[i];
        const auto card = slotCardBounds_[i];
        const bool isSelected = (i == selectedSlotIndex_);

        g.setColour(isSelected ? kSlotSelectedBg : kSlotBg);
        g.fillRoundedRectangle(card, 4.0f);

        g.setColour(isSelected ? kCyanGlow : kBorderSubtle);
        g.drawRoundedRectangle(card, 4.0f, isSelected ? 1.6f : 1.0f);

        // Slot Number ("01" .. "16")
        g.setColour(isSelected ? kTextPrimary : kTextMuted);
        g.setFont(juce::FontOptions(11.5f, juce::Font::bold));
        g.drawText(slot.number,
                   card.withWidth(24.0f).withTrimmedLeft(6.0f),
                   juce::Justification::centredLeft, false);

        if (slot.active) {
            // Mini waveform thumbnail box
            const auto thumb = juce::Rectangle<float>(
                card.getX() + 28.0f, card.getY() + 5.0f, 34.0f,
                card.getHeight() - 10.0f);
            g.setColour(juce::Colour(0xff090d14));
            g.fillRoundedRectangle(thumb, 3.0f);
            g.setColour(slot.waveColour);
            g.drawLine(thumb.getX() + 3.0f, thumb.getCentreY(),
                       thumb.getCentreX(), thumb.getY() + 4.0f, 1.3f);
            g.drawLine(thumb.getCentreX(), thumb.getY() + 4.0f,
                       thumb.getRight() - 3.0f, thumb.getBottom() - 4.0f, 1.3f);

            // Title & Subtitle
            g.setColour(kTextPrimary);
            g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
            g.drawText(slot.title,
                       juce::Rectangle<float>(thumb.getRight() + 8.0f,
                                              card.getY() + 3.0f, 92.0f, 15.0f),
                       juce::Justification::centredLeft, false);

            g.setColour(slot.waveColour.withAlpha(0.80f));
            g.setFont(juce::FontOptions(9.5f));
            g.drawText(slot.subtitle,
                       juce::Rectangle<float>(thumb.getRight() + 8.0f,
                                              card.getY() + 17.0f, 92.0f, 13.0f),
                       juce::Justification::centredLeft, false);
        } else {
            g.setColour(kTextMuted);
            g.setFont(juce::FontOptions(11.5f));
            g.drawText("Empty",
                       card.withTrimmedLeft(32.0f),
                       juce::Justification::centredLeft, false);
        }

        // Channel badge ("CH1" .. "OFF")
        g.setColour(slot.active ? kTextMuted : kTextMuted.withAlpha(0.55f));
        g.setFont(juce::FontOptions(10.5f));
        g.drawText(slot.channel,
                   card.withTrimmedRight(26.0f).withLeft(card.getRight() - 58.0f),
                   juce::Justification::centredRight, false);

        // Power icon circle
        g.setColour(slot.active ? kCyanGlow : kTextMuted.withAlpha(0.4f));
        g.drawEllipse(card.getRight() - 18.0f, card.getCentreY() - 5.0f,
                      10.0f, 10.0f, 1.3f);
    }
}

void VstEngineAudioProcessorEditor::drawBottomKeyboardBar(
    juce::Graphics& g, juce::Rectangle<float> bounds) const {
    g.setColour(kPanelBg);
    g.fillRoundedRectangle(bounds, 5.0f);
    g.setColour(kBorderSubtle);
    g.drawRoundedRectangle(bounds, 5.0f, 1.0f);

    // Render 5-octave Piano Keyboard (C0 .. C6)
    const auto kbBounds = bounds.withTrimmedTop(28.0f)
                              .withTrimmedLeft(74.0f)
                              .withTrimmedRight(12.0f)
                              .reduced(4.0f, 4.0f);
    const int numWhiteKeys = 42;
    const float whiteW = kbBounds.getWidth() / static_cast<float>(numWhiteKeys);

    for (int i = 0; i < numWhiteKeys; ++i) {
        const float kx = kbBounds.getX() + static_cast<float>(i) * whiteW;
        const auto keyRect =
            juce::Rectangle<float>(kx, kbBounds.getY(), whiteW - 1.0f,
                                   kbBounds.getHeight());
        const bool isHighlightedScaleNote = (i == 3 || i == 17);
        g.setColour(isHighlightedScaleNote ? juce::Colour(0xff9adfff)
                                           : juce::Colour(0xffeef2f7));
        g.fillRoundedRectangle(keyRect, 2.0f);

        if (i % 7 == 0) {
            g.setColour(kShellBg);
            g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
            g.drawText("C" + juce::String(i / 7),
                       keyRect.withTop(keyRect.getBottom() - 14.0f),
                       juce::Justification::centred, false);
        }
    }

    // Black keys
    for (int i = 0; i < numWhiteKeys - 1; ++i) {
        const int deg = i % 7;
        if (deg == 2 || deg == 6) {
            continue;
        }
        const float bx =
            kbBounds.getX() + (static_cast<float>(i) + 0.68f) * whiteW;
        g.setColour(juce::Colour(0xff11161f));
        g.fillRoundedRectangle(bx, kbBounds.getY(), whiteW * 0.64f,
                               kbBounds.getHeight() * 0.60f, 2.0f);
    }
}

void VstEngineAudioProcessorEditor::paint(juce::Graphics& g) {
    g.fillAll(kShellBg);
    drawTopHeaderBar(g, topBarBounds_);
    drawInstrumentRack(g, rackPanelBounds_);
    drawBottomKeyboardBar(g, bottomBarBounds_);
}

void VstEngineAudioProcessorEditor::resized() {
    auto full = getLocalBounds();

    // 1. Top Global Header Bar (44 px)
    auto topBar = full.removeFromTop(44);
    topBarBounds_ = topBar.toFloat();

    projectPresetBox_.setBounds(350, 9, 150, 26);
    saveProjectBtn_.setBounds(508, 9, 54, 26);
    seedValueLabel_.setBounds(static_cast<int>(topBarBounds_.getCentreX() + 18.0f),
                              9, 58, 26);
    randomSeedBtn_.setBounds(static_cast<int>(topBarBounds_.getCentreX() + 80.0f),
                             9, 28, 26);
    masterOutputKnob_.setBounds(full.getWidth() - 205, 6, 32, 32);
    panicButton_.setBounds(full.getWidth() - 132, 9, 54, 26);
    settingsButton_.setBounds(full.getWidth() - 72, 9, 62, 26);

    auto body = full.reduced(6, 6);

    // 2. Left Instrument Rack Column (228 px wide)
    auto rackArea = body.removeFromLeft(228);
    rackPanelBounds_ = rackArea.toFloat();

    auto slotsListArea = rackArea.reduced(6, 4).withTrimmedTop(28).withTrimmedBottom(95);
    const float slotH = static_cast<float>(slotsListArea.getHeight()) / 16.0f;
    for (std::size_t i = 0; i < slotCardBounds_.size(); ++i) {
        const float y =
            static_cast<float>(slotsListArea.getY()) + static_cast<float>(i) * slotH;
        slotCardBounds_[i] = juce::Rectangle<float>(
            static_cast<float>(slotsListArea.getX()), y + 1.0f,
            static_cast<float>(slotsListArea.getWidth()), slotH - 2.5f);
    }

    body.removeFromLeft(6);

    // 3. Bottom Keyboard / Chords / Scale Bar (96 px high)
    auto bottomBar = body.removeFromBottom(96);
    bottomBarBounds_ = bottomBar.toFloat();

    auto botHeader = bottomBar.removeFromTop(26).reduced(6, 2);
    keyboardTabBtn_.setBounds(botHeader.removeFromLeft(78));
    botHeader.removeFromLeft(4);
    chordsTabBtn_.setBounds(botHeader.removeFromLeft(68));
    botHeader.removeFromLeft(4);
    scaleTabBtn_.setBounds(botHeader.removeFromLeft(62));

    midiLearnBtn_.setBounds(botHeader.removeFromRight(76));
    botHeader.removeFromRight(6);
    velocityCurveBox_.setBounds(botHeader.removeFromRight(88));

    body.removeFromBottom(6);

    // 4. Center Module Workspace (hosts AcidPanel / BassPanel)
    moduleWorkspaceBounds_ = body.toFloat();
    updateActiveSlotView();
}

} // namespace vstengine::vst3
