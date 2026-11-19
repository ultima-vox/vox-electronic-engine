// Authoring tool, never used by the plugin or automated factory admission.
// g++ -std=c++20 -O2 -Iproducts/fx -Ilibs/vox-preset \
//   docs/ultima-vox-music/fx/evidence/export-wave-a-presets.cpp \
//   libs/vox-preset/StateContainer.cpp -o build/export-wave-a-presets
#include "content/FactoryCandidates.h"
#include "engine/PortablePreset.h"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>

int main(int argc,char** argv){
    using namespace vox::fx;
    if(argc!=2)return 1;
    const std::filesystem::path root(argv[1]);
    std::filesystem::create_directories(root/"presets");
    std::ofstream setup(root/"audition-settings.tsv");
    setup<<"ID\tMIDI-note\tvelocity\tduration-param-seconds\toutput\tBPM";
    for(int i=1;i<=4;++i)setup<<"\tFX"<<i<<"-type\tFX"<<i<<"-wet\tFX"<<i<<"-amount";
    setup<<'\n'<<std::setprecision(std::numeric_limits<float>::max_digits10);
    for(const auto& e:content::reviewEntries){
        const auto capture=content::makePreview(e,48000);
        portable::Sound sound;
        sound.state.program=capture.program;sound.state.pattern=capture.pattern;sound.state.macros=capture.settings.macroRoutes;
        sound.state.effects=0;
        for(unsigned i=0;i<4;++i)sound.state.effects|=std::uint64_t(effects::pack(capture.settings.effects[i]))<<(16*i);
        portable::Tags tags{std::string(e.name),std::string(e.style),std::string(e.mood),std::string(content::useCase(e)),static_cast<unsigned>(e.role)};
        std::vector<std::byte> bytes,again;
        portable::Sound restored;portable::Tags read;
        if(capture.useUserSample||!portable::encode(sound,bytes,&tags)||!portable::decode(bytes,restored,&read)||
           !portable::encode(restored,again,&read)||again!=bytes)return 2;
        std::ofstream preset(root/"presets"/(std::string(e.id)+".voxfx"),std::ios::binary);
        preset.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
        if(!preset)return 3;
        const auto& s=capture.settings;
        setup<<e.id<<'\t'<<s.note<<'\t'<<s.velocity<<'\t'<<s.duration<<'\t'<<s.gain<<'\t'<<s.tempo;
        for(const auto fx:s.effects)setup<<'\t'<<static_cast<unsigned>(fx.type)<<'\t'<<fx.wet<<'\t'<<fx.amount;
        setup<<'\n';
    }
    return setup?0:4;
}
