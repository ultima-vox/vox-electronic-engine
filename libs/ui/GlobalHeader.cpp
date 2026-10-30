#include "GlobalHeader.h"
#include "vox-ui/Tokens.h"
#include "vox-ui/Typography.h"
#include "vox-ui/VoxPaint.h"
#include <cmath>
namespace vstengine::ui {
namespace {
using namespace vox::ui::tokens;
void label(juce::Label &l, const juce::String &text) {
  l.setText(text, juce::dontSendNotification);
  l.setFont(vox::ui::paint::spaced(11.0f, 0.04f));
  l.setColour(juce::Label::textColourId, colour::textSecondary);
  l.setJustificationType(juce::Justification::centredLeft);
  l.setInterceptsMouseClicks(false, false);
}
std::unique_ptr<juce::Drawable> icon(juce::Path path, float stroke = 1.4f) {
  auto drawable = std::make_unique<juce::DrawablePath>();
  drawable->setPath(path);
  drawable->setFill(juce::FillType(juce::Colours::transparentBlack));
  drawable->setStrokeFill(juce::FillType(colour::textSecondary));
  drawable->setStrokeType(juce::PathStrokeType(
      stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
  return drawable;
}
juce::Path gear() {
  juce::Path p;
  constexpr float c = 7.0f;
  p.addEllipse(c - 2.4f, c - 2.4f, 4.8f, 4.8f);
  for (int i = 0; i < 8; ++i) {
    const auto a = static_cast<float>(i) * juce::MathConstants<float>::pi / 4;
    p.startNewSubPath(c + std::cos(a) * 4.4f, c + std::sin(a) * 4.4f);
    p.lineTo(c + std::cos(a) * 6.6f, c + std::sin(a) * 6.6f);
  }
  p.addEllipse(c - 4.4f, c - 4.4f, 8.8f, 8.8f);
  return p;
}
juce::Path dice() {
  // Isometric cube with pips: the VOX "new seed" glyph.
  juce::Path p;
  p.startNewSubPath(8, 1);
  p.lineTo(14.5f, 4.5f);
  p.lineTo(14.5f, 11.5f);
  p.lineTo(8, 15);
  p.lineTo(1.5f, 11.5f);
  p.lineTo(1.5f, 4.5f);
  p.closeSubPath();
  p.startNewSubPath(1.5f, 4.5f);
  p.lineTo(8, 8);
  p.lineTo(14.5f, 4.5f);
  p.startNewSubPath(8, 8);
  p.lineTo(8, 15);
  for (auto [x, y] : {std::pair{8.0f, 4.4f}, {4.6f, 9.6f}, {11.4f, 9.6f}})
    p.addEllipse(x - 0.7f, y - 0.7f, 1.4f, 1.4f);
  return p;
}
} // namespace
GlobalHeader::GlobalHeader(juce::AudioProcessorValueTreeState &s, Callbacks cb)
    : state(s) {
  label(midi, "MIDI");
  label(cpu, "CPU 0%");
  label(outputLabel, "Output");
  label(seedLabel, "Seed");
  for (auto *l : {&midi, &cpu, &outputLabel, &seedLabel})
    addAndMakeVisible(*l);
  juce::Path left;
  left.startNewSubPath(9, 1);
  left.lineTo(3, 7);
  left.lineTo(9, 13);
  previousButton.setType(vox::ui::VoxButton::Type::Secondary);
  previousButton.setButtonText({});
  previousButton.setIcon(icon(left, 1.6f));
  previousButton.setTitle("Previous Rack scene");
  juce::Path right;
  right.startNewSubPath(3, 1);
  right.lineTo(9, 7);
  right.lineTo(3, 13);
  nextButton.setType(vox::ui::VoxButton::Type::Secondary);
  nextButton.setButtonText({});
  nextButton.setIcon(icon(right, 1.6f));
  nextButton.setTitle("Next Rack scene");
  seedButton.setType(vox::ui::VoxButton::Type::Icon);
  seedButton.setIcon(icon(dice(), 1.2f));
  seedButton.setTitle("New seed");
  settingsButton.setIcon(icon(gear(), 1.3f));
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
  settingsButton.setTooltip(
      "MIDI source, editor size, audio routing / sends / ducking, MIDI export");
  output.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
  output.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f,
                             juce::MathConstants<float>::pi * 2.75f, true);
  output.setTextBoxStyle(juce::Slider::TextBoxRight, false, 58, 20);
  output.setTooltip(
      "Master output after Rack and effects; double-click for unity gain");
  output.setDoubleClickReturnValue(true, 1.0);
  // The seed is a value field: drag or type; it never auto-generates.
  seed.setSliderStyle(juce::Slider::LinearBar);
  seed.getProperties().set("vox.valueField", true);
  seed.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 70, 24);
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
  seed.textFromValueFunction = [](double v) { return juce::String(v, 0); };
  seed.updateText();
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
    repaint(statusBounds);
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
  if (midiActive != active) {
    midiActive = active;
    repaint(statusBounds);
  }
}
void GlobalHeader::setCpuLoad(float load) {
  cpuValue = juce::jlimit(0.0f, 4.0f, load);
  cpu.setText("CPU " + juce::String(juce::roundToInt(cpuValue * 100.0f)) + "%",
              juce::dontSendNotification);
  cpu.setColour(juce::Label::textColourId,
                cpuValue > 0.8f ? colour::warning : colour::textSecondary);
  repaint(cpuBarBounds);
}
void GlobalHeader::setOutputPeaks(float l, float r) {
  peaks[0] = juce::jmax(l, peaks[0] * 0.8f);
  peaks[1] = juce::jmax(r, peaks[1] * 0.8f);
  repaint(meterBounds);
}
void GlobalHeader::paint(juce::Graphics &g) {
  namespace paint = vox::ui::paint;
  const auto all = getLocalBounds().toFloat();
  g.setGradientFill(juce::ColourGradient(colour::panel.brighter(0.05f), 0, 0,
                                         colour::panel, 0, all.getBottom(),
                                         false));
  g.fillRect(all);
  g.setColour(colour::border);
  g.fillRect(all.withTop(all.getBottom() - 1.0f));

  // Brand: symmetric waveform mark + spaced wordmark.
  auto b = brandBounds.toFloat();
  auto mark = b.removeFromLeft(juce::jmin(44.0f, b.getHeight() * 1.05f));
  const auto cy = mark.getCentreY();
  juce::Path wave;
  constexpr float amps[] = {0.08f, 0.2f, 0.12f, 0.42f, 0.25f, 0.85f, 0.4f,
                            1.0f,  0.55f, 0.7f, 0.3f,  0.45f, 0.15f, 0.22f,
                            0.08f};
  constexpr int n = static_cast<int>(std::size(amps));
  const auto step = mark.getWidth() / static_cast<float>(n + 1);
  const auto h = mark.getHeight() * 0.46f;
  for (int i = 0; i < n; ++i) {
    const auto x = mark.getX() + step * static_cast<float>(i + 1);
    wave.startNewSubPath(x, cy - amps[i] * h);
    wave.lineTo(x, cy + amps[i] * h);
  }
  g.setColour(colour::accent.withAlpha(0.22f));
  g.strokePath(wave, juce::PathStrokeType(3.2f, juce::PathStrokeType::curved,
                                          juce::PathStrokeType::rounded));
  g.setColour(colour::accent);
  g.strokePath(wave, juce::PathStrokeType(1.4f, juce::PathStrokeType::curved,
                                          juce::PathStrokeType::rounded));
  b.removeFromLeft(10.0f);
  // Largest wordmark size (10..17px) that fits the brand box.
  auto wordFont = paint::spaced(17.0f, 0.07f);
  const auto wordWidth = juce::GlyphArrangement::getStringWidth(
      wordFont, "VOX ELECTRONIC ENGINE");
  if (wordWidth > b.getWidth())
    wordFont = paint::spaced(
        juce::jmax(10.0f, 17.0f * b.getWidth() / wordWidth), 0.07f);
  g.setColour(colour::text);
  g.setFont(wordFont);
  g.drawText("VOX ELECTRONIC ENGINE",
             b.withHeight(b.getHeight() * 0.58f).toNearestInt(),
             juce::Justification::bottomLeft, false);
  g.setColour(colour::accent);
  // Tagline tracks to the wordmark width without clipping.
  auto tagFont = paint::spaced(8.0f, 0.62f);
  for (auto kerning = 0.62f;
       kerning > 0.1f && juce::GlyphArrangement::getStringWidth(
                             tagFont, "CREATE EVOLVE TRANSCEND") > b.getWidth();
       kerning -= 0.08f)
    tagFont = paint::spaced(8.0f, kerning);
  g.setFont(tagFont);
  g.drawText("CREATE EVOLVE TRANSCEND",
             b.withTrimmedTop(b.getHeight() * 0.62f).toNearestInt(),
             juce::Justification::topLeft, false);

  // Seed field container.
  paint::panel(g, seedBounds.toFloat(), false);

  // Telemetry: transport + MIDI dots, segmented CPU, stereo output meter.
  const auto dot = [&](juce::Rectangle<int> area, bool on, juce::Colour c) {
    const auto d = area.toFloat().withSizeKeepingCentre(7.0f, 7.0f);
    if (on) {
      g.setColour(c.withAlpha(0.25f));
      g.fillEllipse(d.expanded(2.5f));
    }
    g.setColour(on ? c : colour::textMuted);
    g.fillEllipse(d);
  };
  dot(midiDotBounds, midiActive, colour::accent);
  juce::Path play;
  const auto t = transportBounds.toFloat().withSizeKeepingCentre(7.0f, 8.0f);
  play.addTriangle(t.getX(), t.getY(), t.getX(), t.getBottom(), t.getRight(),
                   t.getCentreY());
  g.setColour(transportActive ? colour::success : colour::textMuted);
  g.fillPath(play);

  constexpr int segments = 10;
  const auto bar = cpuBarBounds.toFloat();
  const auto segW = bar.getWidth() / segments;
  const auto lit = juce::roundToInt(juce::jlimit(0.0f, 1.0f, cpuValue) *
                                    static_cast<float>(segments));
  for (int i = 0; i < (bar.getWidth() > segments * 2.0f ? segments : 0); ++i) {
    const auto seg = juce::Rectangle<float>(bar.getX() + segW * i, bar.getY(),
                                            segW - 1.5f, bar.getHeight());
    g.setColour(i < juce::jmax(lit, cpuValue > 0.0f ? 1 : 0)
                    ? (i >= 8 ? colour::warning : colour::accent)
                    : colour::borderSubtle);
    g.fillRect(seg);
  }
  for (int c = 0; c < 2; ++c)
    paint::meterBar(g,
                    meterBounds.toFloat()
                        .withHeight(3.0f)
                        .translated(0.0f, static_cast<float>(c) * 5.0f),
                    peaks[c], false);
}
void GlobalHeader::resized() {
  auto a = getLocalBounds().reduced(10, 0);
  const auto row = [](juce::Rectangle<int> r) {
    return r.withSizeKeepingCentre(r.getWidth(), 30);
  };
  // Width tiers: the settings label needs >= 1280 px, the CPU bar and the
  // full seed field >= 1120 px; below that they collapse to icon/number.
  const auto wide = getWidth() >= 1280, medium = getWidth() >= 1120;
  brandBounds = a.removeFromLeft(juce::jlimit(180, 268, getWidth() / 5 - 20))
                    .reduced(0, 9);
  a.removeFromLeft(8);
  previousButton.setBounds(row(a.removeFromLeft(30)));
  a.removeFromLeft(4);
  nextButton.setBounds(row(a.removeFromLeft(30)));
  a.removeFromLeft(8);

  // Right side, outermost first.
  auto right = a;
  settingsButton.setBounds(row(right.removeFromRight(wide ? 96 : 36)));
  settingsButton.setButtonText(wide ? "Settings" : juce::String());
  right.removeFromRight(6);
  panicButton.setBounds(row(right.removeFromRight(62)));
  right.removeFromRight(10);
  output.setBounds(row(right.removeFromRight(96)).withSizeKeepingCentre(96, 40));
  outputLabel.setBounds(row(right.removeFromRight(46)));
  right.removeFromRight(8);
  auto meter = right.removeFromRight(36);
  meterBounds = meter.withSizeKeepingCentre(36, 8);
  right.removeFromRight(10);
  auto cpuArea = right.removeFromRight(medium ? 100 : 54);
  cpu.setBounds(row(cpuArea.removeFromLeft(54)));
  cpuBarBounds = cpuArea.withSizeKeepingCentre(cpuArea.getWidth(), 10);
  right.removeFromRight(8);
  midi.setBounds(row(right.removeFromRight(34)));
  midiDotBounds = right.removeFromRight(14);
  transportBounds = right.removeFromRight(16);
  statusBounds = transportBounds.getUnion(midiDotBounds);
  right.removeFromRight(10);

  // Left cluster, adjacent: scene, save, seed. Free space sits between it
  // and the telemetry group.
  const int seedWidth = medium ? 150 : 130, saveWidth = 56;
  auto left = right;
  preset.setBounds(row(left.removeFromLeft(juce::jlimit(
      100, 240, left.getWidth() - seedWidth - saveWidth - 16))));
  left.removeFromLeft(8);
  saveButton.setBounds(row(left.removeFromLeft(saveWidth)));
  left.removeFromLeft(8);
  seedBounds = row(left.removeFromLeft(juce::jmin(seedWidth, left.getWidth())));
  auto seedInner = seedBounds.reduced(8, 3);
  seedLabel.setBounds(seedInner.removeFromLeft(34));
  seedButton.setBounds(seedInner.removeFromRight(26));
  seed.setBounds(seedInner);
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
  return preset.getWidth() >= 100;
}
} // namespace vstengine::ui
