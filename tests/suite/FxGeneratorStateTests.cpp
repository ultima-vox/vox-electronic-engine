#include "engine/GeneratorState.h"
#include <cstdio>
int main(){
    using namespace vox::fx::generator;int fails=0;auto check=[&](bool ok,const char* m){if(!ok){std::printf("FAIL: %s\n",m);++fails;}};
    ControllerState value;value.seed=123456;value.locks.layers={all,gain,pan|timing,0};
    std::vector<std::byte> bytes;ControllerState result;
    check(statecodec::encode(value,bytes)&&statecodec::decode(bytes,result)&&result.seed==value.seed&&result.locks.layers==value.locks.layers,"generator state roundtrip");
    auto previous=result;auto future=bytes;future[0]=std::byte{2};
    check(!statecodec::decode(future,result)&&result.seed==previous.seed,"future rejected transactionally");
    for(std::size_t i=0;i<bytes.size();++i)check(!statecodec::decode(std::span(bytes).first(i),result),"truncation rejected");
    value.seed=0;check(!statecodec::encode(value,bytes),"invalid seed rejected");
    value.seed=1;value.locks.layers[1]=128;check(!statecodec::encode(value,bytes),"unknown lock rejected");
    value.locks.layers[1]=0;value.locks.duration=true;check(!statecodec::encode(value,bytes),"unserialized duration lock rejected");
    std::printf("FX generator state: %d failure(s)\n",fails);return fails?1:0;
}
