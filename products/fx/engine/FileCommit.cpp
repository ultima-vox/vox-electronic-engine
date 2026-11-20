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
 #include <cstdio>
 #include <unistd.h>
#endif

namespace vox::fx::fileexport::detail {
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
