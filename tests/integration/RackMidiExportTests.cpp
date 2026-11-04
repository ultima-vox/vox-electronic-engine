#include "PluginProcessor.h"
#include "modules/BuiltInProvider.h"
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <set>
#include <vector>

namespace {
int checks{};
void require(bool value, const char *message) {
  ++checks;
  if (!value) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}
void set(VstEngineAudioProcessor &processor, const std::string &id,
         float plain) {
  auto *parameter = processor.parameters().getParameter(id);
  require(parameter != nullptr, "actual host endpoint exists");
  parameter->setValueNotifyingHost(parameter->convertTo0to1(plain));
}
std::string endpoint(std::size_t slot, const char *suffix) {
  return "slot" +
         juce::String(static_cast<int>(slot + 1))
             .paddedLeft('0', 2)
             .toStdString() +
         suffix;
}
void install(VstEngineAudioProcessor &processor, std::size_t slot,
             int channel) {
  juce::String diagnostic;
  require(processor.loadSlotInstrument(
              slot, vstengine::modules::bassInstrumentId, diagnostic),
          "static built-in module loads through production Rack");
  set(processor, endpoint(slot, "MidiIn"), static_cast<float>(channel));
}
void author(VstEngineAudioProcessor &processor, std::size_t slot,
            int noteOffset, vstengine::sequence::TimingMode timing) {
  processor.selectSlot(slot);
  auto edit = processor.sequenceEditSnapshot();
  edit.value.clear();
  edit.value.setNumSteps(16);
  edit.value.setTimingMode(timing);
  for (const int step : {0, 2}) {
    edit.value[step].gate = true;
    edit.value[step].noteOffset = noteOffset + step;
    edit.value[step].velocity = 0.7f;
    edit.value[step].probability = 1.0f;
    edit.value[step].gateWidth = 0.5f;
  }
  require(processor.commitSequenceEdit(edit),
          "authored pattern commits through shared revision adapter");
}
juce::MemoryBlock serialize(VstEngineAudioProcessor &processor,
                            std::size_t slot) {
  juce::MemoryBlock bytes;
  juce::String diagnostic;
  require(processor.serializeSlotPatternMidi(slot, bytes, diagnostic),
          "actual processor serializes slot to SMF");
  require(diagnostic.isEmpty() && bytes.getSize() > 14,
          "successful export returns bytes and clears error");
  return bytes;
}
struct Note {
  int pitch{}, channel{};
  double on{}, off{};
};
std::vector<Note> parse(const juce::MemoryBlock &bytes) {
  juce::MemoryInputStream stream(bytes, false);
  juce::MidiFile file;
  require(file.readFrom(stream),
          "export bytes parse as real Standard MIDI File");
  require(file.getNumTracks() == 1 && file.getTimeFormat() == 960,
          "export has one authored track with canonical 960 PPQ");
  std::vector<Note> notes;
  const auto *track = file.getTrack(0);
  for (const auto *event : *track) {
    if (!event->message.isNoteOn())
      continue;
    require(event->noteOffObject != nullptr,
            "every exported NoteOn has a matched NoteOff");
    const auto &on = event->message;
    const auto &off = event->noteOffObject->message;
    require(off.getNoteNumber() == on.getNoteNumber() &&
                off.getChannel() == on.getChannel(),
            "SMF NoteOff retains pitch and channel ownership");
    require(off.getTimeStamp() > on.getTimeStamp(),
            "SMF release follows its onset");
    notes.push_back({on.getNoteNumber(), on.getChannel(), on.getTimeStamp(),
                     off.getTimeStamp()});
  }
  return notes;
}
juce::MemoryBlock readFile(const juce::File &file) {
  require(file.existsAsFile(), "export creates a real file");
  juce::MemoryBlock bytes;
  require(file.loadFileAsData(bytes),
          "export file can be re-imported as bytes");
  return bytes;
}
struct OwnedFiles {
  std::vector<juce::File> files;
  ~OwnedFiles() {
    for (const auto &file : files)
      (void)file.deleteFile();
  }
  juce::File keep(juce::File file) {
    files.push_back(file);
    return file;
  }
  void clean() {
    for (const auto &file : files)
      require(file.deleteFile(),
              "test removes only its own unique temporary MIDI files");
    files.clear();
  }
};
void requireFailurePreservesDestination(VstEngineAudioProcessor &processor,
                                        std::size_t slot) {
  constexpr char sentinel[] = "Keep previous MIDI destination";
  juce::MemoryBlock destination(sentinel, sizeof(sentinel));
  const auto before = destination;
  juce::String diagnostic;
  require(!processor.serializeSlotPatternMidi(slot, destination, diagnostic),
          "invalid/empty slot export fails explicitly");
  require(destination == before && diagnostic.isNotEmpty(),
          "failed export preserves caller bytes and explains failure");
  require(processor.createSlotMidiFile(slot) == juce::File{},
          "failed export creates no temporary path");
}
} // namespace

int main() {
  juce::ScopedJuceInitialiser_GUI gui;
  auto processor = std::make_unique<VstEngineAudioProcessor>();
  processor->prepareToPlay(48000, 64);
  set(*processor, "rootNote", 36);
  set(*processor, "rngSeed", 1234);
  constexpr std::array<std::size_t, 3> slots{0, 1, 15};
  constexpr std::array<int, 3> channels{3, 5, 12};
  constexpr std::array<int, 3> offsets{0, 7, 12};
  constexpr std::array<vstengine::sequence::TimingMode, 3> timing{
      vstengine::sequence::TimingMode::sixteenth,
      vstengine::sequence::TimingMode::eighth,
      vstengine::sequence::TimingMode::triplet};
  constexpr std::array<double, 3> stepTicks{240, 480, 320};
  std::array<juce::MemoryBlock, 3> authored;
  for (std::size_t i = 0; i < slots.size(); ++i) {
    install(*processor, slots[i], channels[i]);
    author(*processor, slots[i], offsets[i], timing[i]);
    authored[i] = serialize(*processor, slots[i]);
    const auto notes = parse(authored[i]);
    require(notes.size() == 2,
            "each explicit Rack slot exports exactly its two authored notes");
    for (std::size_t n = 0; n < notes.size(); ++n) {
      require(notes[n].pitch == 36 + offsets[i] + static_cast<int>(n) * 2,
              "slot SMF contains its own authored pitch rather than slot zero");
      require(notes[n].channel == channels[i],
              "slot SMF uses current host-assigned input channel");
      require(std::abs(notes[n].on -
                       static_cast<double>(n) * 2 * stepTicks[i]) < 1e-9,
              "slot SMF onset follows its own timing mode");
      require(
          std::abs(notes[n].off - notes[n].on - stepTicks[i] * 0.5) < 1e-9,
          "slot SMF gate length follows its authored timing and gate width");
    }
    require(serialize(*processor, slots[i]) == authored[i],
            "unchanged slot and seed export deterministic bytes");
  }
  require(authored[0] != authored[1] && authored[1] != authored[2],
          "different authored slots produce distinct MIDI content");

  // Export is the authored Rack input. Transpose/zones/mute are downstream
  // routing decisions and must not be baked in then applied twice on import.
  set(*processor, "slot16Transpose", 12);
  set(*processor, "slot16Mute", 1);
  set(*processor, "slot16Enabled", 0);
  set(*processor, "slot16KeyLow", 100);
  require(
      serialize(*processor, 15) == authored[2],
      "sound/routing controls do not rewrite authored input-pattern export");
  set(*processor, "slot16MidiIn", 0);
  const auto offNotes = parse(serialize(*processor, 15));
  require(offNotes.size() == 2 && offNotes[0].channel == 1,
          "MIDI Off has explicit neutral channel one export without erasing "
          "the pattern");
  set(*processor, "slot16MidiIn", 12);

  OwnedFiles owned;
  processor->selectSlot(15);
  const auto selected = owned.keep(processor->createGeneratedMidiFile());
  require(readFile(selected) == authored[2],
          "selected export helper follows slot sixteen rather than fixed Bass "
          "slot");
  const auto explicitLast = owned.keep(processor->createPartMidiFile(15));
  require(readFile(explicitLast) == authored[2],
          "explicit helper accepts all sixteen banks");
  processor->selectSlot(1);
  const auto selectedSecond = owned.keep(processor->createGeneratedMidiFile());
  require(readFile(selectedSecond) == authored[1],
          "changing selection changes helper export to its authored slot");

  // A previously captured byte snapshot is independent from later GUI edits;
  // an asynchronous file chooser can retain those bytes safely.
  const auto snapshot = authored[1];
  author(*processor, 1, 19, vstengine::sequence::TimingMode::thirtySecond);
  require(serialize(*processor, 1) != snapshot,
          "subsequent local edit changes the live pattern");
  require(
      readFile(selectedSecond) == snapshot && authored[1] == snapshot,
      "captured/exported snapshot survives subsequent pattern edits unchanged");

  const auto first = owned.keep(processor->createSlotMidiFile(0));
  const auto firstBytes = readFile(first);
  const auto another = owned.keep(processor->createSlotMidiFile(0));
  require(readFile(another) == firstBytes,
          "repeated creation preserves deterministic MIDI content");
  auto secondInstance = std::make_unique<VstEngineAudioProcessor>();
  secondInstance->prepareToPlay(48000, 64);
  set(*secondInstance, "rootNote", 36);
  set(*secondInstance, "rngSeed", 1234);
  install(*secondInstance, 0, 3);
  author(*secondInstance, 0, 0, vstengine::sequence::TimingMode::sixteenth);
  const auto otherInstanceFile =
      owned.keep(secondInstance->createSlotMidiFile(0));
  require(readFile(otherInstanceFile) == firstBytes,
          "another instance can reproduce identical slot data");
  std::set<juce::String> paths;
  for (const auto &file : owned.files)
    require(paths.insert(file.getFullPathName()).second,
            "simultaneous slot/instance exports own distinct temporary paths");
  require(readFile(first) == firstBytes,
          "other exports never truncate a previous export file");

  // Checked file replacement must preserve an existing target on rejected
  // writes, and must not damage a directory accidentally chosen as a file.
  const auto directory =
      juce::File::getSpecialLocation(juce::File::tempDirectory)
          .getChildFile("Vox-Rack-Midi-Test-" + juce::Uuid().toString());
  require(directory.createDirectory().wasOk(),
          "writer fixture creates its own unique directory");
  const auto destination = directory.getChildFile("export.mid");
  juce::String writeDiagnostic;
  require(VstEngineAudioProcessor::writePatternMidiFile(
              destination, authored[0], writeDiagnostic),
          "checked file writer creates complete first MIDI snapshot");
  require(readFile(destination) == authored[0],
          "first checked write matches complete encoded snapshot");
  require(VstEngineAudioProcessor::writePatternMidiFile(
              destination, authored[2], writeDiagnostic),
          "checked file writer replaces existing MIDI destination");
  require(readFile(destination) == authored[2] &&
              parse(readFile(destination)).front().channel == 12,
          "replacement is a complete re-importable second snapshot without "
          "stale bytes");
  require(!VstEngineAudioProcessor::writePatternMidiFile(destination, {},
                                                         writeDiagnostic) &&
              writeDiagnostic.isNotEmpty(),
          "empty data write rejects before touching existing target");
  require(readFile(destination) == authored[2],
          "rejected empty write preserves existing MIDI bytes");
  const auto blocked = directory.getChildFile("directory-target.mid");
  require(blocked.createDirectory().wasOk(),
          "directory collision fixture exists");
  const auto marker = blocked.getChildFile("keep.txt");
  require(marker.replaceWithText("Keep directory contents"),
          "directory fixture writes its own marker");
  require(!VstEngineAudioProcessor::writePatternMidiFile(blocked, authored[0],
                                                         writeDiagnostic),
          "directory destination fails without replacing directory");
  require(marker.loadFileAsString() == "Keep directory contents",
          "failed destination leaves directory marker intact");
  require(!VstEngineAudioProcessor::writePatternMidiFile(
              marker.getChildFile("child.mid"), authored[0], writeDiagnostic),
          "write failure under a regular file is reported");
  require(marker.loadFileAsString() == "Keep directory contents",
          "failed stream creation preserves existing parent file");
  require(directory.deleteRecursively(),
          "test cleans only its owned writer directory");

  // Probability is deterministic for its stored seed and actual SlotId domain.
  processor->selectSlot(0);
  auto probabilityEdit = processor->sequenceEditSnapshot();
  probabilityEdit.value.clear();
  probabilityEdit.value.setNumSteps(64);
  for (int i = 0; i < 64; ++i) {
    probabilityEdit.value[i].gate = true;
    probabilityEdit.value[i].probability = i == 0 ? 1.0f : 0.5f;
    probabilityEdit.value[i].noteOffset = i % 12;
  }
  require(processor->commitSequenceEdit(probabilityEdit),
          "probability fixture commits through production adapter");
  const auto seeded = serialize(*processor, 0);
  const auto seededNotes = parse(seeded);
  require(seededNotes.size() > 1 && seededNotes.size() < 64,
          "seeded fixture includes both played and skipped probability steps");
  require(serialize(*processor, 0) == seeded,
          "probability decisions repeat for the same seed and slot");
  set(*processor, "rngSeed", 98765);
  require(serialize(*processor, 0) != seeded,
          "different stored seed changes the deterministic probability "
          "realization");
  set(*processor, "rngSeed", 1234);
  require(serialize(*processor, 0) == seeded,
          "restoring the seed recovers exact MIDI bytes");

  requireFailurePreservesDestination(*processor, 16);
  requireFailurePreservesDestination(*processor,
                                     std::numeric_limits<std::size_t>::max());
  requireFailurePreservesDestination(*processor, 14);
  install(*processor, 13, 14);
  processor->selectSlot(13);
  auto empty = processor->sequenceEditSnapshot();
  empty.value.clear();
  require(processor->commitSequenceEdit(empty),
          "empty installed pattern fixture commits");
  requireFailurePreservesDestination(*processor, 13);
  empty.value[0].gate = true;
  empty.value[0].probability = 0;
  require(processor->commitSequenceEdit(empty),
          "all-skipped probability fixture commits");
  requireFailurePreservesDestination(*processor, 13);
  require(
      processor->createPartMidiFile(-1) == juce::File{} &&
          processor->createPartMidiFile(16) == juce::File{},
      "out-of-range signed compatibility helper fails without index aliasing");
  owned.clean();
  std::cout << "Rack MIDI export tests passed (" << checks << ")\n";
}
