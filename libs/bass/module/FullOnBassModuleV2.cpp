#define VOX_BUILDING_MODULE 1
#include "instrument/abi/VoxModuleAbiV2.h"
#include "bass/FullOnBass.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <new>
#include <span>

using vstengine::bass::FullOnBassInstance;
using vstengine::bass::fullOnParameters;
namespace {
constexpr uint32_t count = 13, maxMidi = 2048, maxStride = 1024;
std::atomic<unsigned> live{};
const auto descriptors = [] {
    std::array<VoxParameterDescriptorV2, count> result{};
    for (uint32_t i = 0; i < count; ++i) {
        const auto &p = fullOnParameters[i];
        result[i] = {sizeof(VoxParameterDescriptorV2), VOX_MODULE_ABI_V2,
            p.id.data(), p.name.data(), p.unit.data(), p.group.data(), nullptr,
            VOX_PARAM_FLOAT, 0, p.minimum, p.maximum, p.initial, 0.0f,
            static_cast<int32_t>(i), VOX_PARAM_HOST_EXPOSED | VOX_PARAM_AUTOMATABLE};
    }
    return result;
}();
const VoxInstrumentDescriptorV2 instrument{
    sizeof(VoxInstrumentDescriptorV2), VOX_MODULE_ABI_V2,
    "com.ultimavox.fullon-bass", "Full-on Bass", "Ultima Vox", "Bass",
    1, 1, 1, count, descriptors.data(), sizeof(VoxParameterDescriptorV2),
    1, maxMidi, 52, 0, 48000, 2, 0};
bool valueValid(uint32_t i, float v) noexcept {
    return i < count && std::isfinite(v) && v >= fullOnParameters[i].minimum && v <= fullOnParameters[i].maximum;
}
template<class T> bool validStruct(const T* p) noexcept {
    return p && p->structSize >= sizeof(T) && p->version == VOX_MODULE_ABI_V2;
}
template<class T> bool validArray(const T* p, uint32_t n, uint32_t stride) noexcept {
    return n == 0 || (p && stride >= sizeof(T) && stride <= maxStride &&
        stride % alignof(T) == 0 && reinterpret_cast<std::uintptr_t>(p) % alignof(T) == 0);
}
template<class T> T read(const T* p, uint32_t stride, uint32_t i) noexcept {
    T v{}; std::memcpy(&v, reinterpret_cast<const uint8_t*>(p) + static_cast<size_t>(stride)*i, sizeof(v)); return v;
}
}
struct VoxInstanceT {
    FullOnBassInstance engine;
    double sampleRate{};
    uint32_t maxBlock{}, channels{}, maxChanges{}, maxMidiEvents{};
    bool prepared{};
};
namespace {
VoxResult VOX_CALL getInstrument(uint32_t i, const VoxInstrumentDescriptorV2** out) {
    if (!out) return VOX_E_INVALID_ARG;
    *out = i == 0 ? &instrument : nullptr;
    return *out ? VOX_OK : VOX_E_INVALID_ARG;
}
VoxResult VOX_CALL create(const char* id, const VoxCreateParamsV2* p, VoxInstance* out) {
    if (!out) return VOX_E_INVALID_ARG;
    *out = nullptr;
    if (!id || std::strcmp(id, instrument.id) != 0 || !validStruct(p)) return VOX_E_INVALID_ARG;
    try { *out = new VoxInstanceT; ++live; return VOX_OK; }
    catch (const std::bad_alloc&) { return VOX_E_OUT_OF_MEMORY; }
    catch (...) { return VOX_E_INTERNAL; }
}
VoxResult VOX_CALL destroy(VoxInstance s) {
    if (!s) return VOX_E_INVALID_ARG;
    delete s; --live; return VOX_OK;
}
VoxResult VOX_CALL prepare(VoxInstance s, const VoxPrepareSpecV2* p) {
    if (!s || !validStruct(p) || p->maxParamChangesPerBlock > count ||
        p->maxMidiEventsPerBlock < 16 || p->maxMidiEventsPerBlock > maxMidi) return VOX_E_INVALID_ARG;
    try {
        if (!s->engine.prepare({p->sampleRate,p->maxBlockSize,p->outputChannels})) return VOX_E_INVALID_ARG;
        s->sampleRate=p->sampleRate;
        s->maxBlock=p->maxBlockSize; s->channels=p->outputChannels;
        s->maxChanges=p->maxParamChangesPerBlock; s->maxMidiEvents=p->maxMidiEventsPerBlock;
        s->prepared=true; return VOX_OK;
    } catch (...) { s->prepared=false; return VOX_E_INTERNAL; }
}
VoxResult VOX_CALL setAll(VoxInstance s, const float* v, uint32_t n) {
    if (!s || !v || n != count) return VOX_E_INVALID_ARG;
    for (uint32_t i=0;i<n;++i) if (!valueValid(i,v[i])) return VOX_E_INVALID_ARG;
    for (uint32_t i=0;i<n;++i) s->engine.setParameterByIndex(i,v[i]);
    return VOX_OK;
}
VoxResult VOX_CALL reset(VoxInstance s) { if (!s) return VOX_E_INVALID_ARG; s->engine.reset(); return VOX_OK; }
VoxResult VOX_CALL process(VoxInstance s, const VoxProcessContextV2* c) {
    if (!s || !s->prepared || !validStruct(c) || !validStruct(c->transport) ||
        (c->transport->flags & ~VOX_TRANSPORT_KNOWN_FLAGS) ||
        ((c->transport->flags & VOX_TRANSPORT_HAS_TEMPO) &&
         (!std::isfinite(c->transport->tempoBpm) || c->transport->tempoBpm <= 0.0)) ||
        ((c->transport->flags & VOX_TRANSPORT_HAS_PPQ) && !std::isfinite(c->transport->ppqPosition)) ||
        c->numSamples > s->maxBlock ||
        c->outputChannelCount != s->channels || !c->outputs ||
        (c->releaseFlags & ~VOX_KNOWN_RELEASE_FLAGS) || c->paramChangeCount > s->maxChanges ||
        c->midiEventCount > s->maxMidiEvents ||
        !validArray(c->paramChanges,c->paramChangeCount,c->paramChangeStride) ||
        !validArray(c->midiEvents,c->midiEventCount,c->midiEventStride)) return VOX_E_INVALID_ARG;
    for (uint32_t ch=0; ch<s->channels; ++ch) if (!c->outputs[ch]) return VOX_E_INVALID_ARG;
    uint32_t last=0;
    for (uint32_t i=0;i<c->paramChangeCount;++i) {
        const auto p=read(c->paramChanges,c->paramChangeStride,i);
        if (!valueValid(p.index,p.plain) || p.sampleOffset>=c->numSamples || (i && p.sampleOffset<last)) return VOX_E_INVALID_ARG;
        last=p.sampleOffset;
    }
    last=0;
    for (uint32_t i=0;i<c->midiEventCount;++i) {
        const auto m=read(c->midiEvents,c->midiEventStride,i);
        if (m.size == 0 || m.size > sizeof(m.bytes) || m.sampleOffset>=c->numSamples || (i && m.sampleOffset<last)) return VOX_E_INVALID_ARG;
        last=m.sampleOffset;
    }
    const auto flags=c->transport->flags;
    const double bpm=(flags & VOX_TRANSPORT_HAS_TEMPO) ? c->transport->tempoBpm : 0.0;
    const double ppq=(flags & VOX_TRANSPORT_HAS_PPQ) ? c->transport->ppqPosition : 0.0;
    const bool playing=(flags & VOX_TRANSPORT_PLAYING) != 0;
    const double ppqPerSample=(playing && (flags & VOX_TRANSPORT_HAS_PPQ) && bpm > 0.0)
        ? (bpm / (60.0 * s->sampleRate)) : 0.0;
    if (!std::isfinite(ppq + ppqPerSample * c->numSamples)) return VOX_E_INVALID_ARG;
    for (uint32_t ch=0;ch<s->channels;++ch) std::fill_n(c->outputs[ch],c->numSamples,0.0f);
    if (c->releaseFlags & VOX_PANIC_AT_START) s->engine.reset();
    uint32_t cursor=0, pi=0, mi=0;
    std::array<VoxMidiEventV1,maxMidi> midi{};
    while (cursor < c->numSamples) {
        while (pi<c->paramChangeCount) {
            const auto p=read(c->paramChanges,c->paramChangeStride,pi);
            if (p.sampleOffset!=cursor) break;
            s->engine.setParameterByIndex(p.index,p.plain); ++pi;
        }
        uint32_t end=c->numSamples;
        if (pi<c->paramChangeCount) end=read(c->paramChanges,c->paramChangeStride,pi).sampleOffset;
        uint32_t n=0;
        while (mi<c->midiEventCount) {
            const auto m=read(c->midiEvents,c->midiEventStride,mi);
            if (m.sampleOffset>=end) break;
            if (m.size == 3) {
                midi[n].sampleOffset=m.sampleOffset-cursor; midi[n].size=3;
                std::copy_n(m.bytes,3,midi[n].data); ++n;
            }
            ++mi;
        }
        std::array<float*,2> outputs{};
        for (uint32_t ch=0;ch<s->channels;++ch) outputs[ch]=c->outputs[ch]+cursor;
        const double slicePpq=ppq + ppqPerSample * cursor;
        s->engine.process({std::span<float*>(outputs.data(),s->channels),end-cursor,
                           std::span<const VoxMidiEventV1>(midi.data(),n),s->sampleRate,bpm,slicePpq,playing});
        cursor=end;
    }
    if (c->releaseFlags & VOX_RELEASE_ALL_SOUND_AT_END) s->engine.reset();
    else if (c->releaseFlags & VOX_RELEASE_ALL_NOTES_AT_END) s->engine.releaseAllNotes();
    return VOX_OK;
}
VoxResult VOX_CALL save(VoxInstance s, uint8_t* bytes,uint32_t capacity,uint32_t* written,uint32_t* version) {
    if (!s || !written || !version) return VOX_E_INVALID_ARG;
    *written=52; *version=1;
    if (!bytes || capacity<52) return VOX_E_BUFFER_TOO_SMALL;
    uint32_t n=0;
    return s->engine.saveState({reinterpret_cast<std::byte*>(bytes),capacity},n) && n==52 ? VOX_OK : VOX_E_INTERNAL;
}
VoxResult VOX_CALL load(VoxInstance s,uint32_t version,const uint8_t* bytes,uint32_t n) {
    if (!s || (!bytes && n)) return VOX_E_INVALID_ARG;
    if (version!=1) return VOX_E_STATE_VERSION;
    return s->engine.loadState(version,{reinterpret_cast<const std::byte*>(bytes),n}) ? VOX_OK : VOX_E_STATE_CORRUPT;
}
VoxResult VOX_CALL shutdown() { return live.load()==0 ? VOX_OK : VOX_E_INVALID_ARG; }
const VoxModuleV2 module{sizeof(VoxModuleV2),VOX_MODULE_ABI_V2,1,0,getInstrument,create,destroy,prepare,setAll,reset,process,save,load,nullptr,nullptr,shutdown};
}
extern "C" VOX_EXPORT VoxResult VOX_CALL voxModuleEntryV2(uint32_t version,const VoxHostServicesV2* host,const VoxModuleV2** out) {
    if (!out) return VOX_E_INVALID_ARG;
    *out=nullptr;
    if (version!=VOX_MODULE_ABI_V2) return VOX_E_UNSUPPORTED;
    if (!validStruct(host)) return VOX_E_INVALID_ARG;
    *out=&module; return VOX_OK;
}
