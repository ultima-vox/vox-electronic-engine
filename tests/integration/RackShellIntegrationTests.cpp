#include "PluginProcessor.h"
#include "modules/BuiltInProvider.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
namespace {
int checks{};
void require(bool value, const char *what) {
  ++checks;
  if (!value) {
    std::cerr << "FAIL: " << what << '\n';
    std::exit(1);
  }
}
void set(VstEngineAudioProcessor &p, const juce::String &id, float value) {
  auto *a = p.parameters().getParameter(id);
  require(a != nullptr, "parameter exists");
  a->setValueNotifyingHost(a->convertTo0to1(value));
}
double render(VstEngineAudioProcessor &p, bool start = false) {
  juce::AudioBuffer<float> b(2, 512);
  juce::MidiBuffer m;
  if (start)
    m.addEvent(juce::MidiMessage::noteOn(3, 48, 0.8f), 0);
  p.processBlock(b, m);
  double result{};
  for (int c = 0; c < 2; ++c)
    for (int s = 0; s < 512; ++s) {
      require(std::isfinite(b.getSample(c, s)), "finite output");
      result += b.getSample(c, s) * b.getSample(c, s);
    }
  return result;
}
} // namespace
int main() {
  juce::ScopedJuceInitialiser_GUI gui;
  auto a = std::make_unique<VstEngineAudioProcessor>();
  auto b = std::make_unique<VstEngineAudioProcessor>();
  for (auto *p : {a.get(), b.get()}) {
    p->prepareToPlay(48000, 512);
    set(*p, "midiMode", 1);
    juce::String d;
    require(p->loadSlotInstrument(2, vstengine::modules::leadInstrumentId, d),
            "load real Lead fixture");
    const auto channelOf = [p](std::size_t slot) {
      return static_cast<int>(std::lround(
          p->parameters()
              .getRawParameterValue(("slot" + juce::String(static_cast<int>(slot + 1))
                                         .paddedLeft('0', 2) + "MidiIn").toRawUTF8())
              ->load()));
    };
    require(channelOf(2) == 3, "loaded instrument takes the next free channel");
    require(!p->assignSlotChannel(2, 1, d) && channelOf(2) == 3 &&
                !p->channelAvailable(2, 1),
            "a channel owned by another instrument is rejected");
    require(p->assignSlotChannel(2, 0, d) && p->channelAvailable(1, 3),
            "Off releases the channel");
    require(p->assignSlotChannel(2, 3, d), "route Lead fixture");
  }
  set(*b, "outputLevel", 0.5f);
  render(*a, true);
  render(*b, true);
  const auto full = render(*a), half = render(*b);
  require(full > 1e-8, "real DSP audible");
  require(std::abs(half / full - 0.25) < 1e-5,
          "master gain applies after Rack, independently");
  auto peak = a->consumeOutputPeaks();
  require(peak[0] > 0 && peak[1] > 0, "real stereo peaks published");
  peak = a->consumeOutputPeaks();
  require(peak[0] == 0 && peak[1] == 0, "peak consumer clears readings");
  const auto macro =
      a->parameters().getRawParameterValue("slot03Macro01")->load();
  set(*a, "outputLevel", 0);
  render(*a);
  require(render(*a) == 0, "master reaches exact silence after smoothing");
  require(a->parameters().getRawParameterValue("slot03Macro01")->load() ==
              macro,
          "master does not rewrite instrument controls");
  set(*a, "outputLevel", 1);
  set(*a, "slot03.send.2.level", 0.72f);
  set(*a, "slot03.send.2.pre", 1);
  set(*a, "rack.return.2.type", 6);
  set(*a, "rack.return.2.rate", 0.13f);
  set(*a, "rack.duck.source", 3);
  set(*a, "rack.duck.note", 48);
  set(*a, "rack.duck.hold", 200);
  set(*a, "slot02.duck.depth", 0.8f);
  juce::MemoryBlock saved;
  a->getStateInformation(saved);
  require(saved.getSize() > 0, "complete Rack state saved");
  b->setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
  for (const auto *id :
       {"slot03.send.2.level", "slot03.send.2.pre", "rack.return.2.type",
        "rack.return.2.rate", "rack.duck.source", "rack.duck.note",
        "rack.duck.hold", "slot02.duck.depth"})
    require(a->parameters().getRawParameterValue(id)->load() ==
                b->parameters().getRawParameterValue(id)->load(),
            "routing/duck controls round trip");
  require(render(*b) == 0,
          "state commit clears voices, returns and duck runtime");
  b->requestPanic();
  require(render(*b) == 0, "Panic stays silent after restore");
  for (auto *p : {a.get(), b.get()})
    p->releaseResources();
  require(render(*a) == 0, "released processor fails closed");
  a->prepareToPlay(48000, 512);
  require(render(*a) == 0, "reprepare clears runtime");
  std::cout << "Rack shell integration passed (" << checks << ")\n";
}
