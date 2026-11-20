// Build with GCC C++20 -O2 against either revision's products/fx. Pass an
// existing output directory. The synthetic stereo renders are not committed.
#include "engine/NoiseRiser.h"
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

int main(int argc,char** argv) {
    using namespace vox::fx;
    if(argc!=2)return 2;
    std::vector<float> sample(997);
    for(std::size_t i=0;i<sample.size();++i)sample[i]=static_cast<float>(.41*std::sin(.31*i)+.17*std::cos(.053*i));
    for(const int rate:{44100,48000,96000})for(unsigned mode=0;mode<3;++mode){
        sample::Store store;
        auto engine=std::make_unique<NoiseRiser>();engine->prepare(rate);
        NoiseProgram p;p.layers[0].family=NoiseFamily::granular;
        if(mode==1)p.layers[0].grains={38.f,.18f,false};
        if(mode==2)p.layers[0].grains={62.f,.73f,true};
        const bool external=mode!=0;
        if(!engine->setProgram(p)||(external&&(!store.publish(sample,44100)||!engine->attachSampleStore(&store)))||
           !engine->noteOn(1,61,.8,.375,external))return 1;
        std::vector<float> l(rate/2),r(l.size());
        for(std::size_t at=0;at<l.size();at+=127)
            engine->render(l.data()+at,r.data()+at,static_cast<int>(std::min<std::size_t>(127,l.size()-at)));
        const auto path=std::string(argv[1])+"/"+std::to_string(rate)+"-"+std::to_string(mode)+".stereo-f32";
        auto* out=std::fopen(path.c_str(),"wb");if(!out)return 1;
        const bool ok=std::fwrite(l.data(),sizeof(float),l.size(),out)==l.size()&&
                      std::fwrite(r.data(),sizeof(float),r.size(),out)==r.size();
        if(std::fclose(out)!=0||!ok)return 1;
    }
}
