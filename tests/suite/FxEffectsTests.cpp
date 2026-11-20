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
            Chain drive;check(drive.prepare(rate),"drive prepare");
            for (std::size_t i=0;i<4;++i)drive.set(i,{Type::drive,1,1});
            std::array<float,512> left{},right{};
            double peak=0,change=0;
            for(int block=0;block<16;++block){
                left=original;right=original;drive.process(left.data(),right.data(),left.size());
                if(block<8)continue;
                for(std::size_t i=0;i<left.size();++i){peak=std::max(peak,std::abs(static_cast<double>(left[i])));change+=std::abs(left[i]-original[i]);}
            }
            check(peak>0.1&&peak<0.45&&change>1,"four Drive slots reshape an audible signal without saturating at full scale");
            for (std::size_t i=0;i<4;++i)drive.set(i,{Type::drive,1,0});
            for(int block=0;block<40;++block){left=original;right=original;drive.process(left.data(),right.data(),left.size());}
            double error=0;for(std::size_t i=0;i<left.size();++i)error=std::max(error,std::abs(static_cast<double>(left[i]-original[i])));
            check(error<1e-6,"zero Drive amount is unity after smoothing");
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
        {
            Chain delay;check(delay.prepare(rate)&&delay.set(0,{Type::delay,1,0}),"delay prepare");
            std::array<float,512> x{},y{};
            for(int i=0;i<10;++i)delay.process(x.data(),y.data(),x.size());
            x[0]=y[0]=1;double first=0,total=0;std::size_t firstAt=0;
            for(int block=0;block<110;++block){delay.process(x.data(),y.data(),x.size());
                for(std::size_t i=0;i<x.size();++i){const auto at=static_cast<std::size_t>(block)*x.size()+i;
                    if(std::abs(x[i])>1e-4&&!first){first=x[i];firstAt=at;}total+=std::abs(x[i]);}
                x={};y={};}
            check(firstAt==static_cast<std::size_t>(std::round(rate*.05)) && std::abs(first-1)<.001f && std::abs(total-1)<.001f,
                  "fixed delay produces one correctly timed impulse");
            check(unpack(pack({Type::delay,1,.5f})).type==Type::delay,"delay structural state");
        }
        {
            // Compare a swept first echo with a continuous sine reference.
            // During this window no feedback can have reached the read head.
            Chain swept;constexpr float base=.3f;
            check(swept.prepare(rate)&&swept.set(0,{Type::delay,1,base}),"moving free Delay fixture");
            const auto end=static_cast<int>(rate*.27),onset=static_cast<int>(rate*.1);
            constexpr double omega=6.283185307179586*.075;
            const double smoothing=1.-std::exp(-1./(.005*rate));
            float smoothed=.5f;
            std::array<float,8> route{};
            double oldError=0,newError=0,energy=0;bool finite=true;
#ifdef VOX_TEST_INTERPOSE_MALLOC
            voxProbeBegin();
#endif
            for(int n=0;n<end;++n){
                route[1]=n>=onset?static_cast<float>(.002*std::sin(6.283185307179586*7*(n-onset)/rate)):0.f;
                smoothed+=static_cast<float>(((base+route[1])-smoothed)*smoothing);
                float l=n<onset?static_cast<float>(.4*std::sin(omega*n)):0.f,r=-l;
                swept.process(&l,&r,1,route.data());
                const double tap=rate*(.05+.45*smoothed);
                if(n>rate*.21){
                    const double ideal=.4*std::sin(omega*(n-tap));
                    const double staircase=.4*std::sin(omega*(n-std::round(tap)));
                    oldError+=(staircase-ideal)*(staircase-ideal);newError+=(l-ideal)*(l-ideal);energy+=ideal*ideal;
                    finite&=std::isfinite(l)&&l==-r;
                }
            }
#ifdef VOX_TEST_INTERPOSE_MALLOC
            const auto allocations=voxProbeEnd();if(voxProbeAvailable())check(allocations==0,"fractional moving Delay allocates nothing");
#endif
            check(finite&&energy>1&&oldError>0&&newError<oldError*.1,
                  "moving Delay reduces staircase error by at least 10 dB without losing stereo polarity");
            std::printf("free Delay sweep %.0f Hz: relative staircase %.2f dB, fractional %.2f dB\n",rate,
                        10*std::log10(oldError/energy),10*std::log10(newError/energy));
        }
        {
            // Use an exact, hand-authored tap trajectory to force crossings
            // during the morph (not just after it) and isolate correction
            // discontinuities from the intended Doppler motion of the tap.
            const unsigned fade=static_cast<unsigned>(std::round(rate*.005));
            for(double speed:{.003,.031}){
                detail::FreeDelayTap tap;
                double ideal=1000.49,previous=tap.update(ideal,.3f,fade),maxExtraStep=0,legacyStep=0;
                double oldPrevious=previous;unsigned crossings=0;
                const auto step=[&](double next,float target,unsigned entryIndex=0){
                    const auto output=tap.update(next,target,fade);
                    maxExtraStep=std::max(maxExtraStep,std::abs((output-previous)-(next-ideal)));
                    if(entryIndex!=0){
                        const double rounded=std::round(next);
                        const double old=rounded+(next-rounded)*entryIndex/fade;
                        legacyStep=std::max(legacyStep,std::abs((old-oldPrevious)-(next-ideal)));
                        oldPrevious=old;
                        if(std::round(next)!=std::round(ideal))++crossings;
                    }
                    ideal=next;previous=output;
                };
                for(unsigned n=1;n<=fade;++n){const double next=ideal+((n/19)%2==0?speed:-speed);step(next,.32f,n);}
                // The old exit criterion could start a fade on the wrong
                // side of +.5, then cross while still inside .01 sample.
                for(double next:{1000.491,1000.497,1000.499,1000.5,1000.501,1000.502})step(next,.31f);
                for(unsigned n=0;n<2*fade;++n)step(1000.502,.31f);
                check(previous==1001.,"moving tap returns exactly to the held legacy tap");
                // Restart during a fade-out and reverse direction repeatedly.
                for(unsigned n=0;n<fade;++n)step(1000.499,.33f);
                for(unsigned n=0;n<fade/3+2;++n)step(1000.499,.33f);
                check(previous<1000.499&&previous>1000.,"fixture re-enters partway through exit correction");
                for(unsigned n=0;n<fade;++n)step(ideal+(n%2?speed:-speed),n%2?.29f:.33f);
                check(crossings>0&&legacyStep>.4&&maxExtraStep<=.5/fade+1e-10,
                      "crossing residual is bounded through entrance, plateau exit, and interrupted exit");
                std::printf("free Delay boundary %.0f Hz speed %.3f: %u crossings, old entry residual %.6f, max new %.6f samples\n",
                            rate,speed,crossings,legacyStep,maxExtraStep);
            }
            // A linear reading region turns the address residual into a
            // measurable output derivative residual in a real first echo.
            // Choose a held setting that approaches +.5 from below, then
            // cross it early during the 5 ms morph, before feedback arrives.
            const int onset=static_cast<int>(rate*.24),end=onset+static_cast<int>(fade)+1;
            const double sm=1.-std::exp(-1./(.005*rate));
            const auto advance=[&](float v,float target){return v+static_cast<float>((target-v)*sm);};
            const auto address=[&](float v){return rate*(.05+.45*v);};
            float base=.3f;
            for(int i=0;i<1000;++i){
                float v=.5f;for(int n=0;n<onset;++n)v=advance(v,base);
                const double fraction=address(v)-std::floor(address(v));
                if(fraction>.475&&fraction<.493)break;
                base+=.000001f;
            }
            Chain echo;check(echo.prepare(rate)&&echo.set(0,{Type::delay,1,base}),"first echo boundary audio setup");
            const double origin=onset-address(base),slope=.0005;
            float amount=.5f;
            double previousResidual=0.,oldPreviousResidual=0.,maxResidual=0.,oldResidual=0.;
            int crossings=0;
            for(int n=0;n<end;++n){
                const float target=n<onset?base:base+.0001f;
                const double before=address(amount);amount=advance(amount,target);
                const double tap=address(amount);
                if(n==onset)echo.set(0,{Type::delay,1,target});
                float l=n<rate*.12?static_cast<float>(std::clamp(slope*(n-origin),-.4,.4)):0.f,r=-l;
                echo.process(&l,&r,1);
                if(n>=onset-1){
                    const double ideal=slope*(n-tap-origin);
                    const double residual=l-ideal;
                    const double rounded=std::round(tap);
                    const double mix=std::clamp(static_cast<double>(n-onset+1)/fade,0.,1.);
                    const double simulatedOld=slope*(n-(rounded+(tap-rounded)*mix)-origin)-ideal;
                    if(n>=onset){
                        if(std::round(tap)!=std::round(before))++crossings;
                        maxResidual=std::max(maxResidual,std::abs(residual-previousResidual));
                        oldResidual=std::max(oldResidual,std::abs(simulatedOld-oldPreviousResidual));
                    }
                    previousResidual=residual;oldPreviousResidual=simulatedOld;
                }
            }
            check(crossings>0&&oldResidual>slope*.65&&maxResidual<slope*.004,
                  "real delay echo removes the abrupt derivative residual at an early read-head crossing");
            std::printf("free Delay first-echo boundary %.0f Hz: old derivative residual %.9f, measured new %.9f\n",
                        rate,oldResidual,maxResidual);
            // Replay note-like noise plus a transient into real delay memory;
            // split at control edges as well as different callback bounds.
            for(bool dualMono:{true,false}){
                const int frames=static_cast<int>(rate*1.4),release=static_cast<int>(rate*.11);
                const int start=static_cast<int>(rate*.19),interval=static_cast<int>(rate*.0027);
                const auto render=[&](int blockSize){
                    Chain delay;check(delay.prepare(rate)&&delay.set(0,{Type::delay,1,.3f}),"boundary audio fixture prepare");
                    std::vector<float> l(frames),r(frames);
                    std::uint32_t random=17;
                    for(int n=0;n<release;++n){
                        random=1664525u*random+1013904223u;
                        l[n]=.15f*(static_cast<float>(random>>16)/32768.f-1.f);
                        r[n]=dualMono?l[n]:-.6f*l[n];
                    }
                    l[180]+=.42f;r[180]+=dualMono?.42f:-.25f;
                    int at=0;
#ifdef VOX_TEST_INTERPOSE_MALLOC
                    voxProbeBegin();
#endif
                    while(at<frames){
                        constexpr float changes[]{.3001f,.2996f,.301f,.3002f,.296f,.298f,.305f,.303f,.301f};
                        const int i=at<start?-1:(at-start)/interval;
                        const bool held=i>=static_cast<int>(std::size(changes));
                        const float amount=i<0?.3f:held?.3001f:changes[i];
                        const int next=i<0?start:held?frames:std::min(frames,start+(i+1)*interval);
                        const int count=std::min({blockSize-(at%blockSize),next-at,frames-at});
                        delay.set(0,{Type::delay,1,amount});delay.process(l.data()+at,r.data()+at,count);at+=count;
                    }
#ifdef VOX_TEST_INTERPOSE_MALLOC
                    const auto allocations=voxProbeEnd();if(voxProbeAvailable())check(allocations==0,"boundary replay allocates nothing in delay callback");
#endif
                    return std::pair(std::move(l),std::move(r));
                };
                const auto samplewise=render(1);
                for(int size:{64,128,512})check(render(size)==samplewise,"moving and settling noisy tail is sample-exact at 1/64/128/512 frames");
                float echo=0,late=0,stereo=0;bool bounded=true;
                for(int n=start;n<frames;++n){
                    const float l=samplewise.first[n],r=samplewise.second[n];
                    bounded&=std::isfinite(l)&&std::isfinite(r)&&std::abs(l)<1&&std::abs(r)<1;
                    if(n>=static_cast<int>(rate*.35)&&n<static_cast<int>(rate*.52))echo=std::max(echo,std::abs(l));
                    if(n>static_cast<int>(rate*1.3))late=std::max(late,std::abs(l));
                    stereo=std::max(stereo,std::abs(l-r));
                }
                check(bounded&&echo>.005f&&late<.001f&&(dualMono?stereo==0:stereo>.02f),
                      "moving tap keeps repeat energy, bounded decaying tail, and channel independence");
            }
        }
        {
            Chain sync;check(sync.prepare(rate)&&sync.set(0,{Type::tempoDelay,1,0}),"Tempo Delay prepare");
            std::array<float,256> l{},r{};
            const auto silence=[&]{for(int i=0;i<8;++i)sync.process(l.data(),r.data(),l.size());};
            const auto impulse=[&](double bpm){
                sync.prepare(rate);sync.set(0,{Type::tempoDelay,1,0});
                silence();sync.setTempo(bpm);silence();l[0]=r[0]=.5f;
                std::size_t first=0;double peak=0;
                const auto target=static_cast<std::size_t>(std::round(rate*tempoDelaySeconds(bpm)));
                for(std::size_t at=0;at<target+257;at+=l.size()){
                    sync.process(l.data(),r.data(),l.size());
                    for(std::size_t i=0;i<l.size();++i)if(std::abs(l[i])>1e-4){if(peak==0)first=at+i;peak=std::max(peak,std::abs(static_cast<double>(l[i])));}
                    l={};r={};
                }
                check(first==target&&std::abs(peak-.5)<.001,"Tempo Delay follows an exact eighth-note interval after its tap crossfade");
            };
            impulse(120);impulse(150);impulse(20);impulse(std::numeric_limits<double>::quiet_NaN());
            check(includesTempoDelay(pack({Type::tempoDelay,1,.5f})),"tempo delay has a stable packed type distinct from free delay");
        }
        {
            constexpr double rate=8000;Chain echo;check(echo.prepare(rate)&&echo.set(0,{Type::delay,1,1}),"feedback delay setup");
            std::array<float,128> l{},r{};echo.process(l.data(),r.data(),l.size());l={};r={};l[0]=r[0]=1;
            std::vector<float> rendered(9000);std::size_t written=0;
            while(written<rendered.size()){const auto count=std::min<std::size_t>(l.size(),rendered.size()-written);
                echo.process(l.data(),r.data(),count);std::copy_n(l.begin(),count,rendered.begin()+static_cast<std::ptrdiff_t>(written));
                written+=count;l={};r={};}
            check(std::abs(rendered[4000]-1.0f)<.001f&&std::abs(rendered[8000]-.75f)<.005f,
                  "delay feedback creates a second echo at the selected repeat level");
            Chain reuse;check(reuse.prepare(rate)&&reuse.set(0,{Type::delay,1,1}),"delay bank-reuse setup");
            std::array<float,128> a{},b{};reuse.process(a.data(),b.data(),a.size());a={};b={};a[0]=b[0]=1;
            for(int block=0;block<45;++block){reuse.process(a.data(),b.data(),a.size());a={};b={};}
            (void)reuse.set(0,{Type::bypass,1,0});for(int block=0;block<3;++block){reuse.process(a.data(),b.data(),a.size());a={};b={};}
            (void)reuse.set(0,{Type::delay,1,1});double stale=0;
            for(int block=0;block<45;++block){reuse.process(a.data(),b.data(),a.size());for(float value:a)stale+=std::abs(value);a={};b={};}
            check(stale<1e-6,"reused feedback-delay bank does not replay stale samples");
        }
        {
            Chain chorus;check(chorus.prepare(rate)&&chorus.set(0,{Type::chorus,1,.8f}),"chorus prepare");
            std::array<float,512> x{},y{};for(int i=0;i<5;++i)chorus.process(x.data(),y.data(),x.size());
            x[0]=y[0]=1;double delayed=0,stereo=0;
            for(int block=0;block<8;++block){chorus.process(x.data(),y.data(),x.size());
                for(std::size_t i=block==0?1:0;i<x.size();++i){delayed+=std::abs(x[i]);stereo+=std::abs(x[i]-y[i]);}
                x={};y={};}
            check(delayed>.1&&stereo>.01,"chorus produces modulated stereo delay");
        }
        {
            Chain a,b;check(a.prepare(rate)&&b.prepare(rate)&&a.set(0,{Type::reverb,1,.8f})&&b.set(0,{Type::reverb,1,.8f}),"reverb prepare");
            std::array<float,512> x{},y{},xx{},yy{};
            for(int block=0;block<5;++block){a.process(x.data(),y.data(),x.size());b.process(xx.data(),yy.data(),xx.size());}
            double energy=0,stereo=0;
            for(int block=0;block<static_cast<int>(rate*2.1/512)+1;++block){
                x={};y={};if(block==0)x[0]=y[0]=1;xx=x;yy=y;
                a.process(x.data(),y.data(),x.size());
                for(std::size_t i=0;i<xx.size();i+=64)b.process(xx.data()+i,yy.data()+i,64);
                check(x==xx&&y==yy,"reverb block invariant");
                for(std::size_t i=0;i<x.size();++i){check(std::isfinite(x[i])&&std::abs(x[i])<=1,"reverb bounded");
                    energy+=std::abs(x[i]);stereo+=std::abs(x[i]-y[i]);}
            }
            double late=0;for(float v:x)late+=std::abs(v);
            check(energy>.5&&stereo>.05&&late<1e-7,"reverb diffuse stereo decay and finite tail");
            x={};y={};a.process(x.data(),y.data(),x.size());
            double stale=0;for(float value:x)stale+=std::abs(value);
            check(stale==0,"reverb retired bank stays silent");
            check(unpack(pack({Type::reverb,1,.8f})).type==Type::reverb,"reverb structural state");
        }
        c.prepare(rate);for(std::size_t i=0;i<4;++i)check(c.set(i,{Type::flanger,.5f,.3f}),"four flanger slots");
#ifdef VOX_TEST_INTERPOSE_MALLOC
        voxProbeBegin();
#endif
        for(int iteration=0;iteration<16;++iteration){
            const Type types[]{Type::phaser,Type::flanger,Type::delay,Type::chorus,Type::reverb};
            for(std::size_t i=0;i<4;++i)c.set(i,{types[(iteration+static_cast<int>(i))%5],.5f,.3f});
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
    {
        Chain serial;check(serial.prepare(8000),"serial delay prepare");
        for(std::size_t i=0;i<4;++i)serial.set(i,{Type::delay,1,1});
        std::array<float,128> l{},r{};for(int block=0;block<20;++block)serial.process(l.data(),r.data(),l.size());
        l[0]=r[0]=1;double energy=0,late=0,peak=0;std::size_t first=0;
        for(int block=0;block<2625;++block){serial.process(l.data(),r.data(),l.size());
            for(std::size_t i=0;i<l.size();++i){const auto sample=std::abs(l[i]);
                if(sample>1e-5f){if(energy==0)first=static_cast<std::size_t>(block)*l.size()+i;energy+=sample;}
                peak=std::max(peak,static_cast<double>(sample));if(block>2560)late+=sample;}
            l={};r={};}
        check(first==16000&&energy>1.0&&peak<=8.0&&late<1e-6,
              "four serial feedback delays repeat with bounded gain and retire within 41 seconds");
    }
    {
        Chain serial;check(serial.prepare(8000),"serial reverb prepare");
        for(std::size_t i=0;i<4;++i)serial.set(i,{Type::reverb,1,1});
        std::array<float,128> l{},r{};for(int block=0;block<20;++block)serial.process(l.data(),r.data(),l.size());
        l[0]=r[0]=1;double energy=0,late=0;
        for(int block=0;block<520;++block){serial.process(l.data(),r.data(),l.size());
            for(float sample:l){energy+=std::abs(sample);if(block>510)late+=std::abs(sample);}
            l={};r={};}
        check(energy>.1&&late<1e-7,"four serial reverbs decay within eight-second allowance");
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
