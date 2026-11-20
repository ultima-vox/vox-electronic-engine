#include "FileCommit.h"

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

TargetObservation observeTarget(const juce::File& file) {
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

bool commitFile(const juce::File& staged,const juce::File& destination,bool replaceExisting) {
    const auto& from=staged.getFullPathName();
    const auto& to=destination.getFullPathName();
#if JUCE_WINDOWS
    // No MOVEFILE_COPY_ALLOWED: a failed same-volume move must stay a failure.
    return ::MoveFileExW(from.toWideCharPointer(),to.toWideCharPointer(),
                        replaceExisting?MOVEFILE_REPLACE_EXISTING:0)!=0;
#else
    if(replaceExisting)return ::rename(from.toRawUTF8(),to.toRawUTF8())==0;
    // link(2) publishes complete bytes without a check-then-rename race.
    // Filesystems without hard-link support fail closed, with no copy fallback.
    return ::link(from.toRawUTF8(),to.toRawUTF8())==0;
#endif
}
}
