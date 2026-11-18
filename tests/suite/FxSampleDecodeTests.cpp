#include "engine/SampleDecode.h"
#include <bit>
#include <cmath>
#include <cstdio>
#include <vector>

int main(){
    using namespace vox::fx::sample;int failures=0;
    const auto check=[&](bool ok,const char* m){if(!ok){std::printf("FAIL: %s\n",m);++failures;}};
    std::vector<std::uint8_t> wav;
    const auto tag=[&](const char* s){for(int i=0;i<4;++i)wav.push_back(static_cast<std::uint8_t>(s[i]));};
    const auto u16=[&](std::uint16_t n){wav.push_back(static_cast<std::uint8_t>(n));wav.push_back(static_cast<std::uint8_t>(n>>8));};
    const auto u32=[&](std::uint32_t n){for(int i=0;i<4;++i)wav.push_back(static_cast<std::uint8_t>(n>>(8*i)));};
    tag("RIFF");u32(44);tag("WAVE");tag("fmt ");u32(16);u16(1);u16(2);u32(48000);u32(192000);u16(4);u16(16);
    tag("data");u32(8);u16(0x8000);u16(0x4000);u16(0x7fff);u16(0x7fff);
    Decoded sample;check(decodeWav(wav,sample),"decode stereo PCM16");
    check(sample.rate==48000&&sample.mono.size()==2&&sample.mono[0]==-.25f&&
          std::abs(sample.mono[1]-32767.0f/32768)<1e-7f,"decoded sample rate and normalized stereo downmix");
    check(sample.sha256=="4ae8cfae71382eeb3df8fb4338a4c13e9dbedba7e1c4bb6ad87fd834c0d17717","SHA-256 identifies exact source bytes");
    const auto fixture=[](std::uint16_t format,std::uint16_t bits,const std::vector<std::uint8_t>& pcm){
        std::vector<std::uint8_t> out;auto text=[&](const char* t){for(int i=0;i<4;++i)out.push_back(static_cast<std::uint8_t>(t[i]));};
        auto w16=[&](std::uint16_t v){out.push_back(static_cast<std::uint8_t>(v));out.push_back(static_cast<std::uint8_t>(v>>8));};
        auto w32=[&](std::uint32_t v){for(int i=0;i<4;++i)out.push_back(static_cast<std::uint8_t>(v>>(8*i)));};
        const auto padding=pcm.size()&1u;text("RIFF");w32(static_cast<std::uint32_t>(36+pcm.size()+padding));text("WAVE");
        text("fmt ");w32(16);w16(format);w16(1);w32(48000);w32(48000*(bits/8));w16(bits/8);w16(bits);
        text("data");w32(static_cast<std::uint32_t>(pcm.size()));out.insert(out.end(),pcm.begin(),pcm.end());if(padding)out.push_back(0);
        return out;};
    Decoded variants;
    check(decodeWav(fixture(1,24,{0,0,128,255,255,127}),variants)&&variants.mono.size()==2&&
          variants.mono[0]==-1 && variants.mono[1]>.999f,"PCM24 sign extension");
    check(decodeWav(fixture(1,32,{0,0,0,128,255,255,255,127}),variants)&&variants.mono[0]==-1&&
          variants.mono[1]>.999f,"PCM32 signed decoding");
    check(decodeWav(fixture(3,32,{0,0,0,63,0,0,128,191}),variants)&&variants.mono[0]==.5f&&variants.mono[1]==-1,
          "float32 decoding");
    const auto before=variants.mono;
    check(!decodeWav(fixture(3,32,{0,0,192,127}),variants)&&variants.mono==before,"float NaN rejected transactionally");
    check(!decodeWav(fixture(3,32,{0,0,0,64}),variants)&&variants.mono==before,"out-of-range float rejected");
    check(decodeWav(fixture(1,24,{0,0,0}),variants)&&variants.mono.size()==1,"odd data padding parsed");
    Store store;check(store.publish(sample.mono,sample.rate),"decoded content enters bounded store");
    Store::Lease lease;check(store.acquire(lease)&&lease.frames()==2&&lease.data()[0]==-.25f,"decoded data survives publication");store.release(lease);
    auto bad=wav;bad[0]='X';check(!decodeWav(bad,sample)&&sample.mono.size()==2&&sample.mono[0]==-.25f,"bad RIFF rejected transactionally");
    bad=wav;bad[20]=3;check(!decodeWav(bad,sample),"unsupported float16 rejected");
    bad=wav;bad.pop_back();check(!decodeWav(bad,sample),"truncated payload rejected");
    bad=wav;bad[22]=3;check(!decodeWav(bad,sample),"unsupported channel count rejected");
    bad=wav;bad[32]=1;check(!decodeWav(bad,sample),"invalid alignment rejected");
    bad=wav;bad[40]=0xff;bad[41]=0xff;bad[42]=0xff;bad[43]=0xff;
    check(!decodeWav(bad,sample),"oversized chunk rejected");
    // Deterministic malformed-input sweep checks parser bounds under sanitizers.
    std::uint32_t random=1;
    for(int n=0;n<5000;++n){bad=wav;for(int m=0;m<4;++m){random^=random<<13;random^=random>>17;random^=random<<5;
            bad[random%bad.size()]^=static_cast<std::uint8_t>(random>>24);}
        Decoded scratch;(void)decodeWav(bad,scratch);}
    const auto file=juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("vox-fx-sample",".wav",false);
    check(file.replaceWithData(wav.data(),wav.size()),"write fixture");Decoded loaded;
    check(loadWav(file,loaded)&&loaded.sha256==sample.sha256&&loaded.mono==sample.mono,"worker file read matches memory decode");
    check(file.deleteFile(),"remove fixture");
    check(!loadWav(file,loaded)&&loaded.mono==sample.mono,"missing file leaves previous data unchanged");
    std::printf("FX sample WAV decode: %d failure(s)\n",failures);return failures?1:0;
}
