#include "engine/Effects.h"
#include <cstdio>
#include <string>
int main(int argc,char** argv){
    using namespace vox::fx::effects;
    if(argc!=2)return 2;
    for(const int rate:{44100,48000,96000})for(const float amount:{0.f,.43f,1.f}){
        Chain c;if(!c.prepare(rate)||!c.set(0,{Type::delay,.83f,amount}))return 1;
        const auto file=std::string(argv[1])+"/"+std::to_string(rate)+"-"+std::to_string(amount)+".stereo-f32";
        auto* out=std::fopen(file.c_str(),"wb");if(!out)return 1;
        std::array<float,127> a{},b{};
        for(int at=0;at<rate*3;at+=a.size()){
            for(int i=0;i<static_cast<int>(a.size());++i){
                const int sample=at+i;
                a[i]=sample<rate/5?static_cast<float>(.32*std::sin(.227*sample)):0.f;
                b[i]=-.65f*a[i];
            }
            c.process(a.data(),b.data(),a.size());
            if(std::fwrite(a.data(),sizeof(float),a.size(),out)!=a.size()||
               std::fwrite(b.data(),sizeof(float),b.size(),out)!=b.size())return 1;
        }
        if(std::fclose(out)!=0)return 1;
    }
}
