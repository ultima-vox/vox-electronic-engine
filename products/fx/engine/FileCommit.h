#pragma once
#include <juce_core/juce_core.h>

namespace vox::fx::fileexport::detail {
// Publish a closed staging file, never by copying or deleting the destination
// first. A no-replace commit fails if a destination entry already exists.
// Caller owns staging cleanup on either outcome (including a POSIX hard link).
bool commitFile(const juce::File& staged,const juce::File& destination,bool replaceExisting);
}
