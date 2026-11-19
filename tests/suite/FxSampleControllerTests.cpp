#include "engine/SampleController.h"
#include "engine/NoiseRiser.h"
#include <chrono>
#include <cstdio>
#include <vector>
#ifdef VOX_TEST_INTERPOSE_MALLOC
#include "AllocationProbe.h"
#endif
int main(){
    using namespace vox::fx::sample;int failures=0;auto check=[&](bool ok,const char* s){if(!ok){std::printf("FAIL: %s\n",s);++failures;}};
    const std::vector<std::uint8_t> wav{'R','I','F','F',40,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,1,0,
        0x80,0xbb,0,0,0,0x77,1,0,2,0,16,0,'d','a','t','a',4,0,0,0,0,0x80,0xff,0x7f};
    const auto file=juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("vox-fx-sample-controller",".wav",false);
    check(file.replaceWithData(wav.data(),wav.size()),"write fixture");
    {
        std::mutex mutex;std::condition_variable condition;bool entered=false,released=false;
        Controller blocked([&](const juce::File& path,Decoded& decoded){
            {std::unique_lock<std::mutex> lock(mutex);entered=true;condition.notify_all();condition.wait(lock,[&]{return released;});}
            return loadWav(path,decoded);
        });
        check(blocked.import(file),"start gated import");
        {std::unique_lock<std::mutex> lock(mutex);condition.wait(lock,[&]{return entered;});}
        auto pending=blocked.reference();
        check(pending.kind==Reference::Kind::pending&&pending.path==file.getFullPathName().toStdString()&&blocked.selection()==1,
              "pending import persists intended identity as unavailable");
        std::vector<std::byte> state;Reference decoded;
        check(referencecodec::encode(pending,state)&&referencecodec::decode(state,decoded)&&decoded==pending,"pending save roundtrip");
        Controller restored;check(restored.restore(decoded)&&restored.reference()==pending&&restored.selection()==1&&
            restored.status()==Controller::Status::missing,"restored unverified import remains unavailable");
        const auto missing=file.getSiblingFile("no-such-vox-fx-sample.wav");
        check(blocked.import(missing),"supersede gated import");
        check(blocked.reference().path==missing.getFullPathName().toStdString()&&blocked.selection()==1,"superseding save sees newest pending import");
        {const std::lock_guard<std::mutex> lock(mutex);released=true;}condition.notify_all();
        check(blocked.waitUntilSettled(std::chrono::seconds(5))&&blocked.status()==Controller::Status::failed&&
              blocked.reference().kind==Reference::Kind::builtin&&blocked.selection()==0,"failed superseding import rolls back original selection");
    }
    Controller controller;check(controller.selection()==0 && controller.reference().kind==Reference::Kind::builtin,"default built-in mode");
    check(controller.import(file),"queue sample import");
    check(controller.waitUntilSettled(std::chrono::seconds(5))&&controller.status()==Controller::Status::ready,"sample imported on worker");
    const auto ref=controller.reference();const auto token=controller.selection();
    check(ref.valid()&&ref.kind==Reference::Kind::external&&token>1,"import commits reference and token");
    Store::Lease held;check(controller.store().acquire(held,token)&&held.frames()==2&&held.data()[0]==-1,"published decoded content");
    auto mismatch=ref;mismatch.sha256[0]=mismatch.sha256[0]=='a'?'b':'a';
    Controller::PreparedRestore prepared;
    check(Controller::prepareRestore(mismatch,prepared),"prepare validated restore off audio thread");
    controller.commitRestore(std::move(prepared));
    check(controller.selection()==1,"restored reference immediately disables old sample");
    check(controller.waitUntilSettled(std::chrono::seconds(5))&&controller.status()==Controller::Status::missing&&
          controller.reference()==mismatch&&controller.selection()==1,"hash mismatch remains explicitly missing");
    Store::Lease absent;check(!controller.store().acquire(absent,controller.selection())&&held.data()[0]==-1,"old lease lives, new mismatch has no fallback");
    check(controller.restore(ref)&&controller.waitUntilSettled(std::chrono::seconds(5))&&controller.status()==Controller::Status::ready,
          "matching restored reference loads");
    const auto restoredToken=controller.selection();
    check(restoredToken>1&&restoredToken!=token,"restored content has new generation");
    auto missing=file.getSiblingFile("no-such-vox-fx-sample.wav");
    check(controller.import(missing)&&controller.waitUntilSettled(std::chrono::seconds(5))&&
          controller.status()==Controller::Status::failed&&controller.reference()==ref&&controller.selection()==restoredToken,
          "failed import preserves previously selected content");
    {
        const auto copy=file.getNonexistentSibling(false);
        const auto different=file.getSiblingFile(file.getFileNameWithoutExtension()+"-different.wav").getNonexistentSibling(false);
        auto changed=wav;changed.back()^=1;
        check(copy.replaceWithData(wav.data(),wav.size())&&different.replaceWithData(changed.data(),changed.size()),
              "write relocated originals and valid different audio");
        check(controller.relink(different,ref)&&controller.waitUntilSettled(std::chrono::seconds(5))&&
              controller.status()==Controller::Status::failed&&controller.reference()==ref&&controller.selection()==restoredToken,
              "relink rejects a valid WAV with the wrong digest without replacing a ready generation");
        auto moved=ref;moved.path=missing.getFullPathName().toStdString();
        check(controller.restore(moved)&&controller.waitUntilSettled(std::chrono::seconds(5))&&controller.selection()==1,
              "original pathname missing");
        check(!controller.relink(copy,ref),"a stale chooser cannot relink another reference");
        check(controller.relink(copy,moved)&&controller.waitUntilSettled(std::chrono::seconds(5))&&controller.selection()>1&&
              controller.reference().sha256==ref.sha256&&controller.reference().path==copy.getFullPathName().toStdString(),
              "matching relocation publishes a new path and generation");
        check(copy.deleteFile()&&different.deleteFile(),"remove relink fixtures");
    }
    check(controller.restore(mismatch)&&controller.restore({})&&controller.waitUntilSettled(std::chrono::seconds(5))&&
          controller.reference().kind==Reference::Kind::builtin&&controller.selection()==0,"superseded worker cannot restore old reference");
    check(held.data()[0]==-1,"switching to built-in retains active lease");controller.store().release(held);
    {
        Controller concurrent;auto engine=std::make_unique<vox::fx::NoiseRiser>();engine->prepare(48000);engine->attachSampleStore(&concurrent.store());
        vox::fx::NoiseProgram program;program.layers[0].family=vox::fx::NoiseFamily::granular;engine->setProgram(program);
        std::atomic<bool> done{false},finite{true};std::atomic<unsigned long long> audioAllocations{0};std::atomic<unsigned> iterations{0};
        std::thread audio([&]{float l[64]{},r[64]{};
#ifdef VOX_TEST_INTERPOSE_MALLOC
            voxProbeBegin();
#endif
            while(!done.load(std::memory_order_acquire)){
                auto generation=concurrent.selection();engine->noteOn(1,60,1,.05,generation!=0,generation);
                engine->render(l,r,64);for(float v:l)if(!std::isfinite(v))finite.store(false,std::memory_order_relaxed);
                engine->allSoundOff(0);engine->render(l,r,64);iterations.fetch_add(1,std::memory_order_relaxed);
            }
#ifdef VOX_TEST_INTERPOSE_MALLOC
            audioAllocations.store(voxProbeEnd(),std::memory_order_relaxed);
#endif
        });
        while(iterations.load(std::memory_order_relaxed)==0)std::this_thread::yield();
        for(int i=0;i<300;++i){
            if(i%3==0)check(concurrent.import(file),"concurrent import accepted");
            else if(i%3==1)check(concurrent.restore(ref),"concurrent restore accepted");
            else check(concurrent.restore({}),"concurrent builtin accepted");
            const auto snapshot=concurrent.reference();check(snapshot.valid(),"concurrent state snapshot valid");
        }
        done.store(true,std::memory_order_release);audio.join();
        check(iterations.load(std::memory_order_relaxed)>0&&finite.load(std::memory_order_relaxed),"concurrent note on/import/restore output finite");
#ifdef VOX_TEST_INTERPOSE_MALLOC
        if(voxProbeAvailable())check(audioAllocations.load(std::memory_order_relaxed)==0,"concurrent MIDI render allocates nothing");
#endif
    }
    check(file.deleteFile(),"remove fixture");
    std::printf("FX sample controller: %d failure(s)\n",failures);return failures?1:0;
}
