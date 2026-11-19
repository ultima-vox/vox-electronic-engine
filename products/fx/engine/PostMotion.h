#pragma once
#include "Effects.h"
#include "MacroRouting.h"

namespace vox::fx::macro {
// Same signal order for live processing and fixed-tempo export. The caller
// owns the host base values. A route never changes Settings or a host lane.
template<class NextGain>
void renderPost(effects::Chain& fx,Runtime& runtime,float* left,float* right,std::size_t count,
                const std::array<float,8>& macros,NextGain nextGain) noexcept {
    using D=mod::Destination;
    // Fast path stays block-based unless a route actually targets the FX.
    if(!runtime.routesToEffects()&&!runtime.routesTo(D::width)&&!runtime.routesFromFollower()){
        fx.process(left,right,count);
        for(std::size_t i=0;i<count;++i){const auto gain=runtime.processStereo(nextGain(),macros);
            left[i]*=gain.left;right[i]*=gain.right;}
        return;
    }
    for(std::size_t i=0;i<count;++i){
        const auto values=runtime.evaluate(macros,nextGain(),{},std::max(std::abs(left[i]),std::abs(right[i])));
        if(runtime.routesToEffects()){
            constexpr auto first=Runtime::at(D::wet1);
            fx.process(left+i,right+i,1,values.data()+first);
        }else fx.process(left+i,right+i,1);
        if(runtime.routesTo(D::width)){
            const float width=std::clamp(1.f+values[Runtime::at(D::width)],0.f,2.f);
            const float mid=(left[i]+right[i])*.5f,side=(left[i]-right[i])*.5f*width;
            left[i]=mid+side;right[i]=mid-side;
        }
        const auto gain=Runtime::stereo(values);left[i]*=gain.left;right[i]*=gain.right;
    }
}
} // namespace vox::fx::macro
