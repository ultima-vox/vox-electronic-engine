#include "FileCommit.h"
#include "PresetFileCommit.h"
#include <array>
#include <cstdint>

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
#else
 #include <cerrno>
 #include <cstdio>
 #include <sys/stat.h>
 #include <unistd.h>
#endif

namespace vox::fx::fileexport::detail {
bool createPrivateDirectory(const juce::File& directory) {
    const auto& path=directory.getFullPathName();
#if JUCE_WINDOWS
    // The OS user temporary directory supplies the inherited ACL.
    return ::CreateDirectoryW(path.toWideCharPointer(),nullptr)!=0;
#else
    return ::mkdir(path.toRawUTF8(),0700)==0;
#endif
}

TargetState observeTarget(const juce::File& file) {
    if(file==juce::File{})return TargetState::unavailable;
    const auto& path=file.getFullPathName();
#if JUCE_WINDOWS
    // Alternate data streams would modify an existing file's container, not
    // create a normal new file. A colon is reserved in every component other
    // than a drive prefix.
    const auto withoutDrive=path.length()>1&&path[1]==':'?path.substring(2):path;
    if(withoutDrive.containsChar(':'))return TargetState::unavailable;
    if(::GetFileAttributesW(path.toWideCharPointer())!=INVALID_FILE_ATTRIBUTES)return TargetState::occupied;
    const auto error=::GetLastError();
    return (error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND)?TargetState::absent:TargetState::unavailable;
#else
    struct stat info{};
    if(::lstat(path.toRawUTF8(),&info)==0)return TargetState::occupied;
    return errno==ENOENT?TargetState::absent:TargetState::unavailable;
#endif
}

PublishResult publishNewFile(const juce::File& staged,const juce::File& destination) {
    const auto& from=staged.getFullPathName();
    const auto& to=destination.getFullPathName();
#if JUCE_WINDOWS
    // No MOVEFILE_REPLACE_EXISTING and no MOVEFILE_COPY_ALLOWED.
    if(::MoveFileExW(from.toWideCharPointer(),to.toWideCharPointer(),0)!=0)return PublishResult::published;
    const auto error=::GetLastError();
    // Some existing targets (e.g. directories) report access denied. The
    // observation after failure is used for the message, never for a retry.
    return error==ERROR_ALREADY_EXISTS||error==ERROR_FILE_EXISTS||observeTarget(destination)==TargetState::occupied
        ?PublishResult::occupied:PublishResult::failure;
#else
    // link(2) publishes complete bytes or fails if the name is already in
    // use. Unsupported filesystems fail closed; no rename or copy fallback.
    if(::link(from.toRawUTF8(),to.toRawUTF8())==0)return PublishResult::published;
    return errno==EEXIST?PublishResult::occupied:PublishResult::failure;
#endif
}
}

namespace vox::fx::library {
namespace {
enum class TargetState { absent, regular, blocked };
struct TargetObservation {
    TargetState state=TargetState::blocked;
    std::array<std::uint64_t,8> revision{};
    bool operator==(const TargetObservation&) const = default;
};
TargetObservation observePresetTarget(const juce::File& file) {
    if(file==juce::File{})return {};
    const auto& path=file.getFullPathName();
#if JUCE_WINDOWS
    const auto handle=::CreateFileW(path.toWideCharPointer(),FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(handle==INVALID_HANDLE_VALUE){
        const auto error=::GetLastError();
        return {(error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND)?TargetState::absent:TargetState::blocked};
    }
    BY_HANDLE_FILE_INFORMATION info{};
    const bool regular=::GetFileType(handle)==FILE_TYPE_DISK&&::GetFileInformationByHandle(handle,&info)!=0&&
        (info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_READONLY))==0;
    // Do not rely on last-access time (the reader may itself update it).
    ::CloseHandle(handle);
    if(!regular)return {};
    const auto u64=[](DWORD hi,DWORD lo){return (std::uint64_t{hi}<<32)|lo;};
    return {TargetState::regular,{info.dwVolumeSerialNumber,u64(info.nFileIndexHigh,info.nFileIndexLow),
        u64(info.nFileSizeHigh,info.nFileSizeLow),u64(info.ftLastWriteTime.dwHighDateTime,info.ftLastWriteTime.dwLowDateTime),
        u64(info.ftCreationTime.dwHighDateTime,info.ftCreationTime.dwLowDateTime),info.dwFileAttributes}};
#else
    struct stat info{};
    if(::lstat(path.toRawUTF8(),&info)!=0)return {errno==ENOENT?TargetState::absent:TargetState::blocked};
    if(!S_ISREG(info.st_mode)||(info.st_mode&(S_IWUSR|S_IWGRP|S_IWOTH))==0)return {};
 #if JUCE_MAC || JUCE_IOS
    const auto& modified=info.st_mtimespec;
    const auto& changed=info.st_ctimespec;
 #else
    const auto& modified=info.st_mtim;
    const auto& changed=info.st_ctim;
 #endif
    return {TargetState::regular,{static_cast<std::uint64_t>(info.st_dev),static_cast<std::uint64_t>(info.st_ino),
        static_cast<std::uint64_t>(info.st_size),static_cast<std::uint64_t>(modified.tv_sec),static_cast<std::uint64_t>(modified.tv_nsec),
        static_cast<std::uint64_t>(changed.tv_sec),static_cast<std::uint64_t>(changed.tv_nsec),static_cast<std::uint64_t>(info.st_mode)}};
#endif
}

} // namespace

bool writePresetBytes(const juce::File& target,std::span<const std::byte> payload) {
    if(!target.hasFileExtension("voxfx")||payload.empty())return false;
    const auto original=observePresetTarget(target);
    if(original.state==TargetState::blocked)return false;
    juce::TemporaryFile temporary(target);
    {
        juce::FileOutputStream stream(temporary.getFile());
        if(!stream.openedOk()||!stream.write(payload.data(),payload.size()))return false;
        stream.flush();if(stream.getStatus().failed())return false;
    }
    if(temporary.getFile().getSize()!=static_cast<juce::int64>(payload.size()))return false;
    if(original.state==TargetState::absent)return fileexport::detail::publishNewFile(temporary.getFile(),target)==fileexport::detail::PublishResult::published;
    // Keep the pre-existing best-effort preset tag/edit guard. This is not
    // used by audio or SMF and is not an atomic identity comparison.
    if(observePresetTarget(target)!=original)return false;
    const auto& from=temporary.getFile().getFullPathName();const auto& to=target.getFullPathName();
#if JUCE_WINDOWS
    return ::MoveFileExW(from.toWideCharPointer(),to.toWideCharPointer(),MOVEFILE_REPLACE_EXISTING)!=0;
#else
    return ::rename(from.toRawUTF8(),to.toRawUTF8())==0;
#endif
}
}
