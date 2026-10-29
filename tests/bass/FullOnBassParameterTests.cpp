#include "bass/FullOnBass.h"
#include "instrument/InstrumentComplianceHarness.h"
#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#include <tuple>
#include <vector>

namespace {
thread_local bool countAllocations = false;
thread_local std::size_t allocationCount = 0;
}
void* operator new(std::size_t size)
{
    if (countAllocations) ++allocationCount;
    if (void* result = std::malloc(size == 0 ? 1 : size)) return result;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }

using vstengine::bass::FullOnBassInstance;
using vstengine::bass::fullOnParameters;
namespace {
int checks = 0;
void require(bool condition, const char* message)
{
    ++checks;
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
using State = std::array<std::byte, 52>;
State state(const FullOnBassInstance& bass)
{
    State result {};
    std::uint32_t written = 0;
    require(bass.saveState(result, written) && written == result.size(), "save exact payload");
    return result;
}
void writeFloat(State& bytes, std::size_t index, float value)
{
    auto bits = std::bit_cast<std::uint32_t>(value);
    for (std::size_t byte = 0; byte < 4; ++byte)
        bytes[index * 4 + byte] = static_cast<std::byte>((bits >> (byte * 8)) & 255u);
}
struct Render {
    FullOnBassInstance bass;
    double rate;
    std::uint32_t block;
    Render(double sampleRate = 48000, std::uint32_t blockSize = 256)
        : rate(sampleRate), block(blockSize)
    { require(bass.prepare({ rate, block, 2 }), "prepare"); }
    std::vector<float> next(std::span<const VoxMidiEventV1> events = {})
    {
        std::vector<float> left(block), right(block);
        std::array<float*, 2> outputs { left.data(), right.data() };
        bass.process({ outputs, block, events, rate, 145, 0, true });
        require(left == right, "stereo render agrees");
        require(std::all_of(left.begin(), left.end(), [](auto value) {
            return std::isfinite(value); }), "finite audio");
        return left;
    }
};
double energy(const std::vector<float>& audio)
{
    double sum = 0;
    for (auto value : audio) sum += static_cast<double>(value) * value;
    return sum;
}
void scaling(const std::vector<float>& source, const std::vector<float>& scaled,
             float factor, const char* message)
{
    require(energy(source) > 1e-8, "reference is audible");
    float error = 0;
    for (std::size_t i = 0; i < source.size(); ++i)
        error = std::max(error, std::abs(source[i] * factor - scaled[i]));
    require(error < 2e-6f, message);
}
const std::array<VoxMidiEventV1, 1> on {{ { 0, 3, { 0x90, 36, 127 } } }};
const std::array<VoxMidiEventV1, 1> off {{ { 0, 3, { 0x80, 36, 0 } } }};
void independence()
{
    FullOnBassInstance bass;
    const auto factory = state(bass);
    const auto descriptor = FullOnBassInstance::descriptor();
    require(descriptor.id == vstengine::bass::fullOnInstrumentId, "new independent identity");
    require(descriptor.parameters.size() == fullOnParameters.size(), "descriptor complete");
    for (std::size_t index = 0; index < fullOnParameters.size(); ++index) {
        const auto& parameter = fullOnParameters[index];
        require(descriptor.parameters[index].preferredMacro == -1, "no implicit macro binding");
        for (auto value : { parameter.minimum, parameter.maximum,
                            parameter.minimum - 100.f, parameter.maximum + 100.f }) {
            require(bass.loadState(1, factory), "restore baseline");
            require(bass.setParameter(parameter.id, value), "set known parameter");
            auto expected = factory;
            writeFloat(expected, index, std::clamp(value, parameter.minimum, parameter.maximum));
            require(state(bass) == expected, "only named parameter changes, including clamping");
        }
        for (auto invalid : { std::numeric_limits<float>::quiet_NaN(),
                              std::numeric_limits<float>::infinity() }) {
            const auto before = state(bass);
            require(!bass.setParameter(parameter.id, invalid), "reject nonfinite edit");
            require(state(bass) == before, "nonfinite edit is atomic");
            auto bad = factory;
            writeFloat(bad, index, invalid);
            require(!bass.loadState(1, bad), "reject nonfinite state");
            require(state(bass) == before, "nonfinite state is atomic");
        }
        auto bad = factory;
        writeFloat(bad, index, parameter.maximum + 1);
        const auto before = state(bass);
        require(!bass.loadState(1, bad), "reject out-of-range state");
        require(state(bass) == before, "range failure is atomic");
    }
    const auto before = state(bass);
    require(!bass.setParameter("accent", .4f), "no hidden legacy Accent alias");
    require(!bass.loadState(2, factory), "reject unknown schema");
    require(!bass.loadState(1, std::span(factory).first(32)), "reject legacy bytes under new ID");
    require(state(bass) == before, "failed loads preserve state");
    require(bass.loadState(1, {}), "empty payload restores defaults");
    require(state(bass) == factory, "empty load after mutation restores defaults");
    State tooSmall {};
    std::uint32_t written = 99;
    require(!bass.saveState(std::span(tooSmall).first(51), written) && written == 0,
            "short destination fails without reporting saved bytes");
    FullOnBassInstance restored;
    require(restored.loadState(1, factory) && state(restored) == factory, "canonical round trip");
}
void audioIndependence()
{
    for (double rate : { 44100., 48000., 96000. })
        for (std::uint32_t block : { 64u, 256u, 1024u }) {
            Render normal(rate, block), quiet(rate, block), velocity(rate, block);
            quiet.bass.setParameter("output.gain", -8.85f); // default minus 6 dB
            const std::array<VoxMidiEventV1, 1> softer {{ { 0, 3, { 0x90, 36, 64 } } }};
            auto baselineState = state(normal.bass);
            scaling(normal.next(on), quiet.next(on), std::pow(10.f, -6.f / 20.f),
                    "gain scales audio after nonlinear stages");
            normal.bass.reset();
            scaling(normal.next(on), velocity.next(softer), 64.f / 127.f,
                    "velocity changes amplitude, preserving waveform/pitch/envelope");
            require(state(normal.bass) == baselineState && state(velocity.bass) == baselineState,
                    "velocity never edits stored sound parameters");
            for (int i = 0; i < 10; ++i)
                scaling(normal.next(), velocity.next(), 64.f / 127.f,
                        "velocity scaling holds through decay and sustain");
            scaling(normal.next(off), velocity.next(off), 64.f / 127.f,
                    "velocity scaling holds through release");
            normal.bass.reset();
            Render replay(rate, block);
            require(normal.next(on) == replay.next(on), "reset render deterministic");
            auto original = state(normal.bass);
            Render restored(rate, block);
            require(restored.bass.loadState(1, original), "restore before note");
            normal.bass.reset();
            require(normal.next(on) == restored.next(on), "restored audio agrees");
        }
}
void sustainedEdits()
{
    // JUCE ADSR/pitch duration are note-on snapshots; changing their stored
    // value must leave the current envelope intact and affect the next note.
    for (auto id : { "amp.attack", "amp.decay", "amp.sustain", "amp.release", "pitch.time" }) {
        Render baseline, changed;
        require(baseline.next(on) == changed.next(on), "snapshot baseline agrees");
        const float target = std::string_view(id) == "amp.sustain" ? .1f
            : std::string_view(id) == "amp.release" ? 250.f : 100.f;
        changed.bass.setParameter(id, target);
        require(baseline.next() == changed.next(), "ADSR edit does not corrupt active envelope");
        baseline.bass.reset(); changed.bass.reset();
        bool differs = baseline.next(on) != changed.next(on);
        if (std::string_view(id) == "amp.release")
            differs |= baseline.next(off) != changed.next(off);
        else for (int i = 0; i < 120; ++i)
            differs |= baseline.next() != changed.next();
        require(differs, "new note consumes independent envelope setting");
    }
    Render baseline, edited;
    baseline.next(on); edited.next(on);
    edited.bass.setParameter("output.gain", -8.85f);
    const auto reference = baseline.next(), ramped = edited.next();
    const float ratio = std::pow(10.f, -6.f / 20.f);
    float rampError = 0;
    for (std::size_t i = 0; i < reference.size(); ++i) {
        const float factor = 1 + (ratio - 1) * std::min(1.f, (i + 1) / 240.f);
        rampError = std::max(rampError, std::abs(reference[i] * factor - ramped[i]));
    }
    if (rampError >= 2e-6f) std::cerr << "gain ramp max error: " << rampError << '\n';
    require(rampError < 2e-6f, "live gain follows five-ms ramp without retrigger");
    scaling(baseline.next(), edited.next(), std::pow(10.f, -6.f / 20.f),
            "settled live gain preserves phase and envelope continuity");
    Render tracked, untracked;
    tracked.next(on); untracked.next(on);
    tracked.bass.setParameter("filter.keytracking", 1);
    require(tracked.next() != untracked.next(), "live key tracking invalidates coefficient cache");
}
void realtimeAllocationCheck()
{
    FullOnBassInstance bass;
    require(bass.prepare({ 48000, 256, 2 }), "prepare allocation probe");
    std::array<float, 256> left {}, right {};
    std::array<float*, 2> outputs { left.data(), right.data() };
    // Warm the exact path before measuring C++ allocations. Aligned allocator,
    // C malloc, locks and CPU timing are not measured by this probe.
    bass.process({ outputs, 256, on, 48000, 145, 0, true });
    allocationCount = 0;
    countAllocations = true;
    for (int block = 0; block < 256; ++block) {
        bass.setParameter("output.gain", -3.f - (block % 10));
        bass.setParameter("filter.keytracking", (block % 10) * .1f);
        bass.process({ outputs, 256, block % 8 == 0 ? std::span(on)
                         : std::span<const VoxMidiEventV1>(), 48000, 145, 0, true });
    }
    countAllocations = false;
    require(allocationCount == 0, "warmed process/setters do not call C++ new/new[]");
}
void sustainLifecycle()
{
    Render held;
    const std::array<VoxMidiEventV1, 1> pedalDown {{{0, 3, {0xb0, 64, 127}}}};
    const std::array<VoxMidiEventV1, 1> pedalUp {{{0, 3, {0xb0, 64, 0}}}};
    held.next(on);
    held.next(pedalDown);
    held.next(off);
    for (int i = 0; i < 80; ++i) held.next();
    require(energy(held.next()) > 1e-8, "sustain holds released key");
    held.next(pedalUp);
    for (int i = 0; i < 100; ++i) held.next();
    require(energy(held.next()) == 0, "pedal release ends held sound");
    Render wrongChannel;
    const std::array<VoxMidiEventV1, 1> otherPedal {{{0, 3, {0xb1, 64, 127}}}};
    wrongChannel.next(on);
    wrongChannel.next(otherPedal);
    wrongChannel.next(off);
    for (int i = 0; i < 100; ++i) wrongChannel.next();
    require(energy(wrongChannel.next()) == 0, "other channel pedal cannot hold note");

    Render clearedPedal;
    const std::array<VoxMidiEventV1, 1> emptyAllOff {{{0, 3, {0xb0, 123, 0}}}};
    clearedPedal.next(pedalDown);
    clearedPedal.next(emptyAllOff);
    clearedPedal.next(on);
    clearedPedal.next(off);
    for (int i = 0; i < 100; ++i) clearedPedal.next();
    require(energy(clearedPedal.next()) == 0,
            "all notes off clears pedal without an active note");

    for (auto controller : {120}) {
        Render panic;
        const std::array<VoxMidiEventV1, 1> allOff {{{0, 3,
            {0xb0, static_cast<std::uint8_t>(controller), 0}}}};
        panic.next(on);
        panic.next(pedalDown);
        panic.next(off);
        panic.next(allOff);
        require(energy(panic.next()) == 0, "all-off ends sustained voice");
    }
    for (auto controller : {120}) {
        Render timed;
        timed.next(on);
        const std::array<VoxMidiEventV1, 1> allOffAt64 {{{64, 3,
            {0xb0, static_cast<std::uint8_t>(controller), 0}}}};
        const auto audio = timed.next(allOffAt64);
        require(std::any_of(audio.begin(), audio.begin() + 64,
                            [](float x) { return std::abs(x) > 1e-6f; }),
                "audio continues before all-off offset");
        require(std::all_of(audio.begin() + 64, audio.end(),
                            [](float x) { return x == 0.0f; }),
                "all-off silences from its sample offset");
    }
    Render releaseByNote, releaseByController;
    require(releaseByNote.bass.setParameter("amp.release", 250.f)
                && releaseByController.bass.setParameter("amp.release", 250.f),
            "long release configured");
    require(releaseByNote.next(on) == releaseByController.next(on), "release baseline agrees");
    const std::array<VoxMidiEventV1, 1> noteOffAt64 {{{64, 3, {0x80, 36, 0}}}};
    const std::array<VoxMidiEventV1, 1> allNotesAt64 {{{64, 3, {0xb0, 123, 0}}}};
    const auto naturalRelease = releaseByNote.next(noteOffAt64);
    const auto controllerRelease = releaseByController.next(allNotesAt64);
    if (naturalRelease != controllerRelease)
        for (std::size_t i = 0; i < naturalRelease.size(); ++i)
            if (naturalRelease[i] != controllerRelease[i]) {
                std::cerr << "release difference at " << i << ": " << naturalRelease[i] << " vs " << controllerRelease[i] << '\n';
                break;
            }
    require(naturalRelease == controllerRelease, "CC123 follows NoteOff envelope at its sample offset");
    require(std::any_of(controllerRelease.begin() + 64, controllerRelease.end(),
                        [](float x) { return std::abs(x) > 1e-6f; }),
            "CC123 retains audible release tail");
    require(energy(releaseByController.next()) > 1e-8, "CC123 release continues in next block");
    Render tailSoundOff;
    require(tailSoundOff.bass.setParameter("amp.release", 250.f), "configure long tail");
    tailSoundOff.next(on);
    tailSoundOff.next(off);
    const std::array<VoxMidiEventV1, 1> soundOffAt64 {{{64, 3, {0xb0, 120, 0}}}};
    const auto interruptedTail = tailSoundOff.next(soundOffAt64);
    require(std::any_of(interruptedTail.begin(), interruptedTail.begin() + 64,
                        [](float x) { return std::abs(x) > 1e-6f; })
                && std::all_of(interruptedTail.begin() + 64, interruptedTail.end(),
                               [](float x) { return x == 0.0f; }),
            "CC120 silences an existing release at its sample offset");
    for (int i = 0; i < 60; ++i) releaseByController.next();
    require(energy(releaseByController.next()) == 0, "CC123 release reaches silence");
    Render pedalRelease, pedalAllNotes;
    pedalRelease.next(on); pedalAllNotes.next(on);
    pedalRelease.next(pedalDown); pedalAllNotes.next(pedalDown);
    pedalRelease.next(off); pedalAllNotes.next(off);
    const std::array<VoxMidiEventV1, 1> pedalUpAt64 {{{64, 3, {0xb0, 64, 0}}}};
    require(pedalRelease.next(pedalUpAt64) == pedalAllNotes.next(allNotesAt64),
            "CC123 clears sustain and releases deferred note at its offset");
    Render resetState;
    require(resetState.bass.setParameter("filter.cutoff", 1200.0f), "edit cutoff before reset");
    const auto beforeReset = state(resetState.bass);
    resetState.next(on);
    resetState.bass.reset();
    require(state(resetState.bass) == beforeReset, "reset preserves parameter state");
    require(energy(resetState.next()) == 0, "reset silences active voice");

    Render retrigger;
    retrigger.next(on);
    retrigger.next(on);
    retrigger.next(off);
    for (int i = 0; i < 100; ++i) retrigger.next();
    require(energy(retrigger.next()) == 0,
            "one note-off releases a retriggered key");
}
void boundsAndLifecycle()
{
    Render baseline, other;
    auto expected = baseline.next(on);
    const std::array<VoxMidiEventV1, 5> malformed {{
        { 0, 3, { 0x90, 36, 127 } }, { 120, 3, { 0x81, 36, 0 } },
        { 121, 3, { 0x80, 37, 0 } }, { 122, 3, { 0x90, 255, 127 } },
        { 123, 255, { 0x80, 36, 0 } }
    }};
    require(other.next(malformed) == expected, "wrong note/channel and malformed MIDI ignored");
    other.bass.setBypassed(true);
    require(energy(other.next(on)) == 0, "bypass silence");
    other.bass.setBypassed(false);
    other.bass.suspend();
    require(energy(other.next(on)) == 0, "suspend silence");
    other.bass.resume();
    require(other.next(on) == expected, "resume can retrigger deterministically");
    for (int i = 0; i < 100; ++i) other.next(i == 0 ? std::span(off) : std::span<const VoxMidiEventV1>());
    require(energy(other.next()) == 0, "release reaches silence");
    require(!other.bass.prepare({ std::numeric_limits<double>::infinity(), 256, 2 }), "reject infinite rate");
    require(!other.bass.prepare({ 22050, 256, 2 }), "reject unsupported low rate");
    require(!other.bass.prepare({ 48000, 0, 2 }), "reject zero block");
    require(other.bass.prepare({ 192000, 256, 2 }), "highest supported rate");
    require(other.bass.tailSamples() <= FullOnBassInstance::descriptor().budget.maxTailSamples,
            "tail within budget at highest rate");
}

void writeReleaseEvidence()
{
    const char* outputDirectory = std::getenv("VOX_BASS_RELEASE_WAV_DIR");
    if (!outputDirectory || !*outputDirectory) return;
    std::filesystem::create_directories(outputDirectory);
    for (const auto& [name, status, data1] : std::array {
             std::tuple { "note-off.wav", 0x80, 36 },
             std::tuple { "all-notes-off.wav", 0xb0, 123 },
             std::tuple { "all-sound-off.wav", 0xb0, 120 } }) {
        Render voice;
        require(voice.bass.setParameter("amp.release", 250.f), "evidence release configured");
        std::vector<float> samples;
        for (int block = 0; block < 80; ++block) {
            const std::array<VoxMidiEventV1, 1> release {{{64, 3,
                {static_cast<std::uint8_t>(status), static_cast<std::uint8_t>(data1), 0}}}};
            const auto audio = block == 0 ? voice.next(on)
                : block == 20 ? voice.next(release) : voice.next();
            samples.insert(samples.end(), audio.begin(), audio.end());
        }
        std::ofstream file(std::filesystem::path(outputDirectory) / name, std::ios::binary);
        require(static_cast<bool>(file), "open release evidence WAV");
        const auto u16 = [&](std::uint16_t v) { for (unsigned n = 0; n < 2; ++n) file.put(static_cast<char>(v >> (8 * n))); };
        const auto u32 = [&](std::uint32_t v) { for (unsigned n = 0; n < 4; ++n) file.put(static_cast<char>(v >> (8 * n))); };
        const auto bytes = static_cast<std::uint32_t>(samples.size() * sizeof(float));
        file.write("RIFF", 4); u32(36 + bytes); file.write("WAVEfmt ", 8);
        u32(16); u16(3); u16(1); u32(48000); u32(48000 * 4); u16(4); u16(32);
        file.write("data", 4); u32(bytes);
        for (float sample : samples) u32(std::bit_cast<std::uint32_t>(sample));
        require(static_cast<bool>(file), "write release evidence WAV");
    }
}

void writeCpuEvidence()
{
    const char* outputFile = std::getenv("VOX_BASS_CPU_CSV");
    if (!outputFile || !*outputFile) return;
    std::ofstream file(outputFile);
    require(static_cast<bool>(file), "open Bass CPU evidence");
    file << "sample_rate,block_size,voices,blocks,p95_us,p99_us,worst_us,p99_budget_percent\n";
    for (double rate : {44100.0, 48000.0, 96000.0})
        for (std::uint32_t blockSize : {64u, 128u, 512u})
            for (std::size_t voices : {1u, 2u, 8u, 16u}) {
                std::array<FullOnBassInstance, 16> instances;
                std::array<std::vector<float>, 16> left, right;
                std::array<std::array<float*, 2>, 16> outputs;
                for (std::size_t i = 0; i < voices; ++i) {
                    left[i].resize(blockSize); right[i].resize(blockSize);
                    outputs[i] = {left[i].data(), right[i].data()};
                    require(instances[i].prepare({rate, blockSize, 2}), "CPU probe prepare");
                    instances[i].process({outputs[i], blockSize, on, rate, 145, 0, true});
                }
                std::array<double, 200> elapsed {};
                for (int iteration = -20; iteration < static_cast<int>(elapsed.size()); ++iteration) {
                    const auto start = std::chrono::steady_clock::now();
                    for (std::size_t i = 0; i < voices; ++i) {
                        std::fill(left[i].begin(), left[i].end(), 0.0f);
                        std::fill(right[i].begin(), right[i].end(), 0.0f);
                        instances[i].process({outputs[i], blockSize, {}, rate, 145, 0, true});
                    }
                    const auto end = std::chrono::steady_clock::now();
                    if (iteration >= 0)
                        elapsed[static_cast<std::size_t>(iteration)] =
                            std::chrono::duration<double, std::micro>(end - start).count();
                }
                std::sort(elapsed.begin(), elapsed.end());
                const double p95 = elapsed[189], p99 = elapsed[197];
                file << static_cast<int>(rate) << ',' << blockSize << ',' << voices << ','
                     << elapsed.size() << ',' << p95 << ',' << p99 << ',' << elapsed.back()
                     << ',' << p99 * rate / (10000.0 * blockSize) << '\n';
            }
    require(static_cast<bool>(file), "write Bass CPU evidence");
}
void compliance()
{
    // Factory adapter for the common harness; the instance is the real new DSP.
    // This does not register it in the product catalog prematurely.
    struct Provider final : vstengine::instrument::InstrumentProvider {
        std::array<vstengine::instrument::InstrumentDescriptor, 1> descriptions {
            FullOnBassInstance::descriptor() };
        std::span<const vstengine::instrument::InstrumentDescriptor>
            descriptors() const noexcept override { return descriptions; }
        std::unique_ptr<vstengine::instrument::InstrumentInstance> create(
            std::string_view id, const vstengine::instrument::CreateContext&) override
        {
            return id == vstengine::bass::fullOnInstrumentId
                ? std::make_unique<FullOnBassInstance>() : nullptr;
        }
    } provider;
    auto report = vstengine::tests::runInstrumentCompliance(
        provider, vstengine::bass::fullOnInstrumentId);
    for (const auto& failure : report.failures) std::cerr << failure << '\n';
    require(static_cast<bool>(report), "common real-DSP instrument compliance");
    std::cout << "Instrument compliance: " << report.checks << " checks PASS\n";
}
}
int main()
{
    independence(); audioIndependence(); sustainedEdits(); sustainLifecycle(); boundsAndLifecycle();
    realtimeAllocationCheck(); compliance();
    writeReleaseEvidence();
    writeCpuEvidence();
    std::cout << "Full-on Bass independent parameters: " << checks << " checks PASS\n";
}
