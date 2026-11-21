// FX-04 Wavefold anti-aliasing option comparison (evidence tool).
//
// Prototype-only diagnostic. This file is not a test, is not linked into any
// product, and the oversampling candidates here are NOT shipped. The shipped
// implementation lives in products/fx/engine/Effects.h and its ADAA2 row uses
// that exact code. Build and run from the repository root:
//
//   g++ -std=c++20 -O2 -Iproducts/fx
//       docs/ultima-vox-music/fx/evidence/measure-wavefold-options.cpp -o /tmp/wfopt
//   /tmp/wfopt
//
// A bin-centred sine is measured exactly as in FxNonlinearAliasTests: all
// folded (non-harmonic) power relative to a unit-amplitude sine. The 2x/4x
// oversampling prototypes use a centred linear-phase 31-tap halfband; a
// real-time implementation would need that filter's group delay as host
// latency (15 input samples at 2x, 23 at 4x), so their quality numbers are an
// upper bound no shipping path is allowed to claim without PDC work.
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include "engine/Effects.h"
#include <vector>

constexpr double PI = 3.14159265358979323846;

// ---------- exact triangle fold, corrected odd shape ----------
struct Fold {
    double g;
    explicit Fold(double amount) : g(1 + 7*amount) {}
    double f(double x) const {
        const double u = g*x;
        if (u <= -8.0 || u >= 8.0) return 0.0;
        const double au=std::abs(u), m=std::floor(au*.5), s=au-2*m;
        const double tri = s<=1 ? s : 2-s;
        const double folded = (static_cast<int>(m)&1) ? -tri : tri;
        return u<0 ? -folded : folded;
    }
    double F1(double x) const {
        const double u=g*x;
        if (std::abs(u)>=8.0) return 0.0;
        const double au=std::abs(u),m=std::floor(au*.5),s=au-2*m;
        const double a=s<=1 ? s*s*.5 : 2*s-s*s*.5-1;
        const double gv=(static_cast<int>(m)&1)?-a:a;
        return ((static_cast<int>(m)&1)?1.0:0.0)+gv;
    }
    double F1s(double x) const { return F1(x)/g; }
    double F2(double x) const {
        const double u=g*x, au=std::abs(u);
        double h;
        if(au>=8.0) h=4.0;
        else {
            const double m=std::floor(au*.5),s=au-2*m;
            const double b=s<=1 ? s*s*s/6 : s*s-s*s*s/6-s+1.0/3.0;
            h=(static_cast<int>(m)&1)?m+s-b:m+b;
        }
        return (u<0?-h:h)/(g*g);
    }
};

struct ADAA1 {
    double p1=0,p2=0;
    void run(const Fold& shape,double& l,double& r) {
        auto tick=[&](double x,double& p){
            const double d=x-p;
            const double y=std::abs(d)<1e-9?shape.f(x):(shape.F1s(x)-shape.F1s(p))/d;
            p=x; return y;
        };
        l=tick(l,p1);r=tick(r,p2);
    }
};
struct ADAA2 {
    vox::fx::effects::detail::WavefoldMemory l{};
    vox::fx::effects::detail::WavefoldMemory r{};
    void run(double gain,double& x,double& y) {
        x=vox::fx::effects::detail::wavefoldTick(x,gain,l);
        y=vox::fx::effects::detail::wavefoldTick(y,gain,r);
    }
};

// Linear-phase windowed-sinc halfband used only to compare oversampling.
class HalfBand {
public:
    explicit HalfBand(int taps):n_(taps),h_(taps){
        for(int i=0;i<taps;++i){const double t=i-(taps-1)*.5;
            const double sinc=std::abs(t)<1e-12?.5:std::sin(PI*.5*t)/(PI*t);
            const double w=.42-.5*std::cos(2*PI*i/(taps-1))+.08*std::cos(4*PI*i/(taps-1));
            h_[i]=sinc*w;}
        double s=0;for(double v:h_)s+=v;for(double&v:h_)v/=s;
    }
    int delayInputSamples(int stages) const { // total up+down group delay, input samples
        // A stage that raises the rate to F times the input runs at
        // F x input rate; (taps-1)/2 high-rate samples are (taps-1)/(2F)
        // input samples. Upsampling and decimation each contribute once.
        double total=0, factor=2;
        for(int s=0;s<stages;++s){ total += (n_-1)*.5/factor; factor*=2; }
        return static_cast<int>(std::lround(2*total));
    }
    template<class T> void up(const T* in,int n,std::vector<double>& out) const {
        out.assign(std::size_t(2*n),0.0);
        for(int i=0;i<n;++i)for(int k=0;k<n_;++k){const int o=2*i-(n_-1)+k;if(o>=0)out[std::size_t(o)]+=2.0*in[i]*h_[k];}
    }
    void down(const std::vector<double>& in,int nOut,std::vector<float>& out) const {
        out.resize(std::size_t(nOut));
        for(int i=0;i<nOut;++i){const int c=2*i;double s=0;
            for(int k=0;k<n_;++k){const int idx=c-(n_-1)+k;if(idx>=0&&idx<static_cast<int>(in.size()))s+=in[std::size_t(idx)]*h_[k];}
            out[std::size_t(i)]=static_cast<float>(s);}
    }
    int taps() const { return n_; }
private:
    int n_; std::vector<double> h_;
};

enum class Kind { point, adaa1, adaa2, os2, os4, os2adaa, os4adaa };

struct Processor {
    Kind kind; Fold shape; ADAA1 a1; ADAA2 a2; HalfBand hb{31};
    // Scratch buffers are sized for the 128-sample comparison blocks once.
    std::vector<double> h1=std::vector<double>(512),h2=std::vector<double>(512),
        h3=std::vector<double>(512),h4=std::vector<double>(512),
        e1=std::vector<double>(512),e2=std::vector<double>(512);
    std::vector<float> d1=std::vector<float>(256),d2=std::vector<float>(256);
    explicit Processor(Kind k,double amount):kind(k),shape(amount){}
    void foldAtRate(std::vector<double>& a,std::vector<double>& b,int n){
        for(int i=0;i<n;++i){a[std::size_t(i)]=shape.f(a[std::size_t(i)]);b[std::size_t(i)]=shape.f(b[std::size_t(i)]);}
    }
    void adaaAtRate(std::vector<double>& a,std::vector<double>& b,int n){
        for(int i=0;i<n;++i){double l=a[std::size_t(i)],r=b[std::size_t(i)];a2.run(shape.g,l,r);
            a[std::size_t(i)]=l;b[std::size_t(i)]=r;}
    }
    void process(float* L,float* R,int n){
        switch(kind){
        case Kind::point:
            for(int i=0;i<n;++i){L[i]=float(shape.f(L[i]));R[i]=float(shape.f(R[i]));} break;
        case Kind::adaa1:
            for(int i=0;i<n;++i){double l=L[i],r=R[i];a1.run(shape,l,r);L[i]=float(l);R[i]=float(r);} break;
        case Kind::adaa2:
            for(int i=0;i<n;++i){double l=L[i],r=R[i];a2.run(shape.g,l,r);L[i]=float(l);R[i]=float(r);} break;
        case Kind::os2:
            hb.up(L,n,h1);hb.up(R,n,h2);foldAtRate(h1,h2,2*n);
            hb.down(h1,n,d1);hb.down(h2,n,d2);
            std::copy(d1.begin(),d1.begin()+n,L);std::copy(d2.begin(),d2.begin()+n,R);break;
        case Kind::os4:
            hb.up(L,n,h1);hb.up(R,n,h2);
            hb.up(h1.data(),2*n,h3);hb.up(h2.data(),2*n,h4);
            foldAtRate(h3,h4,4*n);
            hb.down(h3,2*n,d1);hb.down(h4,2*n,d2);
            e1.assign(d1.begin(),d1.begin()+2*n);e2.assign(d2.begin(),d2.begin()+2*n);
            hb.down(e1,n,d1);hb.down(e2,n,d2);
            std::copy(d1.begin(),d1.begin()+n,L);std::copy(d2.begin(),d2.begin()+n,R);break;
        case Kind::os2adaa:
            hb.up(L,n,h1);hb.up(R,n,h2);adaaAtRate(h1,h2,2*n);
            hb.down(h1,n,d1);hb.down(h2,n,d2);
            std::copy(d1.begin(),d1.begin()+n,L);std::copy(d2.begin(),d2.begin()+n,R);break;
        case Kind::os4adaa:
            hb.up(L,n,h1);hb.up(R,n,h2);
            hb.up(h1.data(),2*n,h3);hb.up(h2.data(),2*n,h4);
            adaaAtRate(h3,h4,4*n);
            hb.down(h3,2*n,d1);hb.down(h4,2*n,d2);
            e1.assign(d1.begin(),d1.begin()+2*n);e2.assign(d2.begin(),d2.begin()+2*n);
            hb.down(e1,n,d1);hb.down(e2,n,d2);
            std::copy(d1.begin(),d1.begin()+n,L);std::copy(d2.begin(),d2.begin()+n,R);break;
        }
    }
};

// ---------- alias analysis ----------
constexpr int window=16384;
struct Alias{double aliased=0,ratio=0,inputPower=.25*window*window;};
Alias analyse(const std::vector<float>& left,int bin0){
    Alias r;double e=0;
    for(int i=0;i<window;++i){const double x=left[i];e+=x*x;}
    const double total=.5*window*e;double harmonic=0;
    for(int b=bin0;b<=window/2;b+=bin0){
        const double c=2*std::cos(2*PI*b/window);double s1=0,s2=0;
        for(int i=0;i<window;++i){const double s=left[i]+c*s1-s2;s2=s1;s1=s;}
        harmonic+=s1*s1+s2*s2-c*s1*s2;
    }
    r.aliased=std::max(0.0,total-harmonic);r.ratio=total>0?r.aliased/total:0;return r;
}

Alias measure(Kind kind,double amount,double rate,int bin0){
    const std::size_t settle=static_cast<std::size_t>(rate*.2);
    const double f0=bin0*rate/window;
    std::vector<float> L(settle+window),R(L.size());
    for(std::size_t i=0;i<L.size();++i)L[i]=float(std::sin(2*PI*f0*i/rate));
    R=L;Processor p(kind,amount);
    // One pass so the oversampling filters keep their history across the
    // whole analysis window (a real-time implementation runs them serially).
    p.process(L.data(),R.data(),static_cast<int>(L.size()));
    std::vector<float> s(L.begin()+std::ptrdiff_t(settle),L.end());
    return analyse(s,bin0);
}

double cpuNs(Kind kind,double amount,int reps=8000){
    constexpr int block=128;
    Processor p(kind,amount);
    std::vector<float> L(block),R(block),baseL(block),baseR(block);
    for(int i=0;i<block;++i){const double t=i;
        baseL[i]=float(.31*std::sin(t*.017)+.12*std::sin(t*.101));
        baseR[i]=float(.27*std::sin(t*.013+.7)+.09*std::sin(t*.071));}
    auto restore=[&]{std::copy(baseL.begin(),baseL.end(),L.begin());std::copy(baseR.begin(),baseR.end(),R.begin());};
    for(int n=0;n<500;++n){restore();p.process(L.data(),R.data(),block);}
    const auto t0=std::chrono::steady_clock::now();
    for(int n=0;n<reps;++n){restore();p.process(L.data(),R.data(),block);}
    const auto t1=std::chrono::steady_clock::now();
    const double ns=std::chrono::duration<double,std::nano>(t1-t0).count();
    return ns/(double(reps)*block*2);
}
double copyNs(int reps=8000){
    constexpr int block=128;std::vector<float> L(block),R(block),b(block);
    for(int i=0;i<block;++i)b[i]=float(std::sin(i*.1));
    const auto t0=std::chrono::steady_clock::now();
    for(int n=0;n<reps;++n){std::copy(b.begin(),b.end(),L.begin());std::copy(b.begin(),b.end(),R.begin());}
    const auto t1=std::chrono::steady_clock::now();
    return std::chrono::duration<double,std::nano>(t1-t0).count()/(double(reps)*block*2);
}

int main(){
    const char* names[]={"point","ADAA1","ADAA2","2xOS","4xOS","2xOS+ADAA2","4xOS+ADAA2"};
    const double rate=44100;
    printf("44.1 kHz, absolute folded power relative to a unit sine, ratio of output power\n");
    for(int bin:{683,1707,2731}){
        printf("f0 %.0f Hz\n",bin*rate/window);
        for(double amt:{.28,.5,1.0}){
            printf(" amt %.2f",amt);
            for(int k=0;k<7;++k){const auto a=measure(Kind(k),amt,rate,bin);
                printf(" | %s %6.1f dB %.3f",names[k],10*std::log10(a.aliased/a.inputPower+1e-30),a.ratio);}
            printf("\n");
        }
    }
    printf("\nCPU (this container, 128-sample stereo blocks, ns per input sample per channel)\n");
    printf(" harness buffer-restore overhead: %.2f ns\n",copyNs());
    for(double amt:{.5,1.0}){
        printf(" amt %.1f:",amt);
        for(int k=0;k<7;++k)printf(" %s %.2f",names[k],cpuNs(Kind(k),amt));
        printf("\n");
    }
    HalfBand hb(31);
    printf("\n31-tap linear-phase halfband group delay: 2x %d input samples, 4x %d input samples\n",
           hb.delayInputSamples(1),hb.delayInputSamples(2));
    printf("ADAA1/ADAA2 add no integer latency; ADAA's inherent p/2 group delay is ~%.1f / ~%.1f sample.\n",.5,1.0);
    return 0;
}
