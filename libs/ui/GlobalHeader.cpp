#include "GlobalHeader.h"
#include "vox-ui/Tokens.h"
#include "vox-ui/Typography.h"
#include <cmath>
namespace vstengine::ui {
namespace {
void label(juce::Label &l, const juce::String &text) {
  l.setText(text, juce::dontSendNotification);
  l.setFont(vox::ui::typography::makeFont(10.0f));
  l.setColour(juce::Label::textColourId,
              vox::ui::tokens::colour::textSecondary);
  l.setJustificationType(juce::Justification::centred);
  l.setInterceptsMouseClicks(false, false);
}
} // namespace
GlobalHeader::GlobalHeader(juce::AudioProcessorValueTreeState &s, Callbacks cb)
    : state(s) {
  label(midi, "MIDI");
  label(cpu, "CPU 0%");
  label(outputLabel, "OUTPUT");
  label(seedLabel, "SEED");
  for (auto *l : {&midi, &cpu, &outputLabel, &seedLabel})
    addAndMakeVisible(*l);
  const auto icon = [](juce::Path path) {
    auto drawable = std::make_unique<juce::DrawablePath>();
    drawable->setPath(path);
    drawable->setFill(juce::FillType(juce::Colours::transparentBlack));
    drawable->setStrokeFill(
        juce::FillType(vox::ui::tokens::colour::textSecondary));
    drawable->setStrokeType(juce::PathStrokeType(1.4f));
    return drawable;
  };
  juce::Path left;
  left.startNewSubPath(8, 1);
  left.lineTo(2, 7);
  left.lineTo(8, 13);
  previousButton.setIcon(icon(left));
  juce::Path right;
  right.startNewSubPath(2, 1);
  right.lineTo(8, 7);
  right.lineTo(2, 13);
  nextButton.setIcon(icon(right));
  juce::Path dice;
  dice.addRoundedRectangle(0, 0, 14, 14, 2);
  dice.addEllipse(3, 3, 1, 1);
  dice.addEllipse(10, 3, 1, 1);
  dice.addEllipse(6.5f, 6.5f, 1, 1);
  dice.addEllipse(3, 10, 1, 1);
  dice.addEllipse(10, 10, 1, 1);
  seedButton.setType(vox::ui::VoxButton::Type::Icon);
  seedButton.setIcon(icon(dice));
  preset.setTextWhenNothingSelected("Init Rack");
  preset.setTooltip(
      "Complete Rack scenes only: instruments, patterns, routing and effects");
  addAndMakeVisible(preset);
  for (auto *b : {&previousButton, &nextButton, &saveButton, &seedButton,
                  &panicButton, &settingsButton})
    addAndMakeVisible(*b);
  previousButton.onClick = std::move(cb.previous);
  nextButton.onClick = std::move(cb.next);
  saveButton.onClick = std::move(cb.save);
  panicButton.onClick = std::move(cb.panic);
  settingsButton.onClick = std::move(cb.settings);
  preset.onChange = [this, choose = std::move(cb.choosePreset)] {
    if (choose)
      choose(preset.getSelectedItemIndex());
  };
  seedButton.onClick = [this] { randomizeSeed(); };
  seedButton.setTooltip("Choose a new deterministic seed; leaves current "
                        "sounds and patterns unchanged");
  panicButton.setTooltip(
      "Immediately release Rack voices and clear effect tails");
  output.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
  output.setTextBoxStyle(juce::Slider::TextBoxRight, false, 62, 20);
  output.setTooltip(
      "Master output after Rack and effects; double-click for unity gain");
  output.setDoubleClickReturnValue(true, 1.0);
  seed.setSliderStyle(juce::Slider::IncDecButtons);
  seed.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 70, 24);
  seed.setIncDecButtonsMode(juce::Slider::incDecButtonsNotDraggable);
  seed.setTooltip(
      "Deterministic generation seed; editing does not automatically generate");
  seed.textFromValueFunction = [](double v) { return juce::String(v, 0); };
  for (auto *slider : {&output, &seed})
    addAndMakeVisible(*slider);
  outputAttachment =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          state, "outputLevel", output);
  seedAttachment =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          state, "rngSeed", seed);
  // SliderAttachment installs parameter formatters; set the visible dB unit
  // afterwards.
  output.textFromValueFunction = [](double v) {
    return v <= 0.0 ? juce::String("-inf")
                    : juce::String(20.0 * std::log10(v), 1) + " dB";
  };
  output.valueFromTextFunction = [](const juce::String &t) {
    return t.containsIgnoreCase("inf")
               ? 0.0
               : std::pow(10.0, t.getDoubleValue() / 20.0);
  };
  output.updateText();
}
void GlobalHeader::randomizeSeed() {
  if (auto *p = state.getParameter("rngSeed")) {
    // Exactly representable in APVTS' float snapshot, including all low bits.
    const auto value =
        static_cast<float>(juce::Random::getSystemRandom().nextInt(0x1000000));
    p->beginChangeGesture();
    p->setValueNotifyingHost(p->convertTo0to1(value));
    p->endChangeGesture();
  }
}
void GlobalHeader::setPresetName(const juce::String &name) {
  preset.setText(name.isEmpty() ? "Unsaved Rack" : name,
                 juce::dontSendNotification);
}
void GlobalHeader::setTransportActive(bool active) {
  if (transportActive != active) {
    transportActive = active;
    repaint();
  }
}
void GlobalHeader::setPresetEntries(const juce::StringArray &names,
                                    int selected) {
  if (presetNames != names) {
    presetNames = names;
    preset.clear(juce::dontSendNotification);
    preset.addItemList(names, 1);
  }
  preset.setSelectedItemIndex(selected, juce::dontSendNotification);
  previousButton.setEnabled(!names.isEmpty());
  nextButton.setEnabled(!names.isEmpty());
}
void GlobalHeader::setMidiActivity(bool active) {
  midi.setColour(juce::Label::textColourId,
                 active ? vox::ui::tokens::colour::accent
                        : vox::ui::tokens::colour::textMuted);
}
void GlobalHeader::setCpuLoad(float load) {
  cpuValue = juce::jlimit(0.0f, 4.0f, load);
  cpu.setText("CPU " + juce::String(juce::roundToInt(cpuValue * 100.0f)) + "%",
              juce::dontSendNotification);
  cpu.setColour(juce::Label::textColourId,
                cpuValue > 0.8f ? vox::ui::tokens::colour::warning
                                : vox::ui::tokens::colour::textSecondary);
}
void GlobalHeader::setOutputPeaks(float l, float r) {
  peaks[0] = juce::jmax(l, peaks[0] * 0.8f);
  peaks[1] = juce::jmax(r, peaks[1] * 0.8f);
  repaint(meterBounds);
}
void GlobalHeader::paint(juce::Graphics &g) {
  using namespace vox::ui::tokens;
  g.fillAll(colour::panel);
  g.setColour(colour::border);
  g.drawRect(getLocalBounds(), 1);
  auto b = brandBounds.toFloat();
  juce::Path wave;
  const auto x = b.getX() + 3, cy = b.getCentreY();
  wave.startNewSubPath(x, cy);
  constexpr float y[] = {0, -4, 8, -18, 14, -9, 4, -2, 0};
  for (int i = 0; i < 9; ++i)
    wave.lineTo(x + static_cast<float>(i) * 4, cy + y[i]);
  g.setColour(colour::accent);
  g.strokePath(wave, juce::PathStrokeType(1.5f));
  g.setFont(vox::ui::typography::makeFont(12.5f));
  g.setColour(colour::text);
  g.drawText("VOX ELECTRONIC ENGINE",
             brandBounds.withTrimmedLeft(42).withHeight(24),
             juce::Justification::centredLeft);
  g.setFont(vox::ui::typography::makeFont(8.5f));
  g.setColour(colour::accentDim);
  g.drawText("C R E A T E   E V O L V E   T R A N S C E N D",
             brandBounds.withTrimmedLeft(42).withTrimmedTop(23),
             juce::Justification::centredLeft);
  for (int c = 0; c < 2; ++c) {
    auto row = meterBounds.withHeight(4).translated(0, c * 6);
    g.setColour(colour::control);
    g.fillRect(row);
    const auto db = peaks[c] > 0 ? 20.0f * std::log10(peaks[c]) : -60.0f;
    g.setColour(peaks[c] >= 1.0f ? colour::danger : colour::accent);
    g.fillRect(row.withWidth(juce::roundToInt(
        row.getWidth() * juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f))));
  }
  g.setColour(transportActive ? colour::success : colour::textMuted);
  g.fillEllipse(static_cast<float>(midi.getRight() - 3), 11.0f, 3.0f, 3.0f);
}
void GlobalHeader::resized() {
  auto a = getLocalBounds().reduced(8, 6);
  brandBounds = a.removeFromLeft(238);
  previousButton.setBounds(a.removeFromLeft(26).reduced(1, 5));
  nextButton.setBounds(a.removeFromLeft(26).reduced(1, 5));
  const int presetWidth = juce::jlimit(100, 205, getWidth() - 940);
  preset.setBounds(a.removeFromLeft(presetWidth).reduced(2, 5));
  saveButton.setBounds(a.removeFromLeft(48).reduced(2, 5));
  seedLabel.setBounds(a.removeFromLeft(30));
  seed.setBounds(a.removeFromLeft(90).reduced(1, 6));
  seedButton.setBounds(a.removeFromLeft(38).reduced(1, 5));
  settingsButton.setBounds(a.removeFromRight(70).reduced(1, 5));
  panicButton.setBounds(a.removeFromRight(51).reduced(1, 5));
  output.setBounds(a.removeFromRight(114).reduced(0, 1));
  outputLabel.setBounds(a.removeFromRight(43));
  auto meter = a.removeFromRight(46);
  meterBounds = meter.withSizeKeepingCentre(40, 10);
  cpu.setBounds(a.removeFromRight(58));
  midi.setBounds(a.removeFromRight(40));
}
bool GlobalHeader::controlsFit() const noexcept {
  const std::array<const juce::Component *, 14> controls{
      &preset,      &previousButton, &nextButton, &saveButton, &seedButton,
      &panicButton, &settingsButton, &output,     &seed,       &midi,
      &cpu,         &outputLabel,    &seedLabel,  this};
  for (std::size_t i = 0; i + 1 < controls.size(); ++i) {
    if (controls[i]->getBounds().isEmpty() ||
        !getLocalBounds().contains(controls[i]->getBounds()))
      return false;
    for (std::size_t j = i + 1; j + 1 < controls.size(); ++j)
      if (controls[i]->getBounds().intersects(controls[j]->getBounds()))
        return false;
  }
  return true;
}
}
