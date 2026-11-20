#pragma once
#include <juce_core/juce_core.h>
#include <array>
#include <cstdint>

namespace vox::fx::fileexport::detail {
// Create a new scratch directory, never reusing an existing entry.
bool createPrivateDirectory(const juce::File& directory);

enum class TargetState { absent, regular, blocked };
struct TargetObservation {
    TargetState state=TargetState::blocked;
    std::array<std::uint64_t,8> revision{};
    bool operator==(const TargetObservation&) const = default;
};
// Best-effort file ID and metadata. No content read and no leaf-link follow.
// A final observation is not an atomic conditional replace.
TargetObservation observeTarget(const juce::File&);

// Publish a closed staging file, never by copying or deleting the destination
// first. A no-replace commit fails if a destination entry already exists.
// Caller owns staging cleanup on either outcome (including a POSIX hard link).
bool commitFile(const juce::File& staged,const juce::File& destination,bool replaceExisting);
}
