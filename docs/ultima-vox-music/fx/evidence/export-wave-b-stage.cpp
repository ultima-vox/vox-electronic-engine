// Authoring tool for Wave B staging cues; not a factory installer.
// Compile with pinned JUCE core/cryptography and vox_preset. See triage handoff.
#include "content/ReviewWaveBAssets.h"
#include "content/ReviewWaveBPerformance.h"
#include "engine/PortablePreset.h"
#include "engine/SampleDecode.h"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <set>

namespace {
using namespace vox::fx;
namespace b=content::waveb;
struct RenderStats { double peak=0,monoRatio=0,last5=0,dc=0; };
RenderStats measure(const std::vector<std::uint8_t>& wav,unsigned rate){
    RenderStats value;
    const auto decode=[&](std::size_t i){const int word=wav[i]|(wav[i+1]<<8)|(wav[i+2]<<16);return ((word^0x800000)-0x800000)/8388607.;};
    double left=0,right=0,total=0,mono=0;
    for(std::size_t i=44;i+5<wav.size();i+=6){
        const auto l=decode(i),r=decode(i+3);left+=l;right+=r;
        const auto peak=std::max(std::abs(l),std::abs(r));value.peak=std::max(value.peak,peak);
        total+=(l*l+r*r)*.5;mono+=(l+r)*(l+r)*.25;
        if(wav.size()-i<=rate/200*6)value.last5=std::max(value.last5,peak);
    }
    value.monoRatio=total>0?std::sqrt(mono/total):0;
    value.dc=std::max(std::abs(left),std::abs(right))*6/(wav.size()-44);
    return value;
}
bool write(const std::filesystem::path& path,const auto& bytes){
    std::ofstream f(path,std::ios::binary);f.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));return bool(f);
}
std::string digest(const auto& bytes){return juce::SHA256(bytes.data(),bytes.size()).toHexString().toStdString();}
}

int main(int argc,char** argv){
    if(argc!=2)return 1;
    const std::filesystem::path root(argv[1]);
    for(const auto* dir:{"solo","presets","sources"})std::filesystem::create_directories(root/dir);
    std::ofstream list(root/"manifest.tsv"),metrics(root/"metrics.tsv"),settings(root/"settings.tsv");
    list<<"id\tname\trole\tstyle\tmode\tbpm\tbars\tnote\tpair\ttonality\tsource\tsource-sha256\tFPRG\tFMOD\tsolo-48k-sha256\tstatus\tuse\n";
    metrics<<"id\trate\tframes\tpeak\tDC\tmono/stereo-rms\tlast-5ms-peak\n"<<std::fixed<<std::setprecision(8);
    settings<<"id\tBPM\tMIDI-note\tvelocity\tseconds\toutput";
    for(int i=1;i<=4;++i)settings<<"\tFX"<<i<<"-type\tFX"<<i<<"-wet\tFX"<<i<<"-amount";
    settings<<'\n'<<std::setprecision(9);
    std::set<std::string> ids,recipes,soloHashes;
    for(const auto& e:content::reviewEntries)ids.insert(std::string(e.id));
    const auto audit=[&](const content::Entry& e,std::string_view pair,offline::Capture capture,const sample::Reference& source){
        const std::string id(e.id);
        if(!ids.insert(id).second||!source.valid()||capture.useUserSample!=(source.kind==sample::Reference::Kind::external))return false;
        std::vector<std::byte> program,motion,preset,again;
        if(!programcodec::encode(capture.program,program)||!macro::codec::encode(capture.settings.macroRoutes,motion)||
           !recipes.insert(digest(program)).second)return false;
        portable::Sound sound,restored;portable::Tags tagsOut;
        sound.state.program=capture.program;sound.state.pattern=capture.pattern;sound.state.macros=capture.settings.macroRoutes;sound.sample=source;sound.state.effects=0;
        for(unsigned i=0;i<4;++i)sound.state.effects|=std::uint64_t(effects::pack(capture.settings.effects[i]))<<(16*i);
        const portable::Tags tags{std::string(e.name),std::string(e.style),std::string(e.mood),std::string(content::useCase(e)),static_cast<unsigned>(e.role)};
        if(!portable::encode(sound,preset,&tags)||!portable::decode(preset,restored,&tagsOut)||restored.sample!=source||
           restored.state.macros!=sound.state.macros||!portable::encode(restored,again,&tagsOut)||preset!=again||
           !write(root/"presets"/(id+".voxfx"),preset))return false;
        if(e.mode==PlayMode::sequence){
            std::vector<std::byte> expected,actual;
            if(!pattern::codec::encode(capture.pattern,expected)||!pattern::codec::encode(restored.state.pattern,actual)||
               expected!=actual)return false;
        }
        std::string solo;
        for(const auto rate:{44100u,48000u,96000u}){
            capture.settings.sampleRate=rate;
            std::vector<std::uint8_t> wav,repeat;
            if(!offline::renderWav(capture,wav)||wav.size()<50||((wav.size()-44)%6)!=0)return false;
            const auto m=measure(wav,rate);
            if(m.peak<=.0001||m.peak>=.5||m.dc>=.01||m.monoRatio<.8||m.last5>=.001)return false;
            metrics<<id<<'\t'<<rate<<'\t'<<(wav.size()-44)/6<<'\t'<<m.peak<<'\t'<<m.dc<<'\t'<<m.monoRatio<<'\t'<<m.last5<<'\n';
            if(rate==48000){
                // Receiving-host values are re-applied from the sidecar;
                // use the decoded Program, Pattern and Motion, not the input objects.
                auto reopened=capture;
                reopened.program=restored.state.program;reopened.pattern=restored.state.pattern;
                reopened.settings.macroRoutes=restored.state.macros;
                if(!offline::renderWav(reopened,repeat)||repeat!=wav||!write(root/"solo"/(id+".wav"),wav))return false;
                solo=digest(wav);if(!soloHashes.insert(solo).second)return false;
            }
        }
        constexpr const char* roles[]{"impact","riser","fall","sweep","zap","rhythm","atmosphere","stinger"};
        constexpr const char* modes[]{"oneShot","gate","continuous","loop","sequence","transition"};
        list<<id<<'\t'<<e.name<<'\t'<<roles[static_cast<unsigned>(e.role)]<<'\t'<<e.style<<'\t'<<modes[static_cast<unsigned>(e.mode)]
            <<'\t'<<e.bpm<<'\t'<<(e.mode==PlayMode::sequence?unsigned(capture.pattern.bars):unsigned(e.bars))<<'\t'<<unsigned(e.note)<<'\t'<<pair<<'\t'<<(e.pitchedSource?content::scaleName(capture.program.tonality.scale):"keyless")
            <<'\t'<<(capture.useUserSample?source.path:"built-in")<<'\t'<<(capture.useUserSample?source.sha256:"-")<<'\t'
            <<std::to_integer<unsigned>(program[0])<<'\t'<<std::to_integer<unsigned>(motion[0])<<'\t'<<solo<<"\tstaged-review-only\t"<<content::useCase(e)<<'\n';
        settings<<id<<'\t'<<e.bpm<<'\t'<<unsigned(e.note)<<'\t'<<capture.settings.velocity<<'\t'<<capture.settings.duration<<'\t'<<capture.settings.gain;
        for(const auto& fx:capture.settings.effects)settings<<'\t'<<static_cast<unsigned>(fx.type)<<'\t'<<fx.wet<<'\t'<<fx.amount;
        settings<<'\n';return true;
    };
    for(const auto& cue:b::staged)if(!audit(cue.sound.entry,cue.pair,b::makePreview(cue,48000),{}))return 2;
    for(const auto& cue:b::assetCues){
        const auto bytes=b::sourceWav(cue.asset);sample::Decoded decoded;
        if(!sample::decodeWav(bytes,decoded)||!write(root/"sources"/cue.sourceName,bytes))return 3;
        auto capture=b::makeAssetPreview(cue);capture.sampleRate=decoded.rate;capture.sample=std::move(decoded.mono);
        sample::Reference source{sample::Reference::Kind::external,cue.sourceName,decoded.sha256.toStdString()};
        if(!audit(cue.sound.entry,"-",std::move(capture),source))return 4;
    }
    for(const auto& cue:b::performance){
        const auto capture=b::makePreview(cue,48000);
        if(capture.settings.macroRoutes.count!=cue.routes.size()||!audit(cue.sound.entry,"-",capture,{}))return 5;
    }
    return list&&metrics&&settings?0:6;
}
