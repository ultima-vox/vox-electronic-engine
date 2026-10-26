#if __has_include("ui/AcidPanel.h")
#include "ui/AcidPanel.h"
#else
#include "AcidPanel.h"
#endif

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace vstengine::ui {

namespace {

// V2 Mockup Dark-Cyan & Emerald-Teal Palette
const juce::Colour kBgDeep         { 0xff0a0e14 };
const juce::Colour kCardBg         { 0xff101620 };
const juce::Colour kCardBorder     { 0xff1e2b3e };
const juce::Colour kCyanPrimary    { 0xff00d2ff };
const juce::Colour kCyanBright     { 0xff5ce6ff };
const juce::Colour kTealAccent     { 0xff00f5b8 };
const juce::Colour kTextWhite      { 0xffedf2f7 };
const juce::Colour kTextMuted      { 0xff7f8fa6 };
const juce::Colour kCellInactive   { 0xff141c28 };
const juce::Colour kCellBorder     { 0xff202d40 };

[[nodiscard]] juce::String formatFloat2(double value) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << value;
    return juce::String(oss.str());
}

[[nodiscard]] std::string makeSlotParamId(std::size_t slotIndex,
                                          const std::string& paramId) {
    return "slot_" + std::to_string(slotIndex + 1) + "_" + paramId;
}

} // namespace

// ============================================================================
// EnvelopeCurveView (Interactive ADSR Curve Display for ENVELOPE & MODULATION)
// ============================================================================

void AcidPanel::EnvelopeCurveView::setParameters(float a, float d, float s,
                                                 float r) noexcept {
    attack_ = std::clamp(a, 0.0f, 1.0f);
    decay_ = std::clamp(d, 0.0f, 1.0f);
    sustain_ = std::clamp(s, 0.0f, 1.0f);
    release_ = std::clamp(r, 0.0f, 1.0f);
    repaint();
}

void AcidPanel::EnvelopeCurveView::paint(juce::Graphics& g) {
    const auto b = getLocalBounds().toFloat().reduced(2.0f);
    g.setColour(juce::Colour(0xff0b1018));
    g.fillRoundedRectangle(b, 4.0f);

    // Subtle grid lines
    g.setColour(kCardBorder.withAlpha(0.45f));
    for (int i = 1; i <= 3; ++i) {
        const float y = b.getY() + (b.getHeight() * static_cast<float>(i) / 4.0f);
        g.drawHorizontalLine(static_cast<int>(y), b.getX(), b.getRight());
    }

    const float pad = 6.0f;
    const float x0 = b.getX() + pad;
    const float xEnd = b.getRight() - pad;
    const float yTop = b.getY() + pad;
    const float yBot = b.getBottom() - pad;
    const float w = xEnd - x0;
    const float h = yBot - yTop;

    const float xAttack = x0 + w * (0.04f + 0.24f * attack_);
    const float xDecay = xAttack + w * (0.10f + 0.28f * decay_);
    const float xSustain = xDecay + w * 0.20f;
    const float xRelease =
        std::min(xEnd, xSustain + w * (0.08f + 0.24f * release_));
    const float ySustain = yBot - h * sustain_;

    juce::Path curve;
    curve.startNewSubPath(x0, yBot);
    curve.lineTo(xAttack, yTop);
    curve.quadraticTo((xAttack + xDecay) * 0.5f,
                      yTop + (ySustain - yTop) * 0.65f, xDecay, ySustain);
    curve.lineTo(xSustain, ySustain);
    curve.quadraticTo((xSustain + xRelease) * 0.5f,
                      ySustain + (yBot - ySustain) * 0.7f, xRelease, yBot);

    // Translucent cyan fill under curve
    juce::Path fillPath = curve;
    fillPath.lineTo(xRelease, yBot);
    fillPath.lineTo(x0, yBot);
    fillPath.closeSubPath();

    juce::ColourGradient fillGrad(kCyanPrimary.withAlpha(0.28f), x0, yTop,
                                  kCyanPrimary.withAlpha(0.02f), x0, yBot, false);
    g.setGradientFill(fillGrad);
    g.fillPath(fillPath);

    // Glowing cyan stroke + control nodes
    g.setColour(kCyanBright);
    g.strokePath(curve, juce::PathStrokeType(1.8f));

    const std::array<juce::Point<float>, 4> nodes {{
        { xAttack, yTop },
        { xDecay, ySustain },
        { xSustain, ySustain },
        { xRelease, yBot }
    }};
    for (const auto& pt : nodes) {
        g.fillEllipse(pt.x - 3.5f, pt.y - 3.5f, 7.0f, 7.0f);
    }
}

// ============================================================================
// StepMatrixComponent (V2 5-Lane x 16-Step Matrix with right-hand lane boxes)
// ============================================================================

AcidPanel::StepMatrixComponent::StepMatrixComponent(AcidPanel& owner)
    : owner_(owner) {
    static constexpr std::array<const char*, 5> kLaneNames {
        "Note", "Accent", "Slide", "Gate", "Octave"
    };
    for (std::size_t i = 0; i < laneTargetBoxes_.size(); ++i) {
        auto& box = laneTargetBoxes_[i];
        box.addItem(kLaneNames[i], 1);
        box.addItem("Velocity", 2);
        box.addItem("Mod Step", 3);
        box.setSelectedId(1, juce::dontSendNotification);
        addAndMakeVisible(box);
    }
}

juce::String AcidPanel::StepMatrixComponent::noteName(
    std::uint8_t midiNote, std::int8_t octaveShift) {
    static constexpr std::array<const char*, 12> kNames {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };
    const int n = std::clamp(static_cast<int>(midiNote), 0, 127);
    const int octave = (n / 12) - 1 + static_cast<int>(octaveShift);
    return juce::String(kNames[static_cast<std::size_t>(n % 12)]) +
           juce::String(octave);
}

int AcidPanel::StepMatrixComponent::stepAtPosition(float x) const noexcept {
    const float leftCol = 62.0f;
    const float rightCol = 78.0f;
    const float gridW = static_cast<float>(getWidth()) - leftCol - rightCol;
    if (x < leftCol || x >= leftCol + gridW || gridW <= 0.0f) {
        return -1;
    }
    const int stepCount =
        std::clamp(static_cast<int>(owner_.sequence_.stepCount), 1, 16);
    const float stepW = gridW / static_cast<float>(stepCount);
    return std::clamp(static_cast<int>((x - leftCol) / stepW), 0, stepCount - 1);
}

int AcidPanel::StepMatrixComponent::laneAtPosition(float y) const noexcept {
    const float headerH = 18.0f;
    const float gridH = static_cast<float>(getHeight()) - headerH;
    if (y < headerH || gridH <= 0.0f) {
        return -1;
    }
    const float laneH = gridH / 5.0f;
    return std::clamp(static_cast<int>((y - headerH) / laneH), 0, 4);
}

void AcidPanel::StepMatrixComponent::resized() {
    const float headerH = 18.0f;
    const float rightCol = 74.0f;
    const float gridH = static_cast<float>(getHeight()) - headerH;
    const float laneH = gridH / 5.0f;

    for (std::size_t lane = 0; lane < laneTargetBoxes_.size(); ++lane) {
        const float y = headerH + static_cast<float>(lane) * laneH;
        laneTargetBoxes_[lane].setBounds(
            juce::Rectangle<float>(static_cast<float>(getWidth()) - rightCol,
                                   y + 2.0f, rightCol - 2.0f, laneH - 4.0f)
                .toNearestInt());
    }
}

void AcidPanel::StepMatrixComponent::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds().toFloat();
    const float leftCol = 62.0f;
    const float rightCol = 78.0f;
    const float headerH = 18.0f;
    const float gridW = bounds.getWidth() - leftCol - rightCol;
    const float gridH = bounds.getHeight() - headerH;
    const int stepCount =
        std::clamp(static_cast<int>(owner_.sequence_.stepCount), 1, 16);
    const float stepW = gridW / static_cast<float>(stepCount);
    const float laneH = gridH / 5.0f;

    static constexpr std::array<const char*, 5> kLaneTitles {
        "Note", "Accent", "Slide", "Gate", "Octave"
    };

    // Left lane titles
    g.setFont(juce::FontOptions(12.0f));
    g.setColour(kTextMuted);
    for (int lane = 0; lane < 5; ++lane) {
        const float y = headerH + static_cast<float>(lane) * laneH;
        g.drawText(kLaneTitles[static_cast<std::size_t>(lane)],
                   juce::Rectangle<float>(4.0f, y, leftCol - 8.0f, laneH),
                   juce::Justification::centredLeft, false);
    }

    for (int stepIdx = 0; stepIdx < stepCount; ++stepIdx) {
        const auto& step =
            owner_.sequence_.steps[static_cast<std::size_t>(stepIdx)];
        const float x = leftCol + static_cast<float>(stepIdx) * stepW;
        const bool isPlayhead = (stepIdx == owner_.playheadStep_);

        // Top Step Number Header (1 .. 16)
        g.setColour(isPlayhead ? kCyanBright : kTextMuted);
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        g.drawText(juce::String(stepIdx + 1),
                   juce::Rectangle<float>(x, 0.0f, stepW, headerH - 2.0f),
                   juce::Justification::centred, false);

        for (int lane = 0; lane < 5; ++lane) {
            const float y = headerH + static_cast<float>(lane) * laneH;
            const auto cell =
                juce::Rectangle<float>(x, y, stepW, laneH).reduced(1.5f, 1.5f);

            g.setColour(kCellInactive);
            g.fillRoundedRectangle(cell, 3.0f);
            g.setColour(isPlayhead ? kCyanBright : kCellBorder);
            g.drawRoundedRectangle(cell, 3.0f, 1.0f);

            switch (lane) {
                case 0: { // Note ("C3" on bright cyan pill if active, "--" if muted)
                    if (step.gate) {
                        const auto pill = cell.reduced(2.0f, 2.5f);
                        g.setColour(kCyanBright.withAlpha(0.90f));
                        g.fillRoundedRectangle(pill, 3.0f);
                        g.setColour(kBgDeep);
                        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
                        g.drawText(noteName(step.note, 0), pill,
                                   juce::Justification::centred, false);
                    } else {
                        g.setColour(kTextMuted.withAlpha(0.55f));
                        g.setFont(juce::FontOptions(11.0f));
                        g.drawText("--", cell, juce::Justification::centred, false);
                    }
                    break;
                }
                case 1: { // Accent (glowing cyan/teal square LED)
                    const float sq = std::min(cell.getWidth(), cell.getHeight()) * 0.52f;
                    const auto led = juce::Rectangle<float>(
                        cell.getCentreX() - sq * 0.5f,
                        cell.getCentreY() - sq * 0.5f, sq, sq);
                    if (step.accent) {
                        g.setColour(kTealAccent);
                        g.fillRoundedRectangle(led, 2.5f);
                    } else {
                        g.setColour(kCellBorder);
                        g.drawRoundedRectangle(led, 2.5f, 1.0f);
                    }
                    break;
                }
                case 2: { // Slide (emerald/teal badge bar when active)
                    if (step.slide) {
                        const auto badge = cell.reduced(4.0f, 4.5f);
                        g.setColour(kTealAccent.withAlpha(0.32f));
                        g.fillRoundedRectangle(badge, 2.5f);
                        g.setColour(kTealAccent);
                        g.drawRoundedRectangle(badge, 2.5f, 1.2f);
                    }
                    break;
                }
                case 3: { // Gate (vertical glowing cyan bar inside each step)
                    const float barW = 4.0f;
                    const float barH =
                        step.gate ? (cell.getHeight() * 0.72f)
                                  : (cell.getHeight() * 0.25f);
                    const auto bar = juce::Rectangle<float>(
                        cell.getCentreX() - barW * 0.5f,
                        cell.getBottom() - barH - 2.5f, barW, barH);
                    g.setColour(step.gate ? kCyanBright
                                          : kCyanPrimary.withAlpha(0.28f));
                    g.fillRoundedRectangle(bar, 1.5f);
                    break;
                }
                case 4: { // Octave ("0", "+1", "-1")
                    juce::String octStr = "0";
                    if (step.octave > 0) {
                        octStr = "+" + juce::String(static_cast<int>(step.octave));
                    } else if (step.octave < 0) {
                        octStr = juce::String(static_cast<int>(step.octave));
                    }
                    g.setColour(step.octave != 0 ? kCyanBright : kTextMuted);
                    g.setFont(juce::FontOptions(11.0f,
                                                step.octave != 0 ? juce::Font::bold
                                                                 : juce::Font::plain));
                    g.drawText(octStr, cell, juce::Justification::centred, false);
                    break;
                }
            }
        }
    }
}

void AcidPanel::StepMatrixComponent::mouseDown(const juce::MouseEvent& event) {
    const int stepIdx = stepAtPosition(event.position.x);
    const int lane = laneAtPosition(event.position.y);
    if (stepIdx < 0 || lane < 0) {
        return;
    }

    dragStep_ = stepIdx;
    dragLane_ = lane;
    dragStartY_ = event.getPosition().y;

    auto& step = owner_.sequence_.steps[static_cast<std::size_t>(stepIdx)];
    dragStartNote_ = step.note;

    switch (lane) {
        case 0:
            if (!step.gate) {
                step.gate = true;
            }
            break;
        case 1:
            step.accent = !step.accent;
            step.velocity = step.accent ? 122u : 96u;
            break;
        case 2:
            step.slide = !step.slide;
            break;
        case 3:
            step.gate = !step.gate;
            if (!step.gate) {
                step.accent = false;
            }
            break;
        case 4:
            if (step.octave == 0) {
                step.octave = 1;
            } else if (step.octave == 1) {
                step.octave = -1;
            } else {
                step.octave = 0;
            }
            break;
    }

    owner_.notifySequenceChanged();
    repaint();
}

void AcidPanel::StepMatrixComponent::mouseDrag(const juce::MouseEvent& event) {
    if (dragStep_ < 0 || dragLane_ != 0) {
        return;
    }
    const int deltaY = dragStartY_ - event.getPosition().y;
    const int semitoneDelta = deltaY / 7;
    auto& step = owner_.sequence_.steps[static_cast<std::size_t>(dragStep_)];
    step.note = static_cast<std::uint8_t>(
        std::clamp(static_cast<int>(dragStartNote_) + semitoneDelta, 24, 84));
    owner_.notifySequenceChanged();
    repaint();
}

// ============================================================================
// AcidPanel V2 Implementation
// ============================================================================

AcidPanel::AcidPanel(juce::AudioProcessorValueTreeState* apvts,
                     std::size_t slotIndex, std::uint32_t initialGlobalSeed)
    : apvts_(apvts),
      slotIndex_(slotIndex),
      globalSeed_(initialGlobalSeed),
      stepMatrix_(*this) {
    patternParams_.rootNote = 48; // C3 as shown in V2 mockup ("C3", "D3", "E3", "G3")
    patternParams_.density = 0.75f;
    patternParams_.variation = 0.25f;
    patternParams_.swing = 0.15f;

    setupHeaderAndBanner();
    setupRow1SynthModules();
    setupRow2StepSequencer();
    setupRow3ModAndPerformance();

    addAndMakeVisible(stepMatrix_);
    triggerGenerate();
    startTimerHz(30);
}

AcidPanel::~AcidPanel() {
    stopTimer();
}

void AcidPanel::bindEngine(acid::AcidEngine* engine) noexcept {
    engine_ = engine;
}

void AcidPanel::setGlobalSeed(std::uint32_t seed) noexcept {
    globalSeed_ = seed;
}

void AcidPanel::setSlotIndex(std::size_t slotIndex) {
    slotIndex_ = slotIndex;
    midiChannelSelector_.setSelectedId(static_cast<int>(slotIndex % 16) + 1,
                                       juce::dontSendNotification);
}

void AcidPanel::setPlayheadStep(int stepIndex) noexcept {
    if (playheadStep_ != stepIndex) {
        playheadStep_ = stepIndex;
        stepMatrix_.repaint();
    }
}

void AcidPanel::setSequence(const sequence::Sequence& sequence) {
    sequence_ = sequence;
    stepMatrix_.repaint();
}

juce::String AcidPanel::formatKnobValue(double normValue,
                                        enum classKnobFormat format) {
    switch (format) {
        case enum classKnobFormat::frequencyHz: {
            const float hz = acid::AcidEngine::cutoffNormalizedToHz(
                static_cast<float>(normValue));
            if (hz >= 1000.0f) {
                return formatFloat2(hz / 1000.0f) + " kHz";
            }
            return juce::String(static_cast<int>(std::round(hz))) + " Hz";
        }
        case enum classKnobFormat::seconds: {
            const double sec = normValue * normValue * 2.0;
            return formatFloat2(sec) + " s";
        }
        case enum classKnobFormat::decibels: {
            const double db = (normValue - 0.85) * 24.0;
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(1) << db << " dB";
            return juce::String(oss.str());
        }
        case enum classKnobFormat::pan: {
            const int p = static_cast<int>(std::round((normValue - 0.5) * 100.0));
            if (std::abs(p) <= 1) return "C";
            return (p < 0) ? ("L" + juce::String(-p)) : ("R" + juce::String(p));
        }
        case enum classKnobFormat::semitones: {
            const double st = (normValue - 0.5) * 24.0;
            return formatFloat2(st);
        }
        case enum classKnobFormat::percent: {
            return juce::String(static_cast<int>(std::round(normValue * 100.0))) + "%";
        }
        case enum classKnobFormat::normalized:
        default:
            return formatFloat2(normValue);
    }
}

void AcidPanel::setupHeaderAndBanner() {
    presetSelector_.addItem("Acid", 1);
    presetSelector_.addItem("Classic 303", 2);
    presetSelector_.addItem("Psy Acid", 3);
    presetSelector_.addItem("Dark Acid", 4);
    presetSelector_.addItem("Techno Drive", 5);
    presetSelector_.setSelectedId(1, juce::dontSendNotification);
    addAndMakeVisible(presetSelector_);

    addAndMakeVisible(prevPresetButton_);
    addAndMakeVisible(nextPresetButton_);
    addAndMakeVisible(favoriteButton_);

    for (int ch = 1; ch <= 16; ++ch) {
        midiChannelSelector_.addItem("CH" + juce::String(ch), ch);
    }
    midiChannelSelector_.setSelectedId(2, juce::dontSendNotification);
    addAndMakeVisible(midiChannelSelector_);

    for (std::size_t i = 0; i < subTabButtons_.size(); ++i) {
        subTabButtons_[i].onClick = [this, i]() {
            updateActiveSubTab(static_cast<SubTab>(i));
        };
        addAndMakeVisible(subTabButtons_[i]);
    }
    updateActiveSubTab(SubTab::sound);
}

void AcidPanel::setupRow1SynthModules() {
    // 1. OSCILLATOR Card (Saw + Tune 0.00, Fine 0.00, Wave ^, PW 0)
    oscWaveBox_.addItem("Saw", 1);
    oscWaveBox_.addItem("Square 303", 2);
    oscWaveBox_.addItem("Sine / Sub", 3);
    oscWaveBox_.addItem("Triangle", 4);
    oscWaveBox_.setSelectedId(1, juce::dontSendNotification);
    addAndMakeVisible(oscWaveBox_);

    configureRotaryKnob(oscKnobs_[0], "Tune", "osc_tune", 0.50f, enum classKnobFormat::semitones);
    configureRotaryKnob(oscKnobs_[1], "Fine", "osc_fine", 0.50f, enum classKnobFormat::semitones);
    configureRotaryKnob(oscKnobs_[2], "Wave", "waveform", 0.50f, enum classKnobFormat::normalized);
    configureRotaryKnob(oscKnobs_[3], "PW",   "osc_pw",   0.50f, enum classKnobFormat::normalized);

    // 2. FILTER Card (Low Pass 24 dB + Cutoff 250 Hz, Resonance 0.75, Env Amt 0.60, Key Track 0.20)
    filterModeBox_.addItem("Low Pass 24 dB", 1);
    filterModeBox_.addItem("Diode 18 dB (303)", 2);
    filterModeBox_.addItem("Band Pass 12 dB", 3);
    filterModeBox_.addItem("High Pass 12 dB", 4);
    filterModeBox_.setSelectedId(1, juce::dontSendNotification);
    filterModeBox_.onChange = [this]() {
        syncKnobToEngine("filter_mode",
                         static_cast<float>(filterModeBox_.getSelectedId() - 1) / 3.0f);
    };
    addAndMakeVisible(filterModeBox_);

    configureRotaryKnob(filterKnobs_[0], "Cutoff",    "cutoff",       0.37f, enum classKnobFormat::frequencyHz);
    configureRotaryKnob(filterKnobs_[1], "Resonance", "resonance",    0.75f, enum classKnobFormat::normalized);
    configureRotaryKnob(filterKnobs_[2], "Env Amt",   "envelope",     0.60f, enum classKnobFormat::normalized);
    configureRotaryKnob(filterKnobs_[3], "Key Track", "key_tracking", 0.20f, enum classKnobFormat::normalized);

    // 3. ENVELOPE Card (AMP / FILTER tabs + Interactive Curve + A 0.00s, D 0.10s, S 0.80, R 0.25s)
    envAmpTabBtn_.onClick = [this]() { updateEnvelopeTab(EnvelopeTab::amp); };
    envFilterTabBtn_.onClick = [this]() { updateEnvelopeTab(EnvelopeTab::filter); };
    addAndMakeVisible(envAmpTabBtn_);
    addAndMakeVisible(envFilterTabBtn_);
    addAndMakeVisible(envCurveView_);

    configureRotaryKnob(envKnobs_[0], "A", "amp_attack",  0.00f, enum classKnobFormat::seconds);
    configureRotaryKnob(envKnobs_[1], "D", "amp_decay",   0.22f, enum classKnobFormat::seconds);
    configureRotaryKnob(envKnobs_[2], "S", "amp_sustain", 0.80f, enum classKnobFormat::normalized);
    configureRotaryKnob(envKnobs_[3], "R", "amp_release", 0.35f, enum classKnobFormat::seconds);
    updateEnvelopeTab(EnvelopeTab::amp);

    // 4. DISTORTION / DRIVE Card (Analog Tube + Drive 0.65, Tone 0.40, Mix 0.70)
    distModeBox_.addItem("Analog Tube", 1);
    distModeBox_.addItem("Soft Clip", 2);
    distModeBox_.addItem("Hard Clip", 3);
    distModeBox_.addItem("Wavefold", 4);
    distModeBox_.setSelectedId(1, juce::dontSendNotification);
    distModeBox_.onChange = [this]() {
        syncKnobToEngine("dist_mode",
                         static_cast<float>(distModeBox_.getSelectedId() - 1) / 3.0f);
    };
    addAndMakeVisible(distModeBox_);

    configureRotaryKnob(distKnobs_[0], "Drive", "drive",     0.65f, enum classKnobFormat::normalized);
    configureRotaryKnob(distKnobs_[1], "Tone",  "dist_tone", 0.40f, enum classKnobFormat::normalized);
    configureRotaryKnob(distKnobs_[2], "Mix",   "dist_mix",  0.70f, enum classKnobFormat::normalized);

    // 5. ACCENT, SLIDE, OUTPUT Mini Cards
    configureRotaryKnob(accentKnob_,    "Amount", "accent", 0.58f, enum classKnobFormat::decibels);
    configureRotaryKnob(slideKnob_,     "Time",   "slide",  0.30f, enum classKnobFormat::normalized);
    configureRotaryKnob(outputKnobs_[0], "Volume", "output", 0.75f, enum classKnobFormat::decibels);
    configureRotaryKnob(outputKnobs_[1], "Pan",    "pan",    0.50f, enum classKnobFormat::pan);
}

void AcidPanel::setupRow2StepSequencer() {
    playButton_.onClick = [this]() {
        isPlaying_ = true;
        if (transportCallback_) transportCallback_(true);
    };
    stopButton_.onClick = [this]() {
        isPlaying_ = false;
        playheadStep_ = -1;
        if (transportCallback_) transportCallback_(false);
        stepMatrix_.repaint();
    };
    generateButton_.onClick = [this]() { triggerGenerate(); };
    mutateButton_.onClick = [this]() { triggerMutate(); };
    clearButton_.onClick = [this]() { triggerClear(); };

    addAndMakeVisible(playButton_);
    addAndMakeVisible(stopButton_);
    addAndMakeVisible(loopStepsButton_);
    addAndMakeVisible(generateButton_);
    addAndMakeVisible(mutateButton_);
    addAndMakeVisible(clearButton_);

    patternPresetBox_.addItem("Classic 303", 1);
    patternPresetBox_.addItem("Psy Acid", 2);
    patternPresetBox_.addItem("Dark Acid", 3);
    patternPresetBox_.addItem("Techno Drive", 4);
    patternPresetBox_.setSelectedId(1, juce::dontSendNotification);
    patternPresetBox_.onChange = [this]() {
        patternParams_.profile = static_cast<acid::AcidProfile>(
            std::clamp(patternPresetBox_.getSelectedId() - 1, 0, 6));
        triggerGenerate();
    };
    addAndMakeVisible(patternPresetBox_);

    configureRotaryKnob(swingKnob_, "Swing", "seq_swing", 0.15f, enum classKnobFormat::percent);
    configureRotaryKnob(densityKnob_, "Density", "seq_density", 0.75f, enum classKnobFormat::percent);
    configureRotaryKnob(variationKnob_, "Variation", "seq_variation", 0.25f, enum classKnobFormat::percent);

    resolutionBox_.addItem("1/8", 1);
    resolutionBox_.addItem("1/16", 2);
    resolutionBox_.addItem("1/32", 3);
    resolutionBox_.setSelectedId(2, juce::dontSendNotification);
    addAndMakeVisible(resolutionBox_);

    lengthBox_.addItem("8", 1);
    lengthBox_.addItem("12", 2);
    lengthBox_.addItem("16", 3);
    lengthBox_.setSelectedId(3, juce::dontSendNotification);
    lengthBox_.onChange = [this]() {
        static constexpr std::array<std::uint32_t, 3> kLengths { 8u, 12u, 16u };
        const int idx = std::clamp(lengthBox_.getSelectedId() - 1, 0, 2);
        patternParams_.length = kLengths[static_cast<std::size_t>(idx)];
        sequence_.stepCount = patternParams_.length;
        stepMatrix_.repaint();
        notifySequenceChanged();
    };
    addAndMakeVisible(lengthBox_);
}

void AcidPanel::setupRow3ModAndPerformance() {
    // 1. MODULATION Card (ENV 1..2, LFO 1..3, STEP + Curve + Target + Amount, A, D, S, R)
    for (std::size_t i = 0; i < modTabButtons_.size(); ++i) {
        modTabButtons_[i].onClick = [this, i]() {
            updateModSourceTab(static_cast<ModSourceTab>(i));
        };
        addAndMakeVisible(modTabButtons_[i]);
    }
    updateModSourceTab(ModSourceTab::env1);
    addAndMakeVisible(modCurveView_);

    modTargetBox_.addItem("Filter Cutoff", 1);
    modTargetBox_.addItem("Pitch", 2);
    modTargetBox_.addItem("Pulse Width", 3);
    modTargetBox_.addItem("Resonance", 4);
    modTargetBox_.addItem("Drive", 5);
    modTargetBox_.addItem("Pan", 6);
    modTargetBox_.setSelectedId(1, juce::dontSendNotification);
    modTargetBox_.onChange = [this]() {
        syncKnobToEngine("mod_target",
                         static_cast<float>(modTargetBox_.getSelectedId() - 1) / 5.0f);
    };
    addAndMakeVisible(modTargetBox_);

    configureRotaryKnob(modKnobs_[0], "Amount",  "mod_amount",  0.70f, enum classKnobFormat::normalized);
    configureRotaryKnob(modKnobs_[1], "Attack",  "mod_attack",  0.00f, enum classKnobFormat::seconds);
    configureRotaryKnob(modKnobs_[2], "Decay",   "mod_decay",   0.38f, enum classKnobFormat::seconds);
    configureRotaryKnob(modKnobs_[3], "Sustain", "mod_sustain", 0.80f, enum classKnobFormat::normalized);
    configureRotaryKnob(modKnobs_[4], "Release", "mod_release", 0.42f, enum classKnobFormat::seconds);
    modCurveView_.setParameters(0.00f, 0.38f, 0.80f, 0.42f);

    // 2. PERFORMANCE Card (Portamento 0.00 s, Unison 1, Detune 0.10, Spread 0.40)
    configureRotaryKnob(perfKnobs_[0], "Portamento", "portamento",    0.00f, enum classKnobFormat::seconds);
    configureRotaryKnob(perfKnobs_[1], "Unison",     "unison_voices", 0.00f, enum classKnobFormat::normalized);
    configureRotaryKnob(perfKnobs_[2], "Detune",     "unison_detune", 0.10f, enum classKnobFormat::normalized);
    configureRotaryKnob(perfKnobs_[3], "Spread",     "stereo_spread", 0.40f, enum classKnobFormat::normalized);

    unisonCountBox_.addItem("1", 1);
    unisonCountBox_.addItem("2", 2);
    unisonCountBox_.addItem("3", 3);
    unisonCountBox_.addItem("4", 4);
    unisonCountBox_.setSelectedId(1, juce::dontSendNotification);
    unisonCountBox_.onChange = [this]() {
        const float norm =
            static_cast<float>(unisonCountBox_.getSelectedId() - 1) / 3.0f;
        perfKnobs_[1].slider.setValue(norm, juce::sendNotificationSync);
    };
    addAndMakeVisible(unisonCountBox_);

    // 3. PLAY MODE Card (Mono + Legato, Retrigger, Vel -> Accent, Slide on Overlap)
    playModeBox_.addItem("Mono", 1);
    playModeBox_.addItem("Acid Legato", 2);
    playModeBox_.addItem("Unison Stack", 3);
    playModeBox_.setSelectedId(1, juce::dontSendNotification);
    addAndMakeVisible(playModeBox_);

    legatoToggle_.setToggleState(true, juce::dontSendNotification);
    retriggerToggle_.setToggleState(false, juce::dontSendNotification);
    velAccentToggle_.setToggleState(true, juce::dontSendNotification);
    slideOverlapToggle_.setToggleState(true, juce::dontSendNotification);

    for (auto* toggle : { &legatoToggle_, &retriggerToggle_,
                          &velAccentToggle_, &slideOverlapToggle_ }) {
        toggle->onClick = [this]() { syncPlayModeFlags(); };
        addAndMakeVisible(*toggle);
    }
}

void AcidPanel::configureRotaryKnob(LabeledKnob& knob,
                                    const juce::String& title,
                                    const std::string& paramId,
                                    float defaultVal,
                                    enum classKnobFormat format) {
    knob.paramId = paramId;
    knob.format = format;
    knob.slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    knob.slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    knob.slider.setRange(0.0, 1.0, 0.001);
    knob.slider.setDoubleClickReturnValue(true, defaultVal);
    knob.slider.setValue(defaultVal, juce::dontSendNotification);
    knob.slider.setColour(juce::Slider::rotarySliderFillColourId, kCyanPrimary);
    knob.slider.setColour(juce::Slider::rotarySliderOutlineColourId, kCellInactive);
    knob.slider.setColour(juce::Slider::thumbColourId, kCyanBright);

    knob.titleLabel.setText(title, juce::dontSendNotification);
    knob.titleLabel.setJustificationType(juce::Justification::centred);
    knob.titleLabel.setColour(juce::Label::textColourId, kTextMuted);
    knob.titleLabel.setFont(juce::FontOptions(11.0f));

    knob.valueLabel.setText(formatKnobValue(defaultVal, format),
                            juce::dontSendNotification);
    knob.valueLabel.setJustificationType(juce::Justification::centred);
    knob.valueLabel.setColour(juce::Label::textColourId, kTextWhite);
    knob.valueLabel.setFont(juce::FontOptions(11.0f, juce::Font::bold));

    knob.slider.onValueChange = [this, &knob]() {
        const double v = knob.slider.getValue();
        knob.valueLabel.setText(formatKnobValue(v, knob.format),
                                juce::dontSendNotification);

        if (knob.paramId == "seq_density") {
            patternParams_.density = static_cast<float>(v);
            return;
        }
        if (knob.paramId == "seq_variation") {
            patternParams_.variation = static_cast<float>(v);
            return;
        }
        if (knob.paramId == "seq_swing") {
            patternParams_.swing = static_cast<float>(v) * 0.5f;
            return;
        }

        syncKnobToEngine(knob.paramId, static_cast<float>(v));
        envCurveView_.setParameters(
            static_cast<float>(envKnobs_[0].slider.getValue()),
            static_cast<float>(envKnobs_[1].slider.getValue()),
            static_cast<float>(envKnobs_[2].slider.getValue()),
            static_cast<float>(envKnobs_[3].slider.getValue()));
        modCurveView_.setParameters(
            static_cast<float>(modKnobs_[1].slider.getValue()),
            static_cast<float>(modKnobs_[2].slider.getValue()),
            static_cast<float>(modKnobs_[3].slider.getValue()),
            static_cast<float>(modKnobs_[4].slider.getValue()));
    };

    if (apvts_ != nullptr) {
        const std::string fullParamId = makeSlotParamId(slotIndex_, paramId);
        if (apvts_->getParameter(fullParamId) != nullptr) {
            knob.attachment = std::make_unique<
                juce::AudioProcessorValueTreeState::SliderAttachment>(
                *apvts_, fullParamId, knob.slider);
        } else if (apvts_->getParameter(paramId) != nullptr) {
            knob.attachment = std::make_unique<
                juce::AudioProcessorValueTreeState::SliderAttachment>(
                *apvts_, paramId, knob.slider);
        }
    }

    addAndMakeVisible(knob.slider);
    addAndMakeVisible(knob.titleLabel);
    addAndMakeVisible(knob.valueLabel);
}

void AcidPanel::syncKnobToEngine(const std::string& paramId, float value) {
    if (engine_ != nullptr) {
        engine_->setParameter(paramId, value);
    }
}

void AcidPanel::updateActiveSubTab(SubTab tab) {
    activeTab_ = tab;
    for (std::size_t i = 0; i < subTabButtons_.size(); ++i) {
        const bool isSelected = (static_cast<SubTab>(i) == activeTab_);
        subTabButtons_[i].setColour(
            juce::TextButton::buttonColourId,
            isSelected ? kCyanPrimary : kCardBg);
        subTabButtons_[i].setColour(
            juce::TextButton::textColourOffId,
            isSelected ? kBgDeep : kTextMuted);
    }
    repaint();
}

void AcidPanel::updateEnvelopeTab(EnvelopeTab tab) {
    activeEnvTab_ = tab;
    const bool isAmp = (tab == EnvelopeTab::amp);
    envAmpTabBtn_.setColour(juce::TextButton::buttonColourId,
                            isAmp ? kCyanPrimary : kCardBg);
    envAmpTabBtn_.setColour(juce::TextButton::textColourOffId,
                            isAmp ? kBgDeep : kTextMuted);
    envFilterTabBtn_.setColour(juce::TextButton::buttonColourId,
                               !isAmp ? kCyanPrimary : kCardBg);
    envFilterTabBtn_.setColour(juce::TextButton::textColourOffId,
                               !isAmp ? kBgDeep : kTextMuted);

    envKnobs_[0].paramId = isAmp ? "amp_attack"  : "flt_attack";
    envKnobs_[1].paramId = isAmp ? "amp_decay"   : "decay";
    envKnobs_[2].paramId = isAmp ? "amp_sustain" : "flt_sustain";
    envKnobs_[3].paramId = isAmp ? "amp_release" : "flt_release";

    envCurveView_.setParameters(
        static_cast<float>(envKnobs_[0].slider.getValue()),
        static_cast<float>(envKnobs_[1].slider.getValue()),
        static_cast<float>(envKnobs_[2].slider.getValue()),
        static_cast<float>(envKnobs_[3].slider.getValue()));
}

void AcidPanel::updateModSourceTab(ModSourceTab tab) {
    activeModTab_ = tab;
    for (std::size_t i = 0; i < modTabButtons_.size(); ++i) {
        const bool isSelected = (static_cast<ModSourceTab>(i) == activeModTab_);
        modTabButtons_[i].setColour(
            juce::TextButton::buttonColourId,
            isSelected ? kCyanPrimary.withAlpha(0.32f) : kCardBg);
        modTabButtons_[i].setColour(
            juce::TextButton::textColourOffId,
            isSelected ? kCyanBright : kTextMuted);
    }
}

void AcidPanel::syncPlayModeFlags() {
    if (engine_ != nullptr) {
        engine_->setPlayModeOptions(
            legatoToggle_.getToggleState(),
            retriggerToggle_.getToggleState(),
            velAccentToggle_.getToggleState(),
            slideOverlapToggle_.getToggleState());
    }
}

void AcidPanel::triggerGenerate() {
    sequence_ = acid::AcidGenerator::generateWithParams(
        patternParams_, globalSeed_, static_cast<std::uint64_t>(slotIndex_));
    stepMatrix_.repaint();
    notifySequenceChanged();
}

void AcidPanel::triggerMutate() {
    sequence_ = acid::AcidGenerator::mutate(
        sequence_, patternParams_, mutationCounter_++, globalSeed_,
        static_cast<std::uint64_t>(slotIndex_));
    stepMatrix_.repaint();
    notifySequenceChanged();
}

void AcidPanel::triggerClear() {
    for (auto& step : sequence_.steps) {
        step.gate = false;
        step.accent = false;
        step.slide = false;
        step.octave = 0;
        step.note = patternParams_.rootNote;
    }
    stepMatrix_.repaint();
    notifySequenceChanged();
}

void AcidPanel::notifySequenceChanged() {
    if (sequenceChangedCallback_) {
        sequenceChangedCallback_(sequence_);
    }
}

void AcidPanel::timerCallback() {}

void AcidPanel::drawModuleCard(juce::Graphics& g, juce::Rectangle<float> bounds,
                               const juce::String& title,
                               bool drawPowerIcon) const {
    g.setColour(kCardBg);
    g.fillRoundedRectangle(bounds, 5.0f);

    g.setColour(kCardBorder);
    g.drawRoundedRectangle(bounds, 5.0f, 1.0f);

    if (title.isNotEmpty()) {
        float textX = bounds.getX() + 10.0f;
        if (drawPowerIcon) {
            g.setColour(kCyanBright);
            g.drawEllipse(textX, bounds.getY() + 7.0f, 10.0f, 10.0f, 1.4f);
            g.drawLine(textX + 5.0f, bounds.getY() + 5.0f, textX + 5.0f,
                       bounds.getY() + 11.0f, 1.4f);
            textX += 16.0f;
        }
        g.setColour(kTextWhite);
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        g.drawText(title,
                   juce::Rectangle<float>(textX, bounds.getY() + 4.0f,
                                          bounds.getWidth() - 28.0f, 16.0f),
                   juce::Justification::centredLeft, false);
    }
}

void AcidPanel::drawHeroBanner(juce::Graphics& g,
                               juce::Rectangle<float> bounds) const {
    juce::ColourGradient bgGrad(juce::Colour(0xff081822), bounds.getTopLeft(),
                                juce::Colour(0xff0e242c), bounds.getBottomRight(),
                                false);
    g.setGradientFill(bgGrad);
    g.fillRoundedRectangle(bounds, 5.0f);

    g.setColour(kCardBorder);
    g.drawRoundedRectangle(bounds, 5.0f, 1.0f);

    // Left Neon Title: "ACID" + "CLASSIC ATTITUDE / MODERN POSSIBILITIES"
    g.setColour(kTealAccent);
    g.setFont(juce::FontOptions(28.0f, juce::Font::bold));
    g.drawText("ACID",
               bounds.withTrimmedLeft(18.0f).withTop(bounds.getY() + 12.0f),
               juce::Justification::topLeft, false);

    g.setColour(kTextMuted);
    g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
    g.drawFittedText("CLASSIC ATTITUDE\nMODERN POSSIBILITIES",
                     bounds.withTrimmedLeft(18.0f)
                         .withTop(bounds.getY() + 44.0f)
                         .toNearestInt(),
                     juce::Justification::topLeft, 2);

    // Right Tagline: "SQUELCH / SEQUENCE / DISTORT / TRANSCEND"
    g.setColour(kTextMuted.withAlpha(0.75f));
    g.setFont(juce::FontOptions(9.0f));
    g.drawFittedText("SQUELCH\nSEQUENCE\nDISTORT\nTRANSCEND\n\nAN ICON\nREIMAGINED",
                     bounds.withTrimmedRight(16.0f)
                         .withLeft(bounds.getRight() - 120.0f)
                         .withTop(bounds.getY() + 8.0f)
                         .toNearestInt(),
                     juce::Justification::topLeft, 7);
}

void AcidPanel::paint(juce::Graphics& g) {
    g.fillAll(kBgDeep);

    // Header Title & Slot Metadata ("Acid", "Slot 2 | CH2 | Analog Drive 1.0")
    g.setColour(kTextWhite);
    g.setFont(juce::FontOptions(22.0f, juce::Font::bold));
    g.drawText("Acid", 14, 4, 80, 24, juce::Justification::centredLeft, false);

    g.setColour(kTextMuted);
    g.setFont(juce::FontOptions(11.0f));
    g.drawText("Slot " + juce::String(slotIndex_ + 1) +
                   "  |  CH" + juce::String(midiChannelSelector_.getSelectedId()) +
                   "  |  Analog Drive 1.0",
               14, 26, 260, 16, juce::Justification::centredLeft, false);

    drawHeroBanner(g, heroBannerBounds_);

    // Row 1 Synth Cards
    drawModuleCard(g, oscCardBounds_,    "OSCILLATOR");
    drawModuleCard(g, filterCardBounds_, "FILTER");
    drawModuleCard(g, envCardBounds_,    "ENVELOPE");
    drawModuleCard(g, distCardBounds_,   "DISTORTION / DRIVE");
    drawModuleCard(g, accentCardBounds_, "ACCENT");
    drawModuleCard(g, slideCardBounds_,  "SLIDE");
    drawModuleCard(g, outputCardBounds_, "OUTPUT");

    // Row 2 Step Sequencer Card
    drawModuleCard(g, sequencerCardBounds_, "STEP SEQUENCER");

    // Row 3 Modulation, Performance, Play Mode Cards
    drawModuleCard(g, modCardBounds_,      "MODULATION");
    drawModuleCard(g, perfCardBounds_,     "PERFORMANCE");
    drawModuleCard(g, playModeCardBounds_, "PLAY MODE");
}

void AcidPanel::resized() {
    auto area = getLocalBounds().reduced(10, 6);

    // 1. Header Row
    auto headerRow = area.removeFromTop(38);
    midiChannelSelector_.setBounds(headerRow.removeFromRight(68).reduced(2, 5));
    headerRow.removeFromRight(52);
    favoriteButton_.setBounds(headerRow.removeFromRight(28).reduced(2, 5));
    nextPresetButton_.setBounds(headerRow.removeFromRight(26).reduced(2, 5));
    prevPresetButton_.setBounds(headerRow.removeFromRight(26).reduced(2, 5));
    presetSelector_.setBounds(headerRow.removeFromRight(180).reduced(2, 5));

    area.removeFromTop(4);

    // 2. 6 Sub-Tabs Row
    auto tabsRow = area.removeFromTop(26);
    const int tabW = tabsRow.getWidth() / 6;
    for (auto& btn : subTabButtons_) {
        btn.setBounds(tabsRow.removeFromLeft(tabW).reduced(2, 0));
    }

    area.removeFromTop(6);

    // 3. Hero Banner Row
    heroBannerBounds_ = area.removeFromTop(78).toFloat();
    area.removeFromTop(6);

    // 4. Row 1: OSCILLATOR, FILTER, ENVELOPE, DISTORTION / DRIVE, ACCENT+SLIDE+OUTPUT
    auto row1 = area.removeFromTop(148);
    const int r1W = row1.getWidth();
    const int gap = 6;

    auto oscRect = row1.removeFromLeft(static_cast<int>(r1W * 0.21f));
    row1.removeFromLeft(gap);
    auto fltRect = row1.removeFromLeft(static_cast<int>(r1W * 0.21f));
    row1.removeFromLeft(gap);
    auto envRect = row1.removeFromLeft(static_cast<int>(r1W * 0.22f));
    row1.removeFromLeft(gap);
    auto dstRect = row1.removeFromLeft(static_cast<int>(r1W * 0.17f));
    row1.removeFromLeft(gap);
    auto rightStack = row1;

    oscCardBounds_ = oscRect.toFloat();
    filterCardBounds_ = fltRect.toFloat();
    envCardBounds_ = envRect.toFloat();
    distCardBounds_ = dstRect.toFloat();

    auto rightTop = rightStack.removeFromTop(74);
    rightStack.removeFromTop(gap);
    outputCardBounds_ = rightStack.toFloat();

    const int halfRightW = (rightTop.getWidth() - gap) / 2;
    accentCardBounds_ = rightTop.removeFromLeft(halfRightW).toFloat();
    rightTop.removeFromLeft(gap);
    slideCardBounds_ = rightTop.toFloat();

    // Layout OSCILLATOR
    auto oscInner = oscRect.reduced(8, 4).withTrimmedTop(20);
    oscWaveBox_.setBounds(oscInner.removeFromTop(22));
    oscInner.removeFromTop(4);
    const int oscKw = oscInner.getWidth() / 4;
    for (auto& k : oscKnobs_) {
        auto c = oscInner.removeFromLeft(oscKw);
        k.slider.setBounds(c.removeFromTop(54).reduced(2));
        k.titleLabel.setBounds(c.removeFromTop(14));
        k.valueLabel.setBounds(c);
    }

    // Layout FILTER
    auto fltInner = fltRect.reduced(8, 4).withTrimmedTop(20);
    filterModeBox_.setBounds(fltInner.removeFromTop(22));
    fltInner.removeFromTop(4);
    const int fltKw = fltInner.getWidth() / 4;
    for (auto& k : filterKnobs_) {
        auto c = fltInner.removeFromLeft(fltKw);
        k.slider.setBounds(c.removeFromTop(54).reduced(2));
        k.titleLabel.setBounds(c.removeFromTop(14));
        k.valueLabel.setBounds(c);
    }

    // Layout ENVELOPE (AMP / FILTER tabs + Curve + 4 knobs)
    auto envHeader = envRect.withHeight(22).reduced(6, 3);
    envFilterTabBtn_.setBounds(envHeader.removeFromRight(48));
    envHeader.removeFromRight(3);
    envAmpTabBtn_.setBounds(envHeader.removeFromRight(40));

    auto envInner = envRect.reduced(8, 4).withTrimmedTop(22);
    envCurveView_.setBounds(envInner.removeFromTop(48));
    envInner.removeFromTop(2);
    const int envKw = envInner.getWidth() / 4;
    for (auto& k : envKnobs_) {
        auto c = envInner.removeFromLeft(envKw);
        k.titleLabel.setBounds(c.removeFromTop(12));
        k.slider.setBounds(c.removeFromTop(38).reduced(2));
        k.valueLabel.setBounds(c);
    }

    // Layout DISTORTION / DRIVE
    auto dstInner = dstRect.reduced(8, 4).withTrimmedTop(20);
    distModeBox_.setBounds(dstInner.removeFromTop(22));
    dstInner.removeFromTop(4);
    const int dstKw = dstInner.getWidth() / 3;
    for (auto& k : distKnobs_) {
        auto c = dstInner.removeFromLeft(dstKw);
        k.slider.setBounds(c.removeFromTop(54).reduced(2));
        k.titleLabel.setBounds(c.removeFromTop(14));
        k.valueLabel.setBounds(c);
    }

    // Layout ACCENT, SLIDE, OUTPUT
    auto accInner = accentCardBounds_.toNearestInt().reduced(4, 2).withTrimmedTop(18);
    accentKnob_.slider.setBounds(accInner.removeFromTop(30));
    accentKnob_.titleLabel.setBounds(accInner.removeFromTop(11));
    accentKnob_.valueLabel.setBounds(accInner);

    auto sldInner = slideCardBounds_.toNearestInt().reduced(4, 2).withTrimmedTop(18);
    slideKnob_.slider.setBounds(sldInner.removeFromTop(30));
    slideKnob_.titleLabel.setBounds(sldInner.removeFromTop(11));
    slideKnob_.valueLabel.setBounds(sldInner);

    auto outInner = outputCardBounds_.toNearestInt().reduced(6, 2).withTrimmedTop(18);
    const int outKw = outInner.getWidth() / 2;
    for (auto& k : outputKnobs_) {
        auto c = outInner.removeFromLeft(outKw);
        k.slider.setBounds(c.removeFromTop(28));
        k.titleLabel.setBounds(c.removeFromTop(11));
        k.valueLabel.setBounds(c);
    }

    area.removeFromTop(6);

    // 5. Row 2: STEP SEQUENCER
    auto seqSection = area.removeFromTop(188);
    sequencerCardBounds_ = seqSection.toFloat();
    auto seqInner = seqSection.reduced(8, 4);

    auto seqToolbar = seqInner.removeFromTop(38);
    seqToolbar.removeFromLeft(130); // Space past "STEP SEQUENCER" title
    playButton_.setBounds(seqToolbar.removeFromLeft(26).reduced(1, 6));
    stopButton_.setBounds(seqToolbar.removeFromLeft(26).reduced(1, 6));
    seqToolbar.removeFromLeft(4);
    loopStepsButton_.setBounds(seqToolbar.removeFromLeft(68).reduced(1, 6));
    seqToolbar.removeFromLeft(4);
    generateButton_.setBounds(seqToolbar.removeFromLeft(66).reduced(1, 6));
    mutateButton_.setBounds(seqToolbar.removeFromLeft(56).reduced(1, 6));
    clearButton_.setBounds(seqToolbar.removeFromLeft(48).reduced(1, 6));

    // Right side of Step Sequencer header: Length, Variation, Density, Resolution, Swing, Pattern Preset
    lengthBox_.setBounds(seqToolbar.removeFromRight(48).withTrimmedTop(14));
    seqToolbar.removeFromRight(4);
    auto varArea = seqToolbar.removeFromRight(46);
    variationKnob_.titleLabel.setBounds(varArea.removeFromTop(11));
    variationKnob_.slider.setBounds(varArea.removeFromTop(18));
    variationKnob_.valueLabel.setBounds(varArea);

    auto denArea = seqToolbar.removeFromRight(46);
    densityKnob_.titleLabel.setBounds(denArea.removeFromTop(11));
    densityKnob_.slider.setBounds(denArea.removeFromTop(18));
    densityKnob_.valueLabel.setBounds(denArea);

    resolutionBox_.setBounds(seqToolbar.removeFromRight(56).withTrimmedTop(14));
    seqToolbar.removeFromRight(4);

    auto swgArea = seqToolbar.removeFromRight(44);
    swingKnob_.titleLabel.setBounds(swgArea.removeFromTop(11));
    swingKnob_.slider.setBounds(swgArea.removeFromTop(18));
    swingKnob_.valueLabel.setBounds(swgArea);

    seqToolbar.removeFromRight(4);
    patternPresetBox_.setBounds(seqToolbar.removeFromRight(104).withTrimmedTop(14));

    seqInner.removeFromTop(2);
    stepMatrix_.setBounds(seqInner);

    area.removeFromTop(6);

    // 6. Row 3: MODULATION, PERFORMANCE, PLAY MODE
    auto row3 = area;
    const int r3W = row3.getWidth();
    auto modRect = row3.removeFromLeft(static_cast<int>(r3W * 0.56f));
    row3.removeFromLeft(gap);
    auto perfRect = row3.removeFromLeft(static_cast<int>(r3W * 0.26f));
    row3.removeFromLeft(gap);
    auto playRect = row3;

    modCardBounds_ = modRect.toFloat();
    perfCardBounds_ = perfRect.toFloat();
    playModeCardBounds_ = playRect.toFloat();

    // Layout MODULATION
    auto modHeader = modRect.withHeight(22).reduced(8, 3);
    modHeader.removeFromLeft(110);
    for (auto& btn : modTabButtons_) {
        btn.setBounds(modHeader.removeFromLeft(42).reduced(1, 0));
    }

    auto modInner = modRect.reduced(8, 4).withTrimmedTop(22);
    modCurveView_.setBounds(modInner.removeFromLeft(175).reduced(2));
    modInner.removeFromLeft(8);

    auto modRightTop = modInner.removeFromTop(20);
    modRightTop.removeFromLeft(42);
    modTargetBox_.setBounds(modRightTop.removeFromLeft(115));

    const int modKw = modInner.getWidth() / 5;
    for (auto& k : modKnobs_) {
        auto c = modInner.removeFromLeft(modKw);
        k.slider.setBounds(c.removeFromTop(38).reduced(2));
        k.titleLabel.setBounds(c.removeFromTop(12));
        k.valueLabel.setBounds(c);
    }

    // Layout PERFORMANCE
    auto perfInner = perfRect.reduced(8, 4).withTrimmedTop(22);
    const int perfKw = perfInner.getWidth() / 4;
    for (std::size_t i = 0; i < perfKnobs_.size(); ++i) {
        auto c = perfInner.removeFromLeft(perfKw);
        perfKnobs_[i].slider.setBounds(c.removeFromTop(44).reduced(2));
        perfKnobs_[i].titleLabel.setBounds(c.removeFromTop(13));
        if (i == 1) {
            unisonCountBox_.setBounds(c.reduced(2, 0));
            perfKnobs_[i].valueLabel.setVisible(false);
        } else {
            perfKnobs_[i].valueLabel.setBounds(c);
        }
    }

    // Layout PLAY MODE
    auto playInner = playRect.reduced(8, 4).withTrimmedTop(20);
    playModeBox_.setBounds(playInner.removeFromTop(20));
    playInner.removeFromTop(2);
    const int toggleH = std::max(14, playInner.getHeight() / 4);
    legatoToggle_.setBounds(playInner.removeFromTop(toggleH));
    retriggerToggle_.setBounds(playInner.removeFromTop(toggleH));
    velAccentToggle_.setBounds(playInner.removeFromTop(toggleH));
    slideOverlapToggle_.setBounds(playInner.removeFromTop(toggleH));
}

} // namespace vstengine::ui