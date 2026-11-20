#include "engine/GeneratorState.h"
#include <cstdio>
#include <limits>
int main(){
    using namespace vox::fx::generator;int fails=0;auto check=[&](bool ok,const char* m){if(!ok){std::printf("FAIL: %s\n",m);++fails;}};
    ControllerState value;value.seed=123456;value.locks.layers={all,gain,pan|timing,0};
    value.style=4;value.complexity=2;value.intensity=.2f;value.energy=.4f;value.stereo=.8f;
    std::vector<std::byte> bytes;ControllerState result;
    check(statecodec::encode(value,bytes)&&statecodec::decode(bytes,result)&&result.seed==value.seed&&result.locks.layers==value.locks.layers&&result.style==4&&result.complexity==2&&
          result.intensity==.2f&&result.energy==.4f&&result.stereo==.8f,"generator state roundtrip");
    Result generated;check(generate(result.settings(5),Result{},Locks{},generated)&&
        generated.program.layers[0].family==vox::fx::NoiseFamily::laser&&generated.program.layers[0].enabled&&
        generated.program.layers[1].enabled&&!generated.program.layers[2].enabled&&
        !generated.program.layers[3].enabled,"decoded controls drive real generation");
    auto quiet=result;quiet.intensity=0;quiet.energy=0;quiet.stereo=0;
    Result low;check(generate(quiet.settings(5),Result{},Locks{},low)&&
        low.program.layers[0].gain<generated.program.layers[0].gain&&
        low.program.layers[0].gainCurve.nodes[0].value<generated.program.layers[0].gainCurve.nodes[0].value&&
        low.program.layers[0].pan==0,"intensity, energy and stereo change generated program");
    auto previous=result;auto future=bytes;future[0]=std::byte{5};
    check(!statecodec::decode(future,result)&&result.seed==previous.seed,"future rejected transactionally");
    for(std::size_t i=0;i<bytes.size();++i)check(!statecodec::decode(std::span(bytes).first(i),result),"truncation rejected");
    auto legacy=bytes;legacy.resize(44);legacy[0]=std::byte{2};legacy[8]=std::byte{127};legacy[9]=std::byte{0};ControllerState migrated;
    check(statecodec::decode(legacy,migrated)&&migrated.locks.layers==value.locks.layers&&migrated.style==4,
          "v2 full-layer lock expands to all trajectories; partial bits remain unchanged");
    legacy.resize(24);legacy[0]=std::byte{1};
    check(statecodec::decode(legacy,migrated)&&migrated.seed==value.seed&&migrated.complexity==4&&
          migrated.style==0&&migrated.intensity==.7f&&migrated.energy==.7f&&migrated.stereo==.5f,"v1 migrates to historical UI defaults");
    value.intensity=std::numeric_limits<float>::quiet_NaN();check(!statecodec::encode(value,bytes),"nonfinite intensity rejected");value.intensity=.7f;
    value.complexity=5;check(!statecodec::encode(value,bytes),"invalid complexity rejected");value.complexity=4;
    value.seed=0;check(!statecodec::encode(value,bytes),"invalid seed rejected");
    value.seed=1;value.locks.layers[1]=4096;check(!statecodec::encode(value,bytes),"unknown lock rejected");
    value.locks.layers[1]=0;value.locks.duration=true;check(!statecodec::encode(value,bytes),"unserialized duration lock rejected");
    std::printf("FX generator state: %d failure(s)\n",fails);return fails?1:0;
}
