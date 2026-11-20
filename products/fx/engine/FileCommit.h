#pragma once
#include <juce_core/juce_core.h>

namespace vox::fx::fileexport::detail {
// Create a new private scratch directory, never reusing an existing entry.
bool createPrivateDirectory(const juce::File& directory);

enum class TargetState { absent, occupied, unavailable };
// Early rejection and diagnostics only. No access to the contents and no
// leaf-link follow. An occupied observation is never reclassified for a job.
TargetState observeTarget(const juce::File&);

enum class PublishResult { published, occupied, failure };
// Publish a closed staging file atomically without replacing any entry. No
// copy/delete fallback. Caller owns staging cleanup (including a POSIX link).
PublishResult publishNewFile(const juce::File& staged,const juce::File& destination);
}
