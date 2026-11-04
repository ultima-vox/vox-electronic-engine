// Integration test: plugin project-state compatibility guard.
//
// Replicates the shell's project-state encoding exactly
// (PluginProcessor::getStateInformation):
//   APVTS XML  +  "sequenceData" attribute = base64(Sequence::serialize)
// and proves a saved blob restores an identical canonical sequence. This is
// the automated half of the "existing Cubase projects must reopen" rule from
// issue #11; the other half is the manual Cubase smoke test.
#include "preset/PresetManager.h"
#include "sequence/Sequence.h"
#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

int testsRun = 0;
int testsPassed = 0;

#define require(cond, msg)                                                   \
    do {                                                                     \
        ++testsRun;                                                          \
        if (!(cond)) {                                                       \
            std::cerr << "FAIL: " << msg << " at line " << __LINE__ << "\n"; \
            std::exit(EXIT_FAILURE);                                         \
        }                                                                    \
        ++testsPassed;                                                       \
    } while (0)

void requireEqual(const vstengine::sequence::Sequence& a,
                  const vstengine::sequence::Sequence& b)
{
    require(a.size() == b.size(), "round-trip: size preserved");
    require(a.getTimingMode() == b.getTimingMode(),
            "round-trip: timing mode preserved");
    require(a.getSelectedStart() == b.getSelectedStart(),
            "round-trip: selection start preserved");
    require(a.getSelectedEnd() == b.getSelectedEnd(),
            "round-trip: selection end preserved");
    for (int i = 0; i < a.size(); ++i) {
        const auto& sa = a[i];
        const auto& sb = b[i];
        require(sa.gate == sb.gate, "round-trip: gate");
        require(sa.noteOffset == sb.noteOffset, "round-trip: noteOffset");
        require(std::abs(sa.velocity - sb.velocity) < 1e-6f,
                "round-trip: velocity");
        require(sa.accent == sb.accent, "round-trip: accent");
        require(std::abs(sa.probability - sb.probability) < 1e-6f,
                "round-trip: probability");
        require(sa.ratchetCount == sb.ratchetCount, "round-trip: ratchetCount");
        require(std::abs(sa.slideDuration - sb.slideDuration) < 1e-6f,
                "round-trip: slideDuration");
        require(std::abs(sa.gateWidth - sb.gateWidth) < 1e-6f,
                "round-trip: gateWidth");
    }
}

} // namespace

int main()
{
    using vstengine::sequence::Sequence;

    // Build a populated sequence resembling a live session.
    Sequence seq(16);
    seq.setTimingMode(vstengine::sequence::TimingMode::triplet);
    seq.setSelectedRange(4, 11);
    for (int i = 0; i < 16; ++i) {
        seq[i].gate = (i % 3) != 0;
        seq[i].noteOffset = (i % 7) - 3;
        seq[i].velocity = 0.35f + 0.04f * static_cast<float>(i);
        seq[i].accent = (i % 5) == 2;
        seq[i].probability = (i % 4 == 0) ? 0.5f : 1.0f;
        seq[i].ratchetCount = 1 + (i % 8);
        seq[i].slideDuration = (i % 2) == 0 ? 0.0f : 0.75f;
        seq[i].gateWidth = 0.4f + 0.02f * static_cast<float>(i % 8);
    }

    // --- Shell project-state encoding (must stay byte-stable) --------------
    auto tree = juce::ValueTree("PARAMETERS");
    auto stateParam = juce::ValueTree("PARAM");
    stateParam.setProperty("id", juce::var("outputLevel"), nullptr);
    stateParam.setProperty("value", juce::var(0.42f), nullptr);
    tree.appendChild(stateParam, nullptr);

    std::unique_ptr<juce::XmlElement> xml(tree.createXml());
    juce::MemoryBlock mb;
    seq.serialize(mb);
    xml->setAttribute("sequenceData", mb.toBase64Encoding());
    const auto encoded = xml->toString();

    // --- Restore path (PluginProcessor::setStateInformation) ---------------
    auto restoredXml = juce::parseXML(encoded);
    require(restoredXml != nullptr, "state XML parses");
    const auto seqBase64 = restoredXml->getStringAttribute("sequenceData", {});
    require(seqBase64.isNotEmpty(), "sequenceData attribute present");
    juce::MemoryBlock decoded;
    require(decoded.fromBase64Encoding(seqBase64),
            "sequenceData base64 decodes");
    require(Sequence::isValidSerialization(decoded),
            "sequence blob structurally valid");
    const auto restored = Sequence::deserialize(decoded);
    requireEqual(seq, restored);

    // --- Sequence schema version stays at v2 -------------------------------
    {
        auto* raw = static_cast<const std::uint8_t*>(decoded.getData());
        const std::uint16_t version =
            static_cast<std::uint16_t>(raw[4])
            | static_cast<std::uint16_t>(static_cast<int>(raw[5]) << 8);
        require(version == 2, "sequence schema version is 2");
        require(Sequence::currentVersion == 2, "currentVersion stays 2");
    }

    // --- Rack scene preset carries the sequence blob ----------------------
    {
        const auto dir =
            juce::File::getSpecialLocation(juce::File::tempDirectory)
                .getChildFile("VstEngineStateRoundTrip");
        dir.deleteRecursively();
        dir.createDirectory();

        Sequence fresh(8);
        vstengine::PresetManager manager(
            { [&seq] {
                 juce::MemoryBlock blob;
                 seq.serialize(blob);
                 juce::ValueTree project("PARAMETERS");
                 project.setProperty("sequenceData", blob.toBase64Encoding(),
                                     nullptr);
                 return project;
             },
              [&fresh](const juce::ValueTree& project) {
                  juce::MemoryBlock blob;
                  if (!blob.fromBase64Encoding(
                          project.getProperty("sequenceData").toString())
                      || !Sequence::isValidSerialization(blob))
                      return false;
                  fresh = Sequence::deserialize(blob);
                  return true;
              } },
            dir);
        require(manager.saveFullPreset("Integration Full"),
                "scene preset saves");
        require(manager.loadFullPreset("Integration Full"),
                "scene preset reloads");
        requireEqual(seq, fresh);

        dir.deleteRecursively();
    }

    std::cout << "State round-trip tests passed (" << testsPassed << "/"
              << testsRun << ")\n";
    return EXIT_SUCCESS;
}
