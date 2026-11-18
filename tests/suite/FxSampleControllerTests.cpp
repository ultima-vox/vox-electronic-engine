#include "engine/SampleController.h"
#include <chrono>
#include <cstdio>
#include <vector>
int main(){
    using namespace vox::fx::sample;int failures=0;auto check=[&](bool ok,const char* s){if(!ok){std::printf("FAIL: %s\n",s);++failures;}};
    const std::vector<std::uint8_t> wav{'R','I','F','F',40,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,1,0,
        0x80,0xbb,0,0,0,0x77,1,0,2,0,16,0,'d','a','t','a',4,0,0,0,0,0x80,0xff,0x7f};
    const auto file=juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("vox-fx-sample-controller",".wav",false);
    check(file.replaceWithData(wav.data(),wav.size()),"write fixture");
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
    check(controller.restore(mismatch)&&controller.restore({})&&controller.waitUntilSettled(std::chrono::seconds(5))&&
          controller.reference().kind==Reference::Kind::builtin&&controller.selection()==0,"superseded worker cannot restore old reference");
    check(held.data()[0]==-1,"switching to built-in retains active lease");controller.store().release(held);
    check(file.deleteFile(),"remove fixture");
    std::printf("FX sample controller: %d failure(s)\n",failures);return failures?1:0;
}
