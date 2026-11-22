#pragma once

#include "DrumsLayoutState.h"
#include <juce_core/juce_core.h>

namespace vox::drums {

// Named user workspaces are a message-thread file, never part of a project.
// Applying one copies its DLAY into the processor; it creates no file reference.
class WorkspaceLibrary {
public:
    struct Entry { juce::String name; layout::Document layout; };
    struct Read { std::vector<Entry> entries; juce::String diagnostic; };
    static constexpr int maxEntries = 32, maxNameCharacters = 64;
    static constexpr std::int64_t maxFileBytes = 128 * 1024;
    static juce::File userFile(); // VOX_DRUMS_WORKSPACES overrides the folder.
    explicit WorkspaceLibrary (juce::File file = userFile()) : file_ (std::move (file)) {}
    Read read() const;
    juce::Result write (const std::vector<Entry>&) const;
    const juce::File& file() const noexcept { return file_; }
private:
    juce::File file_;
};

} // namespace vox::drums
