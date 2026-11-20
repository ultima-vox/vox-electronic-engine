#pragma once
#include "PortablePreset.h"
#include "PresetFileCommit.h"
#include <thread>

namespace vox::fx::library {
inline constexpr unsigned maxFiles=512;
// No recursive search, no symlinks. Never derives filesystem paths from
// display names or tags. The browser may also open a loose preset explicitly.
inline bool managedFile(const juce::File& root,const juce::File& file){
    return file.getParentDirectory()==root&&!root.isSymbolicLink()&&!file.isSymbolicLink()&&file.hasFileExtension("voxfx");
}
inline bool read(const juce::File& file,portable::Sound& sound,portable::Tags* tags=nullptr){
    try {
        if(!file.hasFileExtension("voxfx")||file.isSymbolicLink())return false;
        const auto expected=file.getSize();
        if(expected<=0||expected>static_cast<juce::int64>(portable::maxBytes))return false;
        juce::FileInputStream input(file);
        const auto size=input.getTotalLength();if(!input.openedOk()||size<=0||size>static_cast<juce::int64>(portable::maxBytes))return false;
        std::vector<std::byte> bytes(static_cast<std::size_t>(size));
        for(std::size_t at=0;at<bytes.size();){const auto n=input.read(bytes.data()+at,static_cast<int>(bytes.size()-at));if(n<=0)return false;at+=static_cast<unsigned>(n);}
        char extra=0;if(input.read(&extra,1)!=0)return false;
        return portable::decode(bytes,sound,tags);
    }catch(...){return false;}
}
inline bool write(const juce::File& file,const portable::Sound& sound,const portable::Tags* tags=nullptr){
    try {
        if(!file.hasFileExtension("voxfx")||file.isSymbolicLink())return false;
        std::vector<std::byte> bytes;if(!portable::encode(sound,bytes,tags))return false;
        return writePresetBytes(file,bytes);
    }catch(...){return false;}
}
// Worker-only directory scanning. The message thread sees only bounded
// metadata; it never decodes all 512 user sounds synchronously.
struct IndexEntry {
    juce::File file;
    portable::Tags tags;
    PlayMode mode;
    unsigned bars;
    source::Tonality key;
    bool pitched;
    sample::Reference::Kind asset;
};
inline IndexEntry describe(const juce::File& file,portable::Tags tags,const portable::Sound& sound){
    if(tags.name.empty())tags.name=file.getFileNameWithoutExtension().toStdString();
    const auto& p=sound.state.program;
    const bool pitched=std::any_of(p.layers.begin(),p.layers.end(),[](const auto& layer){return layer.enabled&&layer.family!=NoiseFamily::riser;});
    return {file,std::move(tags),p.mode,p.mode==PlayMode::sequence?sound.state.pattern.bars:p.bars,p.tonality,pitched,sound.sample.kind};
}
struct Index {std::vector<IndexEntry> entries;unsigned invalid=0;bool limited=false,unavailable=false;};
class IndexJob {
public:
    explicit IndexJob(juce::File root)
        :worker_([this,root=std::move(root)](std::stop_token stop){
            try {
                if(root.isSymbolicLink())index_.unavailable=true;
                else {
                    unsigned seen=0;
                    for(const auto& entry:juce::RangedDirectoryIterator(root,false,"*.voxfx",juce::File::findFiles,juce::File::FollowSymlinks::no)){
                        if(stop.stop_requested())break;
                        if(seen++>=maxFiles){index_.limited=true;break;}
                        const auto file=entry.getFile();portable::Sound sound;portable::Tags metadata;
                        if(!managedFile(root,file)||!read(file,sound,&metadata)){++index_.invalid;continue;}
                        index_.entries.push_back(describe(file,std::move(metadata),sound));
                    }
                }
            }catch(...){index_.unavailable=true;}
            done_.store(true,std::memory_order_release);
        }){}
    bool ready() const noexcept {return done_.load(std::memory_order_acquire);}
    Index take() {return ready()?std::move(index_):Index{};}
private:
    Index index_;
    std::atomic<bool> done_{false};
    // The destructor only waits for the current bounded (<64 KiB) local
    // preset read, not for enumeration/decoding of the whole directory.
    std::jthread worker_;
};
inline juce::File userRoot(){return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
    .getChildFile("ULTIMA VOX").getChildFile("VOX FX").getChildFile("User Presets");}
} // namespace vox::fx::library
