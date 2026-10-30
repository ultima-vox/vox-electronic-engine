#include "acid/AcidControlInventory.h"
#include "acid/module/AcidModuleMap.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
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

bool readModule(const char* path, std::vector<unsigned char>& file) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    in.seekg(0, std::ios::end);
    const auto size = in.tellg();
    if (size <= 0 || size > 64 * 1024 * 1024) return false;
    file.resize(static_cast<std::size_t>(size));
    in.seekg(0);
    in.read(reinterpret_cast<char*>(file.data()), size);
    return static_cast<std::size_t>(in.gcount()) == file.size();
}

template <typename T>
bool take(const std::vector<unsigned char>& file, std::size_t offset, T& value) {
    if (offset + sizeof(T) > file.size()) return false;
    std::memcpy(&value, file.data() + offset, sizeof(T));
    return true;
}

bool exportsExactly(const char* path, std::vector<std::string> expected) {
    std::vector<unsigned char> file;
    if (!readModule(path, file) || file.size() < 64) return false;
    std::vector<std::string> found;
    if (file[0] == 0x7f && file[1] == 'E' && file[2] == 'L' && file[3] == 'F') {
        std::uint64_t shoff = 0;
        std::uint16_t shentsize = 0, shnum = 0;
        if (!take(file, 40, shoff) || !take(file, 58, shentsize) || !take(file, 60, shnum))
            return false;
        for (std::uint16_t section = 0; section < shnum; ++section) {
            const std::size_t header = static_cast<std::size_t>(shoff) + static_cast<std::size_t>(section) * shentsize;
            std::uint32_t type = 0, link = 0;
            std::uint64_t offset = 0, size = 0, entrySize = 0;
            if (!take(file, header + 4, type) || !take(file, header + 24, offset)
                || !take(file, header + 32, size) || !take(file, header + 40, link)
                || !take(file, header + 56, entrySize) || type != 11 || entrySize < 24)
                continue;
            const std::size_t strings = static_cast<std::size_t>(shoff) + static_cast<std::size_t>(link) * shentsize;
            std::uint64_t stringOffset = 0, stringSize = 0;
            if (!take(file, strings + 24, stringOffset) || !take(file, strings + 32, stringSize))
                return false;
            const std::size_t count = static_cast<std::size_t>(size / entrySize);
            for (std::size_t symbol = 0; symbol < count; ++symbol) {
                const std::size_t at = static_cast<std::size_t>(offset) + symbol * entrySize;
                std::uint32_t name = 0;
                std::uint16_t shndx = 0;
                unsigned char info = 0;
                if (!take(file, at, name) || !take(file, at + 4, info) || !take(file, at + 6, shndx))
                    return false;
                if (shndx == 0) continue;
                const unsigned bind = info >> 4;
                const unsigned kind = info & 0x0f;
                if (bind != 1 || (kind != 1 && kind != 2)) continue;
                const std::size_t nameAt = static_cast<std::size_t>(stringOffset) + name;
                if (nameAt >= file.size()) return false;
                found.emplace_back(reinterpret_cast<const char*>(file.data() + nameAt));
            }
        }
    } else if (file[0] == 'M' && file[1] == 'Z') {
        std::uint32_t lfanew = 0;
        if (!take(file, 0x3c, lfanew) || lfanew + 24u > file.size()) return false;
        std::uint32_t signature = 0;
        std::uint16_t sections = 0, optionalSize = 0;
        if (!take(file, lfanew, signature) || signature != 0x00004550u) return false;
        if (!take(file, lfanew + 6, sections) || !take(file, lfanew + 20, optionalSize)) return false;
        const std::size_t optional = static_cast<std::size_t>(lfanew) + 24u;
        std::uint16_t magic = 0;
        if (!take(file, optional, magic) || magic != 0x20bu) return false;
        std::uint32_t exportRva = 0;
        if (!take(file, optional + 112, exportRva) || exportRva == 0) return false;
        const std::size_t sectionTable = optional + optionalSize;
        auto rvaToOffset = [&](std::uint32_t rva) -> std::size_t {
            for (std::uint16_t i = 0; i < sections; ++i) {
                const std::size_t section = sectionTable + static_cast<std::size_t>(i) * 40u;
                std::uint32_t virtualSize = 0, virtualAddress = 0, rawSize = 0, rawPointer = 0;
                if (!take(file, section + 8, virtualSize) || !take(file, section + 12, virtualAddress)
                    || !take(file, section + 16, rawSize) || !take(file, section + 20, rawPointer))
                    return 0;
                const std::uint32_t span = std::max(virtualSize, rawSize);
                if (rva >= virtualAddress && rva < virtualAddress + span && rawPointer != 0)
                    return static_cast<std::size_t>(rawPointer) + (rva - virtualAddress);
            }
            return 0;
        };
        const std::size_t directory = rvaToOffset(exportRva);
        std::uint32_t nameCount = 0, namesRva = 0;
        if (directory == 0 || !take(file, directory + 24, nameCount) || !take(file, directory + 32, namesRva))
            return false;
        const std::size_t names = rvaToOffset(namesRva);
        if (names == 0) return false;
        for (std::uint32_t i = 0; i < nameCount; ++i) {
            std::uint32_t nameRva = 0;
            if (!take(file, names + static_cast<std::size_t>(i) * 4u, nameRva)) return false;
            const std::size_t nameAt = rvaToOffset(nameRva);
            if (nameAt == 0 || nameAt >= file.size()) return false;
            found.emplace_back(reinterpret_cast<const char*>(file.data() + nameAt));
        }
    } else {
        return false;
    }
    std::sort(found.begin(), found.end());
    std::sort(expected.begin(), expected.end());
    if (found != expected) {
        std::cerr << "exports of " << path << ":";
        for (const auto& name : found) std::cerr << ' ' << name;
        if (found.empty()) std::cerr << " <none>";
        std::cerr << '\n';
    }
    return found == expected;
}

VoxResult VOX_CALL hostLog(void*, std::uint32_t, const char*) { return VOX_OK; }

std::atomic<int> gAudioLogCalls{0};
VoxResult VOX_CALL countingLog(void*, std::uint32_t, const char*) {
    gAudioLogCalls.fetch_add(1, std::memory_order_relaxed);
    return VOX_OK;
}

struct Loaded {
    const VoxModuleV2* module = nullptr;
    VoxInstance instance = nullptr;
    bool open(Library& library,
              VoxResult(VOX_CALL *logger)(void*, std::uint32_t, const char*) = hostLog) {
        auto* entry = reinterpret_cast<VoxModuleEntryV2Fn>(
            library.symbol(VOX_MODULE_ENTRY_V2_NAME));
        if (entry == nullptr) return false;
        const VoxResult rejected = entry(1u, nullptr, &module);
        check(rejected == VOX_E_UNSUPPORTED, "abi 1 is rejected");
        VoxHostServicesV2 host{sizeof(VoxHostServicesV2), VOX_MODULE_ABI_V2, nullptr, logger};
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

void prepare(const VoxModuleV2* module, VoxInstance instance, double rate, std::uint32_t block) {
    VoxPrepareSpecV2 spec{sizeof(VoxPrepareSpecV2), VOX_MODULE_ABI_V2, rate, block, 2, 64, 64};
    check(module->prepare(instance, &spec) == VOX_OK, "prepare accepts the spec");
}

VoxResult processResult(const VoxModuleV2* module, VoxInstance instance, std::vector<float>& left,
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
    return module->process(instance, &context);
}

void render(const VoxModuleV2* module, VoxInstance instance, std::vector<float>& left,
            std::vector<float>& right, const VoxMidiEventV2* midi, std::uint32_t midiCount,
            const VoxParamChangeV2* params, std::uint32_t paramCount, std::uint32_t flags,
            double bpm, double ppq, bool playing) {
    check(processResult(module, instance, left, right, midi, midiCount, params, paramCount, flags,
                        bpm, ppq, playing) == VOX_OK,
          "process accepts a prepared block");
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

} // namespace

int main(int argc, char** argv) {
    using namespace vstengine::acid;
    using namespace vstengine::acid::module;
    if (argc != 3) {
        std::cerr << "usage: acid_module_tests <VoxAcid303> <VoxAcid303TestHooks>\n";
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
    Library hooks(argv[2]);
    check(library.handle != nullptr, "native module library opens");
    check(hooks.handle != nullptr, "test-hook module opens");
    check(exportsExactly(argv[1], {"voxModuleEntryV2"}),
          "production module exports only voxModuleEntryV2");
    check(library.symbol("voxAcid303TestFault") == nullptr
              && library.symbol("voxAcid303FaultCode") == nullptr,
          "production module has no fault export");
    check(exportsExactly(argv[2], {"voxModuleEntryV2", "voxAcid303TestFault"}),
          "test hook is confined to the non-shipping module");
    if (library.handle == nullptr || hooks.handle == nullptr) return 1;
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

    Loaded watched;
    gAudioLogCalls.store(0);
    check(watched.open(library, countingLog), "logger instance opens");
    VoxPrepareSpecV2 rejectedSpec{sizeof(VoxPrepareSpecV2), VOX_MODULE_ABI_V2, 1.0, 64, 2, 8, 8};
    check(watched.module->prepare(watched.instance, &rejectedSpec) != VOX_OK,
          "impossible sample rate is rejected");
    std::uint8_t stale[316]{};
    check(watched.module->loadState(watched.instance, 1, stale, sizeof(stale)) == VOX_E_STATE_VERSION,
          "old state version is rejected");
    check(watched.module->loadState(watched.instance, AcidEngine::stateVersion, stale, sizeof(stale))
              == VOX_E_STATE_CORRUPT,
          "corrupt current state is rejected");
    prepare(watched.module, watched.instance, 48000.0, 128);
    std::vector<float> guardL(128), guardR(128);
    VoxParamChangeV2 poisoned{0, 0, std::numeric_limits<float>::quiet_NaN(), 0};
    render(watched.module, watched.instance, guardL, guardR, &on, 1, &poisoned, 1, 0,
           120.0, 0.0, true);
    check(gAudioLogCalls.load() == 0,
          "entry, prepare, loadState and process do not call host.log");

    auto* faultOf = reinterpret_cast<std::uint32_t (*)(VoxInstance)>(
        hooks.symbol("voxAcid303TestFault"));
    check(faultOf != nullptr, "test hook reads one instance");
    Loaded slotA;
    Loaded slotB;
    check(slotA.open(hooks) && slotB.open(hooks), "two Acid instances open");
    prepare(slotA.module, slotA.instance, 48000.0, 128);
    prepare(slotB.module, slotB.instance, 48000.0, 128);
    check(faultOf != nullptr && faultOf(slotA.instance) == 0 && faultOf(slotB.instance) == 0,
          "a new instance starts with a clear fault");
    std::vector<float> aL(128), aR(128), bL(128), bR(128);
    render(slotA.module, slotA.instance, aL, aR, &on, 1, &poisoned, 1, 0, 120.0, 0.0, true);
    render(slotB.module, slotB.instance, bL, bR, &on, 1, nullptr, 0, 0, 120.0, 0.0, true);
    check(faultOf != nullptr && faultOf(slotA.instance) == 3 && faultOf(slotB.instance) == 0,
          "a fault in slot A does not overwrite slot B");
    check(slotA.module->reset(slotA.instance) == VOX_OK, "slot A reset succeeds");
    check(faultOf != nullptr && faultOf(slotA.instance) == 0 && faultOf(slotB.instance) == 0,
          "reset clears only that instance");
    render(slotB.module, slotB.instance, bL, bR, nullptr, 0, &poisoned, 1, 0, 120.0, 0.0, true);
    check(faultOf != nullptr && faultOf(slotA.instance) == 0 && faultOf(slotB.instance) == 3,
          "slot B keeps its own fault");
    prepare(slotB.module, slotB.instance, 48000.0, 128);
    check(faultOf != nullptr && faultOf(slotB.instance) == 0,
          "prepare clears that instance's fault");

    VoxPrepareSpecV2 tight{sizeof(VoxPrepareSpecV2), VOX_MODULE_ABI_V2, 48000.0, 128, 2, 4, 4};
    check(slotA.module->prepare(slotA.instance, &tight) == VOX_OK, "small event cap prepares");
    std::vector<VoxMidiEventV2> tooManyMidi(5);
    std::vector<VoxParamChangeV2> tooManyParam(5);
    for (std::uint32_t i = 0; i < 5; ++i) {
        tooManyMidi[i] = note(i, 0x80, 48, 0);
        tooManyParam[i] = {indexOf("output"), i, 0.5f, 0};
    }
    std::fill(aL.begin(), aL.end(), 1.0f);
    std::fill(aR.begin(), aR.end(), 1.0f);
    check(processResult(slotA.module, slotA.instance, aL, aR, tooManyMidi.data(), 5, nullptr, 0, 0,
                        120.0, 0.0, false) == VOX_E_INVALID_ARG,
          "MIDI above the prepared cap is rejected");
    check(faultOf != nullptr && faultOf(slotA.instance) == 1, "MIDI overflow is that instance's fault");
    check(std::all_of(aL.begin(), aL.end(), [](float x) { return x == 0.0f; }),
          "rejected MIDI overflow silences the block");
    std::fill(aL.begin(), aL.end(), 1.0f);
    check(processResult(slotA.module, slotA.instance, aL, aR, nullptr, 0, tooManyParam.data(), 5, 0,
                        120.0, 0.0, false) == VOX_E_INVALID_ARG,
          "parameters above the prepared cap are rejected");
    check(faultOf != nullptr && faultOf(slotA.instance) == 4, "parameter overflow is that instance's fault");
    check(std::all_of(aL.begin(), aL.end(), [](float x) { return x == 0.0f; }),
          "rejected parameter overflow silences the block");

    Loaded budget;
    check(budget.open(library, countingLog), "measurement instance opens");
    VoxPrepareSpecV2 wide{sizeof(VoxPrepareSpecV2), VOX_MODULE_ABI_V2, 48000.0, 128, 2, 4096, 4096};
    check(budget.module->prepare(budget.instance, &wide) == VOX_OK, "4096-event prepare succeeds");
    auto measure = [&](std::uint32_t midiCount, std::uint32_t paramCount, double& minimum,
                       double& p95, double& worst) {
        std::vector<VoxMidiEventV2> midi(midiCount);
        std::vector<VoxParamChangeV2> params(paramCount);
        for (std::uint32_t i = 0; i < midiCount; ++i)
            midi[i] = note((midiCount - 1u - i) % 128u, i == 0 ? 0x90 : 0x80,
                           static_cast<std::uint8_t>(i & 127u), 100);
        for (std::uint32_t i = 0; i < paramCount; ++i)
            params[i] = {indexOf("output"), (i * 17u) % 128u, 0.5f, 0};
        std::vector<float> left(128), right(128);
        std::array<double, 20> samples{};
        for (double& sample : samples) {
            const auto started = std::chrono::steady_clock::now();
            const VoxResult result = processResult(
                budget.module, budget.instance, left, right, midi.data(), midiCount,
                params.data(), paramCount, 0, 120.0, 0.0, true);
            sample = std::chrono::duration<double, std::micro>(
                         std::chrono::steady_clock::now() - started)
                         .count();
            check(result == VOX_OK, "measured block is accepted");
        }
        std::sort(samples.begin(), samples.end());
        minimum = samples.front();
        p95 = samples[18];
        worst = samples.back();
        check(finiteBuffer(left), "measured block stays finite");
        check(worst < 5000000.0, "measured block did not hang");
    };
    double typicalMin = 0, typicalP95 = 0, typicalWorst = 0;
    double stressMin = 0, stressP95 = 0, stressWorst = 0;
    double capMin = 0, capP95 = 0, capWorst = 0;
    measure(8, 8, typicalMin, typicalP95, typicalWorst);
    measure(64, 64, stressMin, stressP95, stressWorst);
    measure(4096, 4096, capMin, capP95, capWorst);
    std::cout << std::fixed
              << "ACID_RT samples=128 rate=48000 budget_us=2666.67"
              << " typical_min_us=" << typicalMin
              << " typical_p95_us=" << typicalP95
              << " typical_worst_us=" << typicalWorst
              << " stress64_min_us=" << stressMin
              << " stress64_p95_us=" << stressP95
              << " stress64_worst_us=" << stressWorst
              << " cap4096_min_us=" << capMin
              << " cap4096_p95_us=" << capP95
              << " cap4096_worst_us=" << capWorst
              << "\n";
    check(gAudioLogCalls.load() == 0, "measurements do not call host.log");

    if (gFailures != 0)
        std::cerr << gFailures << " failure(s)\n";
    return gFailures == 0 ? 0 : 1;
}
