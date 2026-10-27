#pragma once
#include "sequence/Sequence.h"
#include <functional>
#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <mutex>

namespace vstengine {

// Explicit parameter-state bridge between the preset module and the host
// integration layer (APVTS in the plugin shell). The preset module must not
// know about the plugin class, the editor or Cubase: everything it needs from
// the live plugin is expressed by this interface and passed in at
// construction. Keeps preset save/load unit-testable without a plugin
// instance and out of the realtime path by construction.
class PresetStateStore {
public:
    virtual ~PresetStateStore() = default;

    // Complete parameter state tree (APVTS copyState equivalent).
    [[nodiscard]] virtual juce::ValueTree copyState() const = 0;
    // Atomically install a new complete parameter state tree.
    virtual void replaceState(juce::ValueTree newState) = 0;
    // Convert a plain (denormalized) parameter value to the normalized form
    // used in preset XML, for the parameter with the given ID. Returns the
    // plain value unchanged when the ID is unknown.
    [[nodiscard]] virtual float convertTo0to1(const char* parameterId,
                                              float plainValue) const = 0;
};

class PresetManager final {
public:
    // Explicit preset schema version. Bump when the on-disk format changes and
    // add a migrator in PresetManager.cpp (migrateToCurrentVersion).
    static constexpr int currentPresetVersion = 2;

    enum class PresetKind { sound, full };
    enum class EngineType { none, bass, kick, legacyCombined };
    using SoundEngine = EngineType; // source compatibility with Phase 6 callers
    enum class PresetSource { factory, user };

    enum class Error {
        none,
        invalidName,
        fileNotFound,
        invalidPreset,
        unsupportedVersion,
        cannotWrite,
        alreadyExists,
        readOnly,
        wrongKind,
        wrongEngine
    };

    struct OperationResult {
        Error error { Error::none };
        juce::String message;

        [[nodiscard]] bool wasOk() const noexcept { return error == Error::none; }
        explicit operator bool() const noexcept { return wasOk(); }
    };

    struct PresetEntry {
        juce::String name;
        PresetSource source { PresetSource::user };
        PresetKind kind { PresetKind::sound };
        EngineType engine { EngineType::none };
        juce::File file;

        [[nodiscard]] bool isReadOnly() const noexcept
        {
            return source == PresetSource::factory;
        }
    };

    struct FullStateCallbacks {
        std::function<juce::ValueTree()> capture;
        std::function<bool(const juce::ValueTree&)> restore;
        // Modern Rack scenes have one transactional project authority. The
        // generic manager never commits APVTS/Sequence around these callbacks.
        std::function<juce::ValueTree()> captureComplete;
        std::function<bool(const juce::ValueTree &)> restoreComplete;
    };

    PresetManager(PresetStateStore& stateStore,
                  vstengine::sequence::Sequence& seq,
                  juce::File directory = getPresetDirectory(),
                  FullStateCallbacks fullState = {});
    PresetManager(PresetStateStore& stateStore,
                  vstengine::sequence::Sequence& seq,
                  FullStateCallbacks fullState);

    OperationResult saveSoundPreset(EngineType engine, const juce::String& name);
    OperationResult saveSoundPreset(const juce::String& name,
                                    EngineType engine = EngineType::bass)
    {
        return saveSoundPreset(engine, name);
    }
    OperationResult saveFullPreset(const juce::String& name);
    OperationResult loadSoundPreset(EngineType engine, const juce::String& name);
    OperationResult loadSoundPreset(const juce::String& name);
    OperationResult loadFullPreset(const juce::String& name);
    OperationResult loadPreset(const PresetEntry& entry);
    juce::StringArray getSoundPresetNames() const;
    juce::StringArray getSoundPresetNames(EngineType engine) const;
    juce::StringArray getFullPresetNames() const;
    juce::Array<PresetEntry> getPresets() const;
    OperationResult renamePreset(const juce::String& oldName,
                                 const juce::String& newName);
    OperationResult renamePreset(const PresetEntry& entry,
                                 const juce::String& newName);
    OperationResult deletePreset(const juce::String& name);
    OperationResult deletePreset(const PresetEntry& entry);
    juce::StringArray getFactoryPresetNames() const;
    juce::StringArray getFactoryPresetNames(EngineType engine) const;
    OperationResult loadFactoryPreset(EngineType engine,
                                      const juce::String& name);
    OperationResult loadFactoryPreset(const juce::String& name);
    juce::String getCurrentPresetName() const {
      const std::scoped_lock lock(stateMutex);
      return currentPresetName;
    }

private:
  mutable std::recursive_mutex stateMutex;
  PresetStateStore &store;
  vstengine::sequence::Sequence &sequence;
  juce::File presetDirectory;
  juce::String currentPresetName;
  FullStateCallbacks fullStateCallbacks;

  static juce::File getPresetDirectory();
  bool serializeToXml(juce::XmlElement &xml, PresetKind kind,
                      EngineType engine = EngineType::none);
  bool deserializeFromXml(const juce::XmlElement &xml, PresetKind expectedKind,
                          EngineType expectedEngine = EngineType::none);
  OperationResult writePresetFile(const juce::XmlElement &xml,
                                  const juce::String &name);
  OperationResult loadPresetFile(const juce::File &file,
                                 PresetKind expectedKind,
                                 EngineType expectedEngine = EngineType::none);
  OperationResult validateName(const juce::String &name) const;
  bool isFactoryName(const juce::String &name) const;
  juce::String makeSafeFilename(const juce::String &name) const;
};

} // namespace vstengine
