#include "content/FactoryCandidates.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>

int main(int argc,char** argv) {
    using namespace vox::fx;
    const std::filesystem::path out=argc==2?argv[1]:"";
    if (!out.empty()) std::filesystem::create_directories(out);
    std::set<std::string_view> ids,names;
    for (const auto& e:content::reviewEntries) {
        if (e.id.empty()||!ids.insert(e.id).second||!names.insert(e.name).second||e.mood.empty()||e.style.empty()) return 1;
        for (auto rate:{44100u,48000u,96000u}) {
            const auto capture=content::makePreview(e,rate);
            std::vector<std::uint8_t> samples;
            if (capture.useUserSample||!offline::renderWav(capture,samples)||samples.size()<50) return 2;
            double peak=0,avgL=0,avgR=0;
            const auto decode=[&](std::size_t i){
                const std::int32_t x=samples[i]|(samples[i+1]<<8)|(samples[i+2]<<16);
                return (x>=0x800000?x-0x1000000:x)/8388607.0;
            };
            const auto frames=(samples.size()-44)/6;
            for (std::size_t i=44;i<samples.size();i+=6) {
                const auto l=decode(i),r=decode(i+3);avgL+=l;avgR+=r;
                peak=std::max(peak,std::max(std::abs(l),std::abs(r)));
            }
            const auto dc=std::max(std::abs(avgL),std::abs(avgR))/frames;
            std::printf("%.*s %u Hz %zu frames peak %.6f DC %.7f\n",static_cast<int>(e.id.size()),e.id.data(),rate,frames,peak,dc);
            if (!(peak>.0001&&peak<.98&&dc<.01)) return 3;
            if (rate==48000) {
                std::vector<std::uint8_t> repeat;
                if(!offline::renderWav(capture,repeat)||samples!=repeat)return 4;
                if (!out.empty()) {
                    std::ofstream file(out/(std::string(e.id)+".wav"),std::ios::binary);
                    file.write(reinterpret_cast<const char*>(samples.data()),static_cast<std::streamsize>(samples.size()));
                    if(!file)return 5;
                }
            }
        }
    }
    return 0;
}
