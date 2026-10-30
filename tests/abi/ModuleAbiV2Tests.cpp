// Module ABI V2, slice 1: real shared-library round trip against the test
// module. The loader and validator live only in this harness; there is no
// production package discovery, registry or editor hosting yet.
#include "instrument/abi/VoxModuleAbiV2.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <iostream>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

extern "C" uint32_t voxAbiLayoutC(uint32_t *out, uint32_t capacity);
#if defined(VOX_TEST_INTERPOSE_MALLOC)
extern "C" void voxProbeBegin(void);
extern "C" unsigned long voxProbeEnd(void);
#endif

namespace {
int run = 0;
#define REQUIRE(condition, message)                                                  \
  do {                                                                               \
    ++run;                                                                           \
    if (!(condition)) {                                                              \
      std::cerr << "FAILED: " << message << " line " << __LINE__ << '\n';            \
      std::exit(EXIT_FAILURE);                                                       \
    }                                                                                \
  } while (false)

constexpr std::size_t maxString = 256;
constexpr std::uint32_t poolPerSlot = 64;
constexpr std::uint32_t maxParameters = 1024;
constexpr std::uint32_t maxParameterStride = 1024; // bounds count * stride
constexpr std::uint32_t maxChoices = 128;
constexpr std::uint32_t hostMaxStateBytes = 1024 * 1024;
constexpr const char *toneId = "test.abi.v2.tone";

// ---------------------------------------------------------------- loading
class Library {
public:
  explicit Library(const std::filesystem::path &path) {
#if defined(_WIN32)
    // Absolute path only, no search path or current directory.
    handle_ = ::LoadLibraryExW(std::filesystem::absolute(path).wstring().c_str(), nullptr,
                               LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                                   LOAD_LIBRARY_SEARCH_SYSTEM32);
#else
    handle_ = ::dlopen(std::filesystem::absolute(path).string().c_str(),
                       RTLD_NOW | RTLD_LOCAL);
#endif
  }
  ~Library() {
    if (handle_ == nullptr)
      return;
#if defined(_WIN32)
    ::FreeLibrary(static_cast<HMODULE>(handle_));
#else
    ::dlclose(handle_);
#endif
  }
  Library(const Library &) = delete;
  Library &operator=(const Library &) = delete;
  bool loaded() const { return handle_ != nullptr; }
  VoxModuleEntryV2Fn entry() const {
#if defined(_WIN32)
    return reinterpret_cast<VoxModuleEntryV2Fn>(
        ::GetProcAddress(static_cast<HMODULE>(handle_), VOX_MODULE_ENTRY_V2_NAME));
#else
    return reinterpret_cast<VoxModuleEntryV2Fn>(::dlsym(handle_, VOX_MODULE_ENTRY_V2_NAME));
#endif
  }

private:
  void *handle_{};
};

// ------------------------------------------------------- host validation
bool boundedString(const char *s, bool allowEmpty = false) {
  if (s == nullptr)
    return false;
  const auto *end = static_cast<const char *>(std::memchr(s, '\0', maxString + 1));
  return end != nullptr && (allowEmpty || end != s);
}

bool versioned(std::uint32_t structSize, std::uint32_t version, std::size_t known) {
  return structSize >= known && version == VOX_MODULE_ABI_V2;
}

bool validModule(const VoxModuleV2 *m) {
  return m != nullptr && versioned(m->structSize, m->version, sizeof(VoxModuleV2)) &&
         m->instrumentCount >= 1 && m->instrumentCount <= 64 && m->getInstrument &&
         m->create && m->destroy && m->prepare && m->setAllParameters && m->reset &&
         m->process && m->saveState && m->loadState && m->shutdown;
}

struct Parameter {
  std::string id, name, unit, group;
  std::vector<std::string> choices;
  std::uint32_t type{}, flags{};
  float minimum{}, maximum{}, defaultValue{}, step{};
  std::int32_t poolIndex{-1};
};

struct Instrument {
  std::string id, name, vendor, category;
  std::uint32_t instrumentVersion{}, contentVersion{}, stateVersion{};
  std::uint32_t maxVoices{}, maxMidiEventsPerBlock{}, maxStateBytes{};
  std::uint32_t latencySamples{}, tailSamples{}, outputChannels{};
  std::vector<Parameter> parameters;
};

bool integral(float v) { return v == std::floor(v); }

template <typename T> bool aligned(const T *pointer) {
  return reinterpret_cast<std::uintptr_t>(pointer) % alignof(T) == 0;
}

// `stride` is the parent array's element size; a record must fit inside it.
bool validParameter(const VoxParameterDescriptorV2 &p, std::uint32_t stride) {
  if (!versioned(p.structSize, p.version, sizeof(VoxParameterDescriptorV2)) ||
      p.structSize > stride ||
      !boundedString(p.id) || !boundedString(p.name) || !boundedString(p.unit, true) ||
      !boundedString(p.group, true) || p.type > VOX_PARAM_CHOICE ||
      (p.flags & ~VOX_PARAM_KNOWN_FLAGS) != 0 || !std::isfinite(p.minimum) ||
      !std::isfinite(p.maximum) || !std::isfinite(p.defaultValue) ||
      !std::isfinite(p.step) || !(p.minimum < p.maximum) || p.step < 0.0f ||
      p.defaultValue < p.minimum || p.defaultValue > p.maximum)
    return false;
  // Float: 0 (continuous) up to the range. Discrete types step by exactly 1.
  if (p.type == VOX_PARAM_FLOAT ? p.step > p.maximum - p.minimum : p.step != 1.0f)
    return false;
  if (p.type != VOX_PARAM_FLOAT &&
      (!integral(p.minimum) || !integral(p.maximum) || !integral(p.defaultValue)))
    return false;
  if (p.type == VOX_PARAM_BOOLEAN && (p.minimum != 0.0f || p.maximum != 1.0f))
    return false;
  if (p.type == VOX_PARAM_CHOICE) {
    // Bound the count before reading any choices[i].
    if (p.choices == nullptr || !aligned(p.choices) || p.choiceCount < 2 ||
        p.choiceCount > maxChoices || p.minimum != 0.0f ||
        static_cast<float>(p.choiceCount) != p.maximum + 1.0f)
      return false;
    for (std::uint32_t i = 0; i < p.choiceCount; ++i)
      if (!boundedString(p.choices[i]))
        return false;
  } else if (p.choiceCount != 0) {
    return false;
  }
  const bool exposed = (p.flags & VOX_PARAM_HOST_EXPOSED) != 0;
  return exposed ? p.poolIndex >= 0 && p.poolIndex < static_cast<std::int32_t>(poolPerSlot)
                 : p.poolIndex == -1;
}

// Validates and deep-copies; the host never keeps module pointers.
bool copyInstrument(const VoxInstrumentDescriptorV2 *d, Instrument &out) {
  if (d == nullptr || !aligned(d) || !versioned(d->structSize, d->version, sizeof(VoxInstrumentDescriptorV2)) ||
      !boundedString(d->id) || !boundedString(d->name) || !boundedString(d->vendor) ||
      !boundedString(d->category, true) || d->parameterCount > maxParameters ||
      (d->parameterCount > 0 && (d->parameters == nullptr || !aligned(d->parameters))) ||
      d->parameterStride < sizeof(VoxParameterDescriptorV2) ||
      d->parameterStride > maxParameterStride ||
      d->parameterStride % alignof(VoxParameterDescriptorV2) != 0 || d->maxVoices == 0 ||
      d->maxMidiEventsPerBlock < 16 || d->maxMidiEventsPerBlock > 2048 ||
      d->maxStateBytes > hostMaxStateBytes || d->outputChannels == 0 ||
      d->outputChannels > 2)
    return false;
  Instrument copy{d->id, d->name, d->vendor, d->category, d->instrumentVersion,
                  d->contentVersion, d->stateVersion, d->maxVoices,
                  d->maxMidiEventsPerBlock, d->maxStateBytes, d->latencySamples,
                  d->tailSamples, d->outputChannels, {}};
  std::set<std::string> ids;
  std::set<std::int32_t> pools;
  const auto *bytes = reinterpret_cast<const std::uint8_t *>(d->parameters);
  for (std::uint32_t i = 0; i < d->parameterCount; ++i) {
    const auto &p = *reinterpret_cast<const VoxParameterDescriptorV2 *>(
        bytes + static_cast<std::size_t>(i) * d->parameterStride);
    if (!validParameter(p, d->parameterStride) || !ids.insert(p.id).second ||
        (p.poolIndex >= 0 && !pools.insert(p.poolIndex).second))
      return false;
    Parameter q{p.id, p.name, p.unit, p.group, {}, p.type, p.flags, p.minimum,
                p.maximum, p.defaultValue, p.step, p.poolIndex};
    for (std::uint32_t c = 0; c < p.choiceCount; ++c)
      q.choices.emplace_back(p.choices[c]);
    copy.parameters.push_back(std::move(q));
  }
  out = std::move(copy);
  return true;
}

// ------------------------------------------------------------ helpers
VoxResult VOX_CALL hostLog(void *, std::uint32_t, const char *) { return VOX_OK; }
const VoxHostServicesV2 hostServices{sizeof(VoxHostServicesV2), VOX_MODULE_ABI_V2, nullptr,
                                     hostLog};

constexpr double rate = 48000.0;
constexpr std::uint32_t block = 128;

VoxPrepareSpecV2 prepareSpec() {
  return {sizeof(VoxPrepareSpecV2), VOX_MODULE_ABI_V2, rate, block, 2, 4, 64};
}

VoxMidiEventV2 midi(std::uint32_t offset, std::uint8_t a, std::uint8_t b, std::uint8_t c) {
  VoxMidiEventV2 e{};
  e.sampleOffset = offset;
  e.size = 3;
  e.bytes[0] = a;
  e.bytes[1] = b;
  e.bytes[2] = c;
  return e;
}

struct Block {
  std::array<std::vector<float>, 2> channels{std::vector<float>(block),
                                             std::vector<float>(block)};
  std::array<float *, 2> pointers{channels[0].data(), channels[1].data()};
  std::vector<VoxParamChangeV2> changes;
  std::vector<VoxMidiEventV2> events;
  std::uint32_t flags{};
  VoxTransportV2 transport{sizeof(VoxTransportV2), VOX_MODULE_ABI_V2, 0, 0, 0, 0,
                           0.0, 0.0, 0.0, 0.0, 0.0, 0};

  VoxProcessContextV2 context() {
    return {sizeof(VoxProcessContextV2),
            VOX_MODULE_ABI_V2,
            block,
            2,
            pointers.data(),
            changes.data(),
            static_cast<std::uint32_t>(changes.size()),
            sizeof(VoxParamChangeV2),
            events.data(),
            static_cast<std::uint32_t>(events.size()),
            sizeof(VoxMidiEventV2),
            flags,
            0,
            0,
            &transport};
  }
  bool silent(std::uint32_t from = 0, std::uint32_t to = block) const {
    for (std::uint32_t i = from; i < to; ++i)
      if (channels[0][i] != 0.0f || channels[1][i] != 0.0f)
        return false;
    return true;
  }
  bool sounding(std::uint32_t from = 0, std::uint32_t to = block) const {
    for (std::uint32_t i = from; i < to; ++i)
      if (channels[0][i] != 0.0f)
        return true;
    return false;
  }
};

std::vector<float> defaults(const Instrument &instrument) {
  std::vector<float> values;
  for (const auto &p : instrument.parameters)
    values.push_back(p.defaultValue);
  return values;
}

VoxInstance makeInstance(const VoxModuleV2 &m, const Instrument &instrument) {
  const VoxCreateParamsV2 params{sizeof(VoxCreateParamsV2), VOX_MODULE_ABI_V2, 0, 0};
  VoxInstance instance{};
  REQUIRE(m.create(toneId, &params, &instance) == VOX_OK && instance != nullptr, "create");
  const auto spec = prepareSpec();
  REQUIRE(m.prepare(instance, &spec) == VOX_OK, "prepare");
  const auto values = defaults(instrument);
  REQUIRE(m.setAllParameters(instance, values.data(),
                             static_cast<std::uint32_t>(values.size())) == VOX_OK,
          "setAllParameters defaults");
  return instance;
}

// Renders `blocks` blocks with a NoteOn at sample 10 of the first block.
std::vector<float> renderPhrase(const VoxModuleV2 &m, VoxInstance instance, int blocks) {
  std::vector<float> out;
  for (int b = 0; b < blocks; ++b) {
    Block x;
    if (b == 0)
      x.events.push_back(midi(10, 0x90, 60, 100));
    const auto c = x.context();
    REQUIRE(m.process(instance, &c) == VOX_OK, "process phrase");
    out.insert(out.end(), x.channels[0].begin(), x.channels[0].end());
  }
  return out;
}

// ---------------------------------------------------------------- tests
void layout() {
  std::array<std::uint32_t, 128> c{};
  const auto count = voxAbiLayoutC(c.data(), static_cast<std::uint32_t>(c.size()));
#define L(type, field) static_cast<std::uint32_t>(offsetof(type, field))
  const std::uint32_t cpp[] = {
      sizeof(VoxHostServicesV2), sizeof(VoxParameterDescriptorV2),
      L(VoxParameterDescriptorV2, choices), L(VoxParameterDescriptorV2, type),
      L(VoxParameterDescriptorV2, minimum), L(VoxParameterDescriptorV2, poolIndex),
      L(VoxParameterDescriptorV2, flags), sizeof(VoxInstrumentDescriptorV2),
      L(VoxInstrumentDescriptorV2, instrumentVersion),
      L(VoxInstrumentDescriptorV2, parameters),
      L(VoxInstrumentDescriptorV2, parameterStride),
      L(VoxInstrumentDescriptorV2, maxStateBytes),
      L(VoxInstrumentDescriptorV2, outputChannels), sizeof(VoxCreateParamsV2),
      sizeof(VoxPrepareSpecV2), L(VoxPrepareSpecV2, sampleRate),
      L(VoxPrepareSpecV2, maxMidiEventsPerBlock), sizeof(VoxParamChangeV2),
      L(VoxParamChangeV2, plain), sizeof(VoxMidiEventV2), L(VoxMidiEventV2, bytes),
      sizeof(VoxProcessContextV2), L(VoxProcessContextV2, outputs),
      L(VoxProcessContextV2, paramChanges), L(VoxProcessContextV2, paramChangeStride),
      L(VoxProcessContextV2, midiEvents), L(VoxProcessContextV2, midiEventStride),
      L(VoxProcessContextV2, releaseFlags), L(VoxProcessContextV2, samplePosition),
      L(VoxProcessContextV2, transport), sizeof(VoxTransportV2), L(VoxTransportV2, flags),
      L(VoxTransportV2, tempoBpm), L(VoxTransportV2, ppqPosition),
      L(VoxTransportV2, loopEndPpq), L(VoxTransportV2, timeInSamples),
      sizeof(VoxModuleV2), L(VoxModuleV2, getInstrument), L(VoxModuleV2, process),
      L(VoxModuleV2, loadState), L(VoxModuleV2, content), L(VoxModuleV2, editor),
      L(VoxModuleV2, shutdown), L(VoxProcessContextV2, pattern),
      L(VoxProcessContextV2, patternGeneration), sizeof(VoxPatternStepV2),
      L(VoxPatternStepV2, gate), L(VoxPatternStepV2, slide), L(VoxPatternStepV2, probability),
      L(VoxPatternStepV2, reserved), sizeof(VoxPatternV2), L(VoxPatternV2, cellStride),
      L(VoxPatternV2, cells), sizeof(VoxPatternCapsV2), L(VoxPatternCapsV2, stepFields),
      L(VoxPatternCapsV2, laneNames), L(VoxPatternCapsV2, laneNotes),
      sizeof(VoxContentApiV2), L(VoxContentApiV2, getPatternCaps),
      L(VoxContentApiV2, getFactoryPattern), L(VoxProcessContextV2, noteOutput),
      sizeof(VoxNoteEventV2), L(VoxNoteEventV2, note), L(VoxNoteEventV2, flags),
      sizeof(VoxNoteOutputV2), L(VoxNoteOutputV2, count), L(VoxNoteOutputV2, events)};
#undef L
  REQUIRE(count == std::size(cpp), "C and C++ layout tables have the same length");
  for (std::uint32_t i = 0; i < count; ++i)
    REQUIRE(c[i] == cpp[i], "C11 and C++20 layout agree at entry " << i);
}

void validation(const VoxModuleV2 &m, const VoxInstrumentDescriptorV2 &d) {
  // Newer minor version: larger structSize with trailing fields is accepted.
  struct Bigger {
    VoxModuleV2 table;
    std::uint64_t future[2];
  } bigger{m, {1, 2}};
  bigger.table.structSize = sizeof(Bigger);
  REQUIRE(validModule(&bigger.table), "larger module structSize accepted");
  auto smaller = m;
  smaller.structSize = sizeof(VoxModuleV2) - 8;
  REQUIRE(!validModule(&smaller), "smaller module structSize rejected");
  auto wrongVersion = m;
  wrongVersion.version = 3;
  REQUIRE(!validModule(&wrongVersion), "unknown module major version rejected");
  auto missing = m;
  missing.process = nullptr;
  REQUIRE(!validModule(&missing), "missing process pointer rejected");
  REQUIRE(!validModule(nullptr), "null module rejected");

  Instrument copy;
  REQUIRE(copyInstrument(&d, copy), "test instrument validates");
  REQUIRE(copy.id == toneId && copy.parameters.size() == 4, "deep copy identity");
  REQUIRE(copy.parameters[3].choices.size() == 3 && copy.parameters[3].choices[2] == "Saw",
          "choices deep-copied");
  REQUIRE(copy.parameters[1].type == VOX_PARAM_INTEGER && copy.parameters[1].minimum == -2.0f,
          "typed integer range copied");
  REQUIRE(!copyInstrument(nullptr, copy), "null descriptor rejected");

  // Mutated copies of the module's own descriptors.
  std::vector<VoxParameterDescriptorV2> params(d.parameters, d.parameters + d.parameterCount);
  auto withParams = [&](auto mutate) {
    auto p = params;
    auto desc = d;
    mutate(p, desc);
    desc.parameters = p.data();
    Instrument ignored;
    return copyInstrument(&desc, ignored);
  };
  using P = std::vector<VoxParameterDescriptorV2>;
  using D = VoxInstrumentDescriptorV2;
  REQUIRE(withParams([](P &, D &) {}), "unmutated copy validates");
  REQUIRE(!withParams([](P &p, D &) { p[0].structSize = 8; }), "short parameter struct");
  REQUIRE(!withParams([](P &p, D &) { p[0].id = nullptr; }), "null parameter id");
  REQUIRE(!withParams([](P &p, D &) { p[0].id = ""; }), "empty parameter id");
  static const std::string longId(maxString + 8, 'x');
  REQUIRE(!withParams([](P &p, D &) { p[0].id = longId.c_str(); }), "oversized id");
  REQUIRE(!withParams([](P &p, D &) { p[1].id = p[0].id; }), "duplicate parameter id");
  REQUIRE(!withParams([](P &p, D &) { p[1].poolIndex = p[0].poolIndex; }), "duplicate pool");
  REQUIRE(!withParams([](P &p, D &) { p[0].poolIndex = 64; }), "pool index out of range");
  REQUIRE(!withParams([](P &p, D &) { p[0].flags = 0; }), "unexposed with pool index");
  REQUIRE(!withParams([](P &p, D &) { p[0].flags |= 0x80u; }), "unknown flag");
  REQUIRE(!withParams([](P &p, D &) { p[0].type = 9; }), "unknown type");
  REQUIRE(!withParams([](P &p, D &) { p[0].maximum = NAN; }), "non-finite maximum");
  REQUIRE(!withParams([](P &p, D &) { p[0].defaultValue = 2.0f; }), "default out of range");
  REQUIRE(!withParams([](P &p, D &) { p[1].defaultValue = 0.5f; }), "fractional integer");
  REQUIRE(!withParams([](P &p, D &) { p[3].choiceCount = 2; }), "choice count mismatch");
  REQUIRE(!withParams([](P &p, D &) { p[3].choices = nullptr; }), "missing choices");
  REQUIRE(!withParams([](P &, D &x) { x.parameterStride = 40; }), "short parameter stride");
  // Rejected before any record is read, so no misaligned access happens.
  REQUIRE(!withParams([](P &, D &x) { x.parameterStride = 81; }), "unaligned parameter stride");
  REQUIRE(!withParams([](P &, D &x) { x.parameterStride = maxParameterStride + 8; }),
          "parameter stride above limit");
  REQUIRE(!withParams([](P &p, D &) { p[0].structSize = sizeof(VoxParameterDescriptorV2) + 8; }),
          "record structSize larger than the parent stride");
  REQUIRE(!withParams([](P &p, D &) { p[1].step = 0.0f; }), "integer step must be 1");
  REQUIRE(!withParams([](P &p, D &) { p[2].step = 0.0f; }), "boolean step must be 1");
  REQUIRE(!withParams([](P &p, D &) { p[3].step = 0.5f; }), "choice step must be 1");
  REQUIRE(!withParams([](P &p, D &) { p[0].step = 2.0f; }), "float step above range");
  REQUIRE(!withParams([](P &p, D &) { p[0].step = -0.1f; }), "negative float step");
  REQUIRE(!withParams([](P &p, D &) {
            p[3].choiceCount = 100000; // choices has 3 entries: must not be read
            p[3].maximum = 99999.0f;
          }),
          "unbounded choice count rejected before reading choices");
  REQUIRE(withParams([](P &, D &x) {
            x.parameterCount = 0;
            x.parameters = nullptr;
          }),
          "instrument without parameters is valid");
  REQUIRE(!withParams([](P &, D &x) { x.structSize = 64; }), "short descriptor struct");
  REQUIRE(!withParams([](P &, D &x) { x.maxStateBytes = hostMaxStateBytes + 1; }),
          "state budget above host limit");
  REQUIRE(!withParams([](P &, D &x) { x.maxMidiEventsPerBlock = 8; }), "MIDI budget below 16");
  REQUIRE(!withParams([](P &, D &x) { x.parameterCount = maxParameters + 1; }),
          "parameter count above limit");

  // A larger parameter stride (future minor fields) is stepped correctly.
  struct WideParameter {
    VoxParameterDescriptorV2 p;
    std::uint64_t future;
  };
  std::vector<WideParameter> wide;
  for (const auto &p : params)
    wide.push_back({p, 7});
  for (auto &w : wide)
    w.p.structSize = sizeof(WideParameter);
  auto wideDesc = d;
  wideDesc.parameters = &wide[0].p;
  wideDesc.parameterStride = sizeof(WideParameter);
  Instrument wideCopy;
  static_assert(sizeof(WideParameter) % alignof(VoxParameterDescriptorV2) == 0);
  REQUIRE(copyInstrument(&wideDesc, wideCopy) && wideCopy.parameters[3].id == "wave",
          "larger aligned parameter stride accepted and stepped");
  for (auto &w : wide)
    w.p.structSize = sizeof(WideParameter) + 8;
  REQUIRE(!copyInstrument(&wideDesc, wideCopy), "record larger than wide stride rejected");
}

void lifecycle(const VoxModuleV2 &m, const Instrument &instrument) {
  const VoxCreateParamsV2 params{sizeof(VoxCreateParamsV2), VOX_MODULE_ABI_V2, 0, 0};
  VoxInstance bad = reinterpret_cast<VoxInstance>(1);
  REQUIRE(m.create("unknown.id", &params, &bad) == VOX_E_INVALID_ARG && bad == nullptr,
          "unknown instrument id rejected, out cleared");
  REQUIRE(m.create(toneId, nullptr, &bad) == VOX_E_INVALID_ARG, "null create params");
  REQUIRE(m.create(toneId, &params, nullptr) == VOX_E_INVALID_ARG, "null create out");

  VoxInstance instance{};
  REQUIRE(m.create(toneId, &params, &instance) == VOX_OK, "create");
  auto badSpec = prepareSpec();
  badSpec.sampleRate = 0.0;
  REQUIRE(m.prepare(instance, &badSpec) == VOX_E_INVALID_ARG, "zero sample rate rejected");
  badSpec = prepareSpec();
  badSpec.structSize = 16;
  REQUIRE(m.prepare(instance, &badSpec) == VOX_E_INVALID_ARG, "short prepare struct");
  badSpec = prepareSpec();
  badSpec.outputChannels = 3;
  REQUIRE(m.prepare(instance, &badSpec) == VOX_E_INVALID_ARG, "too many outputs");
  badSpec = prepareSpec();
  badSpec.maxParamChangesPerBlock = 5; // above parameterCount
  REQUIRE(m.prepare(instance, &badSpec) == VOX_E_INVALID_ARG, "change budget above parameters");
  badSpec = prepareSpec();
  badSpec.maxMidiEventsPerBlock = instrument.maxMidiEventsPerBlock + 1;
  REQUIRE(m.prepare(instance, &badSpec) == VOX_E_INVALID_ARG, "MIDI budget above descriptor");
  badSpec = prepareSpec();
  badSpec.maxMidiEventsPerBlock = 8;
  REQUIRE(m.prepare(instance, &badSpec) == VOX_E_INVALID_ARG, "MIDI budget below 16");
  REQUIRE(m.prepare(nullptr, &badSpec) == VOX_E_INVALID_ARG, "null instance prepare");
  {
    Block x;
    const auto c = x.context();
    REQUIRE(m.process(instance, &c) == VOX_E_INVALID_ARG, "process before prepare rejected");
  }
  const auto spec = prepareSpec();
  REQUIRE(m.prepare(instance, &spec) == VOX_OK, "prepare");

  auto values = defaults(instrument);
  REQUIRE(m.setAllParameters(instance, values.data(), 3) == VOX_E_INVALID_ARG,
          "parameter count mismatch");
  auto invalid = values;
  invalid[1] = 0.5f; // integer octave
  REQUIRE(m.setAllParameters(instance, invalid.data(), 4) == VOX_E_INVALID_ARG,
          "invalid snapshot rejected");
  REQUIRE(m.setAllParameters(instance, values.data(), 4) == VOX_OK, "snapshot");

  // Process argument validation.
  {
    Block x;
    auto c = x.context();
    c.structSize = 32;
    REQUIRE(m.process(instance, &c) == VOX_E_INVALID_ARG, "short process context");
    c = x.context();
    c.structSize = VOX_PROCESS_CONTEXT_V2_BASE_SIZE - 8;
    REQUIRE(m.process(instance, &c) == VOX_E_INVALID_ARG, "context below the V2.0 size");
    c = x.context();
    c.structSize = VOX_PROCESS_CONTEXT_V2_BASE_SIZE; // a V2.0 host, no pattern fields
    c.pattern = reinterpret_cast<const VoxPatternV2 *>(std::uintptr_t{1}); // must not be read
    REQUIRE(m.process(instance, &c) == VOX_OK, "V2.0 context accepted, appended fields ignored");
    c = x.context();
    c.numSamples = 4097;
    REQUIRE(m.process(instance, &c) == VOX_E_INVALID_ARG, "block above prepared size");
    c = x.context();
    c.releaseFlags = 0x80u;
    REQUIRE(m.process(instance, &c) == VOX_E_INVALID_ARG, "unknown release flag");
    x.events.push_back(midi(0, 0x90, 60, 100));
    c = x.context();
    c.midiEventStride = 8;
    REQUIRE(m.process(instance, &c) == VOX_E_INVALID_ARG, "short MIDI stride");
    c = x.context();
    c.midiEventStride = 17;
    REQUIRE(m.process(instance, &c) == VOX_E_INVALID_ARG, "unaligned MIDI stride");
    x.changes.push_back({0, 0, 0.5f, 0});
    c = x.context();
    c.paramChangeStride = 18;
    REQUIRE(m.process(instance, &c) == VOX_E_INVALID_ARG, "unaligned change stride");
    c = x.context();
    c.paramChangeStride = 1024 + 16;
    REQUIRE(m.process(instance, &c) == VOX_E_INVALID_ARG, "oversized change stride");
    // Counts above the prepared budgets are rejected before any record is read
    // (the backing arrays hold one record each).
    c = x.context();
    c.paramChangeCount = prepareSpec().maxParamChangesPerBlock + 1;
    REQUIRE(m.process(instance, &c) == VOX_E_INVALID_ARG, "change count above budget");
    x.changes.clear();
    c = x.context();
    c.midiEventStride = 1024 + 16;
    REQUIRE(m.process(instance, &c) == VOX_E_INVALID_ARG, "oversized MIDI stride");
    c = x.context();
    c.midiEventCount = prepareSpec().maxMidiEventsPerBlock + 1;
    REQUIRE(m.process(instance, &c) == VOX_E_INVALID_ARG, "MIDI count above budget");
    c = x.context();
    c.midiEventCount = 0xFFFFFFFFu;
    REQUIRE(m.process(instance, &c) == VOX_E_INVALID_ARG, "huge MIDI count rejected");
    REQUIRE(m.process(instance, nullptr) == VOX_E_INVALID_ARG, "null context");
    c = x.context();
    c.transport = nullptr;
    REQUIRE(m.process(instance, &c) == VOX_E_INVALID_ARG, "null transport");
    x.transport.flags = 0x100u;
    c = x.context();
    REQUIRE(m.process(instance, &c) == VOX_E_INVALID_ARG, "unknown transport flag");
    x.transport.flags = 0;
    x.transport.structSize = 16;
    c = x.context();
    REQUIRE(m.process(instance, &c) == VOX_E_INVALID_ARG, "short transport struct");
    x.transport.structSize = sizeof(VoxTransportV2);
  }

  // Silence without notes; NoteOn at its sample offset.
  {
    Block x;
    auto c = x.context();
    REQUIRE(m.process(instance, &c) == VOX_OK && x.silent(), "silent without notes");
    Block y;
    y.events.push_back(midi(10, 0x90, 69, 100));
    c = y.context();
    REQUIRE(m.process(instance, &c) == VOX_OK, "process NoteOn");
    REQUIRE(y.silent(0, 10) && y.sounding(10), "NoteOn sample-accurate at offset 10");
    REQUIRE(y.channels[0] == y.channels[1], "both channels written");
  }

  // Parameter change at its offset (block-rate hosts send offset 0).
  {
    Block x;
    x.changes.push_back({0, 64, 0.0f, 0}); // gain -> 0 at sample 64
    auto c = x.context();
    REQUIRE(m.process(instance, &c) == VOX_OK, "process gain change");
    REQUIRE(x.sounding(0, 64) && x.silent(64), "gain change applied at its offset");
  }

  // Panic at start silences from sample 0; a later NoteOn in the block sounds.
  {
    const float gain = 0.5f;
    Block restore;
    restore.changes.push_back({0, 0, gain, 0});
    auto c = restore.context();
    REQUIRE(m.process(instance, &c) == VOX_OK && restore.sounding(), "note still held");
    Block x;
    x.flags = VOX_PANIC_AT_START;
    x.events.push_back(midi(96, 0x90, 72, 100));
    c = x.context();
    REQUIRE(m.process(instance, &c) == VOX_OK, "process panic");
    REQUIRE(x.silent(0, 96) && x.sounding(96), "panic at sample 0, later NoteOn sounds");
  }

  // Saturation fallback releases at block end.
  {
    Block x;
    x.flags = VOX_RELEASE_ALL_SOUND_AT_END;
    auto c = x.context();
    REQUIRE(m.process(instance, &c) == VOX_OK && x.sounding(), "sounds until block end");
    Block y;
    c = y.context();
    REQUIRE(m.process(instance, &c) == VOX_OK && y.silent(), "silent after end-of-block flag");
  }

  // Repeated same-pitch NoteOn retriggers one key; one NoteOff ends it.
  {
    Block x;
    x.events = {midi(0, 0x90, 60, 100), midi(20, 0x90, 60, 90), midi(40, 0x80, 60, 0)};
    auto c = x.context();
    REQUIRE(m.process(instance, &c) == VOX_OK, "process repeated key");
    REQUIRE(x.sounding(0, 40) && x.silent(40), "single NoteOff ends a retriggered key");
  }

  // A larger aligned record stride (future minor fields) is stepped correctly.
  {
    struct WideMidi {
      VoxMidiEventV2 event;
      std::uint64_t future;
    };
    static_assert(sizeof(WideMidi) % alignof(VoxMidiEventV2) == 0);
    const WideMidi wide[] = {{midi(0, 0x90, 60, 100), 1}, {midi(50, 0x80, 60, 0), 2}};
    Block x;
    auto c = x.context();
    c.midiEvents = &wide[0].event;
    c.midiEventCount = 2;
    c.midiEventStride = sizeof(WideMidi);
    REQUIRE(m.process(instance, &c) == VOX_OK, "process wide MIDI stride");
    REQUIRE(x.sounding(0, 50) && x.silent(50), "wide MIDI records stepped by stride");
  }

  // State round trip, buffer and version failures.
  {
    std::vector<std::uint8_t> state(instrument.maxStateBytes);
    std::uint32_t written = 0, version = 0;
    REQUIRE(m.saveState(instance, state.data(), 4, &written, &version) ==
                    VOX_E_BUFFER_TOO_SMALL &&
                written == instrument.maxStateBytes,
            "small buffer reports required size");
    REQUIRE(m.saveState(instance, state.data(), static_cast<std::uint32_t>(state.size()),
                        &written, &version) == VOX_OK &&
                version == instrument.stateVersion,
            "saveState");
    state.resize(written);
    VoxInstance other = makeInstance(m, instrument);
    REQUIRE(m.loadState(other, version + 1, state.data(), written) == VOX_E_STATE_VERSION,
            "newer state version rejected");
    auto corrupt = state;
    corrupt[0] ^= 0xFFu;
    REQUIRE(m.loadState(other, version, corrupt.data(), written) == VOX_E_STATE_CORRUPT,
            "corrupt magic rejected");
    REQUIRE(m.loadState(other, version, state.data(), written - 1) == VOX_E_STATE_CORRUPT,
            "truncated state rejected");
    auto badValue = state;
    const float nan = NAN;
    std::memcpy(badValue.data() + 8, &nan, 4);
    REQUIRE(m.loadState(other, version, badValue.data(), written) == VOX_E_STATE_CORRUPT,
            "non-finite saved value rejected");
    REQUIRE(m.loadState(other, version, state.data(), written) == VOX_OK, "loadState");
    std::vector<std::uint8_t> again(instrument.maxStateBytes);
    std::uint32_t againWritten = 0, againVersion = 0;
    REQUIRE(m.saveState(other, again.data(), static_cast<std::uint32_t>(again.size()),
                        &againWritten, &againVersion) == VOX_OK &&
                againWritten == written &&
                std::memcmp(again.data(), state.data(), written) == 0,
            "state round trip is byte-identical");
    REQUIRE(m.destroy(other) == VOX_OK, "destroy restored instance");
  }

  // reset keeps parameter values: output after reset equals a fresh instance
  // with the same snapshot.
  {
    auto custom = defaults(instrument);
    custom[0] = 0.25f; // gain
    custom[1] = 1.0f;  // octave
    custom[3] = 2.0f;  // saw
    REQUIRE(m.setAllParameters(instance, custom.data(), 4) == VOX_OK, "custom snapshot");
    REQUIRE(m.reset(instance) == VOX_OK, "reset");
    const auto afterReset = renderPhrase(m, instance, 4);
    VoxInstance fresh = makeInstance(m, instrument);
    REQUIRE(m.setAllParameters(fresh, custom.data(), 4) == VOX_OK, "fresh snapshot");
    const auto reference = renderPhrase(m, fresh, 4);
    REQUIRE(afterReset == reference, "reset keeps parameters, output deterministic");
    REQUIRE(m.destroy(fresh) == VOX_OK, "destroy fresh");
  }

  REQUIRE(m.shutdown() == VOX_E_INTERNAL, "shutdown refuses while instances live");
  REQUIRE(m.destroy(instance) == VOX_OK, "destroy");
  REQUIRE(m.destroy(nullptr) == VOX_E_INVALID_ARG, "destroy null");
}

void determinism(const VoxModuleV2 &m, const Instrument &instrument) {
  VoxInstance a = makeInstance(m, instrument);
  VoxInstance b = makeInstance(m, instrument);
  const auto first = renderPhrase(m, a, 8);
  const auto second = renderPhrase(m, b, 8);
  REQUIRE(first == second, "two instances render bit-identical audio");
  double energy = 0.0;
  for (const float v : first)
    energy += static_cast<double>(v) * v;
  REQUIRE(energy > 1.0, "phrase is audible");
  REQUIRE(m.destroy(a) == VOX_OK && m.destroy(b) == VOX_OK, "destroy pair");
}

void noAudioThreadAllocation(const VoxModuleV2 &m, const Instrument &instrument) {
  VoxInstance instance = makeInstance(m, instrument);
  Block x; // all buffers allocated before the probe
  x.events = {midi(0, 0x90, 60, 100), midi(64, 0x80, 60, 0)};
  x.changes = {{0, 0, 0.7f, 0}, {2, 32, 1.0f, 0}};
#if defined(VOX_TEST_INTERPOSE_MALLOC)
  voxProbeBegin();
#endif
  for (int i = 0; i < 1000; ++i) {
    const auto c = x.context();
    REQUIRE(m.process(instance, &c) == VOX_OK, "probed process");
    if (i % 100 == 0)
      REQUIRE(m.reset(instance) == VOX_OK, "probed reset");
  }
#if defined(VOX_TEST_INTERPOSE_MALLOC)
  const auto allocations = voxProbeEnd();
  REQUIRE(allocations == 0, "process/reset allocate nothing (" << allocations << ")");
  std::cout << "allocation probe: 0 allocations in 1000 process + 10 reset calls\n";
#else
  std::cout << "allocation probe: not available on this platform (no interposer)\n";
#endif
  REQUIRE(m.destroy(instance) == VOX_OK, "destroy probed");
}
} // namespace

int main(int argc, char **argv) {
  REQUIRE(argc == 2, "usage: module_abi_v2_tests <module path>");
  layout();

  const VoxModuleV2 *module = nullptr;
  {
    Library library(argv[1]);
    REQUIRE(library.loaded(), "module library loads from absolute path");
    const auto entry = library.entry();
    REQUIRE(entry != nullptr, "voxModuleEntryV2 exported");

    REQUIRE(entry(1, &hostServices, &module) == VOX_E_UNSUPPORTED && module == nullptr,
            "older host ABI rejected");
    REQUIRE(entry(VOX_MODULE_ABI_V2, nullptr, &module) == VOX_E_INVALID_ARG,
            "null host services rejected");
    auto shortHost = hostServices;
    shortHost.structSize = 8;
    REQUIRE(entry(VOX_MODULE_ABI_V2, &shortHost, &module) == VOX_E_INVALID_ARG,
            "short host services rejected");
    REQUIRE(entry(VOX_MODULE_ABI_V2, &hostServices, nullptr) == VOX_E_INVALID_ARG,
            "null module out rejected");
    REQUIRE(entry(VOX_MODULE_ABI_V2, &hostServices, &module) == VOX_OK, "entry");
    REQUIRE(validModule(module), "module table valid");
    REQUIRE(module->editor == nullptr, "editor table stays reserved (NULL)");
    REQUIRE(module->content != nullptr &&
                module->content->structSize >= sizeof(VoxContentApiV2) &&
                module->content->version == VOX_MODULE_ABI_V2 &&
                module->content->getPatternCaps != nullptr &&
                module->content->getFactoryPattern != nullptr,
            "content table is versioned and complete");
    const VoxPatternCapsV2 *caps = nullptr;
    REQUIRE(module->content->getPatternCaps("no.such.instrument", &caps) == VOX_OK &&
                caps == nullptr,
            "an unknown instrument has no pattern");
    REQUIRE(module->content->getPatternCaps(nullptr, &caps) == VOX_E_INVALID_ARG,
            "null instrument id rejected");

    const VoxInstrumentDescriptorV2 *descriptor = nullptr;
    REQUIRE(module->getInstrument(1, &descriptor) == VOX_E_INVALID_ARG &&
                descriptor == nullptr,
            "out-of-range instrument index");
    REQUIRE(module->getInstrument(0, nullptr) == VOX_E_INVALID_ARG, "null descriptor out");
    REQUIRE(module->getInstrument(0, &descriptor) == VOX_OK, "getInstrument");
    validation(*module, *descriptor);
    Instrument instrument;
    REQUIRE(copyInstrument(descriptor, instrument), "deep copy");

    lifecycle(*module, instrument);
    determinism(*module, instrument);
    noAudioThreadAllocation(*module, instrument);
    REQUIRE(module->shutdown() == VOX_OK, "shutdown after last instance");
  } // library unloaded here; no module pointer is used afterwards
  std::cout << "module_abi_v2_tests: " << run << " checks passed\n";
  return 0;
}
