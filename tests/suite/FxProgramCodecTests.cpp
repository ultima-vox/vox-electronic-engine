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
    p.layers[2].pitchCurve.nodes[0].value=0.1f;p.layers[2].pitchCurve.nodes[1].value=0.9f;
    p.layers[1].cutoffCurve.nodes[0].value=0.2f;p.layers[1].cutoffCurve.nodes[1].value=0.8f;
    p.amplitudeCurve.nodes[0].value=0.2f;p.amplitudeCurve.nodes[1].value=0.9f;
    p.pitchCurve.nodes[0].value=0.25f;p.pitchCurve.nodes[1].value=0.75f;
    p.cutoffCurve.nodes[0].value=0.1f;p.cutoffCurve.nodes[1].value=0.6f;
    p.panoramaCurve.nodes[0].value=0.3f;p.panoramaCurve.nodes[1].value=0.7f;
    p.mode=vox::fx::PlayMode::transition;p.triggerPolicy=vox::fx::TriggerPolicy::replace;
    p.tonality={vox::fx::source::Scale::dorian,7};p.layers[1].transpose=-7;
    p.layers[1].filters[0]={vox::fx::source::FilterType::lowpass,750,.85f,4};
    p.layers[1].filters[1]={vox::fx::source::FilterType::notch,2000,1.2f,2};
    p.layers[1].character=.35f;
    constexpr auto characterBytes=vox::fx::programcodec::characterBytes;
    constexpr auto soundBytes=vox::fx::programcodec::soundBytes+characterBytes;
    std::vector<std::byte> bytes,again;check(vox::fx::programcodec::encode(p,bytes),"encode valid program");
    if(bytes.size()<8) { std::printf("FAIL: encoded fixture too short\n"); return 1; }
    vox::fx::NoiseProgram q;check(vox::fx::programcodec::decode(bytes,q),"decode valid program");
    check(q.layers[2].pitchCurve.nodes[0].value==0.1f && q.layers[1].cutoffCurve.nodes[1].value==0.8f,"layer pitch/cutoff trajectory roundtrip");
    check(q.amplitudeCurve.nodes[0].value==0.2f && q.pitchCurve.nodes[1].value==0.75f &&
          q.cutoffCurve.nodes[0].value==0.1f && q.panoramaCurve.nodes[1].value==0.7f,"four global trajectories roundtrip");
    check(q.mode==vox::fx::PlayMode::transition&&q.triggerPolicy==vox::fx::TriggerPolicy::replace,"mode and retrigger policy roundtrip");
    check(vox::fx::programcodec::encode(q,again)&&bytes==again,"deterministic roundtrip");
    const auto before=again;
    for(std::size_t size=0;size<bytes.size();++size){check(!vox::fx::programcodec::decode(std::span(bytes).first(size),q),"reject truncation");
        check(vox::fx::programcodec::encode(q,again)&&again==before,"rejection transactional");}
    auto makeLegacy=[&](std::uint32_t version,std::uint32_t bars){
        std::vector<std::byte> legacy;vox::fx::programcodec::putU32(legacy,version);
        for(const auto& l:p.layers){vox::fx::programcodec::putU32(legacy,l.enabled?1u:0u);
            vox::fx::programcodec::putU32(legacy,static_cast<std::uint32_t>(l.family));
            vox::fx::programcodec::putFloat(legacy,l.timing.offset);vox::fx::programcodec::putFloat(legacy,l.timing.span);
            vox::fx::programcodec::putFloat(legacy,l.gain);vox::fx::programcodec::putFloat(legacy,l.pan);
            vox::fx::programcodec::putCurve(legacy,l.gainCurve);vox::fx::programcodec::putCurve(legacy,l.panCurve);}
        if(version==2)vox::fx::programcodec::putU32(legacy,bars);
        return legacy;
    };
    vox::fx::NoiseProgram migrated; migrated.bars=16;
    auto legacyV1=makeLegacy(1,0);
    check(vox::fx::programcodec::decode(legacyV1,migrated)&&migrated.bars==0&&
          migrated.layers[2].pitchCurve.nodes[0].value==0.5f,"v1 migrates to seconds/default curves");
    auto legacyV2=makeLegacy(2,8);
    check(vox::fx::programcodec::decode(legacyV2,migrated)&&migrated.bars==8,"v2 bars migrate with default new curves");
    auto legacyV3=bytes;legacyV3.resize(legacyV3.size()-soundBytes-8);legacyV3[0]=std::byte{3};
    check(vox::fx::programcodec::decode(legacyV3,migrated)&&migrated.mode==vox::fx::PlayMode::oneShot&&
          migrated.triggerPolicy==vox::fx::TriggerPolicy::overlap&&migrated.pitchCurve.nodes[0].value==.25f,"v3 trajectories migrate with legacy playback defaults");
    auto legacyV4=bytes;legacyV4.resize(legacyV4.size()-soundBytes);legacyV4[0]=std::byte{4};
    check(vox::fx::programcodec::decode(legacyV4,migrated)&&migrated.mode==p.mode&&migrated.triggerPolicy==p.triggerPolicy&&
          migrated.tonality.scale==vox::fx::source::Scale::chromatic&&migrated.layers[1].transpose==0&&
          migrated.layers[1].filters[0].type==vox::fx::source::FilterType::off,"v4 migrates to bypass/source defaults");
    auto legacyV5=bytes;legacyV5.resize(legacyV5.size()-characterBytes);legacyV5[0]=std::byte{5};
    check(vox::fx::programcodec::decode(legacyV5,migrated)&&migrated.layers[1].character==0&&migrated.layers[1].transpose==-7,"v5 migrates with neutral character");
    check(q.layers[1].character==.35f,"v6 character roundtrip");
    check(q.tonality.scale==p.tonality.scale&&q.tonality.root==7&&q.layers[1].transpose==-7&&q.layers[1].filters[0].poles==4,"v5 sound roundtrip");
    auto badSound=bytes;badSound[bytes.size()-soundBytes]=std::byte{12};
    check(!vox::fx::programcodec::decode(badSound,q),"invalid key rejected");
    badSound=bytes;badSound[badSound.size()-characterBytes-1]=std::byte{1};check(!vox::fx::programcodec::decode(badSound,q),"noncanonical pole word rejected");
    p.bars=16; std::vector<std::byte> barBytes; check(vox::fx::programcodec::encode(p,barBytes)&&vox::fx::programcodec::decode(barBytes,migrated)&&migrated.bars==16,"bar roundtrip");
    if(barBytes.size()<4) { std::printf("FAIL: bar fixture too short\n"); return 1; }
    auto invalidBars=barBytes;invalidBars[invalidBars.size()-soundBytes-12]=std::byte{3};check(!vox::fx::programcodec::decode(invalidBars,migrated)&&migrated.bars==16,"invalid bars rejected transactionally");
    auto future=bytes;future[0]=std::byte{9};check(!vox::fx::programcodec::decode(future,q),"future version rejected");
    auto invalidMode=bytes;invalidMode[invalidMode.size()-soundBytes-8]=std::byte{255};
    check(!vox::fx::programcodec::decode(invalidMode,q),"invalid playback mode rejected");
    auto invalidBool=bytes;invalidBool[4]=std::byte{2};check(!vox::fx::programcodec::decode(invalidBool,q),"invalid enabled rejected");
    auto extra=bytes;extra.push_back(std::byte{0});check(!vox::fx::programcodec::decode(extra,q),"trailing data rejected");
    {
        using namespace vox::fx;
        NoiseProgram granular;granular.layers[0].family=NoiseFamily::granular;
        std::vector<std::byte> v6,v7,canonical;
        check(programcodec::encode(granular,v6)&&v6[0]==std::byte{6},"default grains retain FPRG6");
        granular.layers[0].grains={47.5f,.37f,true};
        NoiseProgram roundtrip;
        check(programcodec::encode(granular,v7)&&v7[0]==std::byte{7}&&programcodec::decode(v7,roundtrip)&&
              roundtrip.layers[0].grains==granular.layers[0].grains&&programcodec::encode(roundtrip,canonical)&&canonical==v7,"FPRG7 canonical roundtrip");
        auto bad=v7;bad[bad.size()-programcodec::grainBytes+8]=std::byte{2};
        check(!programcodec::decode(bad,roundtrip)&&roundtrip.layers[0].grains==granular.layers[0].grains,"FPRG7 rejects invalid reverse word");
        check(!programcodec::decode(std::span(v7).first(v7.size()-1),roundtrip),"FPRG7 rejects truncation");
        check(programcodec::decode(v6,roundtrip)&&roundtrip.layers[0].grains==source::GrainShape{},"FPRG6 migrates to 80 ms seeded forward");
        granular.layers[0].grains.position=-.5f;
        check(!programcodec::encode(granular,canonical)&&canonical==v7,"invalid grain position rejected transactionally");
    }
    {
        using namespace vox::fx;
        NoiseProgram custom;std::vector<std::byte> v6,v7,v8,canonical;
        check(programcodec::encode(custom,v6)&&v6[0]==std::byte{6},"default Glitch does not emit FPRG8");
        custom.layers[1].grains={34.f,.2f,true};
        check(programcodec::encode(custom,v7)&&v7[0]==std::byte{7},"default Glitch retains FPRG7 with grain controls");
        custom.layers[0].family=NoiseFamily::glitch;
        custom.layers[0].glitch={source::SliceGrid::thirtySecond,source::SliceRead::reverse,2};
        NoiseProgram restored;
        check(programcodec::encode(custom,v8)&&v8[0]==std::byte{8}&&programcodec::decode(v8,restored)&&
              restored.layers[0].glitch==custom.layers[0].glitch&&restored.layers[1].grains==custom.layers[1].grains&&
              programcodec::encode(restored,canonical)&&canonical==v8,"FPRG8 mixes independent per-layer Glitch and grain settings");
        if(v8.size()>programcodec::glitchBytes){
            for(auto bits:{3u,12u,256u}){
                auto bad=v8;const auto offset=bad.size()-programcodec::glitchBytes;
                bad[offset]=static_cast<std::byte>(bits&255);bad[offset+1]=static_cast<std::byte>(bits>>8);
                check(!programcodec::decode(bad,restored)&&restored.layers[0].glitch==custom.layers[0].glitch,
                      "invalid grid/direction/reserved bit rejects transactionally");
            }
            check(!programcodec::decode(std::span(v8).first(v8.size()-1),restored),"truncated FPRG8 rejected");
        }
        check(programcodec::decode(v7,restored)&&restored.layers[0].glitch==source::GlitchShape{}&&
              programcodec::encode(restored,canonical)&&canonical==v7,"FPRG7 migrates and re-encodes byte-identically");
        custom.layers[0].glitch.gates=16;
        check(!programcodec::encode(custom,canonical)&&canonical==v7,"out-of-range slice mask cannot be published");
    }
    p.layers[0].gain=std::numeric_limits<float>::quiet_NaN();
    check(!vox::fx::programcodec::encode(p,again)&&again==before,"invalid encode is transactional");
    std::mt19937 gen(42);for(int i=0;i<10000;++i){std::vector<std::byte> junk(static_cast<std::size_t>(gen()%512));
        for(auto& b:junk) b=static_cast<std::byte>(gen()%256);
        (void)vox::fx::programcodec::decode(junk,q);
    }
    std::printf("FX Program codec: %d failure(s)\n",fails);return fails?1:0;
}
