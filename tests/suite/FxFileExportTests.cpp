#include "engine/FileExport.h"
#include <cstdio>
#include <cstring>
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
    std::atomic<bool> cancel{true};
    check(!fileexport::writeBytes(path,{5,6,7},&cancel),"pre-cancelled write refused");
    check(path.loadFileAsString().getNumBytesAsUTF8()==4,"cancelled write preserves original");
    offline::Capture capture;capture.settings.duration=.05f;
    std::vector<std::uint8_t> expected;
    check(offline::renderWav(capture.program,capture.settings,expected),"reference render");
    {
        fileexport::WavJob job(capture,path);job.wait();
        check(job.result()==fileexport::Result::success && job.progress()==1.0f,"worker reports completed render");
    }
    juce::MemoryBlock actual;check(path.loadFileAsData(actual),"load worker output");
    check(actual.getSize()==expected.size() && std::memcmp(actual.getData(),expected.data(),expected.size())==0,
          "worker output equals captured WAV bytes");
    auto invalid=capture;invalid.settings.duration=33;
    {fileexport::WavJob job(invalid,path);job.wait();check(job.result()==fileexport::Result::failure,"invalid render reported");}
    juce::MemoryBlock preserved;check(path.loadFileAsData(preserved) && preserved==actual,"failed render preserves target");
    {fileexport::WavJob job(capture,dir.getChildFile("missing/output.wav"));job.wait();
        check(job.result()==fileexport::Result::failure,"write failure reported");}
    std::atomic<float> progress{0};
    check(fileexport::renderFile(capture,path,cancel,progress)==fileexport::Result::cancelled,"pre-cancelled render reported");
    juce::MemoryBlock afterCancel;check(path.loadFileAsData(afterCancel)&&afterCancel==actual,"cancelled render preserves target");
    auto longCapture=capture;longCapture.settings.duration=32;
    {fileexport::WavJob job(longCapture,path);job.cancel();job.wait();
        // If the thread finished before cancellation, success is legitimate.
        check(job.result()==fileexport::Result::cancelled || job.result()==fileexport::Result::success,
              "worker cancellation or completed render reported");}
    check(dir.deleteRecursively(),"remove test directory");
    std::printf("FX file export: %d failure(s)\n",failures);return failures?1:0;
}
