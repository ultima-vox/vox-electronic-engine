#include "engine/FileExport.h"
#include <cstdio>
#include <cstring>
#include <condition_variable>
#include <mutex>
#include <chrono>
#include <vector>

int main() {
    using namespace vox::fx;
    int failures=0;
    const auto check=[&](bool ok,const char* message){if(!ok){std::printf("FAIL: %s\n",message);++failures;}};
    const auto dir=juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("vox-fx-export-test","",false);
    if(!dir.createDirectory().wasOk()){std::puts("FAIL: temporary directory");return 1;}
    const auto path=dir.getChildFile("output.wav");
    const std::vector<std::uint8_t> original{1,2,3,4};
    check(fileexport::writeBytes(path,original),"write original file");
    fileexport::Cancellation cancel;cancel.request();
    check(!fileexport::writeBytes(path,{5,6,7},&cancel),"pre-cancelled write refused");
    check(path.loadFileAsString().getNumBytesAsUTF8()==4,"cancelled write preserves original");
    offline::Capture capture;capture.settings.duration=.05f;
    std::vector<std::uint8_t> expected;
    check(offline::renderWav(capture.program,capture.settings,expected),"reference render");
    {
        fileexport::Job job(capture,path);job.wait();
        check(job.result()==fileexport::Result::success && job.progress()==1.0f,"worker reports completed render");
    }
    juce::MemoryBlock actual;check(path.loadFileAsData(actual),"load worker output");
    check(actual.getSize()==expected.size() && std::memcmp(actual.getData(),expected.data(),expected.size())==0,
          "worker output equals captured WAV bytes");
    auto invalid=capture;invalid.settings.duration=33;
    {fileexport::Job job(invalid,path);job.wait();check(job.result()==fileexport::Result::failure,"invalid render reported");}
    juce::MemoryBlock preserved;check(path.loadFileAsData(preserved) && preserved==actual,"failed render preserves target");
    {fileexport::Job job(capture,dir.getChildFile("missing/output.wav"));job.wait();
        check(job.result()==fileexport::Result::failure,"write failure reported");}
    std::atomic<float> progress{0};
    check(fileexport::renderFile(capture,path,cancel,progress)==fileexport::Result::cancelled,"pre-cancelled render reported");
    juce::MemoryBlock afterCancel;check(path.loadFileAsData(afterCancel)&&afterCancel==actual,"cancelled render preserves target");
    const auto collision=dir.getChildFile("directory.wav");
    check(collision.createDirectory().wasOk()&&collision.getChildFile("keep.txt").replaceWithText("keep"),"create directory collision fixture");
    {fileexport::Job job(capture,collision);job.wait();check(job.result()==fileexport::Result::failure&&
      collision.getChildFile("keep.txt").loadFileAsString()=="keep","directory target is never replaced");}
    fileexport::Cancellation failed;
    check(fileexport::renderFile(capture,collision,failed,progress)==fileexport::Result::failure&&
          !failed.request(),"failure seals cancellation before completion is published");
    fileexport::Cancellation committing;
    check(committing.beginCommit()&&!committing.request()&&!committing.cancelled(),"cancel cannot claim success once replacement starts");
    {
        struct Gate {
            std::mutex mutex;std::condition_variable condition;std::size_t bytes=0;bool resume=false;
        } gate;
        auto longer=capture;longer.settings.duration=.5f;
        const auto holdWrite=[](void* context,std::size_t count) noexcept {
            auto& g=*static_cast<Gate*>(context);std::unique_lock lock(g.mutex);g.bytes=count;
            g.condition.notify_all();g.condition.wait(lock,[&]{return g.resume;});
        };
        fileexport::Job job(longer,path,holdWrite,&gate);
        {
            std::unique_lock lock(gate.mutex);
            const bool entered=gate.condition.wait_for(lock,std::chrono::seconds(5),[&]{return gate.bytes>0;});
            check(entered&&gate.bytes==16384&&job.cancel(),"cancel wins after first successful write chunk, before commit");
            if(!entered)(void)job.cancel();
            gate.resume=true;
        }
        gate.condition.notify_all();job.wait();
        juce::MemoryBlock kept;
        check(job.result()==fileexport::Result::cancelled&&path.loadFileAsData(kept)&&kept==actual,
              "mid-write cancellation keeps destination byte-identical");
        check(dir.findChildFiles(juce::File::findFiles,false).size()==1,"cancel removes partial temporary file");
        gate.bytes=0;gate.resume=false;
        const auto midiPath=dir.getChildFile("original.mid");
        check(midiPath.replaceWithText("original MIDI"),"prepare MIDI overwrite sentinel");
        pattern::Pattern pattern;pattern.steps[0].enabled=true;
        fileexport::Job midi(pattern,125.,midiPath,holdWrite,&gate);
        {
            std::unique_lock lock(gate.mutex);
            const bool entered=gate.condition.wait_for(lock,std::chrono::seconds(5),[&]{return gate.bytes>0;});
            check(entered&&midi.cancel(),"MIDI can be cancelled after writing but before replacement");
            if(!entered)(void)midi.cancel();
            gate.resume=true;
        }
        gate.condition.notify_all();midi.wait();
        check(midi.status().kind==fileexport::Kind::midi&&midi.result()==fileexport::Result::cancelled&&
              midiPath.loadFileAsString()=="original MIDI"&&dir.findChildFiles(juce::File::findFiles,false).size()==2,
              "MIDI cancellation preserves old destination and removes temporary bytes");
        gate.bytes=0;gate.resume=false;
        pattern.bars=4;pattern.grid=pattern::Grid::thirtysecondTriplet;
        for(auto& step:pattern.steps){step.enabled=true;step.ratchets=4;}
        // Hold the encoder after one complete MIDI event, with no file open.
        fileexport::Job encoding(pattern,125.,midiPath,nullptr,&gate,holdWrite);
        {
            std::unique_lock lock(gate.mutex);
            const bool entered=gate.condition.wait_for(lock,std::chrono::seconds(5),[&]{return gate.bytes>0;});
            check(entered&&gate.bytes==1&&dir.findChildFiles(juce::File::findFiles,false).size()==2&&encoding.cancel(),
                  "active maximum-size SMF encoding cooperatively cancels before opening a temporary file");
            if(!entered)(void)encoding.cancel();
            gate.resume=true;
        }
        gate.condition.notify_all();encoding.wait();
        check(encoding.result()==fileexport::Result::cancelled&&midiPath.loadFileAsString()=="original MIDI"&&
              dir.findChildFiles(juce::File::findFiles,false).size()==2,"cancelled encoding leaves no temporary MIDI bytes");
    }
    auto longCapture=capture;longCapture.settings.duration=32;
    {fileexport::Job job(longCapture,path);job.cancel();job.wait();
        // If the thread finished before cancellation, success is legitimate.
        check(job.result()==fileexport::Result::cancelled || job.result()==fileexport::Result::success,
              "worker cancellation or completed render reported");}
    check(dir.deleteRecursively(),"remove test directory");
    std::printf("FX file export: %d failure(s)\n",failures);return failures?1:0;
}
