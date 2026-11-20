// Linux diagnostic. Build the same source with GCC C++20 -O2 against each
// revision's products/fx directory. Measures only four serial delay slots;
// this is not Processor timing, real audio I/O or FX-T18.
#include "engine/Effects.h"
#include <cstdio>
#include <ctime>

namespace {
std::uint64_t threadNanoseconds() {
    timespec t{};
    if(clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t)!=0)return 0;
    return static_cast<std::uint64_t>(t.tv_sec)*1000000000ull+static_cast<std::uint64_t>(t.tv_nsec);
}
}
int main() {
    using namespace vox::fx::effects;
    std::puts("rate,frames,moving,thread_cpu_us_per_block");
    for(const int rate:{44100,48000,96000})for(const int frames:{64,128,512})for(bool moving:{false,true}){
        Chain chain;if(!chain.prepare(rate))return 1;
        for(unsigned slot=0;slot<4;++slot)if(!chain.set(slot,{Type::delay,.32f,.22f}))return 1;
        constexpr int blocks=2048;
        std::array<float,512> l{},r{};
        std::array<std::array<float,8>,2048> routes{};
        for(unsigned i=0;i<routes.size();++i)for(unsigned slot=0;slot<4;++slot)
            routes[i][2*slot+1]=static_cast<float>(.13*std::sin(6.283185307179586*i/routes.size()+.31*slot));
        const auto render=[&](int n){
            l.fill(.13f);r.fill(-.17f);
            if(!moving)chain.process(l.data(),r.data(),frames);
            else for(int i=0;i<frames;++i)chain.process(&l[i],&r[i],1,routes[(n*frames+i)&2047].data());
        };
        for(int i=0;i<256;++i)render(i);
        const auto start=threadNanoseconds();
        for(int n=0;n<blocks;++n)render(n);
        const auto end=threadNanoseconds();
        if(start==0||end<=start||!std::isfinite(l[frames-1])||!std::isfinite(r[frames-1]))return 1;
        std::printf("%d,%d,%d,%.6f\n",rate,frames,moving,(end-start)/(blocks*1000.));
    }
}
