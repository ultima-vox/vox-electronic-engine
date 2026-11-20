// Compile as C++20 against each revision's products/fx include directory.
// Pass an existing, empty output directory. The nine synthetic WAVs stay local.
#include "engine/OfflineRender.h"
#include <cstdio>

int main(int argc,char** argv) {
    if(argc!=2)return 2;
    using namespace vox::fx;
    NoiseProgram program;
    program.layers[0].family=NoiseFamily::glitch;
    for(const int rate:{44100,48000,96000})for(const int character:{-1,0,1}){
        program.layers[0].character=character*.7f;
        offline::Settings settings;
        settings.sampleRate=rate;
        settings.tempo=137;
        settings.duration=.625f;
        std::vector<std::uint8_t> data;
        if(!offline::renderWav(program,settings,data))return 1;
        const auto path=std::string(argv[1])+"/"+std::to_string(rate)+"-"+std::to_string(character)+".wav";
        auto* file=std::fopen(path.c_str(),"wb");
        if(!file)return 1;
        const auto written=std::fwrite(data.data(),1,data.size(),file);
        if(std::fclose(file)!=0||written!=data.size())return 1;
    }
}
