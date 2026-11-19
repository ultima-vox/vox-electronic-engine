#include "engine/MacroRouting.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#ifdef VOX_TEST_INTERPOSE_MALLOC
#include "AllocationProbe.h"
#endif
int main(){
    using namespace vox::fx::macro;int fails=0;auto check=[&](bool ok,const char* msg){if(!ok){std::printf("FAIL: %s\n",msg);++fails;}};
    Config config;check(config.setGainRoute(0,.5f,.01f),"set macro route");
    check(config.setGainRoute(7,-.25f,0),"set second route");
    std::vector<std::byte> encoded;Config restored;
    check(codec::encode(config,encoded)&&codec::decode(encoded,restored)&&restored==config,"routes roundtrip");
    auto invalid=config;invalid.routes[0].depth=std::numeric_limits<float>::quiet_NaN();
    check(!invalid.valid()&&!codec::encode(invalid,encoded),"invalid route rejected");
    Config maximum;maximum.count=64;for(auto& route:maximum.routes)
        route={vox::fx::mod::Source::macro1,vox::fx::mod::Destination::gain,.01f,0};
    std::vector<std::byte> full;Config fullDecoded;
    check(codec::encode(maximum,full)&&codec::decode(full,fullDecoded)&&fullDecoded==maximum,"64 typed routes roundtrip");
    auto unknown=encoded;unknown[8]=std::byte{0xff};
    check(!codec::decode(unknown,restored)&&restored==config,"unsupported source rejected transactionally");
    for(std::size_t n=0;n<encoded.size();++n)
        check(!codec::decode(std::span(encoded).first(n),restored),"truncated route state rejected");
    auto future=encoded;future[0]=std::byte{3};check(!codec::decode(future,restored)&&restored==config,"future state rejected transactionally");
    std::array<float,8> macros{};macros.fill(.5f);macros[0]=1;macros[7]=1;
    Runtime runtime;check(runtime.prepare(48000)&&runtime.setConfig(config),"prepare macro routing");
    auto before=runtime.process(.5f,macros);check(before<.5f,"bipolar negative route and smoothed positive route");
    for(int i=0;i<10000;++i)(void)runtime.process(.5f,macros);
    check(std::abs(runtime.process(.5f,macros)-.75f)<.001f,"routed macro changes real output gain");
    check(config.setGainRoute(0,0,.01f)&&config.count==1,"remove route");
#ifdef VOX_TEST_INTERPOSE_MALLOC
    voxProbeBegin();
#endif
    for(int i=0;i<512;++i)(void)runtime.process(.5f,macros);
#ifdef VOX_TEST_INTERPOSE_MALLOC
    const auto n=voxProbeEnd();if(voxProbeAvailable())check(n==0,"macro audio processing allocates nothing");
#endif
    {
        Config motion;check(motion.setSourceGainRoute(vox::fx::mod::Source::lfo1,.5f,0)&&
                            motion.setSourceGainRoute(vox::fx::mod::Source::random,.25f,0),"LFO and sample-hold routes");
        Runtime a,b;check(a.prepare(48000)&&b.prepare(48000)&&a.setConfig(motion)&&b.setConfig(motion),"prepare motion sources");
        std::array<float,8> neutral{};neutral.fill(.5f);double delta=0;
        for(int i=0;i<48000;++i){const float x=a.process(.5f,neutral),y=b.process(.5f,neutral);
            check(x==y&&std::isfinite(x)&&x>=0&&x<=1,"motion deterministic and bounded");delta+=std::abs(x-.5f);}
        check(delta>1,"motion modulates gain");
        check(motion.setSourceGainRoute(vox::fx::mod::Source::lfo2,.2f,.01f),"second LFO route");
        std::vector<std::byte> motionBytes;Config recalled;
        check(codec::encode(motion,motionBytes)&&codec::decode(motionBytes,recalled)&&recalled==motion,"motion routes persisted");
        check(!motion.setSourceGainRoute(vox::fx::mod::Source::velocity,.5f,0),"voice source cannot target shared gain");
    }
    {
        using vox::fx::mod::Source;using vox::fx::mod::Destination;
        Config routing;
        check(routing.setRoute(Source::macro1,Destination::gain,.25f,0) &&
              routing.setRoute(Source::macro1,Destination::pan,.5f,0) && routing.count==2,
              "same source can target gain and pan");
        Runtime p;check(p.prepare(48000)&&p.setConfig(routing),"prepare stereo route");
        auto values=std::array<float,8>{};values.fill(.5f);values[0]=1;
#ifdef VOX_TEST_INTERPOSE_MALLOC
        voxProbeBegin();
#endif
        for(int i=0;i<512;++i)(void)p.processStereo(.5f,values);
#ifdef VOX_TEST_INTERPOSE_MALLOC
        const auto panAllocations=voxProbeEnd();if(voxProbeAvailable())check(panAllocations==0,"pan routing allocates nothing");
#endif
        const auto right=p.processStereo(.5f,values);
        check(right.left==0 && right.right==.75f,"positive macro pans hard right with gain");
        values[0]=0;const auto left=p.processStereo(.5f,values);
        check(left.left==.25f && left.right==0,"negative macro pans hard left with gain");
        values[0]=.5f;const auto center=p.processStereo(.5f,values);
        check(center.left==.5f && center.right==.5f,"neutral pan preserves stereo channels");
        std::vector<std::byte> bytes;Config roundtrip;
        check(codec::encode(routing,bytes)&&codec::decode(bytes,roundtrip)&&roundtrip==routing,"pan routes roundtrip");
        check(routing.setRoute(Source::macro1,Destination::pan,0,0) && routing.count==1 &&
              routing.routes[0].destination==Destination::gain,"removing pan preserves same-source gain");
        check(routing.setRoute(Source::macro1,Destination::pitch,.5f,0),"macro can target voice pitch");
    }
    std::printf("FX macro routing: %d failure(s)\n",fails);return fails?1:0;
}
