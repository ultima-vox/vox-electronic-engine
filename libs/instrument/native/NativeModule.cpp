#include "instrument/native/NativeModule.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <map>
#include <mutex>
#include <set>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace vstengine::instrument::native {
namespace {

// ------------------------------------------------------------- validation
bool boundedString(const char *s, bool allowEmpty = false) noexcept {
  if (s == nullptr)
    return false;
  const auto *end = static_cast<const char *>(std::memchr(s, '\0', maxDescriptorString + 1));
  return end != nullptr && (allowEmpty || end != s);
}

bool versioned(std::uint32_t structSize, std::uint32_t version, std::size_t known) noexcept {
  return structSize >= known && version == VOX_MODULE_ABI_V2;
}

template <typename T> bool aligned(const T *pointer) noexcept {
  return reinterpret_cast<std::uintptr_t>(pointer) % alignof(T) == 0;
}

bool integral(float v) noexcept { return v == std::floor(v); }

bool importParameter(const VoxParameterDescriptorV2 &p, std::uint32_t stride,
                     ParameterDescriptor &out) {
  if (!versioned(p.structSize, p.version, sizeof(VoxParameterDescriptorV2)) ||
      p.structSize > stride || !boundedString(p.id) || !boundedString(p.name) ||
      !boundedString(p.unit, true) || !boundedString(p.group, true) ||
      p.type > VOX_PARAM_CHOICE || (p.flags & ~VOX_PARAM_KNOWN_FLAGS) != 0 ||
      !std::isfinite(p.minimum) || !std::isfinite(p.maximum) ||
      !std::isfinite(p.defaultValue) || !std::isfinite(p.step) ||
      !(p.minimum < p.maximum) || p.step < 0.0f || p.defaultValue < p.minimum ||
      p.defaultValue > p.maximum)
    return false;
  if (p.type == VOX_PARAM_FLOAT ? p.step > p.maximum - p.minimum : p.step != 1.0f)
    return false;
  if (p.type != VOX_PARAM_FLOAT &&
      (!integral(p.minimum) || !integral(p.maximum) || !integral(p.defaultValue)))
    return false;
  if (p.type == VOX_PARAM_BOOLEAN && (p.minimum != 0.0f || p.maximum != 1.0f))
    return false;
  std::vector<std::string> choices;
  if (p.type == VOX_PARAM_CHOICE) {
    if (p.choices == nullptr || !aligned(p.choices) || p.choiceCount < 2 ||
        p.choiceCount > maxChoices || p.minimum != 0.0f ||
        static_cast<float>(p.choiceCount) != p.maximum + 1.0f)
      return false;
    for (std::uint32_t i = 0; i < p.choiceCount; ++i) {
      if (!boundedString(p.choices[i]))
        return false;
      choices.emplace_back(p.choices[i]);
    }
  } else if (p.choiceCount != 0) {
    return false;
  }
  // This slice binds every native parameter to a host pool endpoint.
  if ((p.flags & VOX_PARAM_HOST_EXPOSED) == 0 || p.poolIndex < 0 ||
      p.poolIndex >= static_cast<std::int32_t>(poolParametersPerSlot))
    return false;
  ParameterDescriptor q;
  q.id = p.id;
  q.name = p.name;
  q.unit = p.unit;
  q.group = p.group;
  q.minimum = p.minimum;
  q.maximum = p.maximum;
  q.defaultValue = p.defaultValue;
  q.step = p.step;
  q.type = static_cast<ParameterType>(p.type);
  q.choices = std::move(choices);
  q.automatable = (p.flags & VOX_PARAM_AUTOMATABLE) != 0;
  q.modulatable = (p.flags & VOX_PARAM_MODULATABLE) != 0;
  q.poolIndex = static_cast<std::int16_t>(p.poolIndex);
  out = std::move(q);
  return true;
}

// ------------------------------------------------------------- loading
void *openLibrary(const std::filesystem::path &path) noexcept {
#if defined(_WIN32)
  // Absolute path only; no search path and no current directory.
  return ::LoadLibraryExW(path.wstring().c_str(), nullptr,
                          LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
#else
  return ::dlopen(path.string().c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
}

void closeLibrary(void *library) noexcept {
#if defined(_WIN32)
  ::FreeLibrary(static_cast<HMODULE>(library));
#else
  ::dlclose(library);
#endif
}

VoxModuleEntryV2Fn entryPoint(void *library) noexcept {
#if defined(_WIN32)
  return reinterpret_cast<VoxModuleEntryV2Fn>(
      ::GetProcAddress(static_cast<HMODULE>(library), VOX_MODULE_ENTRY_V2_NAME));
#else
  return reinterpret_cast<VoxModuleEntryV2Fn>(::dlsym(library, VOX_MODULE_ENTRY_V2_NAME));
#endif
}

VoxResult VOX_CALL hostLog(void *, std::uint32_t, const char *) { return VOX_OK; }
const VoxHostServicesV2 hostServices{sizeof(VoxHostServicesV2), VOX_MODULE_ABI_V2, nullptr,
                                     hostLog};

std::atomic<std::uint64_t> &midiOverflows() noexcept {
  static std::atomic<std::uint64_t> count{};
  return count;
}

std::atomic<std::uint64_t> &invalidNoteReports() noexcept {
  static std::atomic<std::uint64_t> count{0};
  return count;
}

// Module-written note output, checked before the Rack may use any of it: the
// count within the buffer, and every record inside the block, in order, with
// a MIDI note, a 1..127 velocity, a declared lane and known flags. Anything
// else voids the whole block (fail closed) and is counted.
bool validNoteOutput(const VoxNoteOutputV2 &out, std::uint32_t sampleCount,
                     std::uint32_t laneLimit) noexcept {
  if (out.count > out.capacity)
    return false;
  std::uint32_t previous = 0;
  for (std::uint32_t i = 0; i < out.count; ++i) {
    VoxNoteEventV2 e;
    std::memcpy(&e, reinterpret_cast<const std::uint8_t *>(out.events) +
                        static_cast<std::size_t>(i) * out.stride,
                sizeof e);
    if (e.sampleOffset >= sampleCount || e.sampleOffset < previous || e.note > 127 ||
        e.velocity < 1 || e.velocity > 127 || e.lane >= laneLimit ||
        (e.flags & ~VOX_NOTE_EVENT_KNOWN_FLAGS) != 0)
      return false;
    previous = e.sampleOffset;
  }
  return true;
}

std::atomic<std::uint64_t> &moduleFaults() noexcept {
  static std::atomic<std::uint64_t> count{};
  return count;
}

// Process-wide cache. Intentionally never destroyed: modules are not unloaded
// while the process lives, and unloading during static destruction (DllMain on
// Windows) is unsafe.
struct Cache {
  std::mutex mutex;
  std::map<std::string, std::shared_ptr<NativeModule>> loaded;
};
Cache &cache() {
  static auto *instance = new Cache;
  return *instance;
}

// ------------------------------------------------------------- instance
class Instance final : public InstrumentInstance {
public:
  Instance(std::shared_ptr<NativeModule> module, const InstrumentDescriptor &descriptor,
           VoxInstance handle)
      : module_(std::move(module)), table_(&module_->table()), descriptor_(descriptor),
        handle_(handle), values_(descriptor.parameters.size()),
        sent_(descriptor.parameters.size()), changes_(descriptor.parameters.size()) {
    for (std::size_t i = 0; i < values_.size(); ++i)
      values_[i] = sent_[i] = descriptor.parameters[i].defaultValue;
  }
  ~Instance() override { (void)table_->destroy(handle_); }

  bool prepare(const PrepareSpec &spec) override {
    const auto midiCapacity = std::max<std::uint32_t>(16, descriptor_.budget.maxMidiEventsPerBlock);
    const VoxPrepareSpecV2 v2{sizeof(VoxPrepareSpecV2),
                              VOX_MODULE_ABI_V2,
                              spec.sampleRate,
                              spec.maximumBlockSize,
                              spec.outputChannels,
                              static_cast<std::uint32_t>(values_.size()),
                              midiCapacity};
    prepared_ = false;
    if (table_->prepare(handle_, &v2) != VOX_OK)
      return false;
    midi_.assign(midiCapacity, VoxMidiEventV2{});
    sampleRate_ = spec.sampleRate;
    maximumBlock_ = spec.maximumBlockSize;
    outputChannels_ = spec.outputChannels;
    if (!values_.empty() &&
        table_->setAllParameters(handle_, values_.data(),
                                 static_cast<std::uint32_t>(values_.size())) != VOX_OK)
      return false;
    sent_ = values_;
    samplePosition_ = 0;
    discontinuity_ = true;
    // Controller-side recovery point: only a fully successful reprepare
    // (prepare + parameter snapshot) clears a latched fault.
    faulted_.store(false, std::memory_order_relaxed);
    prepared_ = true;
    return true;
  }

  void reset() noexcept override {
    discontinuity_ = true;
    // A faulted or unprepared module is quarantined: not re-entered.
    if (!prepared_ || faulted_.load(std::memory_order_relaxed))
      return;
    if (table_->reset(handle_) != VOX_OK)
      fault();
  }
  void suspend() noexcept override { suspended_ = true; }
  void resume() noexcept override {
    suspended_ = false;
    discontinuity_ = true;
  }
  void setBypassed(bool bypassed) noexcept override { bypassed_ = bypassed; }

  bool setParameter(std::string_view id, float plain) noexcept override {
    const auto &parameters = descriptor_.parameters;
    // The Rack sends parameters in descriptor order every block; try the next
    // index first so the common case is O(1) without allocation.
    std::size_t index = parameters.size();
    if (hint_ < parameters.size() && parameters[hint_].id == id)
      index = hint_;
    else
      for (std::size_t i = 0; i < parameters.size(); ++i)
        if (parameters[i].id == id) {
          index = i;
          break;
        }
    if (index == parameters.size() || !std::isfinite(plain))
      return false;
    const auto &p = parameters[index];
    if (plain < p.minimum || plain > p.maximum ||
        (p.type != ParameterType::floating && plain != std::floor(plain)))
      return false;
    values_[index] = plain;
    hint_ = index + 1;
    return true;
  }

  void process(const ProcessBlock &block) noexcept override {
    hint_ = 0;
    if (block.noteOutput != nullptr) // a silent or failed block generates nothing
      block.noteOutput->count = block.noteOutput->dropped = 0;
    if (!prepared_ || suspended_ || faulted_.load(std::memory_order_relaxed) ||
        block.sampleCount > maximumBlock_ ||
        block.outputs.size() != outputChannels_) {
      silence(block);
      return;
    }
    std::uint32_t changeCount = 0;
    for (std::size_t i = 0; i < values_.size(); ++i)
      if (values_[i] != sent_[i]) {
        changes_[changeCount++] = {static_cast<std::uint32_t>(i), 0, values_[i], 0};
        sent_[i] = values_[i];
      }
    const auto midiCount =
        static_cast<std::uint32_t>(std::min(block.midi.size(), midi_.size()));
    for (std::uint32_t i = 0; i < midiCount; ++i) {
      const auto &source = block.midi[i];
      VoxMidiEventV2 event{};
      event.sampleOffset = source.sampleOffset;
      event.size = std::min<std::uint32_t>(source.size, 3);
      std::memcpy(event.bytes, source.data, event.size);
      midi_[i] = event;
    }
    // More routed events than the module's prepared budget: the tail (which
    // may hold releases) cannot be delivered, so fail closed and end every
    // voice of this instance at the block end instead of risking a stuck note.
    const bool overflow = block.midi.size() > midi_.size();
    if (overflow)
      midiOverflows().fetch_add(1, std::memory_order_relaxed);
    fillTransport(block);
    const VoxProcessContextV2 context{sizeof(VoxProcessContextV2),
                                      VOX_MODULE_ABI_V2,
                                      block.sampleCount,
                                      outputChannels_,
                                      block.outputs.data(),
                                      changes_.data(),
                                      changeCount,
                                      sizeof(VoxParamChangeV2),
                                      midi_.data(),
                                      midiCount,
                                      sizeof(VoxMidiEventV2),
                                      overflow ? VOX_RELEASE_ALL_SOUND_AT_END : 0u,
                                      0,
                                      samplePosition_,
                                      &transport_,
                                      block.pattern,
                                      block.patternGeneration,
                                      block.noteOutput};
    const bool ok = table_->process(handle_, &context) == VOX_OK;
    if (!ok) {
      fault();
      silence(block);
    } else if (bypassed_) {
      silence(block);
    }
    if (auto *out = block.noteOutput) {
      // A failed or silenced block plays nothing, so it reports nothing.
      if (!ok || bypassed_) {
        out->count = out->dropped = 0;
      } else if (!validNoteOutput(*out, block.sampleCount, noteLanes_)) {
        out->count = 0;
        out->dropped = voidedNoteOutput;
        invalidNoteReports().fetch_add(1, std::memory_order_relaxed);
      }
    }
    samplePosition_ += block.sampleCount;
  }

  bool loadState(std::uint32_t schemaVersion, std::span<const std::byte> payload) noexcept override {
    if (payload.empty())
      return true; // Init: a freshly created module already holds defaults.
    if (table_->loadState(handle_, schemaVersion,
                          reinterpret_cast<const std::uint8_t *>(payload.data()),
                          static_cast<std::uint32_t>(payload.size())) != VOX_OK)
      return false;
    discontinuity_ = true;
    return true;
  }

  bool saveState(std::span<std::byte> destination,
                 std::uint32_t &bytesWritten) const noexcept override {
    std::uint32_t version = 0;
    bytesWritten = 0;
    return table_->saveState(handle_, reinterpret_cast<std::uint8_t *>(destination.data()),
                             static_cast<std::uint32_t>(destination.size()), &bytesWritten,
                             &version) == VOX_OK &&
           version == descriptor_.stateVersion;
  }

  std::uint32_t latencySamples() const noexcept override {
    return descriptor_.budget.maxLatencySamples;
  }
  std::uint32_t tailSamples() const noexcept override { return descriptor_.budget.maxTailSamples; }

private:
  // Latched until the next successful prepare; never calls the module again
  // in between. Lock- and allocation-free.
  void fault() noexcept {
    if (!faulted_.exchange(true, std::memory_order_relaxed))
      moduleFaults().fetch_add(1, std::memory_order_relaxed);
  }

  static void silence(const ProcessBlock &block) noexcept {
    for (auto *channel : block.outputs)
      std::fill_n(channel, block.sampleCount, 0.0f);
  }

  void fillTransport(const ProcessBlock &block) noexcept {
    std::uint32_t flags = 0;
    const bool tempo = std::isfinite(block.bpm) && block.bpm > 0.0;
    const bool ppq = std::isfinite(block.ppqPosition);
    if (tempo)
      flags |= VOX_TRANSPORT_HAS_TEMPO;
    if (ppq)
      flags |= VOX_TRANSPORT_HAS_PPQ;
    if (block.transportPlaying)
      flags |= VOX_TRANSPORT_PLAYING;
    bool jump = discontinuity_ || block.transportPlaying != wasPlaying_;
    if (!jump && block.transportPlaying && tempo && ppq && predictionValid_) {
      const double halfSample = 0.5 * block.bpm / (60.0 * sampleRate_);
      jump = std::abs(block.ppqPosition - predictedPpq_) > halfSample;
    }
    if (jump)
      flags |= VOX_TRANSPORT_DISCONTINUITY;
    transport_ = {sizeof(VoxTransportV2), VOX_MODULE_ABI_V2, flags, 0, 0, 0,
                  tempo ? block.bpm : 0.0, ppq ? block.ppqPosition : 0.0, 0.0, 0.0, 0.0, 0};
    predictionValid_ = tempo && ppq && block.transportPlaying;
    if (predictionValid_)
      predictedPpq_ = block.ppqPosition + block.sampleCount * block.bpm / (60.0 * sampleRate_);
    wasPlaying_ = block.transportPlaying;
    discontinuity_ = false;
  }

  std::shared_ptr<NativeModule> module_;
  const VoxModuleV2 *table_;
  const InstrumentDescriptor &descriptor_;
  VoxInstance handle_;
  std::vector<float> values_, sent_;
  std::vector<VoxParamChangeV2> changes_;
  std::vector<VoxMidiEventV2> midi_;
  VoxTransportV2 transport_{};
  double sampleRate_{};
  double predictedPpq_{};
  std::uint64_t samplePosition_{};
  std::atomic<bool> faulted_{};
  // Lanes a reported note may name; 0 = no pattern, so no notes.
  std::uint32_t noteLanes_{descriptor_.patternContent ? descriptor_.patternContent->caps.laneCount
                                                      : 0u};
  std::size_t hint_{};
  std::uint32_t maximumBlock_{};
  std::uint32_t outputChannels_{};
  bool prepared_{}, suspended_{}, bypassed_{}, discontinuity_{true};
  bool wasPlaying_{}, predictionValid_{};
};

class Provider final : public InstrumentProvider {
public:
  explicit Provider(std::shared_ptr<NativeModule> module) : module_(std::move(module)) {}
  std::span<const InstrumentDescriptor> descriptors() const noexcept override {
    return module_->descriptors();
  }
  std::unique_ptr<InstrumentInstance> create(std::string_view instrumentId,
                                             const CreateContext &context) override {
    for (const auto &descriptor : module_->descriptors())
      if (descriptor.id == instrumentId) {
        const auto slot =
            context.slotIndex < maxSlots ? context.slotIndex : VOX_SLOT_INDEX_NONE;
        const VoxCreateParamsV2 params{sizeof(VoxCreateParamsV2), VOX_MODULE_ABI_V2, slot, 0};
        VoxInstance handle{};
        if (module_->table().create(descriptor.id.c_str(), &params, &handle) != VOX_OK ||
            handle == nullptr)
          return {};
        return std::make_unique<Instance>(module_, descriptor, handle);
      }
    return {};
  }

private:
  std::shared_ptr<NativeModule> module_;
};
} // namespace

std::uint64_t totalMidiOverflows() noexcept {
  return midiOverflows().load(std::memory_order_relaxed);
}

std::uint64_t totalInvalidNoteReports() noexcept {
  return invalidNoteReports().load(std::memory_order_relaxed);
}

std::uint64_t totalModuleFaults() noexcept {
  return moduleFaults().load(std::memory_order_relaxed);
}

bool validModuleTable(const VoxModuleV2 *m) noexcept {
  return m != nullptr && aligned(m) && versioned(m->structSize, m->version, sizeof(VoxModuleV2)) &&
         m->instrumentCount >= 1 && m->instrumentCount <= maxInstrumentsPerModule &&
         m->getInstrument && m->create && m->destroy && m->prepare && m->setAllParameters &&
         m->reset && m->process && m->saveState && m->loadState && m->shutdown &&
         m->editor == nullptr &&
         (m->content == nullptr ||
          (aligned(m->content) &&
           versioned(m->content->structSize, m->content->version, sizeof(VoxContentApiV2)) &&
           m->content->getPatternCaps && m->content->getFactoryPattern));
}

bool importDescriptor(const VoxInstrumentDescriptorV2 *d, InstrumentDescriptor &out,
                      std::string &diagnostic) {
  diagnostic = "invalid native instrument descriptor";
  if (d == nullptr || !aligned(d) ||
      !versioned(d->structSize, d->version, sizeof(VoxInstrumentDescriptorV2)) ||
      !boundedString(d->id) || !boundedString(d->name) || !boundedString(d->vendor) ||
      !boundedString(d->category, true) || d->parameterCount > maxParametersPerInstrument ||
      (d->parameterCount > 0 && (d->parameters == nullptr || !aligned(d->parameters))) ||
      d->parameterStride < sizeof(VoxParameterDescriptorV2) ||
      d->parameterStride > maxRecordStride ||
      d->parameterStride % alignof(VoxParameterDescriptorV2) != 0 || d->maxVoices == 0 ||
      d->maxMidiEventsPerBlock < 16 || d->maxMidiEventsPerBlock > 2048 ||
      d->outputChannels == 0 || d->outputChannels > 2 || d->instrumentVersion == 0 ||
      d->contentVersion == 0 || d->stateVersion == 0)
    return false;
  InstrumentDescriptor copy;
  copy.id = d->id;
  copy.providerId = providerId;
  copy.name = d->name;
  copy.vendor = d->vendor;
  copy.instrumentVersion = d->instrumentVersion;
  copy.contentVersion = d->contentVersion;
  copy.stateVersion = d->stateVersion;
  copy.capabilities = Capability::notes | Capability::standardEditor;
  copy.budget.maxVoices = d->maxVoices;
  copy.budget.maxMidiEventsPerBlock = d->maxMidiEventsPerBlock;
  copy.budget.maxStateBytes = d->maxStateBytes;
  copy.budget.maxLatencySamples = d->latencySamples;
  copy.budget.maxTailSamples = d->tailSamples;
  copy.directParameters = d->parameterCount > 0;
  std::set<std::string> ids;
  std::set<std::int16_t> pools;
  const auto *bytes = reinterpret_cast<const std::uint8_t *>(d->parameters);
  for (std::uint32_t i = 0; i < d->parameterCount; ++i) {
    const auto &p = *reinterpret_cast<const VoxParameterDescriptorV2 *>(
        bytes + static_cast<std::size_t>(i) * d->parameterStride);
    ParameterDescriptor parameter;
    if (!importParameter(p, d->parameterStride, parameter)) {
      diagnostic = "invalid native parameter descriptor";
      return false;
    }
    if (!ids.insert(parameter.id).second || !pools.insert(parameter.poolIndex).second) {
      diagnostic = "duplicate native parameter id or pool index";
      return false;
    }
    copy.parameters.push_back(std::move(parameter));
  }
  out = std::move(copy);
  diagnostic.clear();
  return true;
}

bool importContent(const VoxContentApiV2 *content, InstrumentDescriptor &descriptor,
                   std::string &diagnostic) {
  descriptor.patternContent.reset();
  if (content == nullptr)
    return true;
  const VoxPatternCapsV2 *raw = nullptr;
  if (content->getPatternCaps(descriptor.id.c_str(), &raw) != VOX_OK) {
    diagnostic = "native pattern caps query failed";
    return false;
  }
  if (raw == nullptr)
    return true; // this instrument has no pattern
  auto imported = std::make_shared<pattern::Content>();
  std::string why;
  if (!aligned(raw) || !pattern::importCaps(raw, imported->caps, why)) {
    diagnostic = "invalid native pattern caps: " + why;
    return false;
  }
  auto buffer = std::make_unique<pattern::Pattern>();
  for (std::uint32_t i = 0; i < raw->factoryPatternCount; ++i) {
    buffer->cells.fill(pattern::defaultStep());
    auto view = buffer->view();
    view.laneCount = view.stepsPerBar = view.barCount = 0;
    const char *name = nullptr;
    auto stored = std::make_shared<pattern::Pattern>();
    if (content->getFactoryPattern(descriptor.id.c_str(), i, &name, &view) != VOX_OK ||
        !boundedString(name) || view.cells != buffer->cells.data() ||
        view.cellStride != sizeof(VoxPatternStepV2) ||
        view.cellCapacity != buffer->cells.size() ||
        !pattern::fromView(view, imported->caps, *stored, why)) {
      diagnostic = "invalid native factory pattern " + std::to_string(i) +
                   (why.empty() ? std::string{} : ": " + why);
      return false;
    }
    for (const auto &existing : imported->factory)
      if (existing.name == name) {
        diagnostic = "duplicate native factory pattern name";
        return false;
      }
    imported->factory.push_back({name, std::move(stored)});
  }
  descriptor.patternContent = std::move(imported);
  return true;
}

NativeModule::~NativeModule() = default; // never reached: modules live until exit

std::shared_ptr<NativeModule> NativeModule::acquire(const std::filesystem::path &path,
                                                    std::string &diagnostic) {
  if (!path.is_absolute()) {
    diagnostic = "native module path must be absolute: " + path.string();
    return {};
  }
  std::error_code error;
  const auto canonical = std::filesystem::weakly_canonical(path, error);
  if (error || !std::filesystem::is_regular_file(canonical, error)) {
    diagnostic = "native module not found: " + path.string();
    return {};
  }
  auto &c = cache();
  const std::lock_guard lock(c.mutex);
  if (const auto found = c.loaded.find(canonical.string()); found != c.loaded.end())
    return found->second; // entered once per process
  void *library = openLibrary(canonical);
  if (library == nullptr) {
    diagnostic = "native module failed to load: " + canonical.string();
    return {};
  }
  const auto entry = entryPoint(library);
  const VoxModuleV2 *table = nullptr;
  if (entry == nullptr || entry(VOX_MODULE_ABI_V2, &hostServices, &table) != VOX_OK) {
    closeLibrary(library);
    diagnostic = "native module entry failed: " + canonical.string();
    return {};
  }
  std::shared_ptr<NativeModule> module(new NativeModule);
  bool valid = validModuleTable(table);
  if (valid) {
    module->table_ = table;
    for (std::uint32_t i = 0; i < table->instrumentCount && valid; ++i) {
      const VoxInstrumentDescriptorV2 *raw = nullptr;
      InstrumentDescriptor descriptor;
      valid = table->getInstrument(i, &raw) == VOX_OK &&
              importDescriptor(raw, descriptor, diagnostic) &&
              importContent(table->content, descriptor, diagnostic);
      if (valid)
        module->descriptors_.push_back(std::move(descriptor));
    }
  } else {
    diagnostic = "invalid native module table";
  }
  if (!valid) {
    if (table != nullptr && table->shutdown != nullptr)
      (void)table->shutdown();
    closeLibrary(library);
    diagnostic += ": " + canonical.string();
    return {};
  }
  module->library_ = library;
  module->path_ = canonical;
  c.loaded.emplace(canonical.string(), module);
  return module;
}

std::unique_ptr<InstrumentProvider> makeProvider(std::shared_ptr<NativeModule> module) {
  return std::make_unique<Provider>(std::move(module));
}

LoadReport loadConfiguredModules(std::string_view pathList) {
  LoadReport report;
  while (!pathList.empty()) {
    const auto split = pathList.find(';');
    auto entry = pathList.substr(0, split);
    pathList = split == std::string_view::npos ? std::string_view{} : pathList.substr(split + 1);
    while (!entry.empty() && (entry.front() == ' ' || entry.front() == '\t'))
      entry.remove_prefix(1);
    while (!entry.empty() && (entry.back() == ' ' || entry.back() == '\t'))
      entry.remove_suffix(1);
    if (entry.empty())
      continue;
    std::string diagnostic;
    if (auto module = NativeModule::acquire(std::filesystem::path(std::string(entry)), diagnostic))
      report.providers.push_back(makeProvider(std::move(module)));
    else
      report.diagnostics.push_back(std::move(diagnostic));
  }
  return report;
}

} // namespace vstengine::instrument::native
