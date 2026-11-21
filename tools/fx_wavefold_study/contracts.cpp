// Diagnostic regression anchors for the unchanged production Chain.
#include "engine/Effects.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

int main(){
    using namespace vox::fx::effects;
    constexpr double rate=48000;constexpr int settle=24000,n=4096;
    std::vector<float> dry(n),left(settle+n),right(settle+n),first(n);
    for(int i=0;i<n;++i)dry[i]=static_cast<float>(.9*std::sin(2*std::acos(-1.0)*997*i/rate));
    auto run=[&](float wet,float amount){
        Chain chain;if(!chain.prepare(rate)||!chain.set(0,{Type::wavefold,wet,amount}))return false;
        std::fill(left.begin(),left.end(),0);std::copy(dry.begin(),dry.end(),left.begin()+settle);right=left;
        chain.process(left.data(),right.data(),left.size());return true;
    };
    if(!run(0,1))return 1;
    for(int i=0;i<n;++i)if(std::abs(left[settle+i]-dry[i])>1e-6f)return 2;
    if(!run(1,0))return 3;
    for(int i=0;i<n;++i)if(std::abs(left[settle+i]-dry[i])>1e-6f)return 4;
    if(!run(1,1))return 5;
    std::copy(left.begin()+settle,left.end(),first.begin());
    if(!run(1,1)||!std::equal(first.begin(),first.end(),left.begin()+settle))return 6;
    std::puts("unchanged Chain: settled dry, amount-zero, deterministic fresh prepare PASS");
}
