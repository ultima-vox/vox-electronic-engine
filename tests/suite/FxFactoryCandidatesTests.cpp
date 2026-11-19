#include "content/FactoryCandidates.h"
#include "engine/ProgramCodec.h"
#include <map>
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
    std::set<std::uint64_t> audioIds,recipeIds;
    std::map<std::string_view,unsigned> styles;
    std::array<unsigned,8> roles{};std::array<unsigned,6> modes{};
    std::ofstream manifest;
    if(!out.empty()){
        manifest.open(out/"manifest.tsv");
        manifest<<"schema\tID\trevision\tname\trole\tstyle\tmood\tmode\tpitched-source\tstatus\tprovenance\tuse-case\tduration-kind\tduration\tbpm\troot\tscale\tassets\trecipe-fnv64\n";
    }
    constexpr const char* roleNames[]{"impact","riser","fall","sweep","zap","rhythm","atmosphere","stinger"};
    constexpr const char* modeNames[]{"oneShot","gate","continuous","loop","sequence","transition"};
    constexpr auto hash=[](auto&& data){
        std::uint64_t value=0xcbf29ce484222325ULL;
        for(auto octet:data)value=(value^static_cast<std::uint8_t>(octet))*0x100000001b3ULL;
        return value;
    };
    for (const auto& e:content::reviewEntries) {
        if (e.id.empty()||!ids.insert(e.id).second||!names.insert(e.name).second||e.mood.empty()||e.style.empty()) return 1;
        ++roles[static_cast<unsigned>(e.role)];++modes[static_cast<unsigned>(e.mode)];
        ++styles[e.style];
        const auto ref=content::makePreview(e,48000);
        const auto& p=ref.program;
        std::vector<std::byte> canonical;
        auto normalized=p;normalized.tonality={};normalized.bars=0;
        // Disallow duplicate layer/curve/mode structures even if the title,
        // audition pitch, root or duration differs. No seed enters the hash.
        if(!programcodec::encode(normalized,canonical))return 11;
        const auto structural=hash(canonical);
        if(!recipeIds.insert(structural).second){std::printf("Duplicate Program: %s\n",e.id.data());return 12;}
        if(!e.pitchedSource){
            for(const auto& l:p.layers)if(l.enabled&&l.family!=NoiseFamily::riser)return 6;
        }
        if(manifest.is_open())manifest<<content::schemaVersion<<'\t'<<e.id<<'\t'<<e.revision<<'\t'<<e.name<<'\t'<<roleNames[static_cast<unsigned>(e.role)]
            <<'\t'<<e.style<<'\t'<<e.mood<<'\t'<<modeNames[static_cast<unsigned>(e.mode)]<<'\t'<<e.pitchedSource<<"\treview-only\t"<<content::provenance<<'\t'<<content::useCase(e)<<'\t'
            <<(e.bars?"bars":"seconds")<<'\t'<<(e.bars?static_cast<float>(e.bars):e.seconds)<<'\t'<<e.bpm<<'\t'
            <<(e.pitchedSource&&p.tonality.scale!=source::Scale::chromatic?std::to_string(p.tonality.root):"-")<<'\t'
            <<(e.pitchedSource?content::scaleName(p.tonality.scale):"-")<<"\tbuiltin\t"<<std::hex<<structural<<std::dec<<'\n';
        for (auto rate:{44100u,48000u,96000u}) {
            const auto capture=content::makePreview(e,rate);
            std::vector<std::uint8_t> samples;
            if (capture.useUserSample||!offline::renderWav(capture,samples)||samples.size()<50) return 2;
            double peak=0,avgL=0,avgR=0,stereo=0,mono=0,edge=0;
            const auto decode=[&](std::size_t i){
                const std::int32_t x=samples[i]|(samples[i+1]<<8)|(samples[i+2]<<16);
                return (x>=0x800000?x-0x1000000:x)/8388607.0;
            };
            const auto frames=(samples.size()-44)/6;
            for (std::size_t i=44;i<samples.size();i+=6) {
                const auto l=decode(i),r=decode(i+3);avgL+=l;avgR+=r;
                const double samplePeak=std::max(std::abs(l),std::abs(r));peak=std::max(peak,samplePeak);
                stereo+=(l*l+r*r)*.5;mono+=(l+r)*(l+r)*.25;
                if(i>=samples.size()-std::min<std::size_t>(frames,rate/200)*6)edge=std::max(edge,samplePeak);
            }
            const auto dc=std::max(std::abs(avgL),std::abs(avgR))/frames;
            std::printf("%.*s %u Hz %zu frames peak %.6f DC %.7f mono/stereo RMS %.5f end5ms %.7f\n",static_cast<int>(e.id.size()),e.id.data(),rate,frames,peak,dc,std::sqrt(mono/stereo),edge);
            if (!(peak>.0001&&peak<.98&&dc<.01)) return 3;
            if (rate==48000) {
                std::vector<std::uint8_t> repeat;
                if(!offline::renderWav(capture,repeat)||samples!=repeat)return 4;
                std::uint64_t fingerprint=0xcbf29ce484222325ULL;
                for(std::size_t i=44;i<samples.size();++i)fingerprint=(fingerprint^samples[i])*0x100000001b3ULL;
                if(!audioIds.insert(fingerprint).second)return 7;
                if (!out.empty()) {
                    std::ofstream file(out/(std::string(e.id)+".wav"),std::ios::binary);
                    file.write(reinterpret_cast<const char*>(samples.data()),static_cast<std::streamsize>(samples.size()));
                    if(!file)return 5;
                }
            }
        }
    }
    for(unsigned i=0;i<roles.size();++i){std::printf("%s: %u candidates, 0 approved\n",roleNames[i],roles[i]);if(roles[i]!=7)return 8;}
    for(unsigned i=0;i<modes.size();++i)std::printf("mode %s: %u\n",modeNames[i],modes[i]);
    for(const auto& [style,count]:styles)std::printf("style %s: %u\n",style.data(),count);
    if(std::any_of(modes.begin(),modes.end(),[](unsigned n){return n==0;}))return 9;
    if(!out.empty()&&!manifest)return 10;
    return 0;
}
