#include "instrument/abi/VoxModuleAbiV2.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#ifdef VOX_BASS_ALLOCATION_PROBE
extern "C" void voxProbeBegin(void);
extern "C" unsigned long voxProbeEnd(void);
#endif

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } } while (0)
int main(int argc,char** argv) {
    CHECK(argc==2);
#if defined(_WIN32)
    auto lib=LoadLibraryExA(argv[1],nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    CHECK(lib);
    auto entry=reinterpret_cast<VoxModuleEntryV2Fn>(GetProcAddress(lib,VOX_MODULE_ENTRY_V2_NAME));
#else
    void* lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
    if (!lib) { std::fprintf(stderr,"%s\n",dlerror()); return 1; }
    auto entry=reinterpret_cast<VoxModuleEntryV2Fn>(dlsym(lib,VOX_MODULE_ENTRY_V2_NAME));
#endif
    CHECK(entry);
    VoxHostServicesV2 host{sizeof(host),VOX_MODULE_ABI_V2,nullptr,nullptr}; const VoxModuleV2* module=nullptr;
    CHECK(entry(VOX_MODULE_ABI_V2,&host,&module)==VOX_OK && module);
    const VoxInstrumentDescriptorV2* desc=nullptr; CHECK(module->getInstrument(0,&desc)==VOX_OK);
    CHECK(desc->parameterCount==13 && !std::strcmp(desc->id,"com.ultimavox.fullon-bass"));
    std::array<float,13> values{};
    for (unsigned i=0;i<13;++i) { CHECK(desc->parameters[i].poolIndex==static_cast<int>(i)); values[i]=desc->parameters[i].defaultValue; }
    VoxCreateParamsV2 create{sizeof(create),VOX_MODULE_ABI_V2,0,0}; VoxInstance a=nullptr,b=nullptr;
    CHECK(module->create(desc->id,&create,&a)==VOX_OK); CHECK(module->create(desc->id,&create,&b)==VOX_OK);
    VoxPrepareSpecV2 spec{sizeof(spec),VOX_MODULE_ABI_V2,48000,256,2,13,128};
    CHECK(module->prepare(a,&spec)==VOX_OK && module->prepare(b,&spec)==VOX_OK);
    CHECK(module->setAllParameters(a,values.data(),13)==VOX_OK && module->setAllParameters(b,values.data(),13)==VOX_OK);
    std::array<uint8_t,52> saved{}, saved2{}; uint32_t written=0,version=0;
    CHECK(module->saveState(a,saved.data(),52,&written,&version)==VOX_OK && written==52 && version==1);
    for (unsigned i=0;i<13;++i) {
        auto edited=values; edited[i]=desc->parameters[i].maximum;
        CHECK(module->setAllParameters(a,edited.data(),13)==VOX_OK);
        std::array<uint8_t,52> changed{};
        CHECK(module->saveState(a,changed.data(),52,&written,&version)==VOX_OK);
        for (unsigned j=0;j<13;++j)
            if (i!=j) CHECK(std::memcmp(changed.data()+j*4,saved.data()+j*4,4)==0);
        auto invalid=edited; invalid[i]=NAN;
        CHECK(module->setAllParameters(a,invalid.data(),13)==VOX_E_INVALID_ARG);
        std::array<uint8_t,52> after{};
        CHECK(module->saveState(a,after.data(),52,&written,&version)==VOX_OK && after==changed);
    }
    CHECK(module->loadState(a,1,saved.data(),52)==VOX_OK);
    CHECK(module->loadState(b,version,saved.data(),52)==VOX_OK);
    CHECK(module->saveState(b,saved2.data(),52,&written,&version)==VOX_OK && saved==saved2);
    std::array<float,256> left{},right{},left2{},right2{}; float* out[]{left.data(),right.data()}; float* out2[]{left2.data(),right2.data()};
    VoxMidiEventV2 on{0,3,{0x90,36,127}};
    VoxProcessContextV2 ctx{sizeof(ctx),VOX_MODULE_ABI_V2,256,2,out,nullptr,0,sizeof(VoxParamChangeV2),&on,1,sizeof(on),0,0,0};
    CHECK(module->process(a,&ctx)==VOX_OK); ctx.outputs=out2; CHECK(module->process(b,&ctx)==VOX_OK);
    CHECK(left==left2 && right==right2 && left==right);
    CHECK(std::any_of(left.begin(),left.end(),[](float x){return std::abs(x)>1e-5f;}));
    // Short MIDI messages can reach the Rack even when Bass does not use them.
    // They must not fault the module or prevent subsequent notes from playing.
    CHECK(module->reset(a)==VOX_OK);
    std::array<VoxMidiEventV2, 3> shortMidi{{{0, 1, {0xf8}}, {1, 2, {0xc0, 4}}, {2, 3, {0x90, 36, 127}}}};
    ctx.outputs=out; ctx.midiEvents=shortMidi.data(); ctx.midiEventCount=3;
    CHECK(module->process(a,&ctx)==VOX_OK);
    CHECK(std::any_of(left.begin(),left.end(),[](float x){return std::abs(x)>1e-5f;}));
    VoxParamChangeV2 change{0,128,1200.0f,0};
    ctx.outputs=out; ctx.midiEvents=nullptr; ctx.midiEventCount=0;
    ctx.paramChanges=&change; ctx.paramChangeCount=1;
    CHECK(module->process(a,&ctx)==VOX_OK);
    std::array<uint8_t,52> automated{}, other{};
    CHECK(module->saveState(a,automated.data(),52,&written,&version)==VOX_OK);
    CHECK(std::memcmp(automated.data(),saved.data(),4)!=0);
    CHECK(std::memcmp(automated.data()+4,saved.data()+4,48)==0);
    CHECK(module->saveState(b,other.data(),52,&written,&version)==VOX_OK && other==saved);
    ctx.paramChangeCount=14;
    CHECK(module->process(a,&ctx)==VOX_E_INVALID_ARG);
    ctx.paramChangeCount=1; change.plain=NAN;
    CHECK(module->process(a,&ctx)==VOX_E_INVALID_ARG);
    CHECK(module->saveState(a,other.data(),52,&written,&version)==VOX_OK && other==automated);
    ctx.paramChanges=nullptr; ctx.paramChangeCount=0;
    CHECK(module->reset(b)==VOX_OK); ctx.outputs=out2; ctx.midiEvents=nullptr; ctx.midiEventCount=0; CHECK(module->process(b,&ctx)==VOX_OK);
    CHECK(std::all_of(left2.begin(),left2.end(),[](float x){return x==0.0f;}));
    ctx.outputs=out; ctx.releaseFlags=VOX_PANIC_AT_START; CHECK(module->process(a,&ctx)==VOX_OK);
    CHECK(std::all_of(left.begin(),left.end(),[](float x){return x==0.0f;}));
    // Restore edited state after rendering, then reprepare at another rate.
    CHECK(module->loadState(a,1,automated.data(),52)==VOX_OK);
    CHECK(module->loadState(b,1,automated.data(),52)==VOX_OK);
    spec.sampleRate=44100;
    CHECK(module->prepare(a,&spec)==VOX_OK && module->prepare(b,&spec)==VOX_OK);
    ctx.releaseFlags=0; ctx.midiEvents=&on; ctx.midiEventCount=1;
    ctx.outputs=out; CHECK(module->process(a,&ctx)==VOX_OK);
    ctx.outputs=out2; CHECK(module->process(b,&ctx)==VOX_OK);
    CHECK(left==left2 && right==right2);
    CHECK(std::any_of(left.begin(),left.end(),[](float x){return std::abs(x)>1e-5f;}));
    CHECK(module->loadState(b,1,saved.data(),52)==VOX_OK);
    CHECK(module->prepare(b,&spec)==VOX_OK);
    CHECK(module->process(b,&ctx)==VOX_OK);
    CHECK(left!=left2); // Original cutoff must render differently from restored edit.
#ifdef VOX_BASS_ALLOCATION_PROBE
    ctx.releaseFlags=0; ctx.outputs=out;
    change={0,128,1200.0f,0}; ctx.paramChanges=&change; ctx.paramChangeCount=1;
    ctx.midiEvents=&on; ctx.midiEventCount=1;
    CHECK(module->process(a,&ctx)==VOX_OK);
    voxProbeBegin();
    bool processOk=true;
    for (int i=0;i<1000;++i) processOk = module->process(a,&ctx)==VOX_OK && processOk;
    const auto allocations=voxProbeEnd();
    CHECK(processOk && allocations==0);
#endif
    // Block-end release flags must preserve the CC123/CC120 distinction.
    values[7]=250.0f;
    CHECK(module->setAllParameters(a,values.data(),13)==VOX_OK);
    CHECK(module->setAllParameters(b,values.data(),13)==VOX_OK);
    CHECK(module->reset(a)==VOX_OK && module->reset(b)==VOX_OK);
    std::array<VoxMidiEventV2,2> held{{{0,3,{0x90,36,127}}, {0,3,{0xb0,64,127}}}};
    ctx.paramChanges=nullptr; ctx.paramChangeCount=0; ctx.releaseFlags=0;
    ctx.midiEvents=held.data(); ctx.midiEventCount=2; ctx.outputs=out;
    CHECK(module->process(a,&ctx)==VOX_OK); ctx.outputs=out2; CHECK(module->process(b,&ctx)==VOX_OK);
    ctx.midiEvents=nullptr; ctx.midiEventCount=0; ctx.releaseFlags=VOX_RELEASE_ALL_NOTES_AT_END;
    ctx.outputs=out; CHECK(module->process(a,&ctx)==VOX_OK);
    ctx.releaseFlags=0; ctx.outputs=out2; CHECK(module->process(b,&ctx)==VOX_OK);
    CHECK(left==left2);
    ctx.outputs=out; CHECK(module->process(a,&ctx)==VOX_OK);
    std::array<VoxMidiEventV2,2> release{{{0,3,{0x80,36,0}}, {0,3,{0xb0,64,0}}}};
    ctx.outputs=out2; ctx.midiEvents=release.data(); ctx.midiEventCount=2;
    CHECK(module->process(b,&ctx)==VOX_OK);
    CHECK(left==left2 && std::any_of(left.begin(),left.end(),[](float x){return std::abs(x)>1e-5f;}));
    ctx.outputs=out; ctx.midiEvents=nullptr; ctx.midiEventCount=0;
    ctx.releaseFlags=VOX_RELEASE_ALL_SOUND_AT_END | VOX_RELEASE_ALL_NOTES_AT_END;
    CHECK(module->process(a,&ctx)==VOX_OK);
    ctx.releaseFlags=0; CHECK(module->process(a,&ctx)==VOX_OK);
    CHECK(std::all_of(left.begin(),left.end(),[](float x){return x==0.0f;}));
    ctx.midiEvents=&on; ctx.midiEventCount=1;
    CHECK(module->process(a,&ctx)==VOX_OK);
    CHECK(std::any_of(left.begin(),left.end(),[](float x){return std::abs(x)>1e-5f;}));
    CHECK(module->destroy(a)==VOX_OK && module->destroy(b)==VOX_OK && module->shutdown()==VOX_OK);
#if defined(_WIN32)
    CHECK(FreeLibrary(lib)!=0);
#else
    CHECK(dlclose(lib)==0);
#endif
    std::puts("Full-on Bass V2 real dynamic module smoke PASS");
}
