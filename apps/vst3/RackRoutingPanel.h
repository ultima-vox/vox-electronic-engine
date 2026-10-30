#pragma once
#include "PluginProcessor.h"
#include "ui/InstrumentIdentity.h"
#include "ui/common/UiComponents.h"
#include "vox-ui/Tokens.h"
#include "vox-ui/Typography.h"
#include "vox-ui/VoxPaint.h"
// Shared Rack utility window. It does not instantiate any instrument-specific
// UI. Every control binds to an existing Rack routing parameter; painting only
// reads parameter values and the published duck envelope.
class RackRoutingPanel final : public juce::Component, private juce::Timer {
  using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
  using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
  using ComboAttachment =
      juce::AudioProcessorValueTreeState::ComboBoxAttachment;
  struct SendCell {
    juce::Slider gain;
    juce::ToggleButton pre{"PRE"};
  };
  struct Row {
    juce::Label name;
    std::array<SendCell, 4> sends;
    juce::Slider depth;
    std::string instrumentId;
  };
  struct Return {
    juce::Label title;
    juce::ComboBox type;
    juce::ToggleButton enabled{"ON"};
    std::array<juce::Slider, 5> values;
    std::array<juce::Label, 5> labels;
  };
  // Scrolled surface; paints the panel grammar behind the controls.
  struct Content final : juce::Component {
    std::function<void(juce::Graphics &)> painter;
    void paint(juce::Graphics &g) override {
      if (painter)
        painter(g);
    }
  };
  VstEngineAudioProcessor &processor;
  vstengine::ui::VoxLookAndFeel look;
  juce::Viewport viewport;
  Content content;
  juce::Label heading, explanation, reduction;
  std::array<juce::Label, 5> columns;
  std::array<Row, 16> rows;
  std::array<Return, 4> returns;
  juce::ComboBox source;
  std::array<juce::Slider, 4> timing;
  std::array<juce::Label, 5> duckLabels;
  std::vector<std::unique_ptr<SliderAttachment>> sliders;
  std::vector<std::unique_ptr<ButtonAttachment>> buttons;
  std::vector<std::unique_ptr<ComboAttachment>> combos;
  juce::Rectangle<int> matrixPanel, returnsPanel, duckPanel, envelopeArea,
      meterArea;
  std::array<juce::Rectangle<int>, 4> returnStrips;
  float shownReduction{-1.0f};

  void label(juce::Label &l, const juce::String &text, float size = 11.0f,
             juce::Colour colour = vox::ui::tokens::colour::textSecondary,
             float kerning = 0.08f) {
    l.setText(text, juce::dontSendNotification);
    l.setFont(vox::ui::paint::spaced(size, kerning));
    l.setColour(juce::Label::textColourId, colour);
    l.setInterceptsMouseClicks(false, false);
    content.addAndMakeVisible(l);
  }
  void slider(juce::Slider &s, const std::string &id, const juce::String &name,
              juce::Slider::SliderStyle style = juce::Slider::LinearBar) {
    s.setSliderStyle(style);
    if (style == juce::Slider::LinearBar)
      s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 0, 0);
    else
      s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 54, 16);
    s.setName(id);
    s.setTooltip(name);
    content.addAndMakeVisible(s);
    sliders.push_back(
        std::make_unique<SliderAttachment>(processor.parameters(), id, s));
    // Display only: whole numbers for note/ms, two decimals for gains.
    const bool whole = s.getInterval() >= 1.0 || s.getMaximum() > 10.0;
    s.textFromValueFunction = [whole](double v) {
      return whole ? juce::String(juce::roundToInt(v)) : juce::String(v, 2);
    };
    s.updateText();
    // The envelope graph follows the bound A/H/R parameters.
    if (id.rfind("rack.duck.", 0) == 0)
      s.onValueChange = [this] { content.repaint(envelopeArea.expanded(2, 14)); };
  }
  void toggle(juce::ToggleButton &b, const std::string &id) {
    b.setName(id);
    content.addAndMakeVisible(b);
    buttons.push_back(
        std::make_unique<ButtonAttachment>(processor.parameters(), id, b));
  }
  void combo(juce::ComboBox &c, const std::string &id) {
    c.setName(id);
    content.addAndMakeVisible(c);
    combos.push_back(
        std::make_unique<ComboAttachment>(processor.parameters(), id, c));
  }
  [[nodiscard]] float value(const char *id) const {
    const auto *raw = processor.parameters().getRawParameterValue(id);
    return raw != nullptr ? raw->load() : 0.0f;
  }
  void timerCallback() override {
    const auto meta = processor.rackSlotSummaries();
    bool identityChanged = false;
    for (std::size_t i = 0; i < rows.size(); ++i) {
      identityChanged |= rows[i].instrumentId != meta[i].instrumentId;
      rows[i].instrumentId = meta[i].instrumentId;
      rows[i].name.setText(meta[i].instrumentId.empty()
                               ? juce::String("Empty")
                               : juce::String(meta[i].name),
                           juce::dontSendNotification);
      rows[i].name.setColour(juce::Label::textColourId,
                             meta[i].instrumentId.empty()
                                 ? vox::ui::tokens::colour::textMuted
                                 : vox::ui::tokens::colour::text);
    }
    for (std::size_t i = 0; i < meta.size(); ++i)
      source.changeItemText(
          static_cast<int>(i + 2),
          "Slot " + juce::String(static_cast<int>(i + 1)) + " / " +
              (meta[i].instrumentId.empty() ? "Empty"
                                            : juce::String(meta[i].name)));
    const int selectedSource = source.getSelectedId() - 2;
    const bool unavailable =
        selectedSource >= 0 && selectedSource < 16 &&
        meta[static_cast<std::size_t>(selectedSource)].resolution !=
            vstengine::instrument::ResolutionStatus::resolved;
    const auto now = processor.currentDuckReduction();
    reduction.setText(
        "GR " + juce::String(now * 100.0f, 0) + "%" +
            (unavailable ? "  |  source unavailable; gain is unity" : ""),
        juce::dontSendNotification);
    reduction.setColour(juce::Label::textColourId,
                        unavailable ? vox::ui::tokens::colour::warning
                                    : vox::ui::tokens::colour::textSecondary);
    if (std::abs(now - shownReduction) > 1.0e-3f) {
      shownReduction = now;
      content.repaint(meterArea.expanded(2));
    }
    if (identityChanged)
      content.repaint(matrixPanel);
  }

  void paintContent(juce::Graphics &g) {
    namespace paint = vox::ui::paint;
    namespace colour = vox::ui::tokens::colour;
    g.fillAll(colour::background);
    for (const auto &area : {matrixPanel, returnsPanel, duckPanel})
      paint::panel(g, area.toFloat());
    paint::sectionTitle(g, matrixPanel.withHeight(34).reduced(14, 0),
                        "SEND MATRIX", false, false);
    paint::sectionTitle(g, returnsPanel.withHeight(34).reduced(14, 0),
                        "SEND RETURNS  |  100% WET", false, false);
    paint::sectionTitle(g, duckPanel.withHeight(34).reduced(14, 0),
                        "KICK DUCKING", false, false);

    // Matrix rows: alternate wells, identity dot per occupied slot.
    for (std::size_t s = 0; s < rows.size(); ++s) {
      const auto r = rows[s].name.getBounds().withX(matrixPanel.getX() + 8)
                         .withRight(matrixPanel.getRight() - 8)
                         .expanded(0, 2);
      if (s % 2 == 0) {
        g.setColour(colour::control.withAlpha(0.55f));
        g.fillRoundedRectangle(r.toFloat(), 3.0f);
      }
      g.setColour(colour::textMuted);
      g.setFont(paint::spaced(11.0f, 0.04f));
      g.drawText(juce::String(static_cast<int>(s + 1)).paddedLeft('0', 2),
                 r.withWidth(30).withTrimmedLeft(8), juce::Justification::centredLeft);
      const auto dot = juce::Rectangle<float>(7.0f, 7.0f).withCentre(
          {static_cast<float>(r.getX() + 38), static_cast<float>(r.getCentreY())});
      if (rows[s].instrumentId.empty()) {
        g.setColour(colour::borderSubtle);
        g.drawEllipse(dot, 1.0f);
      } else {
        const auto accent =
            vstengine::ui::identity::forInstrument(rows[s].instrumentId).accent;
        g.setColour(accent.withAlpha(0.25f));
        g.fillEllipse(dot.expanded(2.5f));
        g.setColour(accent);
        g.fillEllipse(dot);
      }
    }
    // Return strips.
    for (std::size_t b = 0; b < returnStrips.size(); ++b) {
      paint::panel(g, returnStrips[b].toFloat(), true);
      if (returns[b].enabled.getToggleState())
        paint::selectedOutline(g, returnStrips[b].toFloat(), 5.0f);
    }
    paintEnvelope(g);
  }

  // Duck envelope from the bound A/H/R values (ms) on a sqrt time axis, plus
  // the live gain-reduction meter published by the audio thread.
  void paintEnvelope(juce::Graphics &g) {
    namespace colour = vox::ui::tokens::colour;
    namespace paint = vox::ui::paint;
    if (envelopeArea.isEmpty())
      return;
    const auto area = envelopeArea.toFloat();
    g.setColour(colour::graph);
    g.fillRoundedRectangle(area, 4.0f);
    g.setColour(colour::borderSubtle);
    for (int i = 1; i < 4; ++i)
      g.drawHorizontalLine(
          juce::roundToInt(area.getY() + area.getHeight() * i / 4.0f),
          area.getX() + 4.0f, area.getRight() - 4.0f);
    g.drawRoundedRectangle(area, 4.0f, 1.0f);

    const auto a = std::sqrt(juce::jmax(0.0f, value("rack.duck.attack")));
    const auto h = std::sqrt(juce::jmax(0.0f, value("rack.duck.hold")));
    const auto r = std::sqrt(juce::jmax(1.0f, value("rack.duck.release")));
    const auto span = std::sqrt(100.0f) + std::sqrt(500.0f) + std::sqrt(2000.0f);
    const auto inner = area.reduced(10.0f, 12.0f);
    const auto scale = inner.getWidth() / span;
    const auto top = inner.getY(), bottom = inner.getBottom();
    // Gain curve: unity, dips to full reduction, holds, recovers.
    const auto x0 = inner.getX() + 6.0f;
    const auto x1 = x0 + a * scale, x2 = x1 + h * scale, x3 = x2 + r * scale;
    juce::Path curve;
    curve.startNewSubPath(inner.getX(), top);
    curve.lineTo(x0, top);
    curve.lineTo(x1, bottom);
    curve.lineTo(x2, bottom);
    curve.quadraticTo(x2 + (x3 - x2) * 0.25f, top + (bottom - top) * 0.1f, x3, top);
    curve.lineTo(inner.getRight(), top);
    auto fill = curve;
    fill.lineTo(inner.getRight(), top);
    fill.lineTo(inner.getX(), top);
    fill.closeSubPath();
    g.setGradientFill(juce::ColourGradient(colour::accent.withAlpha(0.05f), 0, top,
                                           colour::accent.withAlpha(0.3f), 0, bottom,
                                           false));
    g.fillPath(fill);
    g.setColour(colour::accent.withAlpha(0.3f));
    g.strokePath(curve, juce::PathStrokeType(3.0f));
    g.setColour(colour::accent);
    g.strokePath(curve, juce::PathStrokeType(1.4f));
    g.setFont(paint::spaced(9.0f, 0.1f));
    g.setColour(colour::textMuted);
    const auto mark = [&](float from, float to, const char *text) {
      g.drawText(text, juce::Rectangle<float>(from, bottom + 1.0f,
                                              juce::jmax(14.0f, to - from), 11.0f),
                 juce::Justification::centred);
    };
    mark(x0, x1, "A");
    mark(x1, x2, "H");
    mark(x2, x3, "R");

    // Vertical GR meter.
    const auto m = meterArea.toFloat();
    g.setColour(colour::graph);
    g.fillRoundedRectangle(m, 3.0f);
    const auto gr = juce::jlimit(0.0f, 1.0f, processor.currentDuckReduction());
    if (gr > 0.0f) {
      g.setColour(colour::accent);
      g.fillRoundedRectangle(m.withHeight(m.getHeight() * gr).reduced(2.0f, 0.0f)
                                 .withTrimmedTop(2.0f),
                             2.0f);
    }
    g.setColour(colour::borderSubtle);
    g.drawRoundedRectangle(m, 3.0f, 1.0f);
  }

public:
  explicit RackRoutingPanel(VstEngineAudioProcessor &p) : processor(p) {
    namespace colour = vox::ui::tokens::colour;
    setLookAndFeel(&look);
    content.painter = [this](juce::Graphics &g) { paintContent(g); };
    addAndMakeVisible(viewport);
    viewport.setViewedComponent(&content, false);
    viewport.setScrollBarsShown(true, false);
    label(heading, "RACK AUDIO ROUTING", 15.0f, colour::text, 0.14f);
    label(explanation,
          "Slots -> four wet returns -> Main. PRE taps before slot fader/pan; "
          "both taps follow ducking and mute/solo. Returns keep their tails.",
          11.0f, colour::textMuted, 0.02f);
    for (int b = 0; b < 4; ++b)
      label(columns[static_cast<std::size_t>(b)],
            "RETURN " + juce::String(b + 1), 10.0f);
    label(columns[4], "DUCK DEPTH", 10.0f);
    for (std::size_t s = 0; s < rows.size(); ++s) {
      auto &row = rows[s];
      label(row.name, "", 12.0f, colour::text, 0.02f);
      for (std::size_t b = 0; b < 4; ++b) {
        slider(row.sends[b].gain,
               vstengine::host::sendParameterId(s, b, "level"), "Send level");
        toggle(row.sends[b].pre, vstengine::host::sendParameterId(s, b, "pre"));
        row.sends[b].pre.setTooltip("Pre-fader tap (off = post fader/pan)");
      }
      slider(row.depth,
             "slot" +
                 juce::String(static_cast<int>(s + 1))
                     .paddedLeft('0', 2)
                     .toStdString() +
                 ".duck.depth",
             "Ducking depth (0 = bypass, 1 = silence at full envelope)");
    }
    const juce::StringArray types{"Distortion", "Wavefolder", "Phaser",
                                  "Flanger",    "Chorus",     "Bitcrusher",
                                  "Delay",      "Reverb"};
    constexpr const char *fields[]{"level", "pan", "amount", "rate",
                                   "character"};
    for (std::size_t b = 0; b < 4; ++b) {
      auto &r = returns[b];
      label(r.title, "RETURN " + juce::String(static_cast<int>(b + 1)), 12.0f,
            colour::text, 0.14f);
      r.type.addItemList(types, 1);
      combo(r.type, vstengine::host::returnParameterId(b, "type"));
      toggle(r.enabled, vstengine::host::returnParameterId(b, "enabled"));
      r.enabled.onStateChange = [this] { content.repaint(returnsPanel); };
      for (std::size_t v = 0; v < 5; ++v) {
        label(r.labels[v], v == 4 ? juce::String("CHAR")
                                  : juce::String(fields[v]).toUpperCase(),
              9.0f,
              colour::textSecondary, 0.1f);
        r.labels[v].setJustificationType(juce::Justification::centred);
        slider(r.values[v], vstengine::host::returnParameterId(b, fields[v]),
               fields[v], juce::Slider::RotaryHorizontalVerticalDrag);
      }
      r.values[1].getProperties().set("vox.bipolar", true);
    }
    source.addItem("Off", 1);
    for (int i = 1; i <= 16; ++i)
      source.addItem("Slot " + juce::String(i), i + 1);
    combo(source, "rack.duck.source");
    label(duckLabels[0], "KICK EVENT SOURCE", 10.0f);
    constexpr const char *ids[]{"rack.duck.note", "rack.duck.attack",
                                "rack.duck.hold", "rack.duck.release"};
    constexpr const char *names[]{"NOTE", "ATTACK MS", "HOLD MS",
                                  "RELEASE MS"};
    for (std::size_t i = 0; i < 4; ++i) {
      label(duckLabels[i + 1], names[i], 9.0f);
      duckLabels[i + 1].setJustificationType(juce::Justification::centred);
      slider(timing[i], ids[i], names[i],
             juce::Slider::RotaryHorizontalVerticalDrag);
    }
    label(reduction, "", 11.0f);
    timerCallback();
    setSize(1040, 780);
    startTimerHz(15);
  }
  ~RackRoutingPanel() override {
    stopTimer();
    setLookAndFeel(nullptr);
  }
  void paint(juce::Graphics &g) override {
    g.fillAll(vox::ui::tokens::colour::background);
  }
  void resized() override {
    viewport.setBounds(getLocalBounds().reduced(10));
    const int width = juce::jmax(
        900, viewport.getWidth() - viewport.getScrollBarThickness() - 4);
    constexpr int pad = 14, rowH = 24;
    heading.setBounds(4, 4, width - 8, 24);
    explanation.setBounds(4, 28, width - 8, 18);

    // Send matrix.
    const int matrixRows = static_cast<int>(rows.size());
    matrixPanel = {0, 54, width, 34 + 24 + matrixRows * rowH + 10};
    constexpr int nameWidth = 186, depthWidth = 140;
    const int sendWidth = (width - pad * 2 - nameWidth - depthWidth - 12) / 4;
    const int headerY = matrixPanel.getY() + 34;
    const int sendX = matrixPanel.getX() + pad + nameWidth;
    for (int b = 0; b < 4; ++b)
      columns[static_cast<std::size_t>(b)].setBounds(sendX + b * sendWidth,
                                                     headerY, sendWidth - 10, 20);
    const int depthX = sendX + sendWidth * 4 + 12;
    columns[4].setBounds(depthX, headerY, depthWidth, 20);
    for (int s = 0; s < matrixRows; ++s) {
      auto &row = rows[static_cast<std::size_t>(s)];
      const int y = headerY + 24 + s * rowH;
      row.name.setBounds(matrixPanel.getX() + pad + 36, y, nameWidth - 40,
                         rowH - 4);
      for (int b = 0; b < 4; ++b) {
        const int x = sendX + b * sendWidth;
        auto &cell = row.sends[static_cast<std::size_t>(b)];
        cell.gain.setBounds(x, y + 2, sendWidth - 62, rowH - 6);
        cell.pre.setBounds(x + sendWidth - 58, y + 2, 46, rowH - 6);
      }
      row.depth.setBounds(depthX, y + 2, depthWidth - 4, rowH - 6);
    }

    // Returns: four strips; header (title, ON, type) then five knobs.
    returnsPanel = {0, matrixPanel.getBottom() + 12, width, 34 + 176 + 12};
    const int stripW = (width - pad * 2 - 3 * 10) / 4;
    constexpr int knob = 46;
    for (int b = 0; b < 4; ++b) {
      auto &r = returns[static_cast<std::size_t>(b)];
      auto strip = juce::Rectangle<int>(pad + b * (stripW + 10),
                                        returnsPanel.getY() + 34, stripW, 176);
      returnStrips[static_cast<std::size_t>(b)] = strip;
      auto inner = strip.reduced(10, 10);
      auto top = inner.removeFromTop(24);
      r.enabled.setBounds(top.removeFromRight(44));
      r.title.setBounds(top);
      inner.removeFromTop(6);
      r.type.setBounds(inner.removeFromTop(26));
      inner.removeFromTop(10);
      const int cell = inner.getWidth() / 5;
      for (int v = 0; v < 5; ++v) {
        auto c = juce::Rectangle<int>(inner.getX() + v * cell, inner.getY(), cell,
                                      inner.getHeight());
        r.labels[static_cast<std::size_t>(v)].setBounds(c.removeFromTop(14));
        r.values[static_cast<std::size_t>(v)].setBounds(
            c.withSizeKeepingCentre(juce::jmin(cell, 54), knob + 18));
      }
    }

    // Kick ducking: source, four knobs, envelope graph, GR meter.
    duckPanel = {0, returnsPanel.getBottom() + 12, width, 34 + 120 + 14};
    auto duck = duckPanel.reduced(pad, 0).withTrimmedTop(34).withTrimmedBottom(14);
    auto left = duck.removeFromLeft(190);
    duckLabels[0].setBounds(left.removeFromTop(18));
    source.setBounds(left.removeFromTop(28));
    left.removeFromTop(10);
    reduction.setBounds(left.removeFromTop(20));
    duck.removeFromLeft(14);
    for (std::size_t i = 0; i < 4; ++i) {
      auto c = duck.removeFromLeft(78);
      duckLabels[i + 1].setBounds(c.removeFromTop(16));
      timing[i].setBounds(c.withSizeKeepingCentre(60, knob + 22));
    }
    duck.removeFromLeft(16);
    meterArea = duck.removeFromRight(14).reduced(0, 4);
    duck.removeFromRight(10);
    envelopeArea = duck.reduced(0, 4);
    content.setSize(width, duckPanel.getBottom() + 8);
  }
};
