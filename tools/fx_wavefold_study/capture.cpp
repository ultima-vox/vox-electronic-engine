// Diagnostic-only capture of the existing production FX Chain. No DSP changes.
#include "engine/Effects.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

int main(int argc,char** argv) {
    if(argc!=5)return 2;
    const int rate=std::atoi(argv[1]),bin=std::atoi(argv[2]),window=std::atoi(argv[3]);
    const bool multi=std::atoi(argv[4])!=0;
    if(rate<8000||bin<=0||window<1024)return 2;
    vox::fx::effects::Chain chain;
    if(!chain.prepare(rate)||!chain.set(0,{vox::fx::effects::Type::wavefold,1.f,1.f}))return 3;
    const int settle=rate/2,total=settle+window;
    std::vector<float> left(total),right(total);
    const double pi=std::acos(-1.0);
    const int b2=static_cast<int>(std::lround(9000.0*window/rate));
    const int b3=static_cast<int>(std::lround(11000.0*window/rate));
    for(int i=0;i<total;++i){
        const auto tone=[&](int b){return std::sin(2*pi*b*i/window);};
        left[i]=static_cast<float>(multi?.42*tone(bin)+.31*tone(b2)+.22*tone(b3):tone(bin));
    }
    right=left;
    chain.process(left.data(),right.data(),left.size());
    return std::fwrite(left.data()+settle,sizeof(float),window,stdout)==static_cast<std::size_t>(window)?0:4;
}
