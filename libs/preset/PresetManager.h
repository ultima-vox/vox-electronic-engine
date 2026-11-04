#pragma once
#include <functional>
#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <mutex>

namespace vstengine {

// User Rack scene presets. A scene is the complete project state captured and
// restored through one transactional authority (the plugin's project state);
// the preset module never touches APVTS or sequences itself. Files are
// written atomically to the user preset directory.
class PresetManager final {
public:
    // Scene file schema. Only this exact version and format are accepted.
    static constexpr int currentPresetVersion = 2;
    static constexpr const char* sceneFormat = "rack-project-v5";

    enum class Error {
        none,
        invalidName,
        fileNotFound,
        invalidPreset,
        unsupportedVersion,
        cannotWrite,
        alreadyExists
    };

    struct OperationResult {
        Error error { Error::none };
        juce::String message;

        [[nodiscard]] bool wasOk() const noexcept { return error == Error::none; }
        explicit operator bool() const noexcept { return wasOk(); }
    };

    struct PresetEntry {
        juce::String name;
        juce::File file;
    };

    struct SceneCallbacks {
        std::function<juce::ValueTree()> capture;
        std::function<bool(const juce::ValueTree&)> restore;
    };

    explicit PresetManager(SceneCallbacks scene,
                           juce::File directory = getPresetDirectory());

    OperationResult saveFullPreset(const juce::String& name);
    OperationResult loadFullPreset(const juce::String& name);
    OperationResult loadPreset(const PresetEntry& entry);
    juce::StringArray getFullPresetNames() const;
    juce::Array<PresetEntry> getPresets() const;
    OperationResult renamePreset(const juce::String& oldName,
                                 const juce::String& newName);
    OperationResult renamePreset(const PresetEntry& entry,
                                 const juce::String& newName);
    OperationResult deletePreset(const juce::String& name);
    OperationResult deletePreset(const PresetEntry& entry);
    juce::String getCurrentPresetName() const {
      const std::scoped_lock lock(stateMutex);
      return currentPresetName;
    }

private:
  mutable std::recursive_mutex stateMutex;
  SceneCallbacks sceneCallbacks;
  juce::File presetDirectory;
  juce::String currentPresetName;

  static juce::File getPresetDirectory();
  OperationResult loadPresetFile(const juce::File &file);
  OperationResult validateName(const juce::String &name) const;
  juce::String makeSafeFilename(const juce::String &name) const;
};

} // namespace vstengine
