#include "engine/PatternCodec.h"
#include <cstdio>
#include <vector>
int main(){
    using namespace vox::fx::pattern;
    int fails=0;auto check=[&](bool ok,const char* msg){if(!ok){std::printf("FAIL: %s\n",msg);++fails;}};
    Pattern p;p.bars=4;p.seed=0xdeadbeef;
    for(std::size_t i=0;i<p.steps.size();++i){auto& s=p.steps[i];s.enabled=i%2==0;s.note=static_cast<std::uint8_t>(i%128);
        s.velocity=static_cast<std::uint8_t>(1+i%127);s.probability=static_cast<std::uint8_t>(i%101);s.ratchets=static_cast<std::uint8_t>(1+i%4);}
    std::vector<std::byte> bytes,round;check(codec::encode(p,bytes),"encode valid pattern");
    if(bytes.empty())return 1;
    Pattern restored;check(codec::decode(bytes,restored)&&codec::encode(restored,round)&&round==bytes,"roundtrip all pattern fields");
    const auto old=round;
    for(std::size_t n=0;n<bytes.size();++n){check(!codec::decode(std::span(bytes).first(n),restored),"reject truncated data");
        check(codec::encode(restored,round)&&round==old,"failed decode preserves pattern");}
    auto legacy=bytes;legacy[0]=std::byte{1};legacy.erase(legacy.begin()+12,legacy.begin()+20);legacy.resize(67*4);
    Pattern migrated;check(codec::decode(legacy,migrated)&&migrated.grid==Grid::sixteenth && migrated.bars==4 &&
          migrated.steps[63].note==p.steps[63].note && !migrated.steps[64].enabled,"FPAT v1 migration");
    auto legacyV2=bytes;legacyV2[0]=std::byte{2};legacyV2.erase(legacyV2.begin()+16,legacyV2.begin()+20);
    check(codec::decode(legacyV2,migrated)&&migrated.swing==0 && migrated.steps[191].note==p.steps[191].note,"FPAT v2 migration");
    auto malformed=bytes;malformed[0]=std::byte{4};check(!codec::decode(malformed,restored),"future version rejected");
    malformed=bytes;malformed.push_back(std::byte{0});check(!codec::decode(malformed,restored),"trailing bytes rejected");
    malformed=bytes;malformed[23]=std::byte{0x80};check(!codec::decode(malformed,restored),"reserved bits rejected");
    p.steps[0].ratchets=0;check(!codec::encode(p,round)&&round==old,"invalid encode transactional");
    std::printf("FX pattern codec: %d failure(s)\n",fails);return fails?1:0;
}
