#include "engine/SampleController.h"
#include "engine/NoiseRiser.h"
#include "engine/PortablePreset.h"
#include "content/ReviewWaveBAssets.h"
#include <chrono>
#include <cstdio>
#include <vector>
#ifdef VOX_TEST_INTERPOSE_MALLOC
#include "AllocationProbe.h"
#endif
int main(int argc,char** argv){
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
    {
        std::atomic<bool> entered{false},sawCancel{false};
        Controller interruptible(Controller::CancelLoader{[&](const juce::File&,Decoded&,const std::atomic<bool>* stop){
            entered.store(true,std::memory_order_release);
            const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(3);
            while(!stop->load(std::memory_order_relaxed)&&std::chrono::steady_clock::now()<until)std::this_thread::yield();
            sawCancel.store(stop->load(std::memory_order_relaxed),std::memory_order_release);return false;
        }});
        check(interruptible.import(file),"queue cooperatively cancellable sample");
        const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(3);
        while(!entered.load(std::memory_order_acquire)&&std::chrono::steady_clock::now()<until)std::this_thread::yield();
        check(entered.load()&&interruptible.restore({}),"restore supersedes in-flight decode");
        while(!sawCancel.load(std::memory_order_acquire)&&std::chrono::steady_clock::now()<until)std::this_thread::yield();
        check(sawCancel.load()&&interruptible.selection()==0&&interruptible.status()==Controller::Status::builtin,
              "sample worker sees cancellation without resurrecting the superseded import");
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
    // Exercise the bring-your-own-asset workflow on long musical material,
    // not only the two-sample ownership fixture above.
    using namespace vox::fx;
    const auto reviewRoot=argc==2?juce::File::getCurrentWorkingDirectory().getChildFile(juce::String::fromUTF8(argv[1])):juce::File{};
    if(argc==2&&!reviewRoot.createDirectory().wasOk())return 1;
    for(const auto& cue:content::waveb::assetCues){
        const auto root=juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("vox-fx-wave-b-"+juce::Uuid().toString());
        if(!root.createDirectory().wasOk()){check(false,"create Wave B temporary directory");continue;}
        const auto original=root.getChildFile(cue.sourceName);
        const auto relocated=root.getChildFile("relocated.wav");
        const auto other=root.getChildFile("wrong-source.wav");
        const auto src=content::waveb::sourceWav(cue.asset);
        auto wrong=src;wrong[99]^=1;
        check(original.replaceWithData(src.data(),src.size())&&other.replaceWithData(wrong.data(),wrong.size()),"author source and a different valid WAV");
        Controller importing;
        check(importing.import(original)&&importing.waitUntilSettled(std::chrono::seconds(5))&&importing.status()==Controller::Status::ready,
            "import authored musical source");
        auto capture=content::waveb::makeAssetPreview(cue);
        const auto copyAsset=[&](Controller& from,offline::Capture& target){
            Store::Lease lease;
            if(!from.store().acquire(lease,from.selection()))return false;
            target.sample.assign(lease.data(),lease.data()+lease.frames());target.sampleRate=lease.rate();
            from.store().release(lease);return true;
        };
        std::vector<std::uint8_t> before,after;
        check(copyAsset(importing,capture)&&offline::renderWav(capture,before),"render sample-backed authored cue");
        portable::Sound value,restored;
        value.state.program=capture.program;value.state.macros=capture.settings.macroRoutes;value.sample=importing.reference();
        for(unsigned i=0;i<4;++i){
            const auto fx=capture.settings.effects[i];const auto shift=i*16;
            value.state.effects=(value.state.effects&~(std::uint64_t{0xffff}<<shift))|(std::uint64_t{effects::pack(fx)}<<shift);
        }
        const auto& entry=cue.sound.entry;
        portable::Tags tags{std::string(entry.name),std::string(entry.style),std::string(entry.mood),std::string(content::useCase(entry)),static_cast<unsigned>(entry.role)};
        std::vector<std::byte> portableBytes;
        check(portable::encode(value,portableBytes,&tags)&&portable::decode(portableBytes,restored)&&restored.sample==value.sample&&
              restored.state.macros==value.state.macros&&restored.state.program.layers[0].grains==value.state.program.layers[0].grains,
              "portable cue retains asset hash, FPRG7 grain shape and FMOD3 envelope");
        check(original.moveFileTo(relocated),"simulate transfer to a different asset path");
        Controller reopening;
        check(reopening.restore(restored.sample)&&reopening.waitUntilSettled(std::chrono::seconds(5))&&reopening.status()==Controller::Status::missing,
              "original path absent; reopen must not silently switch source");
        check(reopening.relink(other,restored.sample)&&reopening.waitUntilSettled(std::chrono::seconds(5))&&reopening.selection()==1,
              "reject a different musical WAV by hash");
        check(reopening.relink(relocated,restored.sample)&&reopening.waitUntilSettled(std::chrono::seconds(5))&&
              reopening.reference().sha256==value.sample.sha256&&reopening.selection()>1,"relink exact asset at new path");
        auto reopened=content::waveb::makeAssetPreview(cue);
        reopened.program=restored.state.program;reopened.settings.macroRoutes=restored.state.macros;
        // Reference settings carry host FX wet/amount bases, not portable VXUP.
        check(copyAsset(reopening,reopened)&&offline::renderWav(reopened,after)&&after==before,"restored and relocated cue renders byte-exactly");
        double peak=0;
        for(std::size_t i=44;i+2<after.size();i+=3){
            const int raw=after[i]|after[i+1]<<8|after[i+2]<<16;
            peak=std::max(peak,std::abs((raw^0x800000)-0x800000)/8388607.);
        }
        check(peak>.001&&peak<.5,"audible and bounded relocated fixture");
        std::printf("Wave B external asset cue %.*s peak %.6f source SHA-256 %s\n",
            static_cast<int>(entry.id.size()),entry.id.data(),peak,value.sample.sha256.c_str());
        if(argc==2&&after==before&&peak>.001){
            // A review package intentionally omits the build-machine's temp
            // path. On opening, choose the distributed WAV with Relink.
            value.sample.path=cue.sourceName;
            std::vector<std::byte> shared;
            const auto id=juce::String::fromUTF8(entry.id.data(),static_cast<int>(entry.id.size()));
            check(portable::encode(value,shared,&tags)&&
                  reviewRoot.getChildFile(id+".voxfx").replaceWithData(shared.data(),shared.size())&&
                  reviewRoot.getChildFile(cue.sourceName).replaceWithData(src.data(),src.size())&&
                  reviewRoot.getChildFile(id+".wav").replaceWithData(after.data(),after.size()),"export Wave B audition and hash-relink package");
        }
        check(root.deleteRecursively(),"remove authored source fixture files");
    }
    check(file.deleteFile(),"remove fixture");
    std::printf("FX sample controller: %d failure(s)\n",failures);return failures?1:0;
}
