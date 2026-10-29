#include "acid/AcidControlInventory.h"
#include "acid/module/AcidModuleMap.h"

#include <cmath>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {

int gFailures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        ++gFailures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

struct Library {
    void* handle = nullptr;
    explicit Library(const char* path) {
#if defined(_WIN32)
        handle = LoadLibraryA(path);
#else
        handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
#endif
    }
    ~Library() {
#if defined(_WIN32)
        if (handle != nullptr) FreeLibrary(static_cast<HMODULE>(handle));
#else
        if (handle != nullptr) dlclose(handle);
#endif
    }
    void* symbol(const char* name) const {
#if defined(_WIN32)
        return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle), name));
#else
        return dlsym(handle, name);
#endif
    }
};

VoxResult VOX_CALL hostLog(void*, std::uint32_t, const char*) { return VOX_OK; }

struct Loaded {
    const VoxModuleV2* module = nullptr;
    VoxInstance instance = nullptr;
    bool open(Library& library) {
        auto* entry = reinterpret_cast<VoxModuleEntryV2Fn>(
            library.symbol(VOX_MODULE_ENTRY_V2_NAME));
        if (entry == nullptr) return false;
        const VoxResult rejected = entry(1u, nullptr, &module);
        check(rejected == VOX_E_UNSUPPORTED, "abi 1 is rejected");
        VoxHostServicesV2 host{sizeof(VoxHostServicesV2), VOX_MODULE_ABI_V2, nullptr, hostLog};
        if (entry(VOX_MODULE_ABI_V2, &host, &module) != VOX_OK || module == nullptr)
            return false;
        VoxCreateParamsV2 params{sizeof(VoxCreateParamsV2), VOX_MODULE_ABI_V2, 0, 0};
        return module->create(vstengine::acid::module::instrumentId, &params, &instance) == VOX_OK
            && instance != nullptr;
    }
    ~Loaded() {
        if (module != nullptr && instance != nullptr) module->destroy(instance);
    }
};

std::uint32_t indexOf(std::string_view id) {
    const auto& table = vstengine::acid::module::controls();
    for (std::uint32_t i = 0; i < table.size(); ++i)
        if (id == table[i].id) return i;
    return 999;
}

void render(const VoxModuleV2* module, VoxInstance instance, std::vector<float>& left,
            std::vector<float>& right, const VoxMidiEventV2* midi, std::uint32_t midiCount,
            const VoxParamChangeV2* params, std::uint32_t paramCount, std::uint32_t flags,
            double bpm, double ppq, bool playing) {
    float* outputs[2] = {left.data(), right.data()};
    VoxTransportV2 transport{};
    transport.structSize = sizeof(VoxTransportV2);
    transport.version = VOX_MODULE_ABI_V2;
    transport.flags = VOX_TRANSPORT_HAS_TEMPO | VOX_TRANSPORT_HAS_PPQ
        | (playing ? VOX_TRANSPORT_PLAYING : 0u);
    transport.tempoBpm = bpm;
    transport.ppqPosition = ppq;
    VoxProcessContextV2 context{};
    context.structSize = sizeof(VoxProcessContextV2);
    context.version = VOX_MODULE_ABI_V2;
    context.numSamples = static_cast<std::uint32_t>(left.size());
    context.outputChannelCount = 2;
    context.outputs = outputs;
    context.paramChanges = params;
    context.paramChangeCount = paramCount;
    context.paramChangeStride = sizeof(VoxParamChangeV2);
    context.midiEvents = midi;
    context.midiEventCount = midiCount;
    context.midiEventStride = sizeof(VoxMidiEventV2);
    context.releaseFlags = flags;
    context.transport = &transport;
    const VoxResult result = module->process(instance, &context);
    check(result == VOX_OK, "process accepts a prepared block");
}

float peak(const std::vector<float>& buffer) {
    float value = 0.0f;
    for (float sample : buffer) value = std::max(value, std::fabs(sample));
    return value;
}

bool finiteBuffer(const std::vector<float>& buffer) {
    for (float sample : buffer)
        if (!std::isfinite(sample)) return false;
    return true;
}

VoxMidiEventV2 note(std::uint32_t offset, std::uint8_t status, std::uint8_t noteNumber,
                    std::uint8_t velocity) {
    VoxMidiEventV2 event{};
    event.sampleOffset = offset;
    event.size = 3;
    event.bytes[0] = status;
    event.bytes[1] = noteNumber;
    event.bytes[2] = velocity;
    return event;
}

void prepare(const VoxModuleV2* module, VoxInstance instance, double rate, std::uint32_t block) {
    VoxPrepareSpecV2 spec{sizeof(VoxPrepareSpecV2), VOX_MODULE_ABI_V2, rate, block, 2, 64, 64};
    check(module->prepare(instance, &spec) == VOX_OK, "prepare accepts the spec");
}

} // namespace

int main(int argc, char** argv) {
    using namespace vstengine::acid;
    using namespace vstengine::acid::module;
    if (argc != 2) {
        std::cerr << "usage: acid_module_tests <VoxAcid303 library>\n";
        return 2;
    }

    check(controls().size() == acidControlInventory.size(), "module map covers the 80 inventory ids");
    check(controls().size() == 80, "canonical map has 80 parameters");
    AcidEngine engine;
    for (std::size_t i = 0; i < controls().size(); ++i) {
        const auto& control = controls()[i];
        const auto& inventory = acidControlInventory[i];
        check(control.id == inventory.id, "map order matches the inventory");
        check(inventory.poolIndex == i, "pool index is the descriptor index");
        if (control.domain == Domain::playFlag) continue;
        const float norm = engine.parameter(control.id);
        const float plain = normToPlain(control, norm);
        const float back = plainToNorm(control, plain);
        if (continuous(control.domain))
            check(std::fabs(back - norm) < 1.0e-5f, "continuous plain value restores engine norm");
        else
            check(normToPlain(control, back) == normToPlain(control, norm),
                  "discrete plain value restores the decoded choice");
        if (control.choiceCount > 0) {
            for (std::uint32_t choice = 0; choice < control.choiceCount; ++choice) {
                const float encoded = plainToNorm(control, static_cast<float>(choice));
                check(normToPlain(control, encoded) == static_cast<float>(choice),
                      "every selector index survives the engine bin");
            }
        }
    }
    check(normToPlain(controls()[indexOf("cutoff")], 0.0f) == 20.0f, "cutoff endpoint is 20 Hz");
    check(std::fabs(normToPlain(controls()[indexOf("cutoff")], 1.0f) - 18000.0f) < 0.1f,
          "cutoff endpoint is 18 kHz");
    check(normToPlain(controls()[indexOf("unison_voices")], 0.0f) == 1.0f, "unison default is one voice");
    check(normToPlain(controls()[indexOf("unison_voices")], 1.0f) == 4.0f, "unison maximum is four voices");

    Library library(argv[1]);
    check(library.handle != nullptr, "native module library opens");
    if (library.handle == nullptr) return 1;
    Loaded loaded;
    check(loaded.open(library), "module entry creates an instance");
    if (loaded.module == nullptr || loaded.instance == nullptr) return 1;

    const VoxInstrumentDescriptorV2* descriptor = nullptr;
    check(loaded.module->getInstrument(0, &descriptor) == VOX_OK, "instrument descriptor is published");
    check(descriptor != nullptr && descriptor->parameterCount == 80, "descriptor exposes 80 parameters");
    check(descriptor->stateVersion == AcidEngine::stateVersion, "module state version is the current engine version");
    check(std::strcmp(descriptor->id, instrumentId) == 0, "instrument id is com.ultimavox.acid");
    check(descriptor->parameters != nullptr && descriptor->parameterStride == sizeof(VoxParameterDescriptorV2),
          "parameter stride matches the pinned ABI");
    if (descriptor != nullptr && descriptor->parameters != nullptr) {
        for (std::uint32_t i = 0; i < descriptor->parameterCount; ++i) {
            const auto* parameter = reinterpret_cast<const VoxParameterDescriptorV2*>(
                reinterpret_cast<const std::uint8_t*>(descriptor->parameters)
                + static_cast<std::size_t>(i) * descriptor->parameterStride);
            check(std::strcmp(parameter->id, controls()[i].id) == 0, "descriptor id follows the map");
            check(parameter->poolIndex == static_cast<std::int32_t>(i), "pool indices are 0..79");
            check(parameter->defaultValue >= parameter->minimum
                      && parameter->defaultValue <= parameter->maximum,
                  "default lies in the published range");
            check((parameter->flags & VOX_PARAM_HOST_EXPOSED) != 0, "every control is host exposed");
            if (parameter->type == VOX_PARAM_CHOICE)
                check(parameter->choices != nullptr && parameter->choiceCount >= 2,
                      "choices are published");
        }
    }

    VoxCreateParamsV2 badParams{sizeof(VoxCreateParamsV2), VOX_MODULE_ABI_V2, 1, 0};
    VoxInstance rejected = nullptr;
    check(loaded.module->create("com.ultimavox.other", &badParams, &rejected) == VOX_E_INVALID_ARG,
          "unknown instrument id is rejected");

    prepare(loaded.module, loaded.instance, 48000.0, 512);
    std::vector<float> left(512), right(512);
    auto on = note(0, 0x90, 48, 100);
    render(loaded.module, loaded.instance, left, right, &on, 1, nullptr, 0, 0, 120.0, 0.0, true);
    check(finiteBuffer(left) && finiteBuffer(right), "note render is finite");
    check(peak(left) > 0.01f, "note on is audible");

    Loaded other;
    check(other.open(library), "second instance opens");
    prepare(other.module, other.instance, 48000.0, 512);
    std::vector<float> left2(512), right2(512);
    render(other.module, other.instance, left2, right2, &on, 1, nullptr, 0, 0, 120.0, 0.0, true);
    check(left == left2 && right == right2, "two fresh instances are deterministic");

    const std::uint32_t output = indexOf("output");
    VoxParamChangeV2 late{output, 400, 0.0f, 0};
    VoxParamChangeV2 immediate{output, 0, 0.0f, 0};
    Loaded loud;
    Loaded early;
    check(loud.open(library) && early.open(library), "gain instances open");
    prepare(loud.module, loud.instance, 48000.0, 512);
    prepare(early.module, early.instance, 48000.0, 512);
    std::vector<float> loudL(512), loudR(512), earlyL(512), earlyR(512);
    render(loud.module, loud.instance, loudL, loudR, &on, 1, &late, 1, 0, 120.0, 0.0, true);
    render(early.module, early.instance, earlyL, earlyR, &on, 1, &immediate, 1, 0, 120.0, 0.0, true);
    float earlyPeak = 0.0f, loudPeak = 0.0f;
    for (std::size_t i = 250; i < 400; ++i) {
        earlyPeak = std::max(earlyPeak, std::fabs(earlyL[i]));
        loudPeak = std::max(loudPeak, std::fabs(loudL[i]));
    }
    check(loudPeak > earlyPeak * 2.0f, "parameter offset does not mute samples before it");

    Loaded held;
    check(held.open(library), "release instance opens");
    prepare(held.module, held.instance, 48000.0, 512);
    std::vector<float> blockL(512), blockR(512);
    render(held.module, held.instance, blockL, blockR, &on, 1, nullptr, 0, 0, 120.0, 0.0, true);
    auto off = note(0, 0x80, 48, 0);
    for (int i = 0; i < 200; ++i)
        render(held.module, held.instance, blockL, blockR, i == 0 ? &off : nullptr, i == 0 ? 1u : 0u,
               nullptr, 0, 0, 120.0, 0.0, true);
    check(peak(blockL) < 1.0e-3f && peak(blockR) < 1.0e-3f, "note off decays to silence");

    Loaded panic;
    check(panic.open(library), "panic instance opens");
    prepare(panic.module, panic.instance, 48000.0, 256);
    std::vector<float> panicL(256), panicR(256);
    render(panic.module, panic.instance, panicL, panicR, &on, 1, nullptr, 0, 0, 120.0, 0.0, true);
    check(peak(panicL) > 0.01f, "panic fixture was sounding");
    render(panic.module, panic.instance, panicL, panicR, nullptr, 0, nullptr, 0,
           VOX_PANIC_AT_START, 120.0, 1.0, true);
    check(peak(panicL) < 1.0e-4f && peak(panicR) < 1.0e-4f, "panic at block start silences the voice");

    auto cc120 = note(0, 0xB0, 120, 0);
    Loaded killed;
    check(killed.open(library), "cc120 instance opens");
    prepare(killed.module, killed.instance, 48000.0, 256);
    render(killed.module, killed.instance, panicL, panicR, &on, 1, nullptr, 0, 0, 120.0, 0.0, true);
    render(killed.module, killed.instance, panicL, panicR, &cc120, 1, nullptr, 0, 0, 120.0, 1.0, true);
    check(peak(panicL) < 1.0e-4f, "CC120 silences the sounding channel");

    Loaded overlap;
    check(overlap.open(library), "overlap instance opens");
    prepare(overlap.module, overlap.instance, 48000.0, 256);
    VoxMidiEventV2 pair[2] = {note(0, 0x90, 40, 90), note(64, 0x90, 67, 110)};
    render(overlap.module, overlap.instance, panicL, panicR, pair, 2, nullptr, 0, 0, 120.0, 0.0, true);
    check(peak(panicL) > 0.01f && finiteBuffer(panicL), "overlapping notes stay finite and audible");

    for (double rate : {44100.0, 48000.0, 96000.0}) {
        for (std::uint32_t block : {64u, 128u, 512u}) {
            Loaded rateCase;
            check(rateCase.open(library), "rate instance opens");
            prepare(rateCase.module, rateCase.instance, rate, block);
            std::vector<float> a(block), b(block);
            auto local = note(0, 0x90, 36, 80);
            render(rateCase.module, rateCase.instance, a, b, &local, 1, nullptr, 0, 0, 133.0, 0.0, true);
            check(finiteBuffer(a) && finiteBuffer(b), "rate and block size stay finite");
        }
    }

    std::uint8_t state[AcidEngine::encodedStateBytes]{};
    std::uint32_t written = 0, version = 0;
    check(loaded.module->saveState(loaded.instance, state, sizeof(state), &written, &version) == VOX_OK,
          "current state saves");
    check(written == AcidEngine::encodedStateBytes && version == AcidEngine::stateVersion,
          "saved blob is the current engine encoding");
    check(loaded.module->loadState(loaded.instance, 1, state, written) == VOX_E_STATE_VERSION,
          "legacy state version is rejected");
    std::uint8_t corrupt[AcidEngine::encodedStateBytes];
    std::memcpy(corrupt, state, sizeof(state));
    corrupt[0] ^= 0xff;
    check(loaded.module->loadState(loaded.instance, version, corrupt, written) == VOX_E_STATE_CORRUPT,
          "corrupt state is rejected");
    std::uint8_t after[AcidEngine::encodedStateBytes]{};
    std::uint32_t writtenAfter = 0, versionAfter = 0;
    check(loaded.module->saveState(loaded.instance, after, sizeof(after), &writtenAfter, &versionAfter) == VOX_OK
              && std::memcmp(state, after, sizeof(state)) == 0,
          "rejected state does not change the instance");
    Loaded restored;
    check(restored.open(library), "restore instance opens");
    prepare(restored.module, restored.instance, 48000.0, 128);
    check(restored.module->loadState(restored.instance, version, state, written) == VOX_OK,
          "current state loads");

    const std::uint32_t mode = indexOf("filter_mode");
    float lows[80]{};
    float highs[80]{};
    for (std::uint32_t i = 0; i < 80; ++i) {
        const auto* parameter = reinterpret_cast<const VoxParameterDescriptorV2*>(
            reinterpret_cast<const std::uint8_t*>(descriptor->parameters) + i * descriptor->parameterStride);
        lows[i] = highs[i] = parameter->defaultValue;
    }
    lows[mode] = 0.0f;
    highs[mode] = 3.0f;
    Loaded lowpass;
    Loaded highpass;
    check(lowpass.open(library) && highpass.open(library), "filter instances open");
    prepare(lowpass.module, lowpass.instance, 48000.0, 512);
    prepare(highpass.module, highpass.instance, 48000.0, 512);
    check(lowpass.module->setAllParameters(lowpass.instance, lows, 80) == VOX_OK, "lowpass parameters apply");
    check(highpass.module->setAllParameters(highpass.instance, highs, 80) == VOX_OK, "highpass parameters apply");
    std::vector<float> lp(512), lpr(512), hp(512), hpr(512);
    auto saw = note(0, 0x90, 36, 120);
    double lpEdge = 0.0, hpEdge = 0.0;
    for (int i = 0; i < 4; ++i) {
        render(lowpass.module, lowpass.instance, lp, lpr, i == 0 ? &saw : nullptr, i == 0 ? 1u : 0u,
               nullptr, 0, 0, 120.0, 0.0, true);
        render(highpass.module, highpass.instance, hp, hpr, i == 0 ? &saw : nullptr, i == 0 ? 1u : 0u,
               nullptr, 0, 0, 120.0, 0.0, true);
        for (std::size_t sample = 1; sample < lp.size(); ++sample) {
            lpEdge += std::fabs(lp[sample] - lp[sample - 1]);
            hpEdge += std::fabs(hp[sample] - hp[sample - 1]);
        }
    }
    check(hpEdge > lpEdge, "highpass mode is brighter than lowpass mode");

    if (gFailures != 0)
        std::cerr << gFailures << " failure(s)\n";
    return gFailures == 0 ? 0 : 1;
}
