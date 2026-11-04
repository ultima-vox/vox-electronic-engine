#include "pattern/Pattern.h"
#include <cstring>
#include <set>

namespace vstengine::pattern {
namespace {
constexpr std::uint32_t magic = 0x32545056u; // "VPT2", little-endian
constexpr std::size_t maxLaneNameBytes = 64;
constexpr std::uint32_t maxFactoryPatterns = 64;

bool fail(std::string &why, const char *message) {
  why = message;
  return false;
}

bool boundedName(const char *text) noexcept {
  if (text == nullptr)
    return false;
  std::size_t n = 0;
  while (n <= maxLaneNameBytes && text[n] != '\0')
    ++n;
  return n > 0 && n <= maxLaneNameBytes;
}

bool sameStep(const VoxPatternStepV2 &a, const VoxPatternStepV2 &b) noexcept {
  return std::memcmp(&a, &b, sizeof a) == 0;
}

// One cell against the declared fields. `why` names the first violation.
bool validStep(const VoxPatternStepV2 &s, std::uint32_t fields, std::string &why) {
  if ((s.flags & ~VOX_STEP_KNOWN_FLAGS) != 0)
    return fail(why, "unknown step flag");
  for (const auto byte : s.reserved)
    if (byte != 0)
      return fail(why, "reserved step bytes are not zero");
  if (s.velocity < 1 || s.velocity > 127 || s.note < -48 || s.note > 48 || s.octave < -4 ||
      s.octave > 4 || s.gate < 1 || s.gate > 2048 || s.slide > 2048 || s.probability > 100 ||
      s.ratchet < 1 || s.ratchet > 8)
    return fail(why, "step value out of range");
  const auto unread = [fields](std::uint32_t field) { return (fields & field) == 0; };
  if ((unread(VOX_FIELD_ACCENT) && (s.flags & VOX_STEP_ACCENT)) ||
      (unread(VOX_FIELD_SLIDE) && (s.flags & VOX_STEP_SLIDE)) ||
      (unread(VOX_FIELD_TIE) && (s.flags & VOX_STEP_TIE)) ||
      (unread(VOX_FIELD_VELOCITY) && s.velocity != VOX_STEP_DEFAULT_VELOCITY) ||
      (unread(VOX_FIELD_NOTE) && s.note != 0) || (unread(VOX_FIELD_OCTAVE) && s.octave != 0) ||
      (unread(VOX_FIELD_SLIDE) && s.slide != 0) ||
      (unread(VOX_FIELD_GATE) && s.gate != VOX_STEP_DEFAULT_GATE) ||
      (unread(VOX_FIELD_PROBABILITY) && s.probability != VOX_STEP_DEFAULT_PROBABILITY) ||
      (unread(VOX_FIELD_RATCHET) && s.ratchet != VOX_STEP_DEFAULT_RATCHET))
    return fail(why, "step uses a field the instrument does not read");
  if ((s.flags & VOX_STEP_ON) == 0 && (s.flags & ~VOX_STEP_ON) != 0)
    return fail(why, "a rest step carries accent, slide or tie");
  return true;
}

void put16(std::byte *&out, std::uint16_t v) noexcept {
  *out++ = static_cast<std::byte>(v & 0xffu);
  *out++ = static_cast<std::byte>(v >> 8);
}
std::uint16_t get16(const std::byte *&in) noexcept {
  const auto v = static_cast<std::uint16_t>(std::to_integer<std::uint16_t>(in[0]) |
                                            (std::to_integer<std::uint16_t>(in[1]) << 8));
  in += 2;
  return v;
}
} // namespace

VoxPatternV2 Pattern::view() const noexcept {
  return {sizeof(VoxPatternV2),
          VOX_MODULE_ABI_V2,
          laneCount,
          stepsPerBar,
          barCount,
          timing,
          sizeof(VoxPatternStepV2),
          static_cast<std::uint32_t>(cells.size()),
          const_cast<VoxPatternStepV2 *>(cells.data())};
}

bool operator==(const Pattern &a, const Pattern &b) noexcept {
  return a.laneCount == b.laneCount && a.stepsPerBar == b.stepsPerBar &&
         a.barCount == b.barCount && a.timing == b.timing &&
         std::memcmp(a.cells.data(), b.cells.data(), sizeof a.cells) == 0;
}

VoxPatternStepV2 defaultStep() noexcept {
  VoxPatternStepV2 s{};
  s.velocity = VOX_STEP_DEFAULT_VELOCITY;
  s.gate = VOX_STEP_DEFAULT_GATE;
  s.probability = VOX_STEP_DEFAULT_PROBABILITY;
  s.ratchet = VOX_STEP_DEFAULT_RATCHET;
  return s;
}

Pattern emptyPattern(const Caps &caps) noexcept {
  Pattern p;
  p.laneCount = caps.laneCount;
  p.stepsPerBar = caps.maxStepsPerBar < 16 ? caps.maxStepsPerBar : 16;
  p.barCount = 1;
  p.cells.fill(defaultStep());
  return p;
}

void reshape(const Pattern &source, std::uint32_t stepsPerBar, std::uint32_t barCount,
             Pattern &out) noexcept {
  if (&out == &source)
    return; // callers pass a separate destination
  out.laneCount = source.laneCount;
  out.timing = source.timing;
  out.stepsPerBar = stepsPerBar;
  out.barCount = barCount;
  out.cells.fill(defaultStep());
  for (std::uint32_t lane = 0; lane < source.laneCount; ++lane)
    for (std::uint32_t bar = 0; bar < barCount && bar < source.barCount; ++bar)
      for (std::uint32_t step = 0; step < stepsPerBar && step < source.stepsPerBar; ++step)
        out.at(lane, bar, step) = source.at(lane, bar, step);
}

double stepLengthPpq(std::uint32_t timing) noexcept {
  switch (timing) {
  case VOX_TIMING_EIGHTH:
    return 0.5;
  case VOX_TIMING_THIRTY_SECOND:
    return 0.125;
  case VOX_TIMING_EIGHTH_TRIPLET:
    return 1.0 / 3.0;
  default:
    return 0.25;
  }
}

bool validateCaps(const Caps &c, std::string &why) {
  if (c.playback != VOX_PATTERN_PLAYBACK_HOST && c.playback != VOX_PATTERN_PLAYBACK_INTERNAL)
    return fail(why, "pattern playback must be HOST or INTERNAL");
  if (c.laneCount < 1 || c.laneCount > VOX_PATTERN_V2_MAX_LANES || c.maxStepsPerBar < 1 ||
      c.maxStepsPerBar > VOX_PATTERN_V2_MAX_STEPS_PER_BAR || c.maxBars < 1 ||
      c.maxBars > VOX_PATTERN_V2_MAX_BARS)
    return fail(why, "pattern dimensions out of range");
  if ((c.stepFields & ~VOX_FIELD_KNOWN) != 0)
    return fail(why, "unknown pattern step field");
  if (c.laneNames.size() != c.laneCount)
    return fail(why, "one name per pattern lane");
  for (const auto &name : c.laneNames)
    if (!boundedName(name.c_str()))
      return fail(why, "pattern lane name empty or too long");
  if (c.playback == VOX_PATTERN_PLAYBACK_HOST) {
    if (c.laneCount != 1 || !c.laneNotes.empty())
      return fail(why, "HOST playback has one note lane and no lane notes");
    return true;
  }
  if (c.laneNotes.size() != c.laneCount)
    return fail(why, "INTERNAL playback needs one note per lane");
  std::set<std::uint8_t> notes;
  for (const auto note : c.laneNotes)
    if (note > 127 || !notes.insert(note).second)
      return fail(why, "lane notes must be distinct MIDI notes");
  return true;
}

bool validate(const Pattern &p, const Caps &c, std::string &why) {
  if (p.laneCount != c.laneCount)
    return fail(why, "pattern lane count does not match the instrument");
  if (p.stepsPerBar < 1 || p.stepsPerBar > c.maxStepsPerBar || p.barCount < 1 ||
      p.barCount > c.maxBars)
    return fail(why, "pattern dimensions exceed the instrument");
  if (p.timing > VOX_TIMING_EIGHTH_TRIPLET)
    return fail(why, "unknown pattern timing");
  const auto used = p.cellCount();
  for (std::size_t i = 0; i < used; ++i)
    if (!validStep(p.cells[i], c.stepFields, why))
      return false;
  const auto rest = defaultStep();
  for (std::size_t i = used; i < p.cells.size(); ++i)
    if (!sameStep(p.cells[i], rest))
      return fail(why, "cells outside the pattern are not default");
  return true;
}

bool importCaps(const VoxPatternCapsV2 *raw, Caps &out, std::string &why) {
  if (raw == nullptr || raw->structSize < sizeof(VoxPatternCapsV2) ||
      raw->version != VOX_MODULE_ABI_V2)
    return fail(why, "invalid pattern caps table");
  if (raw->factoryPatternCount > maxFactoryPatterns)
    return fail(why, "too many factory patterns");
  if (raw->laneCount < 1 || raw->laneCount > VOX_PATTERN_V2_MAX_LANES ||
      raw->laneNames == nullptr)
    return fail(why, "pattern lane table missing or out of range");
  Caps caps;
  caps.playback = raw->playback;
  caps.laneCount = raw->laneCount;
  caps.maxStepsPerBar = raw->maxStepsPerBar;
  caps.maxBars = raw->maxBars;
  caps.stepFields = raw->stepFields;
  for (std::uint32_t lane = 0; lane < raw->laneCount; ++lane) {
    if (!boundedName(raw->laneNames[lane]))
      return fail(why, "pattern lane name empty or too long");
    caps.laneNames.emplace_back(raw->laneNames[lane]);
  }
  if (raw->laneNotes != nullptr)
    caps.laneNotes.assign(raw->laneNotes, raw->laneNotes + raw->laneCount);
  if (!validateCaps(caps, why))
    return false;
  out = std::move(caps);
  return true;
}

bool fromView(const VoxPatternV2 &v, const Caps &caps, Pattern &out, std::string &why) {
  if (v.structSize < sizeof(VoxPatternV2) || v.version != VOX_MODULE_ABI_V2 ||
      v.cells == nullptr || v.cellStride < sizeof(VoxPatternStepV2) || v.cellStride > 256 ||
      v.cellStride % alignof(VoxPatternStepV2) != 0)
    return fail(why, "invalid pattern view");
  if (v.laneCount < 1 || v.laneCount > VOX_PATTERN_V2_MAX_LANES || v.stepsPerBar < 1 ||
      v.stepsPerBar > VOX_PATTERN_V2_MAX_STEPS_PER_BAR || v.barCount < 1 ||
      v.barCount > VOX_PATTERN_V2_MAX_BARS)
    return fail(why, "pattern dimensions out of range");
  const auto used = static_cast<std::size_t>(v.laneCount) * v.stepsPerBar * v.barCount;
  if (used > v.cellCapacity)
    return fail(why, "pattern larger than its cell buffer");
  auto copy = std::make_unique<Pattern>();
  copy->laneCount = v.laneCount;
  copy->stepsPerBar = v.stepsPerBar;
  copy->barCount = v.barCount;
  copy->timing = v.timing;
  copy->cells.fill(defaultStep());
  const auto *bytes = reinterpret_cast<const std::uint8_t *>(v.cells);
  for (std::size_t i = 0; i < used; ++i)
    std::memcpy(&copy->cells[i], bytes + i * v.cellStride, sizeof(VoxPatternStepV2));
  if (!validate(*copy, caps, why))
    return false;
  out = *copy;
  return true;
}

void encode(const Pattern &p, std::vector<std::byte> &destination) {
  const auto used = p.cellCount();
  destination.resize(encodedHeaderBytes + used * encodedCellBytes);
  auto *out = destination.data();
  for (int shift = 0; shift < 32; shift += 8)
    *out++ = static_cast<std::byte>((magic >> shift) & 0xffu);
  put16(out, static_cast<std::uint16_t>(encodingVersion));
  *out++ = static_cast<std::byte>(p.laneCount);
  *out++ = static_cast<std::byte>(p.stepsPerBar);
  *out++ = static_cast<std::byte>(p.barCount);
  *out++ = static_cast<std::byte>(p.timing);
  put16(out, 0);
  for (std::size_t i = 0; i < used; ++i) {
    const auto &s = p.cells[i];
    *out++ = static_cast<std::byte>(s.flags);
    *out++ = static_cast<std::byte>(s.velocity);
    *out++ = static_cast<std::byte>(static_cast<std::uint8_t>(s.note));
    *out++ = static_cast<std::byte>(static_cast<std::uint8_t>(s.octave));
    put16(out, s.gate);
    put16(out, s.slide);
    *out++ = static_cast<std::byte>(s.probability);
    *out++ = static_cast<std::byte>(s.ratchet);
  }
}

bool decode(std::span<const std::byte> bytes, const Caps &caps, Pattern &out, std::string &why) {
  if (bytes.size() < encodedHeaderBytes || bytes.size() > maximumEncodedBytes)
    return fail(why, "pattern payload size out of range");
  const auto *in = bytes.data();
  std::uint32_t storedMagic = 0;
  for (int shift = 0; shift < 32; shift += 8)
    storedMagic |= std::to_integer<std::uint32_t>(*in++) << shift;
  if (storedMagic != magic)
    return fail(why, "not a VPT2 pattern");
  if (get16(in) != encodingVersion)
    return fail(why, "unsupported pattern encoding version");
  auto p = std::make_unique<Pattern>();
  p->laneCount = std::to_integer<std::uint32_t>(*in++);
  p->stepsPerBar = std::to_integer<std::uint32_t>(*in++);
  p->barCount = std::to_integer<std::uint32_t>(*in++);
  p->timing = std::to_integer<std::uint32_t>(*in++);
  if (get16(in) != 0)
    return fail(why, "reserved pattern header bytes are not zero");
  if (p->laneCount < 1 || p->laneCount > VOX_PATTERN_V2_MAX_LANES || p->stepsPerBar < 1 ||
      p->stepsPerBar > VOX_PATTERN_V2_MAX_STEPS_PER_BAR || p->barCount < 1 ||
      p->barCount > VOX_PATTERN_V2_MAX_BARS)
    return fail(why, "pattern dimensions out of range");
  const auto used = p->cellCount();
  if (bytes.size() != encodedHeaderBytes + used * encodedCellBytes)
    return fail(why, "pattern payload length does not match its dimensions");
  p->cells.fill(defaultStep());
  for (std::size_t i = 0; i < used; ++i) {
    auto &s = p->cells[i];
    s.flags = std::to_integer<std::uint8_t>(*in++);
    s.velocity = std::to_integer<std::uint8_t>(*in++);
    s.note = static_cast<std::int8_t>(std::to_integer<std::uint8_t>(*in++));
    s.octave = static_cast<std::int8_t>(std::to_integer<std::uint8_t>(*in++));
    s.gate = get16(in);
    s.slide = get16(in);
    s.probability = std::to_integer<std::uint8_t>(*in++);
    s.ratchet = std::to_integer<std::uint8_t>(*in++);
  }
  if (!validate(*p, caps, why))
    return false;
  out = *p;
  return true;
}

std::uint32_t fingerprint(const Pattern &p) noexcept {
  std::uint32_t hash = 2166136261u; // FNV-1a over the durable fields
  const auto mix = [&hash](std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
      hash ^= (value >> shift) & 0xffu;
      hash *= 16777619u;
    }
  };
  mix(p.laneCount);
  mix(p.stepsPerBar);
  mix(p.barCount);
  mix(p.timing);
  for (std::size_t i = 0; i < p.cellCount(); ++i) {
    const auto &s = p.cells[i];
    mix(s.flags | (s.velocity << 8) | (static_cast<std::uint8_t>(s.note) << 16) |
        (static_cast<std::uint32_t>(static_cast<std::uint8_t>(s.octave)) << 24));
    mix(s.gate | (static_cast<std::uint32_t>(s.slide) << 16));
    mix(s.probability | (s.ratchet << 8));
  }
  return hash;
}

SnapshotBuffer::SnapshotBuffer() : slots_(std::make_unique<std::array<Slot, 3>>()) {
  static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
}

void SnapshotBuffer::publish(const Pattern &source) noexcept {
  auto &slot = (*slots_)[back_];
  slot.pattern = source;
  slot.generation = ++written_;
  back_ = middle_.exchange(back_ | fresh, std::memory_order_acq_rel) & 3u;
}

const Pattern *SnapshotBuffer::acquire(std::uint64_t &generation) noexcept {
  if ((middle_.load(std::memory_order_relaxed) & fresh) != 0u) {
    front_ = middle_.exchange(front_, std::memory_order_acq_rel) & 3u;
    published_ = true;
  }
  if (!published_)
    return nullptr;
  const auto &slot = (*slots_)[front_];
  generation = slot.generation;
  return &slot.pattern;
}

} // namespace vstengine::pattern
