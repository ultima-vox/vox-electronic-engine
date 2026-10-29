#include "pattern/Pattern.h"
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <thread>

namespace {
namespace vp = vstengine::pattern;
int passed = 0;
#define REQUIRE(condition, message)                                                    \
  do {                                                                                 \
    if (!(condition)) {                                                                \
      std::cerr << "FAILED: " << message << " (line " << __LINE__ << ")\n";            \
      std::exit(1);                                                                    \
    }                                                                                  \
    ++passed;                                                                          \
  } while (false)

// Acid-style note lane: NOTE / ACCENT / SLIDE / GATE / OCTAVE plus velocity, tie.
vp::Caps noteCaps() {
  vp::Caps c;
  c.playback = VOX_PATTERN_PLAYBACK_HOST;
  c.laneCount = 1;
  c.maxStepsPerBar = 16;
  c.maxBars = 4;
  c.stepFields = VOX_FIELD_NOTE | VOX_FIELD_OCTAVE | VOX_FIELD_ACCENT | VOX_FIELD_SLIDE |
                 VOX_FIELD_GATE | VOX_FIELD_VELOCITY | VOX_FIELD_TIE;
  c.laneNames = {"Note"};
  return c;
}

// Drum kit: 8 pads, velocity and accent only, internal sequencer.
vp::Caps drumCaps() {
  vp::Caps c;
  c.playback = VOX_PATTERN_PLAYBACK_INTERNAL;
  c.laneCount = 8;
  c.maxStepsPerBar = 16;
  c.maxBars = 4;
  c.stepFields = VOX_FIELD_VELOCITY | VOX_FIELD_ACCENT;
  c.laneNames = {"Kick", "Snare", "Clap", "Closed Hat", "Open Hat", "Tom", "Rim", "Cymbal"};
  c.laneNotes = {36, 38, 39, 42, 46, 45, 37, 49};
  return c;
}

std::unique_ptr<vp::Pattern> make(const vp::Caps &caps, std::uint32_t steps,
                                  std::uint32_t bars) {
  auto p = std::make_unique<vp::Pattern>(vp::emptyPattern(caps));
  p->stepsPerBar = steps;
  p->barCount = bars;
  return p;
}

bool rejects(const vp::Pattern &p, const vp::Caps &caps, const char *expected) {
  std::string why;
  return !vp::validate(p, caps, why) && why.find(expected) != std::string::npos;
}

void capsValidation() {
  std::string why;
  REQUIRE(vp::validateCaps(noteCaps(), why), "note caps valid: " << why);
  REQUIRE(vp::validateCaps(drumCaps(), why), "drum caps valid: " << why);
  auto c = noteCaps();
  c.laneCount = 2;
  c.laneNames = {"A", "B"};
  REQUIRE(!vp::validateCaps(c, why), "HOST playback has exactly one lane");
  c = noteCaps();
  c.laneNotes = {60};
  REQUIRE(!vp::validateCaps(c, why), "HOST playback declares no lane notes");
  c = drumCaps();
  c.laneNotes[3] = 36;
  REQUIRE(!vp::validateCaps(c, why), "duplicate lane notes rejected");
  c = drumCaps();
  c.laneNotes.pop_back();
  REQUIRE(!vp::validateCaps(c, why), "one note per INTERNAL lane");
  c = drumCaps();
  c.playback = VOX_PATTERN_PLAYBACK_HOST | VOX_PATTERN_PLAYBACK_INTERNAL;
  REQUIRE(!vp::validateCaps(c, why), "exactly one playback kind");
  c = drumCaps();
  c.stepFields |= 0x200u;
  REQUIRE(!vp::validateCaps(c, why), "unknown field bit rejected");
  for (const auto [lanes, steps, bars] : {std::array{0u, 16u, 1u}, std::array{17u, 16u, 1u},
                                          std::array{8u, 0u, 1u}, std::array{8u, 65u, 1u},
                                          std::array{8u, 16u, 0u}, std::array{8u, 16u, 5u}}) {
    c = drumCaps();
    c.laneCount = lanes;
    c.maxStepsPerBar = steps;
    c.maxBars = bars;
    REQUIRE(!vp::validateCaps(c, why), "caps dimensions bounded");
  }
  c = drumCaps();
  c.laneNames[2] = "";
  REQUIRE(!vp::validateCaps(c, why), "empty lane name rejected");
  c.laneNames[2] = std::string(65, 'x');
  REQUIRE(!vp::validateCaps(c, why), "long lane name rejected");
}

void acidFiveLaneWorkflow() {
  const auto caps = noteCaps();
  auto p = make(caps, 16, 1);
  // NOTE / OCTAVE / ACCENT / SLIDE / GATE on distinct steps, plus a tie and rests.
  auto &a = p->at(0, 0, 0);
  a.flags = VOX_STEP_ON | VOX_STEP_ACCENT;
  a.note = 7;
  a.octave = -1;
  a.gate = 128;
  auto &b = p->at(0, 0, 1);
  b.flags = VOX_STEP_ON | VOX_STEP_SLIDE;
  b.note = 3;
  b.octave = 1;
  b.slide = 96;
  b.gate = 256;
  b.velocity = 64;
  auto &c = p->at(0, 0, 2);
  c.flags = VOX_STEP_ON | VOX_STEP_TIE;
  c.note = 3;
  c.octave = 1;
  c.gate = 512;
  std::string why;
  REQUIRE(vp::validate(*p, caps, why), "Acid NOTE/ACCENT/SLIDE/GATE/OCTAVE expressible: " << why);
  std::vector<std::byte> bytes;
  vp::encode(*p, bytes);
  REQUIRE(bytes.size() == vp::encodedHeaderBytes + 16 * vp::encodedCellBytes,
          "encoding stores only the used cells");
  auto back = std::make_unique<vp::Pattern>();
  REQUIRE(vp::decode(bytes, caps, *back, why), "note lane decodes: " << why);
  REQUIRE(*back == *p, "note lane round-trips exactly");
  REQUIRE(back->at(0, 0, 1).octave == 1 && back->at(0, 0, 0).octave == -1,
          "octave stored independently from note");

  // A rest cannot carry accent/slide/tie; values stay in range.
  auto bad = std::make_unique<vp::Pattern>(*p);
  bad->at(0, 0, 5).flags = VOX_STEP_ACCENT;
  REQUIRE(rejects(*bad, caps, "rest"), "accented rest rejected");
  for (const auto mutate : {+[](VoxPatternStepV2 &s) { s.note = 49; },
                            +[](VoxPatternStepV2 &s) { s.octave = 5; },
                            +[](VoxPatternStepV2 &s) { s.gate = 0; },
                            +[](VoxPatternStepV2 &s) { s.gate = 2049; },
                            +[](VoxPatternStepV2 &s) { s.slide = 2049; },
                            +[](VoxPatternStepV2 &s) { s.velocity = 0; },
                            +[](VoxPatternStepV2 &s) { s.velocity = 128; }}) {
    bad = std::make_unique<vp::Pattern>(*p);
    mutate(bad->at(0, 0, 0));
    REQUIRE(rejects(*bad, caps, "range"), "out-of-range step rejected");
  }
  bad = std::make_unique<vp::Pattern>(*p);
  bad->at(0, 0, 0).reserved[3] = 1;
  REQUIRE(rejects(*bad, caps, "reserved"), "reserved step bytes must be zero");
  bad = std::make_unique<vp::Pattern>(*p);
  bad->at(0, 0, 0).flags |= 0x10u;
  REQUIRE(rejects(*bad, caps, "unknown step flag"), "unknown step flag rejected");
  bad = std::make_unique<vp::Pattern>(*p);
  bad->at(0, 0, 0).probability = 50; // the note lane does not read probability
  REQUIRE(rejects(*bad, caps, "does not read"), "undeclared field must hold its default");
}

void drumGrid() {
  const auto caps = drumCaps();
  auto p = make(caps, 16, 4);
  for (std::uint32_t bar = 0; bar < 4; ++bar)
    for (std::uint32_t step = 0; step < 16; step += 4) {
      auto &kick = p->at(0, bar, step);
      kick.flags = VOX_STEP_ON | (step == 0 ? VOX_STEP_ACCENT : 0u);
      kick.velocity = 120;
    }
  p->at(3, 2, 6).flags = VOX_STEP_ON;
  p->at(3, 2, 6).velocity = 66;
  std::string why;
  REQUIRE(vp::validate(*p, caps, why), "8x16x4 drum grid valid: " << why);
  std::vector<std::byte> bytes;
  vp::encode(*p, bytes);
  auto back = std::make_unique<vp::Pattern>();
  REQUIRE(vp::decode(bytes, caps, *back, why) && *back == *p, "drum grid round-trips");
  REQUIRE(back->at(3, 2, 6).velocity == 66 && back->at(0, 3, 12).flags == VOX_STEP_ON,
          "cell addressing is [lane][bar][step]");
  const auto before = vp::fingerprint(*p);
  p->at(3, 2, 6).velocity = 67;
  REQUIRE(vp::fingerprint(*p) != before, "fingerprint follows velocity edits");

  auto bad = std::make_unique<vp::Pattern>(*p);
  bad->at(1, 0, 0).note = 2; // drums do not read NOTE
  REQUIRE(rejects(*bad, caps, "does not read"), "no fake note data on drum lanes");
  bad = std::make_unique<vp::Pattern>(*p);
  bad->laneCount = 16; // no fake 16-pad support
  REQUIRE(rejects(*bad, caps, "lane count"), "lane count must match the instrument");
  bad = std::make_unique<vp::Pattern>(*p);
  bad->stepsPerBar = 32;
  REQUIRE(rejects(*bad, caps, "exceed"), "steps per bar bounded by caps");
  bad = std::make_unique<vp::Pattern>(*p);
  bad->barCount = 1; // bars 2..4 now lie outside the pattern but hold notes
  REQUIRE(rejects(*bad, caps, "outside"), "cells outside the dimensions must be default");
  bad = std::make_unique<vp::Pattern>(*p);
  bad->timing = 4;
  REQUIRE(rejects(*bad, caps, "timing"), "unknown timing rejected");
}

void malformedEncodings() {
  const auto caps = drumCaps();
  auto p = make(caps, 16, 2);
  p->at(0, 0, 0).flags = VOX_STEP_ON;
  std::vector<std::byte> good;
  vp::encode(*p, good);
  auto out = std::make_unique<vp::Pattern>();
  std::string why;
  auto truncated = good;
  truncated.pop_back();
  REQUIRE(!vp::decode(truncated, caps, *out, why), "truncated payload rejected");
  auto longer = good;
  longer.push_back(std::byte{0});
  REQUIRE(!vp::decode(longer, caps, *out, why), "trailing bytes rejected");
  REQUIRE(!vp::decode({}, caps, *out, why), "empty payload rejected");
  auto magic = good;
  magic[0] = std::byte{0};
  REQUIRE(!vp::decode(magic, caps, *out, why), "wrong magic rejected");
  auto version = good;
  version[4] = std::byte{2};
  REQUIRE(!vp::decode(version, caps, *out, why), "unknown encoding version rejected");
  auto header = good;
  header[10] = std::byte{1};
  REQUIRE(!vp::decode(header, caps, *out, why), "reserved header bytes rejected");
  // Flip every bit of every byte: decode either rejects or yields a pattern
  // that still validates; it never reads out of bounds (ASan in CI).
  int accepted = 0;
  for (std::size_t i = 0; i < good.size(); ++i)
    for (int bit = 0; bit < 8; ++bit) {
      auto mutated = good;
      mutated[i] ^= static_cast<std::byte>(1u << bit);
      if (vp::decode(mutated, caps, *out, why)) {
        ++accepted;
        REQUIRE(vp::validate(*out, caps, why), "an accepted mutation still validates");
      }
    }
  REQUIRE(accepted < static_cast<int>(good.size() * 8), "most mutations are rejected");
  // A decode failure never touches the destination.
  const auto keep = std::make_unique<vp::Pattern>(*p);
  REQUIRE(!vp::decode(truncated, caps, *out = *p, why) && *out == *keep,
          "failed decode leaves the destination unchanged");
}

void moduleTables() {
  static const char *const names[] = {"Kick", "Snare"};
  static const std::uint8_t notes[] = {36, 38};
  VoxPatternCapsV2 raw{sizeof(VoxPatternCapsV2), VOX_MODULE_ABI_V2, VOX_PATTERN_PLAYBACK_INTERNAL,
                       2, 16, 4, VOX_FIELD_VELOCITY | VOX_FIELD_ACCENT, 1, names, notes};
  vp::Caps caps;
  std::string why;
  REQUIRE(vp::importCaps(&raw, caps, why), "module caps import: " << why);
  REQUIRE(caps.laneNames[1] == "Snare" && caps.laneNotes[1] == 38, "caps copied");
  auto broken = raw;
  broken.structSize = sizeof(VoxPatternCapsV2) - 8;
  REQUIRE(!vp::importCaps(&broken, caps, why), "short caps table rejected");
  broken = raw;
  broken.version = 1;
  REQUIRE(!vp::importCaps(&broken, caps, why), "wrong caps version rejected");
  broken = raw;
  broken.laneNames = nullptr;
  REQUIRE(!vp::importCaps(&broken, caps, why), "missing lane names rejected");
  broken = raw;
  broken.factoryPatternCount = 65;
  REQUIRE(!vp::importCaps(&broken, caps, why), "factory pattern count bounded");
  broken = raw;
  broken.laneNotes = nullptr;
  REQUIRE(!vp::importCaps(&broken, caps, why), "INTERNAL caps need lane notes");
  REQUIRE(!vp::importCaps(nullptr, caps, why), "null caps rejected");

  // A module-written view with a larger stride is copied cell by cell.
  struct Wide {
    VoxPatternStepV2 step;
    std::uint8_t extra[16];
  };
  std::array<Wide, 2 * 16> cells{};
  for (auto &cell : cells)
    cell.step = vp::defaultStep();
  cells[17].step.flags = VOX_STEP_ON;
  cells[17].step.velocity = 90;
  VoxPatternV2 view{sizeof(VoxPatternV2), VOX_MODULE_ABI_V2, 2, 16, 1, VOX_TIMING_SIXTEENTH,
                    sizeof(Wide), static_cast<std::uint32_t>(cells.size()),
                    &cells[0].step};
  auto out = std::make_unique<vp::Pattern>();
  REQUIRE(vp::fromView(view, caps, *out, why), "wide-stride view imports: " << why);
  REQUIRE(out->at(1, 0, 1).velocity == 90, "stride honoured");
  auto small = view;
  small.cellCapacity = 31;
  REQUIRE(!vp::fromView(small, caps, *out, why), "view larger than its buffer rejected");
  auto odd = view;
  odd.cellStride = sizeof(VoxPatternStepV2) + 1;
  REQUIRE(!vp::fromView(odd, caps, *out, why), "misaligned stride rejected");
}

void snapshotBuffer() {
  const auto caps = drumCaps();
  vp::SnapshotBuffer buffer;
  std::uint64_t generation = 0;
  REQUIRE(buffer.acquire(generation) == nullptr, "nothing before the first publish");
  auto p = make(caps, 16, 1);
  p->at(0, 0, 0).flags = VOX_STEP_ON;
  buffer.publish(*p);
  const auto *first = buffer.acquire(generation);
  REQUIRE(first != nullptr && *first == *p && generation == 1, "first snapshot delivered");
  REQUIRE(buffer.acquire(generation) == first && generation == 1,
          "no publish: the held snapshot stays");
  p->at(0, 0, 4).flags = VOX_STEP_ON;
  buffer.publish(*p);
  p->at(0, 0, 8).flags = VOX_STEP_ON;
  buffer.publish(*p);
  const auto *latest = buffer.acquire(generation);
  REQUIRE(latest != nullptr && *latest == *p && generation == 3,
          "two publishes before a read: the newest wins");

  // Concurrent stress: the writer fills every used cell with one velocity per
  // generation; the reader must never observe a mixed (torn) snapshot.
  std::atomic<bool> done{false};
  std::atomic<int> torn{0}, regressions{0};
  std::thread reader([&] {
    std::uint64_t last = 0;
    while (!done.load(std::memory_order_acquire)) {
      std::uint64_t g = 0;
      const auto *s = buffer.acquire(g);
      if (s == nullptr)
        continue;
      if (g < last)
        regressions.fetch_add(1);
      last = g;
      const auto v = s->cells[0].velocity;
      for (std::size_t i = 0; i < s->cellCount(); ++i)
        if (s->cells[i].velocity != v) {
          torn.fetch_add(1);
          break;
        }
    }
  });
  auto w = make(caps, 16, 4);
  for (int k = 0; k < 20000; ++k) {
    const auto v = static_cast<std::uint8_t>(1 + k % 127);
    for (std::size_t i = 0; i < w->cellCount(); ++i)
      w->cells[i].velocity = v;
    buffer.publish(*w);
  }
  done.store(true, std::memory_order_release);
  reader.join();
  REQUIRE(torn.load() == 0, "no torn snapshot under rapid edits");
  REQUIRE(regressions.load() == 0, "generations never go backwards");
}
} // namespace

int main() {
  capsValidation();
  acidFiveLaneWorkflow();
  drumGrid();
  malformedEncodings();
  moduleTables();
  snapshotBuffer();
  std::cout << "Pattern tests passed (" << passed << ")\n";
  return 0;
}
