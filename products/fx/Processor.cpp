#include "Processor.h"
#include <bit>
#include <cmath>

namespace vox::fx {
namespace {
preset::Section encodeParameter (float value)
{
    preset::Section section;
    section.tag = fxParameterSectionTag;
    // FPRM v1: version and IEEE-754 normalized output gain, little endian.
    for (const auto word : { std::uint32_t { 1 }, std::bit_cast<std::uint32_t> (value) })
        for (int shift = 0; shift < 32; shift += 8)
            section.data.push_back (static_cast<std::byte> ((word >> shift) & 0xffu));
    return section;
}
std::uint32_t wordAt (const std::vector<std::byte>& data, std::size_t offset)
{
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i)
        value |= std::to_integer<std::uint32_t> (data[offset + i]) << (8 * i);
    return value;
}
} // namespace

FxProcessor::FxProcessor()
    : vox::suite::SuiteProcessor ({ JucePlugin_Name,
                                    vox::preset::makeTag (VOX_SUITE_STATE_TAG),
                                    1,
                                    juce::Colour::fromString ("FF16CDBA") })
{
    output_ = new juce::AudioParameterFloat (juce::ParameterID { "fx.output.gain", 1 },
                                              "Output Gain", 0.0f, 1.0f, 1.0f);
    addParameter (output_);
}

void FxProcessor::prepareToPlay (double rate, int blockSize)
{
    SuiteProcessor::prepareToPlay (rate, blockSize);
    engine_.prepare (rate);
    outputSmoothing_.reset (std::isfinite (rate) && rate >= 8000.0 && rate <= 384000.0 ? rate : 44100.0, 0.005);
    outputSmoothing_.setCurrentAndTargetValue (output_->get());
    panicPending_.store (false, std::memory_order_relaxed);
}

void FxProcessor::releaseResources() { engine_.prepare (preparedSampleRate()); }
void FxProcessor::reset() { panicPending_.store (true, std::memory_order_release); }

void FxProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    noteMidiSeen (midi);
    buffer.clear();
    if (panicPending_.exchange (false, std::memory_order_acquire)) engine_.allSoundOff (0);
    const int size = buffer.getNumSamples();
    if (buffer.getNumChannels() < 2 || size == 0) return;
    auto* left = buffer.getWritePointer (0);
    auto* right = buffer.getWritePointer (1);
    int position = 0;
    auto renderTo = [&] (int end) {
        if (end > position) engine_.render (left + position, right + position, end - position);
        position = end;
    };
    for (const auto metadata : midi)
    {
        // Ignore malformed and out-of-block packets; never materialize a
        // MidiMessage (a long SysEx message could allocate on this thread).
        if (metadata.numBytes < 1 || metadata.samplePosition < 0 || metadata.samplePosition >= size) continue;
        const auto* bytes = metadata.data;
        const int status = bytes[0];
        const int type = status & 0xf0;
        if ((type != 0x90 && type != 0xb0) || metadata.numBytes < 3) continue;
        const int channel = (status & 0x0f) + 1;
        if (type == 0x90 && bytes[2] != 0) {
            renderTo (metadata.samplePosition);
            engine_.noteOn (channel, bytes[1] & 0x7f, static_cast<float> (bytes[2] & 0x7f) / 127.0f);
        } else if (type == 0xb0 && (bytes[1] == 120 || bytes[1] == 123)) {
            renderTo (metadata.samplePosition);
            engine_.allSoundOff (channel);
        }
        // NoteOff and zero-velocity NoteOn are deliberately ignored in this
        // interim one-shot mode. CC120/123 and reset use a 5 ms fade.
    }
    renderTo (size);
    const auto target = output_->get();
    outputSmoothing_.setTargetValue (std::isfinite (target) ? juce::jlimit (0.0f, 1.0f, target) : 0.0f);
    for (int i = 0; i < size; ++i) {
        const auto gain = outputSmoothing_.getNextValue();
        left[i] *= gain;
        right[i] *= gain;
    }
}

void FxProcessor::writeProductState (std::vector<preset::Section>& out) const
{
    out.push_back (encodeParameter (output_->get()));
}

FxProcessor::ProductRestore FxProcessor::readProductState (const preset::StateDocument& doc, juce::String& error)
{
    float value = 1.0f; // FOUND-01 shell states have no FPRM section.
    if (const auto* section = doc.find (fxParameterSectionTag)) {
        if (section->data.size() != 8 || wordAt (section->data, 0) != 1) {
            error = "Unsupported or malformed FX parameter section";
            return {};
        }
        value = std::bit_cast<float> (wordAt (section->data, 4));
        if (! std::isfinite (value) || value < 0.0f || value > 1.0f) {
            error = "Invalid FX output gain";
            return {};
        }
    }
    ProductRestore result;
    result.sections.push_back (encodeParameter (value));
    result.apply = [this, value] { output_->setValueNotifyingHost (value); };
    result.settle = [] {};
    return result;
}
} // namespace vox::fx

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new vox::fx::FxProcessor(); }
