#include "rack/Rack.h"
#include "rack/AudioRouting.h"
#include "rack/RackState.h"
#include <algorithm>
#include <cmath>
#include <thread>
#include <type_traits>

namespace vstengine::rack {
namespace {
constexpr double pi = 3.14159265358979323846;

struct ActiveProcess final {
    explicit ActiveProcess(std::atomic<std::uint32_t>& counter) noexcept
        : counter_(counter) { counter_.fetch_add(1, std::memory_order_seq_cst); }
    ~ActiveProcess() { counter_.fetch_sub(1, std::memory_order_seq_cst); }
    ActiveProcess(const ActiveProcess&) = delete;
    ActiveProcess& operator=(const ActiveProcess&) = delete;
private:
    std::atomic<std::uint32_t>& counter_;
};

std::uint32_t nextRandom(std::uint32_t& state) noexcept
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

float randomUnit(std::uint32_t& state) noexcept
{
    return static_cast<float>(nextRandom(state) >> 8)
        * (1.0f / 16777215.0f);
}

float macroToPlain(float normalized,
                   const instrument::ParameterDescriptor& parameter) noexcept
{
    normalized = std::clamp(normalized, 0.0f, 1.0f);
    auto plain = parameter.minimum
        + normalized * (parameter.maximum - parameter.minimum);
    if (parameter.step > 0.0f)
        plain = parameter.minimum + std::round(
            (plain - parameter.minimum) / parameter.step) * parameter.step;
    return std::clamp(plain, parameter.minimum, parameter.maximum);
}
const instrument::ParameterDescriptor* findParameter(
    const instrument::InstrumentDescriptor& descriptor,
    std::string_view id) noexcept
{
    for (const auto& parameter : descriptor.parameters)
        if (parameter.id == id) return &parameter;
    return nullptr;
}
// Pattern content of an instrument whose pattern the Rack owns and the module
// plays itself; null for HOST-playback or pattern-less instruments.
const pattern::Content *internalPattern(
    const instrument::InstrumentDescriptor &descriptor) noexcept {
  const auto *content = descriptor.patternContent.get();
  return content != nullptr &&
                 content->caps.playback == VOX_PATTERN_PLAYBACK_INTERNAL
             ? content
             : nullptr;
}
// Init pattern of a freshly loaded INTERNAL instrument: its first factory
// pattern, or an all-rest pattern if it ships none.
void initializePattern(PersistentSlotState &state, const pattern::Content &content) {
  const auto init = std::make_unique<pattern::Pattern>(
      content.factory.empty() ? pattern::emptyPattern(content.caps)
                              : *content.factory.front().pattern);
  state.patternSchemaVersion = VOX_PATTERN_SCHEMA_V2;
  pattern::encode(*init, state.patternPayload);
  state.patternPreset = content.factory.empty() ? std::string{}
                                                : content.factory.front().name;
}
void initializeDirect(PersistentSlotState &state,
                      const instrument::InstrumentDescriptor &descriptor) {
  // Never canonicalize a future/unresolved module with today's descriptor.
  // Init is distinguished by all default version markers and empty payload.
  if ((state.instrumentVersion != 0 &&
       state.instrumentVersion != descriptor.instrumentVersion) ||
      (state.contentVersion != 0 &&
       state.contentVersion != descriptor.contentVersion) ||
      state.instrumentStateVersion > descriptor.stateVersion ||
      state.patternSchemaVersion > VOX_PATTERN_SCHEMA_V2)
    return;
  if (state.instrumentVersion == 0 && state.modulePayload.empty())
    state.instrumentStateVersion = descriptor.stateVersion;
  if (const auto *content = internalPattern(descriptor);
      content != nullptr && state.instrumentVersion == 0 && state.patternPayload.empty())
    initializePattern(state, *content);
  if (descriptor.directParameters && state.directParameters.empty() &&
      state.modulePayload.empty())
    for (const auto &parameter : descriptor.parameters)
      state.directParameters.push_back(
          {parameter.id, parameter.defaultValue, parameter.poolIndex});
}
void canonicalizeResolvedDirect(
    PersistentSlotState &state,
    const instrument::InstrumentDescriptor &descriptor) {
  if (descriptor.directParameters)
    for (auto &value : state.directParameters)
      if (const auto *parameter = findParameter(descriptor, value.id)) {
        // A resolved slot records the endpoint and meaning of each value.
        value.poolIndex = parameter->poolIndex;
        value.spec = instrument::hostparams::parameterSpecHash(*parameter);
        if (std::isfinite(value.value) && value.value >= parameter->minimum &&
            value.value <= parameter->maximum)
          value.value =
              instrument::hostparams::canonicalPlain(value.value, *parameter);
      }
}
} // namespace

std::array<Rack::SlotSummary, instrument::maxSlots> Rack::summaries() const {
  const std::scoped_lock lock(persistentStateMutex_);
  std::array<SlotSummary, instrument::maxSlots> result;
  for (std::size_t i = 0; i < result.size(); ++i) {
    const auto *d = processing_->runtime[i].descriptor;
    result[i] = {state_[i].slotId,
                 state_[i].instrumentId,
                 d ? d->name : state_[i].instrumentId,
                 d ? d->vendor : std::string{},
                 state_[i].instrumentVersion,
                 runtimeState_[i].resolution};
  }
  return result;
}

Rack::Rack(instrument::InstrumentRegistry& registry) : registry_(registry)
{
    for (std::size_t i = 0; i < state_.size(); ++i)
        state_[i].slotId = instrument::initialSlotId(i);
}

void Rack::beginExclusive() const noexcept
{
    mutationGate_.store(true, std::memory_order_seq_cst);
    while (activeProcessors_.load(std::memory_order_seq_cst) != 0)
        std::this_thread::yield();
}

void Rack::endExclusive() const noexcept
{
    mutationGate_.store(false, std::memory_order_seq_cst);
}

bool Rack::prepare(const instrument::PrepareSpec& spec)
{
    if (!std::isfinite(spec.sampleRate) || spec.sampleRate <= 0.0
        || spec.maximumBlockSize == 0
        || spec.outputChannels == 0 || spec.outputChannels > 2)
        return false;
    const std::scoped_lock persistentLock(persistentStateMutex_);
    std::array<std::array<std::vector<float>, 2>, instrument::maxSlots> audio;
    try {
        for (auto& slot : audio)
            for (std::size_t channel = 0; channel < spec.outputChannels; ++channel)
                slot[channel].assign(spec.maximumBlockSize, 0.0f);
    } catch (...) {
        return false; // No live DSP/spec/buffers were touched.
    }
    beginExclusive();
    // A provider can mutate itself before returning false/throwing. Once that
    // starts, rollback is not guaranteed: leave the rack non-processing until
    // every provider successfully prepares on a later retry.
    spec_ = {};
    bool prepared = true;
    try {
      for (auto &slot : processing_->runtime)
        if (slot.instance && !slot.instance->prepare(spec)) {
          prepared = false;
          break;
        }
    } catch (...) {
        prepared = false;
    }
    if (prepared) {
      for (std::size_t index = 0; index < processing_->runtime.size();
           ++index) {
        processing_->runtime[index].audio = std::move(audio[index]);
        for (std::size_t channel = 0; channel < 2; ++channel)
          processing_->runtime[index].pointers[channel] =
              channel < spec.outputChannels
                  ? processing_->runtime[index].audio[channel].data()
                  : nullptr;
      }
        spec_ = spec;
    }
    reset();
    endExclusive();
    return prepared;
}

void Rack::reset() noexcept
{
  processing_->router.reset();
  for (auto &destination : processing_->routed) {
    destination.size = 0;
    destination.dropped = 0;
  }
    for (auto& dropped : droppedMidiEvents_)
        dropped.store(0, std::memory_order_relaxed);
    for (auto &slot : processing_->runtime) {
      if (slot.instance)
        slot.instance->reset();
      if (slot.modulation) {
        slot.modulation->sources = {};
        slot.modulation->lfoPhase = {};
        slot.modulation->randomState = slot.modulation->initialRandomState;
        slot.modulation->sampleHoldStep = -1;
      }
    }
    // ownedNotes is reserved (always zero); reset must not mutate UI metadata.
}

bool Rack::buildSlot(std::size_t index, const PersistentSlotState& state,
                     instrument::ResourceResolver* resources, SlotRuntime& target,
                     RuntimeSlotState& runtimeState, std::string& diagnostic)
{
    if (index >= state_.size()) {
        diagnostic = "invalid rack slot state";
        return false;
    }
    if (!vstengine::rack::state::validateSlot(state, diagnostic)) return false;
    if (state.instrumentId.empty()) {
        runtimeState.resolution = instrument::ResolutionStatus::missingModule;
        return true;
    }
    if (state.patternSchemaVersion > VOX_PATTERN_SCHEMA_V2) {
        runtimeState.resolution = instrument::ResolutionStatus::incompatibleSchema;
        diagnostic = "instrument pattern schema is newer than host";
        return true;
    }
    // The same InstrumentId from another signer is another instrument: the
    // slot stays unresolved (its state preserved) instead of loading into it.
    if (const auto resolution = registry_.resolve(state.instrumentId);
        resolution && state.instrumentVersion != 0 &&
        state.publisherId != resolution.descriptor->publisherId) {
        runtimeState.resolution = instrument::ResolutionStatus::incompatibleSchema;
        diagnostic = "instrument publisher differs from the saved project";
        return true;
    }
    instrument::PersistentModuleState moduleState {
        state.instrumentStateVersion, state.slotId, state.instrumentId,
        state.resolvedProviderId, state.instrumentVersion, state.contentVersion,
        state.modulePayload
    };
    auto prepared = instrument::prepareModuleState(
        moduleState, registry_, resources, static_cast<std::uint32_t>(index));
    runtimeState.resolution = prepared.resolution.status;
    target.descriptor = prepared.resolution.descriptor;
    target.instance = std::move(prepared.instance);
    if (!target.instance) {
        diagnostic = prepared.resolution.diagnostic;
        return prepared.resolution.status !=
               instrument::ResolutionStatus::constructionFailed;
        // Missing/incompatible identity remains a valid unresolved slot.
    }
    // The Rack is the only durable owner of an INTERNAL pattern: it must be a
    // valid V2 pattern for exactly these caps, or the slot is rejected.
    if (const auto *content = internalPattern(*target.descriptor)) {
      auto decoded = std::make_unique<pattern::Pattern>();
      std::string why;
      if (state.patternSchemaVersion != VOX_PATTERN_SCHEMA_V2 ||
          !pattern::decode(state.patternPayload, content->caps, *decoded, why)) {
        diagnostic = "instrument pattern invalid" + (why.empty() ? std::string{} : ": " + why);
        return false;
      }
      target.patterns = std::make_unique<pattern::SnapshotBuffer>();
      target.patterns->publish(*decoded);
      target.noteEvents.assign(noteEventCapacity, VoxNoteEventV2{});
      target.noteOutput = {sizeof(VoxNoteOutputV2), VOX_MODULE_ABI_V2,
                           static_cast<std::uint32_t>(noteEventCapacity),
                           sizeof(VoxNoteEventV2), 0, 0, target.noteEvents.data()};
    } else if (state.patternSchemaVersion == VOX_PATTERN_SCHEMA_V2) {
      diagnostic = "instrument does not own a V2 pattern";
      return false;
    }
    if (target.descriptor->directParameters) {
      if (state.directParameters.size() !=
              target.descriptor->parameters.size() ||
          std::any_of(state.macroAssignments.begin(),
                      state.macroAssignments.end(),
                      [](const auto &id) { return !id.empty(); })) {
        diagnostic = "direct instrument requires exact named base parameters "
                     "and no macro mapping";
        return false;
      }
      for (const auto &value : state.directParameters) {
        const auto *parameter = findParameter(*target.descriptor, value.id);
        if (parameter == nullptr || !std::isfinite(value.value) ||
            value.value < parameter->minimum ||
            value.value > parameter->maximum) {
          diagnostic = "direct parameter identity or range invalid";
          return false;
        }
        // A recorded endpoint must still match: a moved pool index without a
        // version bump would silently remap automation. Unknown (-1) is the
        // documented legacy match-by-ID policy.
        if (value.poolIndex >= 0 && value.poolIndex != parameter->poolIndex) {
          diagnostic = "direct parameter pool index changed without a new "
                       "instrument version";
          return false;
        }
        if (value.spec != 0 &&
            value.spec != instrument::hostparams::parameterSpecHash(*parameter)) {
          diagnostic = "direct parameter meaning (type, range, step, unit or "
                       "choices) changed without a new instrument version";
          return false;
        }
        target.direct[target.directCount++] = {
            instrument::hostparams::directKey(state.instrumentId, value.id),
            parameter,
            instrument::hostparams::canonicalPlain(value.value, *parameter)};
      }
    } else if (!state.directParameters.empty()) {
      diagnostic = "instrument does not accept direct parameters";
      return false;
    }
    // Mapping identity persists independently from descriptor order. Populate
    // only previously-unassigned macros from descriptor preferences.
    if (spec_.sampleRate > 0.0 && !target.instance->prepare(spec_)) {
        target.instance.reset();
        target.descriptor = nullptr;
        runtimeState.resolution = instrument::ResolutionStatus::constructionFailed;
        diagnostic = "instrument prepare failed";
        return false;
    }
    for (std::size_t channel = 0; channel < spec_.outputChannels; ++channel) {
        target.audio[channel].assign(spec_.maximumBlockSize, 0.0f);
        target.pointers[channel] = target.audio[channel].data();
    }
    for (std::size_t macro = 0; macro < state.macros.size(); ++macro) {
        std::string_view parameterId = state.macroAssignments[macro];
        if (parameterId.empty())
            for (const auto& parameter : target.descriptor->parameters)
                if (parameter.preferredMacro == static_cast<std::int8_t>(macro)) {
                    parameterId = parameter.id;
                    break;
                }
        if (!parameterId.empty())
            if (const auto* parameter = findParameter(*target.descriptor, parameterId))
                target.instance->setParameter(parameterId,
                    macroToPlain(state.macros[macro], *parameter));
    }
    for (std::size_t i = 0; i < target.directCount; ++i)
      if (!target.instance->setParameter(target.direct[i].parameter->id,
                                         target.direct[i].base)) {
        diagnostic = "instrument rejected validated direct parameter";
        return false;
      }
    if ((target.descriptor->capabilities
            & instrument::Capability::modulation) != 0) {
        target.modulation = std::make_unique<ModulationRuntime>();
        auto& runtime = *target.modulation;
        if (runtime.registry.add(*target.descriptor)
                != modulation::RegistryStatus::ok
            || runtime.matrix.prepare(runtime.registry,
                target.descriptor->budget.maxModulationRoutes)
                != modulation::MatrixStatus::ok) {
            diagnostic = "instrument modulation descriptor invalid";
            return false;
        }
        runtime.randomState ^= static_cast<std::uint32_t>(state.slotId)
            ^ static_cast<std::uint32_t>(state.slotId >> 32u);
        runtime.initialRandomState = runtime.randomState;
        if (!state.modulationPayload.empty()) {
            modulation::PersistentState persistent;
            if (modulation::deserializeState(state.modulationPayload, persistent)
                    != modulation::StateStatus::ok
                || modulation::restoreState(persistent, runtime.registry,
                    target.descriptor->budget.maxModulationRoutes,
                    runtime.matrix) != modulation::StateStatus::ok) {
                diagnostic = "instrument modulation state invalid";
                return false;
            }
        }
    } else if (!state.modulationPayload.empty()) {
        diagnostic = "instrument does not support modulation";
        return false;
    }
    return true;
}

bool Rack::replaceState(
    const std::array<PersistentSlotState, instrument::maxSlots> &input,
    instrument::ResourceResolver *resources, std::string &diagnostic) try {
  const std::scoped_lock persistentLock(persistentStateMutex_);
  auto replacement = input;
  for (auto &slot : replacement)
    if (const auto resolution = registry_.resolve(slot.instrumentId))
      initializeDirect(slot, *resolution.descriptor);
  if (!state::validate(replacement, diagnostic))
    return false;
  std::array<SlotRuntime, instrument::maxSlots> preparedRuntime;
  std::array<RuntimeSlotState, instrument::maxSlots> preparedState;
  for (std::size_t i = 0; i < replacement.size(); ++i)
    if (!buildSlot(i, replacement[i], resources, preparedRuntime[i],
                   preparedState[i], diagnostic))
      return false;
  for (std::size_t i = 0; i < replacement.size(); ++i)
    if (const auto *descriptor = preparedRuntime[i].descriptor) {
      canonicalizeResolvedDirect(replacement[i], *descriptor);
      replacement[i].resolvedProviderId = descriptor->providerId;
      replacement[i].publisherId = descriptor->publisherId;
      replacement[i].instrumentVersion = descriptor->instrumentVersion;
      replacement[i].instrumentStateVersion = descriptor->stateVersion;
      replacement[i].contentVersion = descriptor->contentVersion;
      for (const auto &parameter : descriptor->parameters)
        if (parameter.preferredMacro >= 0 &&
            parameter.preferredMacro <
                static_cast<std::int8_t>(instrument::macrosPerSlot) &&
            replacement[i]
                .macroAssignments[static_cast<std::size_t>(
                    parameter.preferredMacro)]
                .empty())
          replacement[i].macroAssignments[static_cast<std::size_t>(
              parameter.preferredMacro)] = parameter.id;
    }
  // All allocating metadata work is complete before publishing the runtime.
  static_assert(std::is_nothrow_move_assignable_v<decltype(state_)>);
  static_assert(
      std::is_nothrow_move_assignable_v<decltype(processing_->runtime)>);
  beginExclusive();
  state_ = std::move(replacement);
  processing_->runtime = std::move(preparedRuntime);
  runtimeState_ = preparedState;
  for (std::size_t i = 0; i < processing_->runtime.size(); ++i)
    publishedDescriptors_[i].store(processing_->runtime[i].descriptor,
                                   std::memory_order_release);
  processing_->router.reset();
  for (auto &destination : processing_->routed) {
    destination.size = 0;
    destination.dropped = 0;
  }
  for (auto &dropped : droppedMidiEvents_)
    dropped.store(0, std::memory_order_relaxed);
  endExclusive();
  return true;
} catch (...) {
  diagnostic = "rack state preparation failed";
  return false;
}

bool Rack::loadModule(std::size_t index, std::string_view instrumentId,
                      instrument::ResourceResolver* resources,
                      std::string& diagnostic)
{
    const std::scoped_lock persistentLock(persistentStateMutex_);
    if (index >= state_.size()) { diagnostic = "slot index out of range"; return false; }
    auto replacement = state_[index];
    replacement.instrumentId = instrumentId;
    replacement.modulePayload.clear();
    replacement.directParameters.clear();
    replacement.patternSchemaVersion = VOX_PATTERN_SCHEMA_V1;
    replacement.patternPayload.clear();
    replacement.modulationPayload.clear();
    replacement.resolvedProviderId.clear();
    replacement.publisherId.clear();
    replacement.instrumentVersion = 0;
    replacement.instrumentStateVersion = instrument::stateSchemaVersion;
    replacement.contentVersion = 0;
    replacement.soundPreset.clear();
    replacement.patternPreset.clear();
    replacement.macroAssignments.fill({});
    replacement.macros.fill(0.0f);
    const auto resolution = registry_.resolve(instrumentId);
    if (resolution)
      initializeDirect(replacement, *resolution.descriptor);
    if (resolution && resolution.descriptor)
        for (const auto& parameter : resolution.descriptor->parameters)
            if (parameter.preferredMacro >= 0
                && parameter.preferredMacro < static_cast<std::int8_t>(
                    instrument::macrosPerSlot)) {
                const auto macro = static_cast<std::size_t>(parameter.preferredMacro);
                replacement.macroAssignments[macro] = parameter.id;
                const auto range = parameter.maximum - parameter.minimum;
                replacement.macros[macro] = range > 0.0f
                    ? std::clamp((parameter.defaultValue - parameter.minimum) / range,
                                 0.0f, 1.0f)
                    : 0.0f;
            }
    SlotRuntime prepared;
    RuntimeSlotState preparedState;
    if (!buildSlot(index, replacement, resources, prepared, preparedState,
                   diagnostic)) return false;
    if (prepared.descriptor) {
      canonicalizeResolvedDirect(replacement, *prepared.descriptor);
      replacement.resolvedProviderId = prepared.descriptor->providerId;
      replacement.publisherId = prepared.descriptor->publisherId;
      replacement.instrumentVersion = prepared.descriptor->instrumentVersion;
      replacement.instrumentStateVersion = prepared.descriptor->stateVersion;
      replacement.contentVersion = prepared.descriptor->contentVersion;
      for (const auto &parameter : prepared.descriptor->parameters)
        if (parameter.preferredMacro >= 0 &&
            parameter.preferredMacro <
                static_cast<std::int8_t>(instrument::macrosPerSlot))
          replacement.macroAssignments[static_cast<std::size_t>(
              parameter.preferredMacro)] = parameter.id;
    }
    beginExclusive();
    if (processing_->runtime[index].instance)
      processing_->runtime[index].instance->reset();
    processing_->router.forgetSlot(state_[index].slotId);
    processing_->routed[index].size = 0;
    processing_->routed[index].dropped = 0;
    droppedMidiEvents_[index].store(0, std::memory_order_relaxed);
    state_[index] = std::move(replacement);
    processing_->runtime[index] = std::move(prepared);
    publishedDescriptors_[index].store(processing_->runtime[index].descriptor,
                                       std::memory_order_release);
    runtimeState_[index] = preparedState;
    endExclusive();
    return true;
}

bool Rack::applySoundPreset(std::size_t index, std::string_view presetId,
                            instrument::ResourceResolver* resources,
                            std::string& diagnostic)
{
    const std::scoped_lock persistentLock(persistentStateMutex_);
    if (index >= state_.size()) {
        diagnostic = "slot index out of range";
        return false;
    }
    auto replacement = state_[index];
    const auto resolution = registry_.resolve(replacement.instrumentId);
    if (!resolution || resolution.provider == nullptr) {
        diagnostic = resolution.diagnostic;
        return false;
    }
    const instrument::ContentDescriptor* content = nullptr;
    for (const auto& candidate : resolution.provider->contentDescriptors(
             replacement.instrumentId))
        if (candidate.kind == instrument::ContentKind::soundPreset
            && candidate.id == presetId) {
            content = &candidate;
            break;
        }
    if (content == nullptr) {
        diagnostic = "sound preset not found";
        return false;
    }
    if (resolution.descriptor->directParameters) {
      if (content->macroValueMask != 0 ||
          content->parameterValues.size() !=
              resolution.descriptor->parameters.size()) {
        diagnostic = "direct preset requires complete named base values and no "
                     "macro values";
        return false;
      }
      replacement.directParameters.clear();
      for (const auto &value : content->parameterValues)
        replacement.directParameters.push_back({value.id, value.value});
      if (content->nativeState) {
        if (content->nativeStateVersion !=
                resolution.descriptor->stateVersion ||
            content->nativeState->size() >
                resolution.descriptor->budget.maxStateBytes ||
            content->nativeState->size() > state::maxModulePayloadBytes) {
          diagnostic = "native preset state version or resource budget invalid";
          return false;
        }
        replacement.modulePayload = *content->nativeState;
        replacement.instrumentStateVersion = content->nativeStateVersion;
      }
      initializeDirect(replacement, *resolution.descriptor);
    }
    SlotRuntime prepared;
    RuntimeSlotState preparedState;
    if (!buildSlot(index, replacement, resources, prepared, preparedState,
                   diagnostic)
        || !prepared.instance)
        return false;
    const auto status =
        resolution.descriptor->directParameters
            ? instrument::ContentStatus::ok
            : resolution.provider->applySoundPreset(
                  replacement.instrumentId, presetId, *prepared.instance);
    if (status != instrument::ContentStatus::ok) {
        diagnostic = "sound preset incompatible";
        return false;
    }
    replacement.modulePayload.resize(resolution.descriptor->budget.maxStateBytes);
    std::uint32_t written {};
    if (!prepared.instance->saveState(replacement.modulePayload, written)
        || written > replacement.modulePayload.size()) {
        diagnostic = "sound preset state exceeds budget";
        return false;
    }
    replacement.modulePayload.resize(written);
    replacement.soundPreset = std::string(presetId);
    for (std::size_t macro = 0; macro < replacement.macros.size(); ++macro)
        if ((content->macroValueMask & (1u << macro)) != 0)
            replacement.macros[macro] = std::clamp(
                content->macroValues[macro], 0.0f, 1.0f);

    if (prepared.descriptor)
      canonicalizeResolvedDirect(replacement, *prepared.descriptor);
    beginExclusive();
    if (processing_->runtime[index].instance)
      processing_->runtime[index].instance->reset();
    processing_->router.forgetSlot(replacement.slotId);
    state_[index] = std::move(replacement);
    processing_->runtime[index] = std::move(prepared);
    publishedDescriptors_[index].store(processing_->runtime[index].descriptor,
                                       std::memory_order_release);
    runtimeState_[index] = preparedState;
    processing_->routed[index].clear();
    droppedMidiEvents_[index].store(0, std::memory_order_relaxed);
    endExclusive();
    diagnostic.clear();
    return true;
}

bool Rack::updatePatternState(std::size_t index, std::string presetId,
                              std::uint32_t patternSchema,
                              std::vector<std::byte> payload)
{
    const std::scoped_lock persistentLock(persistentStateMutex_);
    if (index >= state_.size() || presetId.size() > 256 || patternSchema == 0
        || payload.size() > state::maxPatternPayloadBytes)
        return false;
    beginExclusive();
    state_[index].patternPreset = std::move(presetId);
    state_[index].patternSchemaVersion = patternSchema;
    state_[index].patternPayload = std::move(payload);
    endExclusive();
    return true;
}

bool Rack::setPattern(std::size_t index, const pattern::Pattern &edited,
                      std::string &diagnostic) {
  const std::scoped_lock persistentLock(persistentStateMutex_);
  if (index >= state_.size()) {
    diagnostic = "slot index out of range";
    return false;
  }
  auto &runtime = processing_->runtime[index];
  const auto *content = runtime.descriptor ? internalPattern(*runtime.descriptor) : nullptr;
  if (content == nullptr || !runtime.patterns) {
    diagnostic = "slot instrument has no Rack-owned pattern";
    return false;
  }
  if (!pattern::validate(edited, content->caps, diagnostic))
    return false;
  std::vector<std::byte> payload;
  pattern::encode(edited, payload);
  std::string preset;
  for (const auto &factory : content->factory)
    if (*factory.pattern == edited) {
      preset = factory.name;
      break;
    }
  // Durable copy first, then the snapshot, so the audio thread never plays a
  // pattern the project state does not hold. No exclusive section: the audio
  // thread never reads these fields, and an edit must not silence a block.
  state_[index].patternSchemaVersion = VOX_PATTERN_SCHEMA_V2;
  state_[index].patternPayload = std::move(payload);
  state_[index].patternPreset = std::move(preset);
  runtime.patterns->publish(edited);
  diagnostic.clear();
  return true;
}

bool Rack::loadFactoryPattern(std::size_t index, std::size_t factoryIndex,
                              std::string &diagnostic) {
  std::shared_ptr<const pattern::Pattern> chosen;
  {
    const std::scoped_lock persistentLock(persistentStateMutex_);
    const auto *descriptor =
        index < state_.size() ? processing_->runtime[index].descriptor : nullptr;
    const auto *content = descriptor ? internalPattern(*descriptor) : nullptr;
    if (content == nullptr || factoryIndex >= content->factory.size()) {
      diagnostic = "no such factory pattern for this slot";
      return false;
    }
    chosen = content->factory[factoryIndex].pattern;
  }
  return setPattern(index, *chosen, diagnostic);
}

bool Rack::pattern(std::size_t index, pattern::Pattern &out) const {
  const std::scoped_lock persistentLock(persistentStateMutex_);
  if (index >= state_.size() || !processing_->runtime[index].descriptor)
    return false;
  const auto *content = internalPattern(*processing_->runtime[index].descriptor);
  std::string why;
  return content != nullptr &&
         state_[index].patternSchemaVersion == VOX_PATTERN_SCHEMA_V2 &&
         pattern::decode(state_[index].patternPayload, content->caps, out, why);
}

bool Rack::renderPatternHits(std::size_t index, const PersistentSlotState &slot,
                             std::vector<RenderedHit> &hits, double &lengthPpq,
                             std::string &diagnostic) {
  hits.clear();
  lengthPpq = 0.0;
  if (index >= state_.size()) {
    diagnostic = "slot index out of range";
    return false;
  }
  const auto resolution = registry_.resolve(slot.instrumentId);
  const auto *content = resolution ? internalPattern(*resolution.descriptor) : nullptr;
  if (content == nullptr) {
    diagnostic = "slot instrument has no Rack-owned pattern";
    return false;
  }
  SlotRuntime render;
  RuntimeSlotState runtimeState;
  if (!buildSlot(index, slot, nullptr, render, runtimeState, diagnostic) || !render.instance ||
      !render.patterns)
    return false;
  // Tempo-independent result: 120 BPM at 48 kHz puts every 1/960-quarter tick
  // on a whole number of samples, so step and swing positions convert exactly.
  constexpr double rate = 48000.0, bpm = 120.0;
  constexpr std::uint32_t block = 512;
  if (!render.instance->prepare({rate, block, 2})) {
    diagnostic = "instrument prepare failed";
    return false;
  }
  std::uint64_t generation = 0;
  const auto *snapshot = render.patterns->acquire(generation);
  static constexpr double stepPpq[] = {0.25, 0.5, 0.125, 1.0 / 3.0};
  lengthPpq = snapshot->barCount * snapshot->stepsPerBar * stepPpq[snapshot->timing];
  const auto view = snapshot->view();
  std::array<std::vector<float>, 2> audio{std::vector<float>(block), std::vector<float>(block)};
  std::array<float *, 2> channels{audio[0].data(), audio[1].data()};
  const double samplesPerPpq = 60.0 * rate / bpm;
  const auto total = static_cast<std::uint64_t>(std::ceil(lengthPpq * samplesPerPpq));
  for (std::uint64_t start = 0; start < total; start += block) {
    const auto n = static_cast<std::uint32_t>(std::min<std::uint64_t>(block, total - start));
    const double ppq = static_cast<double>(start) / samplesPerPpq;
    render.instance->process({{channels.data(), channels.size()}, n, {}, rate, bpm, ppq, true,
                              &view, generation, &render.noteOutput});
    for (std::uint32_t e = 0; e < render.noteOutput.count; ++e) {
      const auto &event = render.noteEvents[e];
      if (event.sampleOffset >= n)
        continue;
      hits.push_back({ppq + event.sampleOffset / samplesPerPpq, event.note, event.velocity,
                      event.lane, (event.flags & VOX_NOTE_EVENT_ACCENT) != 0});
    }
    // Fail closed: a block whose notes overflowed or were voided as malformed
    // (NativeModule validation) would export a wrong file.
    if (render.noteOutput.dropped != 0) {
      diagnostic = "instrument note output overflowed or was malformed; nothing exported";
      hits.clear();
      return false;
    }
  }
  diagnostic.clear();
  return true;
}

bool Rack::configureModulation(
    std::size_t index, std::span<const modulation::Route> routes,
    std::string& diagnostic)
{
    const std::scoped_lock persistentLock(persistentStateMutex_);
    if (index >= processing_->runtime.size() ||
        !processing_->runtime[index].modulation ||
        processing_->runtime[index].descriptor == nullptr) {
      diagnostic = "slot has no modulation-capable instrument";
      return false;
    }
    auto &modulationRuntime = *processing_->runtime[index].modulation;
    modulation::ModulationMatrix candidate;
    if (candidate.prepare(modulationRuntime.registry,
                          processing_->runtime[index]
                              .descriptor->budget.maxModulationRoutes) !=
        modulation::MatrixStatus::ok) {
      diagnostic = "slot modulation budget invalid";
      return false;
    }
    for (const auto& route : routes)
        if (candidate.addRoute(route) != modulation::MatrixStatus::ok) {
            diagnostic = "slot modulation route invalid";
            return false;
        }
    modulation::PersistentState persistent;
    if (modulation::captureState(candidate, modulationRuntime.registry,
            persistent) != modulation::StateStatus::ok) {
        diagnostic = "slot modulation state capture failed";
        return false;
    }
    std::vector<std::byte> encoded(
        modulation::serializedSize(persistent));
    std::size_t written {};
    if (encoded.empty()
        || encoded.size() > state::maxModulationPayloadBytes
        || modulation::serializeState(persistent, encoded, written)
            != modulation::StateStatus::ok
        || written != encoded.size()) {
        diagnostic = "slot modulation state exceeds host budget";
        return false;
    }
    beginExclusive();
    modulationRuntime.matrix = candidate;
    state_[index].modulationPayload = std::move(encoded);
    endExclusive();
    diagnostic.clear();
    return true;
}

void Rack::applyModulation(
    SlotRuntime& slot, const PersistentSlotState& persistent,
    const ProcessSlotControls& controls,
    const RackRouter::DestinationBuffer& midi,
    std::uint32_t sampleCount, double sampleRate, double bpm,
    double ppq) noexcept
{
    if (!slot.modulation || slot.descriptor == nullptr || sampleRate <= 0.0)
        return;
    auto& runtime = *slot.modulation;
    runtime.sources.macros = controls.macros;
    runtime.sources.envelope *= static_cast<float>(
        std::pow(0.2, static_cast<double>(sampleCount) / sampleRate));
    for (const auto& event : midi.view()) {
        const auto status = event.data[0] & 0xf0u;
        if (status == 0x90u && event.data[2] != 0) {
            runtime.sources.velocity = static_cast<float>(event.data[2]) / 127.0f;
            runtime.sources.envelope = 1.0f;
        } else if (status == 0xb0u && event.data[1] == 1u) {
            runtime.sources.modWheel = static_cast<float>(event.data[2]) / 127.0f;
        } else if (status == 0xd0u) {
            runtime.sources.aftertouch = static_cast<float>(event.data[1]) / 127.0f;
        }
    }
    const auto safeBpm = std::isfinite(bpm) && bpm > 0.0 ? bpm : 120.0;
    const std::array ratesHz { safeBpm / 240.0, safeBpm / 60.0 };
    for (std::size_t lfo = 0; lfo < runtime.lfoPhase.size(); ++lfo) {
        runtime.sources.lfos[lfo] = static_cast<float>(
            std::sin(2.0 * pi * runtime.lfoPhase[lfo]));
        runtime.lfoPhase[lfo] = std::fmod(runtime.lfoPhase[lfo]
            + ratesHz[lfo] * static_cast<double>(sampleCount) / sampleRate,
            1.0);
    }
    const auto step = static_cast<std::int64_t>(
        std::floor((std::isfinite(ppq) ? ppq : 0.0) * 4.0));
    runtime.sources.stepMod = static_cast<float>((step % 16 + 16) % 16)
        / 15.0f;
    if (step != runtime.sampleHoldStep) {
        runtime.sampleHoldStep = step;
        runtime.sources.sampleAndHold = randomUnit(runtime.randomState);
    }
    runtime.sources.random = randomUnit(runtime.randomState);

    const auto destinations = runtime.registry.destinations();
    for (std::size_t index = 0; index < destinations.size(); ++index) {
        const auto& destination = destinations[index];
        const auto range = destination.maximum - destination.minimum;
        runtime.baseNormalized[index] = range > 0.0f
            ? std::clamp((destination.defaultValue - destination.minimum)
                / range, 0.0f, 1.0f)
            : 0.0f;
        if (slot.descriptor->directParameters) {
          auto plain = destination.defaultValue;
          for (const auto &value : persistent.directParameters)
            if (value.id == destination.parameterId) {
              plain = value.value;
              break;
            }
          const auto key = instrument::hostparams::directKey(
              slot.descriptor->id, destination.parameterId);
          for (const auto &change : controls.directValues())
            if (change.key == key && std::isfinite(change.value)) {
              const auto *parameter =
                  findParameter(*slot.descriptor, destination.parameterId);
              plain = parameter ? instrument::hostparams::canonicalPlain(
                                      change.value, *parameter)
                                : change.value;
              break;
            }
          runtime.baseNormalized[index] =
              range > 0.0f ? std::clamp((plain - destination.minimum) / range,
                                        0.0f, 1.0f)
                           : 0.0f;
        }
        for (std::size_t macro = 0; macro < persistent.macroAssignments.size(); ++macro)
            if (persistent.macroAssignments[macro] == destination.parameterId)
                runtime.baseNormalized[index] = controls.macros[macro];
    }
    for (const auto& value : runtime.matrix.evaluate(runtime.sources,
             { runtime.baseNormalized.data(), destinations.size() }))
        if (const auto* destination = runtime.registry.find(value.key))
            (void) slot.instance->setParameter(destination->parameterId,
                                               value.plain);
}

void Rack::setMacro(std::size_t slot, std::size_t macro, float value) noexcept
{
    const std::scoped_lock persistentLock(persistentStateMutex_);
    if (slot >= state_.size() || macro >= instrument::macrosPerSlot
        || !std::isfinite(value)) return;
    value = std::clamp(value, 0.0f, 1.0f);
    beginExclusive();
    state_[slot].macros[macro] = value;
    endExclusive();
    // DSP consumes normalized defaults on the next block via macroToPlain.
    // An explicit host block snapshot takes precedence over these defaults.
}

void Rack::updateControls(std::size_t slot, Routing routing, bool enabled,
                          bool mute, bool solo, bool locked, float level,
                          float pan) noexcept
{
    const std::scoped_lock persistentLock(persistentStateMutex_);
    if (slot >= state_.size() || !valid(routing) || !std::isfinite(level)
        || !std::isfinite(pan)) return;
    beginExclusive();
    state_[slot].routing = routing;
    state_[slot].enabled = enabled;
    state_[slot].mute = mute;
    state_[slot].solo = solo;
    state_[slot].locked = locked;
    state_[slot].level = std::clamp(level, 0.0f, 2.0f);
    state_[slot].pan = std::clamp(pan, -1.0f, 1.0f);
    endExclusive();
}

void Rack::process(std::span<float *> outputs, std::uint32_t sampleCount,
                   std::span<const VoxMidiEventV1> midi, double bpm, double ppq,
                   bool playing, std::span<const VoxMidiEventV1> audition,
                   instrument::SlotId selectedSlot,
                   std::span<const ProcessSlotControls> controls,
                   std::span<const RackRouter::GeneratedSlotEvents> generated,
                   AudioRouting *audioRouting,
                   const AudioRoutingControls *audioControls) noexcept {
  if (mutationGate_.load(std::memory_order_seq_cst))
    return;
  const ActiveProcess active(activeProcessors_);
  if (mutationGate_.load(std::memory_order_seq_cst))
    return;
  // Read the prepared spec only after joining the control/audio gate.
  if (sampleCount > spec_.maximumBlockSize || outputs.empty() ||
      outputs.size() > spec_.outputChannels)
    return;
  if (controls.size() != state_.size()) {
    for (std::size_t i = 0; i < state_.size(); ++i)
      processing_->defaultControls[i] = processControls(state_[i]);
    controls = processing_->defaultControls;
  }
  processing_->router.route(midi, controls, processing_->routed);
  processing_->router.routeAudition(audition, selectedSlot, controls,
                                    processing_->routed);
  processing_->router.routeGenerated(generated, controls, processing_->routed);
  for (auto &destination : processing_->routed)
    destination.sortBySampleOffset();
  const bool anySolo = std::any_of(controls.begin(), controls.end(),
                                   [](const auto &slot) { return slot.solo; });
  // Pass 1: every slot renders into its own buffer. Notes a slot generates
  // itself (an internal drum sequencer) exist only after this pass.
  for (std::size_t i = 0; i < state_.size(); ++i) {
    auto &slot = processing_->runtime[i];
    droppedMidiEvents_[i].store(processing_->routed[i].dropped,
                                std::memory_order_relaxed);
    if (!slot.instance)
      continue;
    for (std::size_t channel = 0; channel < outputs.size(); ++channel)
      std::fill_n(slot.audio[channel].data(), sampleCount, 0.0f);
    for (std::size_t macro = 0; macro < instrument::macrosPerSlot; ++macro) {
      const auto &mapped = state_[i].macroAssignments[macro];
      if (!mapped.empty())
        if (const auto *parameter = findParameter(*slot.descriptor, mapped))
          slot.instance->setParameter(
              mapped, macroToPlain(controls[i].macros[macro], *parameter));
    }
    for (std::size_t binding = 0; binding < slot.directCount; ++binding) {
      const auto &direct = slot.direct[binding];
      auto value = direct.base;
      for (const auto &change : controls[i].directValues())
        if (change.key == direct.key && std::isfinite(change.value)) {
          value = instrument::hostparams::canonicalPlain(change.value,
                                                         *direct.parameter);
          break;
        }
      slot.instance->setParameter(direct.parameter->id, value);
    }
    applyModulation(slot, state_[i], controls[i], processing_->routed[i],
                    sampleCount, spec_.sampleRate, bpm, ppq);
    slot.instance->setBypassed(!controls[i].enabled || controls[i].mute ||
                               (anySolo && !controls[i].solo));
    // The newest published pattern snapshot, held until the next block.
    VoxPatternV2 patternView{};
    const VoxPatternV2 *patternSnapshot = nullptr;
    std::uint64_t patternGeneration = 0;
    if (slot.patterns)
      if (const auto *current = slot.patterns->acquire(patternGeneration)) {
        patternView = current->view();
        patternSnapshot = &patternView;
      }
    slot.instance->process({{slot.pointers.data(), outputs.size()},
                            sampleCount,
                            processing_->routed[i].view(),
                            spec_.sampleRate,
                            bpm,
                            ppq,
                            playing,
                            patternSnapshot,
                            patternGeneration,
                            slot.patterns ? &slot.noteOutput : nullptr});
  }
  // Pass 2: the duck envelope of this block, from the kick source's routed
  // MIDI and its own generated notes of this same block. Render order (the
  // source may sit after its targets) and block size do not matter, and no
  // lookahead or block latency is added.
  if (audioRouting && audioControls && AudioRouting::valid(*audioControls)) {
    auto available = *audioControls;
    std::span<const VoxNoteEventV2> generated;
    bool found = false, routedIgnored = false;
    for (std::size_t i = 0; i < controls.size(); ++i)
      if (controls[i].slotId == available.kickSource &&
          processing_->runtime[i].instance) {
        found = true;
        const auto &source = processing_->runtime[i];
        // A count beyond the buffer is broken output: none of it is used.
        if (source.patterns && source.noteOutput.count <= source.noteEvents.size())
          generated = {source.noteEvents.data(), source.noteOutput.count};
        // Validated by the adapter; a voided or bypassed block reads MAY_PLAY.
        routedIgnored = source.patterns &&
                        source.noteOutput.routedNotes == VOX_ROUTED_NOTES_ALL_IGNORED;
      }
    if (!found)
      available.kickSource = instrument::invalidSlotId;
    if (!audioRouting->begin(available, controls, processing_->routed,
                             sampleCount, generated, routedIgnored))
      audioRouting = nullptr;
  } else
    audioRouting = nullptr;
  // Pass 3: mix.
  for (std::size_t i = 0; i < state_.size(); ++i) {
    auto &slot = processing_->runtime[i];
    if (!slot.instance)
      continue;
    if (audioRouting) {
      audioRouting->mixSlot(i, {slot.pointers.data(), outputs.size()}, outputs,
                            controls[i],
                            controls[i].enabled && !controls[i].mute &&
                                (!anySolo || controls[i].solo),
                            sampleCount);
      continue;
    }
    const float left =
        controls[i].level *
        (controls[i].pan <= 0.0f ? 1.0f : 1.0f - controls[i].pan);
    const float right =
        controls[i].level *
        (controls[i].pan >= 0.0f ? 1.0f : 1.0f + controls[i].pan);
    for (std::uint32_t sample = 0; sample < sampleCount; ++sample) {
      outputs[0][sample] += slot.audio[0][sample] * left;
      if (outputs.size() > 1)
        outputs[1][sample] += slot.audio[1][sample] * right;
    }
  }
  if (audioRouting)
    audioRouting->finish(outputs, sampleCount);
}

const instrument::InstrumentDescriptor* Rack::descriptor(std::size_t index) const noexcept
{
  return index < publishedDescriptors_.size()
             ? publishedDescriptors_[index].load(std::memory_order_acquire)
             : nullptr;
}

std::array<PersistentSlotState, instrument::maxSlots> Rack::snapshotState(
    std::span<const ProcessSlotControls> controls) const
{
    const std::scoped_lock persistentLock(persistentStateMutex_);
    auto snapshot = state_;
    for (std::size_t index = 0; index < snapshot.size(); ++index) {
        if (controls.size() == snapshot.size()) {
            snapshot[index].routing = controls[index].routing;
            snapshot[index].enabled = controls[index].enabled;
            snapshot[index].mute = controls[index].mute;
            snapshot[index].solo = controls[index].solo;
            snapshot[index].locked = controls[index].locked;
            snapshot[index].level = controls[index].level;
            snapshot[index].pan = controls[index].pan;
            snapshot[index].macros = controls[index].macros;
            for (auto &value : snapshot[index].directParameters) {
              const auto key = instrument::hostparams::directKey(
                  snapshot[index].instrumentId, value.id);
              for (const auto &change : controls[index].directValues())
                if (change.key == key && std::isfinite(change.value)) {
                  const auto *descriptor =
                      processing_->runtime[index].descriptor;
                  const auto *parameter =
                      descriptor != nullptr
                          ? findParameter(*descriptor, value.id)
                          : nullptr;
                  value.value = parameter != nullptr
                                    ? instrument::hostparams::canonicalPlain(
                                          change.value, *parameter)
                                    : change.value;
                  break;
                }
            }
        }
        // Module payload is committed transactionally by module/preset loads.
        // Live automated values are persisted through macros above. Touching
        // DSP here would race process() or force host autosave to gate audio.
    }
    return snapshot;
}

std::array<RuntimeSlotState, instrument::maxSlots> Rack::runtimeState() const noexcept
{
    const std::scoped_lock persistentLock(persistentStateMutex_);
    auto snapshot = runtimeState_;
    for (std::size_t index = 0; index < snapshot.size(); ++index)
        snapshot[index].droppedMidiEvents =
            droppedMidiEvents_[index].load(std::memory_order_relaxed);
    return snapshot;
}

std::uint32_t Rack::latencySamples() const noexcept
{
    std::uint32_t result = 0;
    for (const auto &slot : processing_->runtime)
      if (slot.instance)
        result = std::max(result, slot.instance->latencySamples());
    return result;
}
std::uint32_t Rack::tailSamples() const noexcept
{
    std::uint32_t result = 0;
    for (const auto &slot : processing_->runtime)
      if (slot.instance)
        result = std::max(result, slot.instance->tailSamples());
    return result;
}
} // namespace vstengine::rack
