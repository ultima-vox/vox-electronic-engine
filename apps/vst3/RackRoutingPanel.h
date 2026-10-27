#pragma once
#include "PluginProcessor.h"
#include "ui/common/UiComponents.h"
#include "vox-ui/Typography.h"
// Shared Rack utility window. It does not instantiate any instrument-specific
// UI.
class RackRoutingPanel final : public juce::Component, private juce::Timer {
  using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
  using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
  using ComboAttachment =
      juce::AudioProcessorValueTreeState::ComboBoxAttachment;
  struct SendCell {
    juce::Slider gain;
    juce::ToggleButton pre{"Pre"};
  };
  struct Row {
    juce::Label name;
    std::array<SendCell, 4> sends;
    juce::Slider depth;
  };
  struct Return {
    juce::Label title;
    juce::ComboBox type;
    juce::ToggleButton enabled{"On"};
    std::array<juce::Slider, 5> values;
    std::array<juce::Label, 5> labels;
  };
  VstEngineAudioProcessor &processor;
  vstengine::ui::VoxLookAndFeel look;
  juce::Viewport viewport;
  juce::Component content;
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
  void label(juce::Label &l, const juce::String &text) {
    l.setText(text, juce::dontSendNotification);
    l.setFont(vox::ui::typography::makeFont(11.0f));
    content.addAndMakeVisible(l);
  }
  void slider(juce::Slider &s, const std::string &id,
              const juce::String &name) {
    s.setSliderStyle(juce::Slider::LinearHorizontal);
    s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 54, 20);
    s.setName(id);
    s.setTooltip(name);
    content.addAndMakeVisible(s);
    sliders.push_back(
        std::make_unique<SliderAttachment>(processor.parameters(), id, s));
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
  void timerCallback() override {
    const auto meta = processor.rackSlotSummaries();
    for (std::size_t i = 0; i < rows.size(); ++i)
      rows[i].name.setText(
          juce::String(static_cast<int>(i + 1)).paddedLeft('0', 2) + "  " +
              (meta[i].instrumentId.empty() ? "Empty"
                                            : juce::String(meta[i].name)),
          juce::dontSendNotification);
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
    reduction.setText(
        "Duck envelope: " +
            juce::String(processor.currentDuckReduction() * 100.0f, 0) + "%" +
            (unavailable ? " / source unavailable; gain is unity" : ""),
        juce::dontSendNotification);
  }

public:
  explicit RackRoutingPanel(VstEngineAudioProcessor &p) : processor(p) {
    setLookAndFeel(&look);
    addAndMakeVisible(viewport);
    viewport.setViewedComponent(&content, false);
    label(heading, "RACK AUDIO ROUTING / SEND FX / KICK DUCKING");
    label(explanation,
          "Slots -> four wet returns -> Main. Pre/post = slot fader/pan; both "
          "taps follow ducking and mute/solo. Returns keep their tails.");
    label(columns[0], "Return 1");
    label(columns[1], "Return 2");
    label(columns[2], "Return 3");
    label(columns[3], "Return 4");
    label(columns[4], "Duck depth");
    for (std::size_t s = 0; s < rows.size(); ++s) {
      auto &row = rows[s];
      label(row.name, "");
      for (std::size_t b = 0; b < 4; ++b) {
        slider(row.sends[b].gain,
               vstengine::host::sendParameterId(s, b, "level"), "Send level");
        toggle(row.sends[b].pre, vstengine::host::sendParameterId(s, b, "pre"));
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
      label(r.title,
            "RETURN " + juce::String(static_cast<int>(b + 1)) + " / 100% WET");
      r.type.addItemList(types, 1);
      combo(r.type, vstengine::host::returnParameterId(b, "type"));
      toggle(r.enabled, vstengine::host::returnParameterId(b, "enabled"));
      for (std::size_t v = 0; v < 5; ++v) {
        label(r.labels[v], juce::String(fields[v]).toUpperCase());
        slider(r.values[v], vstengine::host::returnParameterId(b, fields[v]),
               fields[v]);
      }
    }
    source.addItem("Off", 1);
    for (int i = 1; i <= 16; ++i)
      source.addItem("Slot " + juce::String(i), i + 1);
    combo(source, "rack.duck.source");
    label(duckLabels[0], "Kick event source");
    constexpr const char *ids[]{"rack.duck.note", "rack.duck.attack",
                                "rack.duck.hold", "rack.duck.release"};
    constexpr const char *names[]{"MIDI note", "Attack / ms", "Hold / ms",
                                  "Release / ms"};
    for (std::size_t i = 0; i < 4; ++i) {
      label(duckLabels[i + 1], names[i]);
      slider(timing[i], ids[i], names[i]);
    }
    label(reduction, "");
    timerCallback();
    setSize(1040, 780);
    startTimerHz(10);
  }
  ~RackRoutingPanel() override {
    stopTimer();
    setLookAndFeel(nullptr);
  }
  void paint(juce::Graphics &g) override {
    g.fillAll(vstengine::ui::colours::background);
  }
  void resized() override {
    viewport.setBounds(getLocalBounds().reduced(10));
    content.setSize(1000, 940);
    heading.setBounds(12, 6, 970, 24);
    explanation.setBounds(12, 32, 970, 26);
    constexpr int nameWidth = 160, sendWidth = 173;
    for (int b = 0; b < 4; ++b)
      columns[b].setBounds(nameWidth + b * sendWidth, 62, sendWidth, 22);
    columns[4].setBounds(852, 62, 140, 22);
    for (int s = 0; s < 16; ++s) {
      auto &row = rows[static_cast<std::size_t>(s)];
      const int y = 86 + s * 28;
      row.name.setBounds(10, y, 148, 26);
      for (int b = 0; b < 4; ++b) {
        const int x = nameWidth + b * sendWidth;
        row.sends[b].gain.setBounds(x, y, 125, 26);
        row.sends[b].pre.setBounds(x + 125, y, 48, 26);
      }
      row.depth.setBounds(852, y, 140, 26);
    }
    for (int b = 0; b < 4; ++b) {
      auto &r = returns[static_cast<std::size_t>(b)];
      const int x = 12 + b * 247;
      r.title.setBounds(x, 550, 240, 24);
      r.type.setBounds(x, 578, 185, 24);
      r.enabled.setBounds(x + 188, 578, 54, 24);
      for (int v = 0; v < 5; ++v) {
        r.labels[v].setBounds(x, 608 + v * 29, 72, 26);
        r.values[v].setBounds(x + 72, 608 + v * 29, 170, 26);
      }
    }
    duckLabels[0].setBounds(12, 772, 172, 24);
    source.setBounds(12, 798, 172, 26);
    for (int i = 0; i < 4; ++i) {
      const int x = 196 + i * 197;
      duckLabels[i + 1].setBounds(x, 772, 190, 24);
      timing[i].setBounds(x, 798, 190, 26);
    }
    reduction.setBounds(12, 836, 500, 24);
  }
};
