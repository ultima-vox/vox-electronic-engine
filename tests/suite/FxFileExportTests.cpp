#include "engine/FileExport.h"
#include <juce_cryptography/juce_cryptography.h>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#elif JUCE_LINUX
 #include <sys/stat.h>
 #include <unistd.h>
#endif

namespace {
struct Hold {
    std::mutex mutex;
    std::condition_variable cv;
    std::size_t count=0;
    bool resume=false;
    static void pause(void* context,std::size_t count) noexcept {
        auto& hold=*static_cast<Hold*>(context);
        std::unique_lock lock(hold.mutex);hold.count=count;
        hold.cv.notify_all();hold.cv.wait(lock,[&]{return hold.resume;});
    }
    bool entered() {
        std::unique_lock lock(mutex);
        return cv.wait_for(lock,std::chrono::seconds(5),[&]{return count>0;});
    }
    void release() {
        {const std::lock_guard lock(mutex);resume=true;}
        cv.notify_all();
    }
};
}

int main() {
    using namespace vox::fx;
    using namespace fileexport;
    int failures=0;
    const auto check=[&](bool ok,const char* message){if(!ok){std::printf("FAIL: %s\n",message);++failures;}};
    const auto dir=juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("vox-fx-export-test","",false);
    if(!dir.createDirectory().wasOk()){std::puts("FAIL: temporary directory");return 1;}
    const auto children=[&]{return dir.findChildFiles(juce::File::findFilesAndDirectories,false).size();};
    const auto same=[](const juce::File& path,const auto& bytes){
        return path.existsAsFile()&&path.getSize()==static_cast<juce::int64>(bytes.size())&&juce::SHA256(path)==juce::SHA256(bytes.data(),bytes.size());
    };
    offline::Capture wav;wav.settings.duration=.075f;
    auto longer=wav;longer.settings.duration=.5f;
    auto invalid=wav;invalid.settings.duration=33;
    pattern::Pattern smf;smf.steps[0].enabled=true;
    std::vector<std::uint8_t> waveBytes,midiBytes;
    check(offline::renderWav(wav,waveBytes)&&pattern::exportMidi(smf,125.,midiBytes),"generate expected WAV and SMF independently");
    const std::vector<std::uint8_t> other{0x41,0x42,0x43,0x44};
    for(const auto kind:{Kind::wav,Kind::midi}){
        const bool audio=kind==Kind::wav;
        const auto& expected=audio?waveBytes:midiBytes;
        const auto extension=audio?".wav":".mid";
        const auto path=dir.getChildFile(juce::String::fromUTF8("caf\xc3\xa9 new file")+extension);
        const auto different=dir.getChildFile(juce::String("other")+extension);
        const auto launch=[&](const juce::File& target,Hold* hold=nullptr){
            if(audio)return std::make_unique<Job>(hold?longer:wav,target,hold?Hold::pause:nullptr,hold);
            return std::make_unique<Job>(smf,125.,target,hold?Hold::pause:nullptr,hold);
        };
        const auto before=children();
        auto first=launch(path);first->wait();
        check(first->result()==Result::success&&first->progress()==1.f&&same(path,expected),"fresh Unicode path has exact expected size and SHA-256");
        check(children()==before+1,"successful publication leaves just the new file");
        check(writeBytes(different,other),"write distinct incumbent payload");
        for(const auto& existing:{path,different}){
            auto repeat=launch(existing);repeat->wait();
            check(repeat->result()==Result::destinationExists&&repeat->progress()==0&&same(existing,existing==path?expected:other),
                  "existing equal or different bytes are refused before rendering or encoding");
        }
        // Occupied at admission cannot become a permitted target when a
        // second process later removes or changes it; the job never retries.
        const auto observed=Destination::observe(different);
        check(different.deleteFile()&&writeBytesTo(observed,expected)==Result::destinationExists&&!different.exists(),
              "occupied observation remains rejected after the incumbent is removed");
        const auto late=dir.getChildFile(juce::String("late")+extension);
        Hold lateWrite;auto racing=launch(late,&lateWrite);
        const bool entered=lateWrite.entered();
        check(entered&&!late.exists()&&late.replaceWithData(other.data(),other.size()),"competing writer wins after observation and staging begins");
        lateWrite.release();racing->wait();
        check(racing->result()==Result::destinationExists&&same(late,other)&&children()==before+2,
              "native publication refuses late target, preserves its size/hash and cleans staging");
        const auto cancelled=dir.getChildFile(juce::String("cancelled")+extension);
        Hold hold;auto mid=launch(cancelled,&hold);
        check(hold.entered()&&mid->cancel(),"cancel after first write before publication");
        hold.release();mid->wait();
        check(mid->result()==Result::cancelled&&!cancelled.exists()&&children()==before+2,"cancellation leaves no target or staging directory");
        if(!audio){
            Hold encoding;Job pending(smf,125.,cancelled,nullptr,&encoding,Hold::pause);
            check(encoding.entered()&&pending.cancel()&&children()==before+2,"cancel during active encoding before staging");
            encoding.release();pending.wait();
            check(pending.result()==Result::cancelled&&!cancelled.exists(),"cancelled encoding leaves no partial SMF");
        }
        const auto collision=dir.getChildFile(juce::String("directory")+extension);
        check(collision.createDirectory().wasOk()&&collision.getChildFile("keep").replaceWithText("untouched"),"prepare directory target");
        auto directory=launch(collision);directory->wait();
        check(directory->result()==Result::destinationExists&&collision.getChildFile("keep").loadFileAsString()=="untouched",
              "existing directory is not changed");
        check(late.setReadOnly(true),"mark incumbent read-only");
        auto readOnly=launch(late);readOnly->wait();
        check(readOnly->result()==Result::destinationExists&&same(late,other),"readonly incumbent remains byte-exact");
        check(late.setReadOnly(false),"restore fixture permission before cleanup");
#if JUCE_WINDOWS
        // Native fixture for the authorized Windows run, not a mocked lock.
        const auto handle=::CreateFileW(late.getFullPathName().toWideCharPointer(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);
        check(handle!=INVALID_HANDLE_VALUE,"hold an exclusive Win32 incumbent handle");
        auto locked=launch(late);locked->wait();
        if(handle!=INVALID_HANDLE_VALUE)::CloseHandle(handle);
        check(locked->result()!=Result::success&&same(late,other),"locked Win32 incumbent remains byte-exact");
#endif
        auto missing=launch(dir.getChildFile("absent-parent").getChildFile(juce::String("output")+extension));missing->wait();
        check(missing->result()==Result::failure&&!dir.getChildFile("absent-parent").exists(),"missing parent fails without creating a fallback path");
#if JUCE_LINUX
        const auto alias=dir.getChildFile(juce::String("dangling")+extension);
        const auto never=dir.getChildFile("missing-link-target");
        check(::symlink(never.getFullPathName().toRawUTF8(),alias.getFullPathName().toRawUTF8())==0,"create dangling link fixture");
        auto badLink=launch(alias);badLink->wait();
        check(badLink->result()==Result::destinationExists&&alias.isSymbolicLink()&&!never.exists(),"dangling leaf remains a link, not a new export");
        check(alias.deleteFile(),"remove dangling test link");
        const auto denied=dir.getChildFile("denied");
        check(denied.createDirectory().wasOk()&&::chmod(denied.getFullPathName().toRawUTF8(),0500)==0,"create denied parent fixture");
        if(::access(denied.getFullPathName().toRawUTF8(),W_OK)!=0){
            auto noAccess=launch(denied.getChildFile(juce::String("blocked")+extension));noAccess->wait();
            check(noAccess->result()==Result::failure&&denied.findChildFiles(juce::File::findFilesAndDirectories,false).isEmpty(),"denied parent stays empty");
        }else std::puts("FX file export: parent denial fixture skipped (process has write privilege)");
        check(::chmod(denied.getFullPathName().toRawUTF8(),0700)==0&&denied.deleteFile(),"remove denied fixture");
#endif
        check(children()==before+3,"only intended incumbents remain, no staging directories");
        std::printf("FX %s create-only: expected bytes=%zu sha256=%s\n",audio?"WAV":"SMF",expected.size(),
            juce::SHA256(expected.data(),expected.size()).toHexString().toRawUTF8());
    }
    const auto fresh=dir.getChildFile("never.wav");
    std::atomic<float> progress{0};Cancellation cancel;
    check(cancel.request()&&renderFile(wav,fresh,cancel,progress)==Result::cancelled&&!fresh.exists(),"pre-cancelled WAV does not render or publish");
    Cancellation failed;
    check(renderFile(invalid,fresh,failed,progress)==Result::failure&&!failed.request()&&!fresh.exists()&&!writeBytes(fresh,{}),"invalid/empty output never publishes");
    Cancellation committing;
    check(committing.beginCommit()&&!committing.request()&&!committing.cancelled(),"cancel cannot win after publication is claimed");
#if JUCE_LINUX
    {
        struct stat a{},b{};
        const auto shm=juce::File("/dev/shm");
        if(::stat(dir.getFullPathName().toRawUTF8(),&a)==0&&::stat(shm.getFullPathName().toRawUTF8(),&b)==0&&a.st_dev!=b.st_dev&&shm.hasWriteAccess()){
            juce::TemporaryFile cross(shm.getNonexistentChildFile("vox-fx-cross-volume",".wav",false));
            const auto staged=cross.getFile();
            check(staged.replaceWithData(other.data(),other.size()),"create cross-device stage");
            check(detail::publishNewFile(staged,fresh)==detail::PublishResult::failure&&!fresh.exists()&&same(staged,other),
                  "native no-replace publication has no cross-device copy fallback");
            std::puts("FX file export: cross-device failure checked");
        }else std::puts("FX file export: cross-device fixture skipped (no writable second filesystem)");
    }
#endif
    juce::File scratch;
    {
        Job staged(wav,dir,nullptr,nullptr,Purpose::drag);staged.wait();
        const auto payload=staged.takeDragFile();
        check(staged.result()==Result::success&&staged.status().purpose==Purpose::drag&&payload,"drag still publishes a dedicated scratch file");
        if(payload){
            scratch=payload->file;check(same(scratch,waveBytes),"prepared drag produces the same WAV payload");
#if JUCE_LINUX
            struct stat attributes{};
            check(::stat(scratch.getParentDirectory().getFullPathName().toRawUTF8(),&attributes)==0&&
                  (attributes.st_mode&0777)==0700,"drag staging directory is private");
#endif
        }
    }
    check(scratch!=juce::File{}&&!scratch.exists()&&!scratch.getParentDirectory().exists(),"unoffered drag scratch directory is removed");
    const auto before=children();Job bad(invalid,dir,nullptr,nullptr,Purpose::drag);bad.wait();
    check(bad.result()==Result::failure&&!bad.takeDragFile()&&children()==before,"failed drag staging leaves no scratch directory");
    check(dir.deleteRecursively(),"remove test directory");
    std::printf("FX file export: %d failure(s)\n",failures);return failures?1:0;
}
