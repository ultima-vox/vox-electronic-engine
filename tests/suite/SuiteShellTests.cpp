// FOUND-01 shell tests, built once per product against that product's plugin
// shared code: lifecycle at every supported rate and block size (including 0),
// silence and finiteness, MIDI acceptance, editor open/close and the
// transactional VOXS state contract.
#include "Processor.h"

#ifdef VOX_TEST_INTERPOSE_MALLOC
#include "AllocationProbe.h"
#endif

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>

namespace {
int failures = 0;
unsigned long probedAllocations = 0;

void check (bool condition, const juce::String& what)
{
    if (! condition)
    {
        std::printf ("FAIL [%s]: %s\n", VOX_TEST_PRODUCT_NAME, what.toRawUTF8());
        ++failures;
    }
}

std::unique_ptr<vox::suite::SuiteProcessor> makeProcessor()
{
    auto* plugin = createPluginFilter();
    auto* suite = dynamic_cast<vox::suite::SuiteProcessor*> (plugin);
    if (suite == nullptr)
        delete plugin;
    return std::unique_ptr<vox::suite::SuiteProcessor> (suite);
}

juce::MemoryBlock stateOf (vox::suite::SuiteProcessor& p)
{
    juce::MemoryBlock block;
    p.getStateInformation (block);
    return block;
}

juce::MemoryBlock encoded (const vox::preset::StateDocument& document)
{
    const auto bytes = vox::preset::encode (document);
    return bytes ? juce::MemoryBlock (bytes->data(), bytes->size()) : juce::MemoryBlock();
}

vox::preset::StateDocument documentWithUi (vox::preset::Tag product, std::uint32_t schema,
                                           std::uint32_t width, std::uint32_t height)
{
    vox::preset::StateDocument document;
    document.product = product;
    document.schema = schema;
    vox::preset::Section ui;
    ui.tag = vox::suite::uiSectionTag;
    for (auto value : { width, height })
        for (int shift = 0; shift < 32; shift += 8)
            ui.data.push_back (static_cast<std::byte> ((value >> shift) & 0xffu));
    document.sections.push_back (std::move (ui));
    return document;
}

void testIdentity()
{
    auto p = makeProcessor();
    check (p != nullptr, "createPluginFilter returns a suite processor");
    if (p == nullptr)
        return;
    check (p->getName() == VOX_TEST_PRODUCT_NAME, "product name");
    check (p->acceptsMidi() && ! p->producesMidi() && ! p->isMidiEffect(), "MIDI in only");
    check (p->getTotalNumInputChannels() == 0, "no audio input (MIDI-triggered instrument)");
    check (p->getTotalNumOutputChannels() == 2, "stereo main output");
    check (p->product().stateTag == vox::preset::makeTag (VOX_TEST_STATE_TAG), "state product tag");
    check (p->getNumPrograms() == 1 && p->getProgramName (0).isNotEmpty(), "one named program (VST3 validator)");
}

void testLifecycle()
{
    auto p = makeProcessor();
    for (double rate : { 44100.0, 48000.0, 96000.0 })
    {
        for (int block : { 32, 64, 128, 512 })
        {
            p->setRateAndBufferSizeDetails (rate, block);
            p->prepareToPlay (rate, block);
            check (juce::exactlyEqual (p->preparedSampleRate(), rate) && p->preparedBlockSize() == block, "prepare records format");

            const auto before = p->midiEventsSeen();
            int sent = 0;
            for (int size : { block, block / 3, 0, 1, block })
            {
                juce::AudioBuffer<float> buffer (2, size);
                for (int ch = 0; ch < 2; ++ch)
                    juce::FloatVectorOperations::fill (buffer.getWritePointer (ch), 1.0f, size);

                juce::MidiBuffer midi;
                if (size > 0)
                {
                    midi.addEvent (juce::MidiMessage::noteOn (1, 36, 0.8f), 0);
                    midi.addEvent (juce::MidiMessage::noteOff (1, 36), size - 1);
                    midi.addEvent (juce::MidiMessage::allNotesOff (1), size / 2);
                    midi.addEvent (juce::MidiMessage::allSoundOff (1), size / 2);
                    sent += 4;
                }
#ifdef VOX_TEST_INTERPOSE_MALLOC
                voxProbeBegin();
                p->processBlock (buffer, midi);
                probedAllocations += voxProbeEnd();
#else
                p->processBlock (buffer, midi);
#endif

                bool silentAndFinite = true;
                for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                    for (int i = 0; i < size; ++i)
                        silentAndFinite = silentAndFinite && std::isfinite (buffer.getSample (ch, i))
                                       && std::fpclassify (buffer.getSample (ch, i)) == FP_ZERO;
                check (silentAndFinite, "shell output is silent and finite at "
                                          + juce::String (rate) + " Hz, block " + juce::String (size));
            }
            check (p->midiEventsSeen() - before == static_cast<std::uint64_t> (sent), "every MIDI event is seen");
            p->releaseResources();
        }
    }
#ifdef VOX_TEST_INTERPOSE_MALLOC
    if (voxProbeAvailable())
        check (probedAllocations == 0, "processBlock allocates nothing on the audio thread ("
                                           + juce::String (static_cast<juce::int64> (probedAllocations)) + " calls)");
#endif
}

void testEditor()
{
    auto p = makeProcessor();
    for (int round = 0; round < 3; ++round)
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (p->createEditorAndMakeActive());
        check (editor != nullptr, "editor opens");
        if (editor == nullptr)
            return;
        check (editor->getWidth() == p->editorWidth() && editor->getHeight() == p->editorHeight(),
               "editor opens at the stored size");
        editor->setSize (1300, 800);
        check (p->editorWidth() == 1300 && p->editorHeight() == 800, "editor size is stored");
        juce::Image image (juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true);
        juce::Graphics g (image);
        editor->paintEntireComponent (g, false);
        p->editorBeingDeleted (editor.get());
    }
}

void testState()
{
    const auto tag = vox::preset::makeTag (VOX_TEST_STATE_TAG);
    const auto foreignTag = vox::preset::makeTag (VOX_TEST_FOREIGN_TAG);

    auto source = makeProcessor();
    source->setEditorSize (1400, 900);
    const auto saved = stateOf (*source);
    const auto decoded = vox::preset::decode (
        std::span (static_cast<const std::byte*> (saved.getData()), saved.getSize()), tag, 1);
    check (decoded.ok() && decoded.document.schema == 1, "saved state is VOXS schema 1 of this product");

    auto target = makeProcessor();
    target->setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    check (target->lastStateError().isEmpty(), "valid state accepted");
    check (target->editorWidth() == 1400 && target->editorHeight() == 900, "UI state restored");
    check (stateOf (*target) == saved, "restore then save is byte-identical");

    // Each rejected blob must leave the live state untouched.
    const auto expectRejected = [&] (const juce::MemoryBlock& blob, const juce::String& what) {
        target->setStateInformation (blob.getData(), static_cast<int> (blob.getSize()));
        check (target->lastStateError().isNotEmpty(), what + ": rejected with a diagnostic");
        check (target->editorWidth() == 1400 && target->editorHeight() == 900, what + ": live state unchanged");
        check (stateOf (*target) == saved, what + ": saved state unchanged");
    };

    expectRejected (encoded (documentWithUi (foreignTag, 1, 1200, 760)), "another product's state");
    expectRejected (encoded (documentWithUi (tag, 2, 1200, 760)), "newer schema");
    expectRejected (encoded (documentWithUi (tag, 1, 10, 10)), "UI size out of range");
    auto noUi = documentWithUi (tag, 1, 1200, 760);
    noUi.sections.clear();
    expectRejected (encoded (noUi), "missing UI section");

    auto truncated = saved;
    truncated.setSize (saved.getSize() - 3);
    expectRejected (truncated, "truncated state");

    auto flipped = saved;
    static_cast<char*> (flipped.getData())[20] ^= 0x40;
    expectRejected (flipped, "corrupted state");

    juce::Random random (1234);
    juce::MemoryBlock garbage (512);
    random.fillBitsRandomly (garbage.getData(), garbage.getSize());
    expectRejected (garbage, "random bytes");

    // A legacy Rack/VstEngine style blob (JUCE binary XML) is not imported.
    juce::MemoryBlock legacy;
    juce::AudioProcessor::copyXmlToBinary (juce::XmlElement ("VstEngine"), legacy);
    expectRejected (legacy, "legacy Rack project state");

    target->setStateInformation (nullptr, 0);
    check (target->lastStateError().isNotEmpty() && target->editorWidth() == 1400, "empty state rejected");

    // Unknown sections from a newer minor writer are kept and written back.
    auto withExtra = documentWithUi (tag, 1, 1100, 720);
    withExtra.sections.push_back ({ vox::preset::makeTag ("ZZZZ"), { std::byte { 7 } } });
    const auto extraBlob = encoded (withExtra);
    target->setStateInformation (extraBlob.getData(), static_cast<int> (extraBlob.getSize()));
    check (target->lastStateError().isEmpty() && target->editorWidth() == 1100, "state with an unknown section accepted");
    const auto resaved = stateOf (*target);
    const auto redecoded = vox::preset::decode (
        std::span (static_cast<const std::byte*> (resaved.getData()), resaved.getSize()), tag, 1);
    const auto* extra = redecoded.document.find (vox::preset::makeTag ("ZZZZ"));
    check (extra != nullptr && extra->data.size() == 1 && extra->data[0] == std::byte { 7 },
           "unknown section preserved on save");
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juce;
    testIdentity();
    testLifecycle();
    testEditor();
    testState();
    if (failures == 0)
        std::printf ("%s shell tests passed\n", VOX_TEST_PRODUCT_NAME);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
