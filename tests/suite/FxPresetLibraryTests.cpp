#include "engine/PresetLibrary.h"
#include "engine/Preview.h"
#include <chrono>
#include <cstdio>
#include <thread>
#ifdef VOX_TEST_INTERPOSE_MALLOC
#include "AllocationProbe.h"
#endif
int main(){
    using namespace vox::fx;
    int failures=0;auto check=[&](bool pass,const char* message){if(!pass){++failures;std::printf("FAIL: %s\n",message);}};
    const auto root=juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("voxfx-"+juce::Uuid().toString());
    if(root.createDirectory().failed())return 1;
    const auto file=root.getChildFile("user.voxfx");portable::Sound original,decoded;portable::Tags tags{"Orbit","Progressive","Luminous","Transition",1,true},read;
    original.state.program.layers[0].transpose=5;
    check(library::managedFile(root,file)&&!library::managedFile(root,file.getSiblingFile("user.txt")),"managed preset extension and directory guard");
    check(library::write(file,original,&tags)&&library::read(file,decoded,&read)&&read.name==tags.name&&read.favorite&&read.role==1&&
          decoded.state.program.layers[0].transpose==5,"tagged managed preset roundtrip");
    auto invalid=tags;invalid.name="invalid\nname";
    check(!library::write(file,original,&invalid)&&library::read(file,decoded,&read)&&read.name==tags.name,"failed save preserves existing preset");
    check(library::write(file,original)&&library::read(file,decoded,&read)&&read.name.empty(),"legacy portable files remain loadable without tags");
    constexpr source::GlitchShape slices{source::SliceGrid::eighth,source::SliceRead::reverse,5};
    original.state.program.layers[0].family=NoiseFamily::glitch;
    original.state.program.layers[0].glitch=slices;
    check(library::write(file,original,&tags)&&file.getSize()<=static_cast<juce::int64>(portable::maxBytes)&&
          library::read(file,decoded,&read)&&decoded.state.program.layers[0].glitch==slices,
          "bounded on-disk preset update carries an authored FPRG8 Program");
    const auto oversized=root.getChildFile("oversized.voxfx");
    std::vector<std::byte> bytes;
    check(portable::encode(original,bytes,&tags),"build v8 preset for library size boundary");
    bytes.resize(portable::maxBytes+1);
    check(oversized.replaceWithData(bytes.data(),bytes.size())&&!library::read(oversized,decoded,&read)&&
          decoded.state.program.layers[0].glitch==slices&&read.name==tags.name,"oversized input cannot change published preset or tags");
    check(!library::managedFile(file,root)&&!library::managedFile(root,root.getChildFile("nested/deep.voxfx")),"nested/outside objects are not managed files");
    offline::Capture a;a.settings.duration=.16f;a.program.layers[0].family=NoiseFamily::laser;
    preview::Job job(a);
    const auto wait=[&](const preview::Job& j){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(j.result()==preview::Result::loading&&std::chrono::steady_clock::now()<end)std::this_thread::sleep_for(std::chrono::milliseconds(2));};
    wait(job);preview::Bus bus;bus.prepare(96000);
    check(job.result()==preview::Result::ready&&job.publish(bus,bus.ticket())&&bus.playing(),"isolated preview render publishes to audition bus");
    std::array<float,256> l{},r{};double peak=0;
#ifdef VOX_TEST_INTERPOSE_MALLOC
    voxProbeBegin();
#endif
    for(int block=0;block<125;++block){l.fill(.003f);r.fill(.006f);bus.mix(l.data(),r.data(),256);
        for(auto value:l)peak=std::max(peak,std::abs(static_cast<double>(value)));}
#ifdef VOX_TEST_INTERPOSE_MALLOC
    const auto count=voxProbeEnd();if(voxProbeAvailable())check(count==0,"preview lease, resampling, mix and natural end allocate nothing");
#endif
    check(peak>.01&&!bus.playing()&&l.back()==.003f&&r.back()==.006f,"preview mix affects only monitoring and returns to dry on end");
    check(job.publish(bus,bus.ticket()),"publish another preview");bus.mix(l.data(),r.data(),256);bus.stop();
    for(int i=0;i<4;++i){l.fill(.002f);r.fill(.002f);bus.mix(l.data(),r.data(),256);}
    check(!bus.playing()&&l.back()==.002f&&r.back()==.002f,"stop fades back to dry");
    const auto stale=bus.ticket();bus.stop();
    check(!job.publish(bus,stale)&&!bus.playing(),"completed worker cannot restart a stopped audition");
    const auto beforePrepare=bus.ticket();bus.prepare(48000);
    check(!job.publish(bus,beforePrepare)&&!bus.playing(),"rate changes invalidate pending audition jobs");
    a.program.layers[0].family=NoiseFamily::granular;
    preview::Job missing(a,{sample::Reference::Kind::external,root.getChildFile("missing.wav").getFullPathName().toStdString(),std::string(64,'a')});
    wait(missing);check(missing.result()==preview::Result::unavailable&&!missing.publish(bus,bus.ticket()),"unavailable WAV never falls back or changes the preview bus");
    (void)root.deleteRecursively();
    std::printf("FX user library and preview: %d failure(s)\n",failures);return failures?1:0;
}
