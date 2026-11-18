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
        c.prepare(rate);for(std::size_t i=0;i<4;++i)check(c.set(i,{Type::phaser,.5f,.3f}),"four phaser slots");
#ifdef VOX_TEST_INTERPOSE_MALLOC
        voxProbeBegin();
#endif
        c.process(l.data(),r.data(),l.size());
#ifdef VOX_TEST_INTERPOSE_MALLOC
        const auto allocations=voxProbeEnd();if(voxProbeAvailable())check(allocations==0,"effects callback allocates nothing");
#endif
        check(!c.set(4,{})&&!c.set(0,{Type::drive,std::numeric_limits<float>::quiet_NaN(),0}),"invalid requests rejected");
        for(std::size_t i=0;i<4;++i)c.set(i,{Type::bypass,1,.5f});
        for(int i=0;i<10;++i){l=original;r=original;c.process(l.data(),r.data(),l.size());}
        l=original;r=original;c.process(l.data(),r.data(),l.size());check(l==original,"bypass exact after transition");
    }
    std::printf("FX effects foundation: %d failure(s)\n",fails);return fails?1:0;
}
