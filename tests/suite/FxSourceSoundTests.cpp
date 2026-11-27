#include "engine/NoiseRiser.h"
#include "engine/ProgramStore.h"
#include <cstdio>
#include <memory>

using namespace vox::fx;
int main(){
    using namespace source;
    int errors=0;
    const auto check=[&](bool pass,const char* what){if(!pass){std::printf("FAIL: %s\n",what);++errors;}};
    constexpr int degrees[][7]{{0,2,4,5,7,9,11},{0,2,3,5,7,8,10},{0,2,3,5,7,9,10},{0,1,3,5,7,8,10}};
    for(int scale=1;scale<=4;++scale)for(int root=0;root<12;++root){
        Tonality t{static_cast<Scale>(scale),static_cast<std::uint8_t>(root)};
        for(int midi=0;midi<128;++midi){
            int best=-100;for(int n=0;n<128;++n){
                const int pc=(n+12-root)%12;
                if(std::find(std::begin(degrees[scale-1]),std::end(degrees[scale-1]),pc)!=std::end(degrees[scale-1])&&std::abs(n-midi)<std::abs(best-midi))best=n;
            }
            check(t.map(midi)==best,"scale degree with lower-note tie-break and MIDI boundaries");
        }
    }
    constexpr double pi=3.14159265358979323846;
    for(double rate:{44100.,48000.,96000.}){
        const auto response=[&](Filter f,double hz){
            FilterState state;state.prepare(f,rate);double sum=0;
            for(int i=0;i<16384;++i){const auto y=state.tick(std::sin(2*pi*hz*i/rate));if(i>=8192)sum+=y*y;}
            return std::sqrt(sum/8192);
        };
        const auto low=response({FilterType::lowpass,1000,.707f,2},100);
        const auto high=response({FilterType::lowpass,1000,.707f,2},6000);
        const auto steep=response({FilterType::lowpass,1000,.707f,4},6000);
        check(low>.68&&high<.025&&steep<.001,"low pass and extra cascade stop-band attenuation");
        check(response({FilterType::highpass,1000,.707f,2},100)<.015,"high pass rejects bass");
        check(response({FilterType::bandpass,1000,.707f,2},1000)>.68,"band pass passes center frequency");
        check(response({FilterType::notch,1000,.707f,4},1000)<.001,"notch rejects center frequency");
    }
    auto a=std::make_unique<NoiseRiser>(),b=std::make_unique<NoiseRiser>();
    NoiseProgram p;
    p.layers[0].family=NoiseFamily::texture;
    auto q=p;q.tonality={Scale::major,0};q.layers[0].transpose=12;
    q.layers[0].filters={Filter{FilterType::lowpass,600,.707f,4},Filter{FilterType::highpass,70,.707f,2}};
    a->prepare(48000);b->prepare(48000);a->setProgram(p);b->setProgram(p);
    check(a->noteOn(1,61,1,.3)&&b->noteOn(1,61,1,.3),"admit pre-edit notes");
    a->setProgram(q);
    for(int n=0;n<4096;++n){float l,r,ll,rr;a->render(&l,&r,1);b->render(&ll,&rr,1);check(l==ll&&r==rr,"held note ignores structural change");}
    a->prepare(48000);b->prepare(48000);a->setProgram(q);b->setProgram(p);
    check(a->noteOn(1,61,1,.3)&&b->noteOn(1,61,1,.3),"admit post-edit notes");
    double delta=0;
    for(int n=0;n<10240;++n){float l,r,ll,rr;a->render(&l,&r,1);b->render(&ll,&rr,1);delta+=std::abs(l-ll);check(std::isfinite(l)&&std::abs(l)<1,"authored event finite and below full scale");}
    check(delta>1,"new voices use authored settings");
    a->prepare(48000);b->prepare(48000);
    q.layers[0].filters={};a->setProgram(q);b->setProgram(p);
    check(a->noteOn(1,61,1,.3)&&b->noteOn(1,72,1,.3),"quantize C-sharp to C then transpose by an octave");
    double maximumError=0;
    for(int n=0;n<4096;++n){float l,r,ll,rr;a->render(&l,&r,1);b->render(&ll,&rr,1);maximumError=std::max(maximumError,std::abs(static_cast<double>(l)-ll));}
    check(maximumError<1e-6,"tonal oscillator follows mapped and transposed frequency");
    for(unsigned i=0;i<12;++i){
        NoiseProgram plain;plain.layers[0].family=static_cast<NoiseFamily>(i);
        auto authored=plain;authored.layers[0].character=.9f;
        a->prepare(48000);b->prepare(48000);a->setProgram(plain);b->setProgram(authored);
        check(a->noteOn(1,60,1,.6)&&b->noteOn(1,60,1,.6),"admit source-character fixture");
        double diff=0;
        for(int n=0;n<12000;++n){float l,r,x,y;a->render(&l,&r,1);b->render(&x,&y,1);diff+=std::abs(l-x);
            check(std::isfinite(x)&&std::isfinite(y)&&std::abs(x)<1&&std::abs(y)<1,"family character remains finite below full scale");}
        check(diff>.02,"family character changes the actual waveform");
    }
    for(unsigned i=0;i<12;++i){
        NoiseProgram plain;plain.layers[0].family=static_cast<NoiseFamily>(i);
        auto authored=plain;authored.layers[0].familyControls={.65f,-.45f,.55f};
        a->prepare(48000);b->prepare(48000);a->setProgram(plain);b->setProgram(authored);
        check(a->noteOn(1,60,1,.6)&&b->noteOn(1,60,1,.6),"admit type-specific control fixture");
        double diff=0;
        for(int n=0;n<12000;++n){float l,r,x,y;a->render(&l,&r,1);b->render(&x,&y,1);diff+=std::abs(l-x);
            check(std::isfinite(x)&&std::isfinite(y)&&std::abs(x)<1&&std::abs(y)<1,"type-specific controls remain bounded");}
        check(diff>.02,"type-specific controls change the actual waveform");
    }
    ProgramStore store;const auto stale=store.versionedProgram().revision;
    check(store.publish(q)&&!store.editTonalityIfRevision({Scale::minor,4},stale),"stale key editor cannot overwrite replacement");
    check(!store.editLayerIfRevision(0,stale,[](NoiseLayer& l){l.transpose=-12;}),"stale source editor cannot overwrite replacement");
    auto current=store.versionedProgram();
    check(current.program.layers[0].transpose==12&&store.editTonalityIfRevision({Scale::minor,4},current.revision)&&
          store.versionedProgram().revision==current.revision,"valid localized key edit preserves replacement revision");
    std::printf("FX source sound: %d failure(s)\n",errors);return errors?1:0;
}
