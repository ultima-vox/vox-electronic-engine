#include "engine/Effects.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#ifdef VOX_TEST_INTERPOSE_MALLOC
#include "AllocationProbe.h"
#endif
int main(){
    using namespace vox::fx::effects;int fails=0;
    auto check=[&](bool b,const char* m){if(!b){std::printf("FAIL: %s\n",m);++fails;}};
    for(double rate:{44100.0,48000.0,96000.0}){
        Chain c;check(c.prepare(rate),"prepare");
        std::array<float,512> l{},r{};for(std::size_t i=0;i<l.size();++i){l[i]=std::sin(i*.07)*.25f;r[i]=-l[i];}
        auto original=l;c.process(l.data(),r.data(),l.size());check(l==original,"true bypass exact");
        for(unsigned type=1;type<static_cast<unsigned>(Type::count);++type){
            c.prepare(rate);check(c.set(0,{static_cast<Type>(type),1,.7f}),"set valid effect");
            double diff=0;for(int block=0;block<12;++block){l=original;r=original;c.process(l.data(),r.data(),l.size());
                for(std::size_t i=0;i<l.size();++i){check(std::isfinite(l[i])&&std::abs(l[i])<=1.001f,"bounded signal");diff+=std::abs(l[i]-original[i]);}}
            check(diff>1,"effect alters signal");
        }
        {
            Chain a,b;a.prepare(rate);b.prepare(rate);
            for(std::size_t i=0;i<4;++i){a.set(i,{static_cast<Type>(i+1),.5f,.8f});b.set(i,{static_cast<Type>(i+1),.5f,.8f});}
            std::array<float,512> la=original,ra=original,lb=original,rb=original;
            a.process(la.data(),ra.data(),la.size());
            for(std::size_t i=0;i<la.size();i+=64)b.process(lb.data()+i,rb.data()+i,64);
            check(la==lb&&ra==rb,"effects are block-size invariant");
        }
        {
            Chain a,b;check(a.prepare(rate)&&b.prepare(rate),"phaser prepare");
            check(a.set(0,{Type::phaser,1,.8f})&&b.set(0,{Type::phaser,1,.8f}),"phaser selectable");
            // Let the structural crossfade finish before injecting an impulse.
            std::array<float,512> x{},y{};
            for(int block=0;block<5;++block){a.process(x.data(),y.data(),x.size());b.process(x.data(),y.data(),x.size());}
            x[0]=1;y[0]=1;auto xx=x,yy=y;a.process(x.data(),y.data(),x.size());
            for(std::size_t i=0;i<xx.size();i+=64)b.process(xx.data()+i,yy.data()+i,64);
            check(x==xx&&y==yy,"phaser block-invariant impulse response");
            double tail=0,stereo=0;for(std::size_t i=1;i<x.size();++i){tail+=std::abs(x[i]);stereo+=std::abs(x[i]-y[i]);}
            check(tail>.01&&stereo>.001,"phaser has stereo allpass impulse response");
            for(int block=0;block<100;++block){x={};y={};a.process(x.data(),y.data(),x.size());}
            double late=0;for(float sample:x)late+=std::abs(sample);
            check(late<1e-6,"phaser tail decays");
            // At zero depth, four cascaded 1 kHz allpasses have a deep
            // notch near tan(pi*f/fs)=tan(pi*1000/fs)*tan(pi/8).
            const auto measure=[&](double frequency){Chain fixed;fixed.prepare(rate);fixed.set(0,{Type::phaser,1,0});
                std::array<float,512> left{},right{};double energy=0;std::size_t n=0;
                for(int block=0;block<40;++block){for(std::size_t i=0;i<left.size();++i,++n){
                    left[i]=right[i]=static_cast<float>(.2*std::sin(6.283185307179586*frequency*n/rate));}
                    fixed.process(left.data(),right.data(),left.size());
                    if(block>=20)for(float value:left)energy+=value*value;}
                return energy;};
            const double notch=rate/3.141592653589793*std::atan(std::tan(3.141592653589793*1000/rate)*std::tan(3.141592653589793/8));
            check(measure(notch)<measure(1000)*.01,"phaser allpass notch matches frequency reference");
            const auto packed=pack({Type::phaser,.5f,.8f});
            check(unpack(packed).type==Type::phaser&&validPacked(static_cast<std::uint64_t>(packed)),"phaser packed type survives");
        }
        {
            Chain a,b;check(a.prepare(rate)&&b.prepare(rate),"flanger prepare");
            check(a.set(0,{Type::flanger,1,.8f})&&b.set(0,{Type::flanger,1,.8f}),"flanger selectable");
            std::array<float,512> x{},y{};
            for(int block=0;block<5;++block){a.process(x.data(),y.data(),x.size());b.process(x.data(),y.data(),x.size());}
            x[0]=1;y[0]=1;auto xx=x,yy=y;
            a.process(x.data(),y.data(),x.size());
            for(std::size_t i=0;i<xx.size();i+=64)b.process(xx.data()+i,yy.data()+i,64);
            check(x==xx&&y==yy,"flanger block invariant");
            double delayed=0,stereo=0;for(std::size_t i=1;i<x.size();++i){delayed+=std::abs(x[i]);stereo+=std::abs(x[i]-y[i]);}
            check(delayed>.1 && stereo>.01,"flanger produces stereo delayed response");
            for(int block=0;block<100;++block){x={};y={};a.process(x.data(),y.data(),x.size());}
            double late=0;for(float sample:x)late+=std::abs(sample);
            check(late<1e-5,"flanger tail decays");
            Chain cold;check(cold.prepare(rate)&&cold.set(0,{Type::flanger,1,0}),"cold flanger setup");
            x={};y={};x[0]=y[0]=1;cold.process(x.data(),y.data(),x.size());
            double firstEcho=0;for(std::size_t i=1;i<x.size();++i)firstEcho+=std::abs(x[i]);
            check(firstEcho>.01,"new flanger retains first impulse during warmup");
            cold.set(0,{Type::bypass,1,0});for(int block=0;block<5;++block){x={};y={};cold.process(x.data(),y.data(),x.size());}
            cold.set(0,{Type::flanger,1,0});double leaked=0;
            for(int block=0;block<5;++block){x={};y={};cold.process(x.data(),y.data(),x.size());for(float v:x)leaked+=std::abs(v);}
            check(leaked<1e-6,"reused delay bank does not expose stale samples");
            check(unpack(pack({Type::flanger,.5f,.7f})).type==Type::flanger,"four-bit effect type");
            // Old packed FXSL v1 encodes 3-bit type and 6/7-bit values.
            const auto legacy=static_cast<std::uint16_t>(static_cast<unsigned>(Type::phaser)|(31u<<3)|(100u<<9));
            const auto migrated=unpackLegacy(legacy);
            check(migrated.type==Type::phaser&&std::abs(migrated.wet-31.0f/63)<1e-6f&&
                  std::abs(migrated.amount-100.0f/127)<1e-6f,"legacy FXSL words decode exactly");
        }
        c.prepare(rate);for(std::size_t i=0;i<4;++i)check(c.set(i,{Type::flanger,.5f,.3f}),"four flanger slots");
#ifdef VOX_TEST_INTERPOSE_MALLOC
        voxProbeBegin();
#endif
        for(int iteration=0;iteration<16;++iteration){
            for(std::size_t i=0;i<4;++i)c.set(i,{iteration%2?Type::phaser:Type::flanger,.5f,.3f});
            c.process(l.data(),r.data(),l.size());
        }
#ifdef VOX_TEST_INTERPOSE_MALLOC
        const auto allocations=voxProbeEnd();if(voxProbeAvailable())check(allocations==0,"effects callback allocates nothing");
#endif
        check(!c.set(4,{})&&!c.set(0,{Type::drive,std::numeric_limits<float>::quiet_NaN(),0}),"invalid requests rejected");
        for(std::size_t i=0;i<4;++i)c.set(i,{Type::bypass,1,.5f});
        for(int i=0;i<10;++i){l=original;r=original;c.process(l.data(),r.data(),l.size());}
        l=original;r=original;c.process(l.data(),r.data(),l.size());check(l==original,"bypass exact after transition");
    }
    for(double rate:{8000.0,384000.0}){
        Chain edge;check(edge.prepare(rate)&&edge.set(0,{Type::flanger,1,0}),"flanger edge sample rate");
        std::array<float,512> left{},right{};left[0]=right[0]=1;double echo=0;
        for(int block=0;block<12;++block){edge.process(left.data(),right.data(),left.size());
            for(std::size_t i=block==0?1:0;i<left.size();++i)echo+=std::abs(left[i]);
            left={};right={};}
        check(echo>.01,"flanger delay response at supported rate extremes");
    }
    std::printf("FX effects foundation: %d failure(s)\n",fails);return fails?1:0;
}
