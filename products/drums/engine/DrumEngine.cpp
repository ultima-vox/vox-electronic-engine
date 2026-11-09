#include "DrumEngine.h"

// The only translation unit that sees the legacy module's C interface.
#include "instrument/abi/VoxModuleAbiV2.h"

#include <algorithm>
#include <cstring>

// The legacy copy is compiled with -DvoxModuleEntryV2=voxDrumsLegacy8EntryV2
// (products/CMakeLists.txt), so its entry never clashes with another module.
extern "C" VoxResult VOX_CALL voxDrumsLegacy8EntryV2 (uint32_t hostAbiVersion, const VoxHostServicesV2* host,
                                                     const VoxModuleV2** module);

namespace vox::drums {
namespace {

constexpr const char* moduleId = "com.ultimavox.fullon-drums";
constexpr std::uint32_t lanes = padCount, bars = patternBars, steps = patternSteps;
constexpr std::uint32_t cellCount = lanes * bars * steps;

struct Module {
    const VoxModuleV2* table = nullptr;
    const VoxInstrumentDescriptorV2* descriptor = nullptr;
};

const Module& module()
{
    static const Module m = [] {
        static const VoxHostServicesV2 services = { sizeof (VoxHostServicesV2), VOX_MODULE_ABI_V2, nullptr, nullptr };
        Module result;
        if (voxDrumsLegacy8EntryV2 (VOX_MODULE_ABI_V2, &services, &result.table) == VOX_OK && result.table != nullptr)
            result.table->getInstrument (0, &result.descriptor);
        return result;
    }();
    return m;
}

const VoxParameterDescriptorV2& descriptorAt (std::uint32_t i)
{
    const auto* d = module().descriptor;
    return *reinterpret_cast<const VoxParameterDescriptorV2*> (
        reinterpret_cast<const std::uint8_t*> (d->parameters) + std::size_t (i) * d->parameterStride);
}

struct FactoryPatterns {
    std::vector<std::string> names;
    std::vector<std::array<VoxPatternStepV2, cellCount>> cells;
};

const FactoryPatterns& factory()
{
    static const FactoryPatterns patterns = [] {
        FactoryPatterns result;
        const auto& m = module();
        if (m.table == nullptr || m.table->content == nullptr)
            return result;
        for (std::uint32_t index = 0;; ++index)
        {
            std::array<VoxPatternStepV2, cellCount> cells {};
            VoxPatternV2 pattern = { sizeof pattern, VOX_MODULE_ABI_V2, 0, 0, 0, 0,
                                     sizeof (VoxPatternStepV2), cellCount, cells.data() };
            const char* name = nullptr;
            if (m.table->content->getFactoryPattern (moduleId, index, &name, &pattern) != VOX_OK)
                break;
            result.names.emplace_back (name != nullptr ? name : "");
            result.cells.push_back (cells);
        }
        return result;
    }();
    return patterns;
}

constexpr std::uint32_t maxNoteOutput = 256;

} // namespace

FactoryStep DrumEngine::factoryStep (int pattern, int lane, int bar, int step) noexcept
{
    const auto& cells = factory().cells;
    if (pattern < 0 || pattern >= static_cast<int> (cells.size()) || lane < 0 || lane >= padCount
        || bar < 0 || bar >= patternBars || step < 0 || step >= patternSteps)
        return {};
    const auto& cell = cells[static_cast<std::size_t> (pattern)]
                            [static_cast<std::size_t> ((lane * patternBars + bar) * patternSteps + step)];
    return { (cell.flags & VOX_STEP_ON) != 0, (cell.flags & VOX_STEP_ACCENT) != 0, cell.velocity };
}

const std::array<std::string, padCount>& DrumEngine::padNames()
{
    static const std::array<std::string, padCount> names { "Kick", "Snare", "Clap", "Closed Hat",
                                                           "Open Hat", "Tom", "Rim", "Cymbal" };
    return names;
}

struct DrumEngine::Impl {
    VoxInstance instance = nullptr;
    std::uint32_t maxBlock = 0;
    double sampleRate = 48000.0;
    double lastPpq = -1.0e300;   // ppq of the previous call, to flag backward jumps
    std::array<VoxMidiEventV2, maxEventsPerCall> midi {};
    std::vector<VoxParamChangeV2> changes;   // sized at construction (parameter count)
    std::vector<VoxPatternV2> patterns;      // views of the factory cells, one per pattern
    std::array<VoxNoteEventV2, maxNoteOutput> notes {};
    VoxNoteOutputV2 noteOutput {};
    std::array<VoxSampleV2, padCount> samples {};
    std::array<std::uint64_t, maxSampleUsage> usageIds {};
    VoxSampleUsageV2 usage {};
};

DrumEngine::DrumEngine() : impl_ (std::make_unique<Impl>())
{
    const auto& m = module();
    if (m.table == nullptr || m.descriptor == nullptr)
        return;
    const VoxCreateParamsV2 cp = { sizeof cp, VOX_MODULE_ABI_V2, 0, 0 };
    if (m.table->create (moduleId, &cp, &impl_->instance) != VOX_OK)
        impl_->instance = nullptr;
    impl_->changes.resize (m.descriptor->parameterCount);
    impl_->noteOutput.structSize = sizeof (VoxNoteOutputV2);
    impl_->noteOutput.version = VOX_MODULE_ABI_V2;
    impl_->noteOutput.capacity = maxNoteOutput;
    impl_->noteOutput.stride = sizeof (VoxNoteEventV2);
    impl_->noteOutput.events = impl_->notes.data();
    for (auto& sample : impl_->samples)
    {
        sample.structSize = sizeof (VoxSampleV2);
        sample.version = VOX_MODULE_ABI_V2;
    }
    impl_->usage.structSize = sizeof (VoxSampleUsageV2);
    impl_->usage.version = VOX_MODULE_ABI_V2;
    impl_->usage.capacity = maxSampleUsage;
    impl_->usage.ids = impl_->usageIds.data();
    for (const auto& cells : factory().cells)
        impl_->patterns.push_back ({ sizeof (VoxPatternV2), VOX_MODULE_ABI_V2, lanes, steps, bars,
                                     VOX_TIMING_SIXTEENTH, sizeof (VoxPatternStepV2), cellCount,
                                     const_cast<VoxPatternStepV2*> (cells.data()) });
}

DrumEngine::~DrumEngine()
{
    if (impl_->instance != nullptr)
        module().table->destroy (impl_->instance);
}

std::uint32_t DrumEngine::parameterCount()
{
    return module().descriptor != nullptr ? module().descriptor->parameterCount : 0u;
}

const std::vector<EngineParameter>& DrumEngine::parameters()
{
    static const std::vector<EngineParameter> table = [] {
        std::vector<EngineParameter> result;
        for (std::uint32_t i = 0; i < parameterCount(); ++i)
        {
            const auto& d = descriptorAt (i);
            EngineParameter p;
            p.index = i;
            p.legacyId = d.id;
            p.name = d.name != nullptr ? d.name : "";
            p.unit = d.unit != nullptr ? d.unit : "";
            p.group = d.group != nullptr ? d.group : "";
            p.type = d.type == VOX_PARAM_CHOICE ? EngineParameter::Type::choice
                   : d.type == VOX_PARAM_BOOLEAN ? EngineParameter::Type::toggle
                                                 : EngineParameter::Type::continuous;
            for (std::uint32_t c = 0; c < d.choiceCount && d.choices != nullptr; ++c)
                p.choices.emplace_back (d.choices[c]);
            p.minimum = d.minimum;
            p.maximum = d.maximum;
            p.defaultValue = d.defaultValue;
            p.step = d.step;
            p.automatable = (d.flags & VOX_PARAM_AUTOMATABLE) != 0;
            result.push_back (std::move (p));
        }
        return result;
    }();
    return table;
}

const std::vector<std::string>& DrumEngine::factoryPatternNames()
{
    return factory().names;
}

const std::array<std::uint8_t, padCount>& DrumEngine::padNotes()
{
    static const std::array<std::uint8_t, padCount> notes = [] {
        std::array<std::uint8_t, padCount> result { 36, 38, 39, 42, 46, 45, 37, 49 };
        const auto& m = module();
        const VoxPatternCapsV2* caps = nullptr;
        if (m.table != nullptr && m.table->content != nullptr
            && m.table->content->getPatternCaps (moduleId, &caps) == VOX_OK && caps != nullptr
            && caps->laneNotes != nullptr && caps->laneCount == padCount)
            for (int i = 0; i < padCount; ++i)
                result[static_cast<std::size_t> (i)] = caps->laneNotes[i];
        return result;
    }();
    return notes;
}

bool DrumEngine::prepare (double sampleRate, std::uint32_t maximumBlockSize)
{
    prepared_ = false;
    if (impl_->instance == nullptr || maximumBlockSize == 0)
        return false;
    // The engine renders at most 8192 frames per call; larger host blocks are
    // split in process().
    impl_->maxBlock = std::min<std::uint32_t> (maximumBlockSize, 8192u);
    impl_->sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    const VoxPrepareSpecV2 spec = { sizeof spec, VOX_MODULE_ABI_V2, sampleRate, impl_->maxBlock, 2u,
                                    parameterCount(), maxEventsPerCall };
    prepared_ = module().table->prepare (impl_->instance, &spec) == VOX_OK;
    return prepared_;
}

bool DrumEngine::setAllParameters (const float* values, std::uint32_t count)
{
    return impl_->instance != nullptr && count == parameterCount()
        && module().table->setAllParameters (impl_->instance, values, count) == VOX_OK;
}

void DrumEngine::reset() noexcept
{
    if (impl_->instance != nullptr)
        module().table->reset (impl_->instance);
}

bool DrumEngine::process (float* left, float* right, std::uint32_t frames,
                          const ParameterChange* changes, std::uint32_t changeCount,
                          const MidiEvent* events, std::uint32_t eventCount,
                          const Transport& transport, int patternIndex, bool panic) noexcept
{
    return process (left, right, frames, changes, changeCount, events, eventCount, transport, patternIndex, panic,
                    nullptr);
}

bool DrumEngine::process (float* left, float* right, std::uint32_t frames,
                          const ParameterChange* changes, std::uint32_t changeCount,
                          const MidiEvent* events, std::uint32_t eventCount,
                          const Transport& transport, int patternIndex, bool panic,
                          const SampleView* samples) noexcept
{
    if (! prepared_ || left == nullptr || right == nullptr)
        return false;

    // The engine always gets the full sample table (an empty slot has id 0):
    // without one it would drop every sample voice.
    for (int pad = 0; pad < padCount; ++pad)
    {
        auto& slot = impl_->samples[static_cast<std::size_t> (pad)];
        const SampleView* view = samples != nullptr ? &samples[pad] : nullptr;
        const bool usable = view != nullptr && view->id != 0 && view->channels != nullptr && view->channelCount > 0;
        slot.id = usable ? view->id : 0u;
        slot.channels = usable ? view->channels : nullptr;
        slot.channelCount = usable ? view->channelCount : 0u;
        slot.frames = usable ? view->frames : 0u;
        slot.sampleRate = usable ? view->sampleRate : 0.0;
        slot.peak = usable ? view->peak : 0.0f;
    }

    VoxTransportV2 tr {};
    tr.structSize = sizeof tr;
    tr.version = VOX_MODULE_ABI_V2;
    tr.flags = (transport.playing ? VOX_TRANSPORT_PLAYING : 0u)
             | (transport.hasTempo ? VOX_TRANSPORT_HAS_TEMPO : 0u)
             | (transport.hasPpq ? VOX_TRANSPORT_HAS_PPQ : 0u)
             | (transport.hasBarStart ? VOX_TRANSPORT_HAS_BAR_START : 0u)
             | (transport.hasTimeSignature ? VOX_TRANSPORT_HAS_TIME_SIGNATURE : 0u)
             | (transport.hasTime ? VOX_TRANSPORT_HAS_TIME : 0u);
    // A loop or seek moves the position backwards; the engine then lets the
    // steps after the new position play again.
    if (transport.hasPpq && transport.ppq < impl_->lastPpq)
        tr.flags |= VOX_TRANSPORT_DISCONTINUITY;
    if (transport.hasPpq)
        impl_->lastPpq = transport.ppq;
    tr.timeSigNumerator = transport.numerator;
    tr.timeSigDenominator = transport.denominator;
    tr.tempoBpm = transport.bpm;

    const bool havePattern = patternIndex >= 0 && patternIndex < static_cast<int> (impl_->patterns.size());
    changeCount = std::min<std::uint32_t> (changeCount, static_cast<std::uint32_t> (impl_->changes.size()));
    for (std::uint32_t i = 0; i < changeCount; ++i)
        impl_->changes[i] = { changes[i].index, 0u, changes[i].value, 0u };

    hitMask_ = 0;
    bool ok = true;
    std::uint32_t done = 0, nextEvent = 0;
    bool first = true;
    while (done < frames || first)
    {
        // A sub-block ends at the engine's block limit, or before the event
        // that would exceed maxEventsPerCall.
        std::uint32_t end = std::min (frames, done + impl_->maxBlock);
        std::uint32_t count = 0;
        while (nextEvent + count < eventCount && events[nextEvent + count].offset < end)
        {
            if (count == maxEventsPerCall)
            {
                end = std::max (done + 1, events[nextEvent + count].offset);
                break;
            }
            ++count;
        }
        std::uint32_t taken = 0;
        for (; taken < count && events[nextEvent + taken].offset < end; ++taken)
        {
            auto& e = impl_->midi[taken];
            e = {};
            e.sampleOffset = events[nextEvent + taken].offset - done;
            e.size = std::min<std::uint32_t> (events[nextEvent + taken].size, 3u);
            std::memcpy (e.bytes, events[nextEvent + taken].bytes, 3);
        }
        const std::uint32_t n = end - done;

        // Musical position at the first sample of this sub-block.
        tr.ppqPosition = transport.ppq + (transport.hasTempo ? static_cast<double> (done) * transport.bpm / 60.0 / impl_->sampleRate : 0.0);
        tr.barStartPpq = transport.barStartPpq;
        tr.timeInSamples = transport.timeInSamples + static_cast<std::int64_t> (done);

        float* outputs[2] = { left + done, right + done };
        VoxProcessContextV2 c {};
        c.structSize = sizeof c;
        c.version = VOX_MODULE_ABI_V2;
        c.numSamples = n;
        c.outputChannelCount = 2;
        c.outputs = outputs;
        c.paramChanges = impl_->changes.data();
        c.paramChangeCount = first ? changeCount : 0u;
        c.paramChangeStride = sizeof (VoxParamChangeV2);
        c.midiEvents = impl_->midi.data();
        c.midiEventCount = taken;
        c.midiEventStride = sizeof (VoxMidiEventV2);
        c.samplePosition = static_cast<std::uint64_t> (std::max<std::int64_t> (0, tr.timeInSamples));
        c.transport = &tr;
        c.pattern = havePattern ? &impl_->patterns[static_cast<std::size_t> (patternIndex)] : nullptr;
        c.patternGeneration = static_cast<std::uint64_t> (patternIndex + 1);
        c.releaseFlags = (first && panic) ? VOX_PANIC_AT_START : 0u;
        c.noteOutput = &impl_->noteOutput;
        c.samples = impl_->samples.data();
        c.sampleSlotCount = padCount;
        c.sampleUsage = &impl_->usage;
        if (n > 0 || first)
        {
            ok = module().table->process (impl_->instance, &c) == VOX_OK && ok;
            for (std::uint32_t i = 0; i < impl_->noteOutput.count && i < maxNoteOutput; ++i)
                if (impl_->notes[i].lane < padCount)
                    hitMask_ |= 1u << impl_->notes[i].lane;
        }
        nextEvent += taken;
        done = end;
        first = false;
        if (frames == 0)
            break;
    }
    // The report of the last call covers every voice still sounding.
    usage_.count = std::min<std::uint32_t> (impl_->usage.count, maxSampleUsage);
    usage_.overflow = impl_->usage.overflow != 0 || ! ok;
    for (std::uint32_t i = 0; i < usage_.count; ++i)
        usage_.ids[i] = impl_->usageIds[i];
    if (! ok)
    {
        std::fill (left, left + frames, 0.0f);
        std::fill (right, right + frames, 0.0f);
    }
    return ok;
}

} // namespace vox::drums
