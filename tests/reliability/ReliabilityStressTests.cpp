#include "PluginProcessor.h"
#include "instrument/HostParameterSchema.h"
#include "modules/BuiltInProvider.h"
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <thread>

namespace {
std::atomic<int> checks {};

void require(bool condition, const char* message)
{
    checks.fetch_add(1, std::memory_order_relaxed);
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

class HostPlayHead final : public juce::AudioPlayHead {
public:
    Optional<PositionInfo> getPosition() const override
    {
        PositionInfo result;
        result.setBpm(bpm.load(std::memory_order_relaxed));
        result.setPpqPosition(ppq.load(std::memory_order_relaxed));
        result.setTimeInSamples(samples.load(std::memory_order_relaxed));
        result.setIsPlaying(playing.load(std::memory_order_relaxed));
        return result;
    }
    void advance(int blockSize, double sampleRate)
    {
        samples.fetch_add(blockSize, std::memory_order_relaxed);
        ppq.store(ppq.load(std::memory_order_relaxed)
            + blockSize / sampleRate * bpm.load(std::memory_order_relaxed) / 60.0,
            std::memory_order_relaxed);
    }
    std::atomic<double> bpm { 145.0 };
    std::atomic<double> ppq { 0.0 };
    std::atomic<std::int64_t> samples { 0 };
    std::atomic<bool> playing { true };
};

void setPlain(VstEngineAudioProcessor& processor, const std::string& id,
              float value)
{
    auto* parameter = processor.parameters().getParameter(id);
    require(parameter != nullptr, "required parameter exists");
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

void verifyFinite(const juce::AudioBuffer<float>& audio)
{
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
        for (int sample = 0; sample < audio.getNumSamples(); ++sample)
            require(std::isfinite(audio.getSample(channel, sample)),
                    "render remains finite");
}

void render(VstEngineAudioProcessor& processor, HostPlayHead& playHead,
            double sampleRate, int blockSize, int channel, int note)
{
    juce::AudioBuffer<float> audio(2, blockSize);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(channel, note, 0.8f), 0);
    midi.addEvent(juce::MidiMessage::controllerEvent(channel, 1,
        (note * 3) & 127), juce::jmin(1, blockSize - 1));
    midi.addEvent(juce::MidiMessage::noteOff(channel, note, 0.0f),
                  blockSize - 1);
    processor.processBlock(audio, midi);
    verifyFinite(audio);
    playHead.advance(blockSize, sampleRate);
}

juce::MemoryBlock capture(VstEngineAudioProcessor& processor)
{
    juce::MemoryBlock result;
    processor.getStateInformation(result);
    require(result.getSize() != 0, "project state capture succeeds");
    return result;
}

void removeChildren(juce::ValueTree& tree, const juce::Identifier& type)
{
    for (;;) {
        const auto child = tree.getChildWithName(type);
        if (!child.isValid()) return;
        tree.removeChild(child, nullptr);
    }
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juce;
    VstEngineAudioProcessor processor;
    HostPlayHead playHead;
    processor.setPlayHead(&playHead);
    setPlain(processor, "midiMode", 3.0f);

    for (const double sampleRate : { 44100.0, 48000.0, 96000.0 })
        for (const int blockSize : { 16, 64, 511, 2048 }) {
            processor.prepareToPlay(sampleRate, blockSize);
            for (const double bpm : { 20.0, 145.0, 400.0 }) {
                playHead.bpm.store(bpm, std::memory_order_relaxed);
                for (int block = 0; block < 5; ++block)
                    render(processor, playHead, sampleRate, blockSize,
                           1 + block % 2, 36 + block);
            }
            processor.releaseResources();
        }

    processor.prepareToPlay(48000.0, 512);
    const auto baseline = capture(processor);
    for (const int size : { 0, 1, 7, 16,
                            static_cast<int>(baseline.getSize() / 2) }) {
        processor.setStateInformation(size == 0 ? nullptr : baseline.getData(), size);
        require(capture(processor) == baseline,
                "truncated project state rejected transactionally");
    }

    auto xml = juce::AudioProcessor::getXmlFromBinary(
        baseline.getData(), static_cast<int>(baseline.getSize()));
    require(xml != nullptr, "captured project XML decodes");
    auto invalidTree = juce::ValueTree::fromXml(*xml);
    auto invalidRack = invalidTree.getChildWithName("RACK");
    require(invalidRack.isValid() && invalidRack.getNumChildren() == 16,
            "captured project contains canonical rack");
    invalidRack.getChild(0).setProperty("keyLow", 120, nullptr);
    invalidRack.getChild(0).setProperty("keyHigh", 12, nullptr);
    juce::MemoryBlock semanticCorruption;
    if (auto invalidXml = invalidTree.createXml())
        juce::AudioProcessor::copyXmlToBinary(*invalidXml, semanticCorruption);
    processor.setStateInformation(semanticCorruption.getData(),
                                  static_cast<int>(semanticCorruption.getSize()));
    require(capture(processor) == baseline,
            "semantically invalid rack state rolls back exactly");

    // Owner-authorized POC replacement: engine v5 explicitly rejects old
    // complete projects. Rejection must preserve the entire committed state;
    // it must never silently reinterpret an old parameter/pattern layout.
    for (const int obsoleteSchema : {1, 2, 3, 4}) {
      auto obsoleteTree = juce::ValueTree::fromXml(*xml);
      removeChildren(obsoleteTree, "RACK");
      removeChildren(obsoleteTree, "EFFECTS");
      obsoleteTree.setProperty("stateSchemaVersion", obsoleteSchema, nullptr);
      auto changed = obsoleteTree.getChildWithProperty("id", "outputLevel");
      require(changed.isValid(), "obsolete fixture has a real parameter");
      changed.setProperty("value", 0.123f, nullptr);
      juce::MemoryBlock obsolete;
      if (auto obsoleteXml = obsoleteTree.createXml())
        juce::AudioProcessor::copyXmlToBinary(*obsoleteXml, obsolete);
      processor.setStateInformation(obsolete.getData(),
                                    static_cast<int>(obsolete.getSize()));
      require(capture(processor) == baseline,
              "obsolete project rejection preserves complete committed state "
              "exactly");
    }
    const auto retained = capture(processor);
    auto retainedXml = juce::AudioProcessor::getXmlFromBinary(
        retained.getData(), static_cast<int>(retained.getSize()));
    require(retainedXml != nullptr, "retained current project serializes");
    const auto retainedTree = juce::ValueTree::fromXml(*retainedXml);
    require(retainedTree.getChildWithName("RACK").isValid() &&
                retainedTree.getChildWithName("EFFECTS").isValid() &&
                static_cast<int>(
                    retainedTree.getProperty("stateSchemaVersion")) == 5,
            "rejected POC state leaves the canonical current project intact");

    std::atomic<bool> mutationDone { false };
    std::thread parameterMutator([&] {
        for (int iteration = 0; iteration < 3000; ++iteration) {
            const auto slot = static_cast<std::size_t>(iteration % 16);
            setPlain(processor, "slot" + juce::String(static_cast<int>(slot + 1)).paddedLeft('0', 2).toStdString()
                + "Level", static_cast<float>(iteration % 101) / 100.0f);
            setPlain(processor,
                vstengine::instrument::hostparams::macroId(slot, iteration % 8),
                static_cast<float>((iteration * 17) % 101) / 100.0f);
        }
        mutationDone.store(true, std::memory_order_release);
    });
    for (int iteration = 0; iteration < 256; ++iteration) {
        const auto slot = static_cast<std::size_t>(iteration % 16);
        juce::String diagnostic;
        const auto action = static_cast<VstEngineAudioProcessor::ChannelConflictAction>(
            1 + iteration % 3);
        require(processor.assignSlotChannel(slot, 1 + iteration % 16,
                    action, diagnostic),
                "rapid routing conflict action stays deterministic");
        if (iteration % 17 == 0) processor.requestPanic();
        if (iteration % 31 == 0)
          processor.setStateInformation(retained.getData(),
                                        static_cast<int>(retained.getSize()));
        playHead.bpm.store(20.0 + (iteration * 37) % 381,
                           std::memory_order_relaxed);
        render(processor, playHead, 48000.0, 512,
               1 + iteration % 16, 36 + iteration % 48);
    }
    parameterMutator.join();
    require(mutationDone.load(std::memory_order_acquire),
            "rapid automation thread completes without deadlock");

    for (int cycle = 0; cycle < 32; ++cycle) {
        const double rate = cycle % 2 == 0 ? 44100.0 : 96000.0;
        const int block = cycle % 3 == 0 ? 64 : cycle % 3 == 1 ? 511 : 2048;
        processor.prepareToPlay(rate, block);
        render(processor, playHead, rate, block, 1 + cycle % 16,
               40 + cycle % 36);
        processor.releaseResources();
    }
    processor.setPlayHead(nullptr);
    std::cout << "Reliability stress passed ("
              << checks.load(std::memory_order_relaxed) << ")\n";
    return EXIT_SUCCESS;
}
