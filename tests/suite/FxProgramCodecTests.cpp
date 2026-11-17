#include "engine/ProgramCodec.h"
#include <cstdio>
#include <limits>
#include <random>
int main() {
    int fails=0; auto check=[&](bool b,const char* s){if(!b){std::printf("FAIL: %s\n",s);++fails;}};
    vox::fx::NoiseProgram p;
    for(int i=0;i<4;++i){auto& l=p.layers[static_cast<std::size_t>(i)]; l.enabled=true; l.family=static_cast<vox::fx::NoiseFamily>(i);
        l.gain=.1f*static_cast<float>(i+1);l.pan=-.75f+.5f*i; l.timing={i*.2f,.2f};
        l.gainCurve.count=3;l.gainCurve.nodes[0]={0,0,vox::fx::timeline::Shape::smooth};
        l.gainCurve.nodes[1]={.5f,1,vox::fx::timeline::Shape::hold};l.gainCurve.nodes[2]={1,.2f,vox::fx::timeline::Shape::linear};}
    std::vector<std::byte> bytes,again;check(vox::fx::programcodec::encode(p,bytes),"encode valid program");
    vox::fx::NoiseProgram q;check(vox::fx::programcodec::decode(bytes,q),"decode valid program");
    check(vox::fx::programcodec::encode(q,again)&&bytes==again,"deterministic roundtrip");
    const auto before=again;
    for(std::size_t size=0;size<bytes.size();++size){check(!vox::fx::programcodec::decode(std::span(bytes).first(size),q),"reject truncation");
        check(vox::fx::programcodec::encode(q,again)&&again==before,"rejection transactional");}
    auto future=bytes;future[0]=std::byte{2};check(!vox::fx::programcodec::decode(future,q),"future version rejected");
    auto invalidBool=bytes;invalidBool[4]=std::byte{2};check(!vox::fx::programcodec::decode(invalidBool,q),"invalid enabled rejected");
    auto extra=bytes;extra.push_back(std::byte{0});check(!vox::fx::programcodec::decode(extra,q),"trailing data rejected");
    p.layers[0].gain=std::numeric_limits<float>::quiet_NaN();
    check(!vox::fx::programcodec::encode(p,again)&&again==before,"invalid encode is transactional");
    std::mt19937 gen(42);for(int i=0;i<10000;++i){std::vector<std::byte> junk(static_cast<std::size_t>(gen()%512));
        for(auto& b:junk) b=static_cast<std::byte>(gen()%256);
        (void)vox::fx::programcodec::decode(junk,q);
    }
    std::printf("FX Program codec: %d failure(s)\n",fails);return fails?1:0;
}
