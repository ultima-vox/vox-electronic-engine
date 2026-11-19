#pragma once
#include "PortablePreset.h"
#include "FileExport.h"

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
        auto payload=std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(bytes.data()),bytes.size());
        return fileexport::writeBytes(file,{payload.begin(),payload.end()});
    }catch(...){return false;}
}
inline juce::File userRoot(){return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
    .getChildFile("ULTIMA VOX").getChildFile("VOX FX").getChildFile("User Presets");}
} // namespace vox::fx::library
