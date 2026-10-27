#include "PresetManager.h"
#include "core/KickParameterIds.h"
#include "core/ResourceLimits.h"
#include "core/SoundParameterIds.h"
#include <array>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <iterator>

namespace vstengine {

namespace {
using Error = PresetManager::Error;
using OperationResult = PresetManager::OperationResult;

OperationResult ok() { return {}; }

OperationResult fail(const Error error, const juce::String& message)
{
    return { error, message };
}

juce::String encodeSequence(const vstengine::sequence::Sequence& seq) {
    juce::MemoryBlock mb;
    seq.serialize(mb);
    return mb.toBase64Encoding();
}

// Fail-safe decode: only replaces the target sequence when the blob decodes
// AND passes structural validation. Corrupt data never touches live state.
bool decodeSequence(const juce::String& encoded, vstengine::sequence::Sequence& seq) {
    juce::MemoryBlock mb;
    if (!mb.fromBase64Encoding(encoded))
        return false;
    if (!vstengine::sequence::Sequence::isValidSerialization(mb))
        return false;
    seq = vstengine::sequence::Sequence::deserialize(mb);
    return true;
}

bool validateParameterTree(const juce::ValueTree& tree)
{
    if (!tree.isValid() || !tree.hasType("PARAMETERS")
        || tree.getNumChildren() == 0)
        return false;

    juce::StringArray ids;
    for (int i = 0; i < tree.getNumChildren(); ++i) {
        const auto child = tree.getChild(i);
        if (!child.hasType("PARAM") || !child.hasProperty("id")
            || !child.hasProperty("value"))
            return false;
        const auto id = child.getProperty("id").toString();
        const auto value = child.getProperty("value");
        double numericValue {};
        if (value.isDouble() || value.isInt() || value.isInt64()) {
            numericValue = static_cast<double>(value);
        } else if (value.isString()) {
            const auto text = value.toString().trim();
            if (text.isEmpty())
                return false;
            const auto* begin = text.toRawUTF8();
            char* end = nullptr;
            errno = 0;
            numericValue = std::strtod(begin, &end);
            if (end == begin || end == nullptr || *end != '\0' || errno == ERANGE)
                return false;
        } else {
            return false;
        }
        if (id.isEmpty() || ids.contains(id)
            || !std::isfinite(numericValue)
            || numericValue < 0.0 || numericValue > 1.0)
            return false;
        ids.add(id);
    }
    return true;
}
}

// --- Preset schema versioning / migration -----------------------------------
//
// The preset XML carries an explicit "version" attribute. Loading dispatches
// on that version: presets at the current version are loaded directly, older
// supported versions are rewritten by a migration chain, and anything newer
// than the current version is rejected cleanly instead of being misread.
//
// When a v2 schema is introduced:
//   1. bump currentPresetVersion to 2,
//   2. write migrateV1ToV2() below and route version-1 presets through it,
//   3. keep the v1 loader reachable through the chain so old presets survive.
// Until then the chain contains only the v1 (current) loader, which is the
// honest state of the format: v1 presets load, there is nothing to migrate yet.
static bool migrateToCurrentVersion(const juce::XmlElement& source,
                                    juce::XmlElement& result)
{
    const auto version = source.getIntAttribute("version", 0);
    switch (version) {
        case 1: // v1 has no engine metadata; loader infers from parameter IDs
            result = source;
            result.setAttribute("version", PresetManager::currentPresetVersion);
            return true;
        case PresetManager::currentPresetVersion:
            result = source;
            return true;
        default:
            // Future/unknown version: reject cleanly (fail safely).
            return false;
    }
}

// Factory psy-bass presets are Sound presets: psy-bass engine sound parameters
// only - no sequence, no global MIDI/generator state. Values are stored as
// plain (denormalized) parameter values here and written into the v1 sound
// preset XML with the parameter's own convertTo0to1() at load time, matching
// exactly how APVTS serializes user presets (which store normalized values).
struct FactorySoundParam {
    const char* id;
    float value;
};

struct FactoryPreset {
    const char* name;
    std::array<FactorySoundParam, 13> params;
};

// Parameter order must match vstengine::core::soundParameterIds.
static const FactoryPreset factoryPresets[] = {
    { "Tight Rolling", { {
        { "drive", 2.2f }, { "release", 0.05f }, { "ampAttack", 0.0005f },
        { "ampDecay", 0.04f }, { "ampSustain", 0.65f },
        { "filterCutoff", 0.55f }, { "filterResonance", 0.5f },
        { "filterDrive", 1.2f }, { "keyTracking", 0.25f },
        { "pitchEnvAmount", 8.0f }, { "pitchEnvTime", 0.02f },
        { "pitchEnvCurve", 2.5f }, { "outputLevel", 1.0f } } } },
    { "Dark Rolling", { {
        { "drive", 3.0f }, { "release", 0.09f }, { "ampAttack", 0.001f },
        { "ampDecay", 0.06f }, { "ampSustain", 0.7f },
        { "filterCutoff", 0.35f }, { "filterResonance", 0.6f },
        { "filterDrive", 1.5f }, { "keyTracking", 0.2f },
        { "pitchEnvAmount", 10.0f }, { "pitchEnvTime", 0.025f },
        { "pitchEnvCurve", 2.0f }, { "outputLevel", 1.0f } } } },
    { "Short Punch", { {
        { "drive", 2.6f }, { "release", 0.02f }, { "ampAttack", 0.0002f },
        { "ampDecay", 0.08f }, { "ampSustain", 0.3f },
        { "filterCutoff", 0.6f }, { "filterResonance", 0.35f },
        { "filterDrive", 0.8f }, { "keyTracking", 0.1f },
        { "pitchEnvAmount", 14.0f }, { "pitchEnvTime", 0.012f },
        { "pitchEnvCurve", 3.0f }, { "outputLevel", 1.0f } } } },
    { "Deep Rolling", { {
        { "drive", 2.0f }, { "release", 0.1f }, { "ampAttack", 0.002f },
        { "ampDecay", 0.14f }, { "ampSustain", 0.55f },
        { "filterCutoff", 0.3f }, { "filterResonance", 0.45f },
        { "filterDrive", 0.7f }, { "keyTracking", 0.3f },
        { "pitchEnvAmount", 6.0f }, { "pitchEnvTime", 0.03f },
        { "pitchEnvCurve", 1.8f }, { "outputLevel", 1.0f } } } },
    { "Hi-Tech Tight", { {
        { "drive", 4.0f }, { "release", 0.025f }, { "ampAttack", 0.0003f },
        { "ampDecay", 0.03f }, { "ampSustain", 0.6f },
        { "filterCutoff", 0.45f }, { "filterResonance", 0.7f },
        { "filterDrive", 2.0f }, { "keyTracking", 0.4f },
        { "pitchEnvAmount", 12.0f }, { "pitchEnvTime", 0.015f },
        { "pitchEnvCurve", 2.2f }, { "outputLevel", 0.9f } } } },
};

PresetManager::PresetManager(PresetStateStore& stateStore,
                             vstengine::sequence::Sequence& seq,
                             juce::File directory,
                             FullStateCallbacks fullState)
    : store(stateStore), sequence(seq), presetDirectory(directory),
      fullStateCallbacks(std::move(fullState)) {
    if (!presetDirectory.exists())
        presetDirectory.createDirectory();
}

PresetManager::PresetManager(PresetStateStore& stateStore,
                             vstengine::sequence::Sequence& seq,
                             FullStateCallbacks fullState)
    : PresetManager(stateStore, seq, getPresetDirectory(),
                    std::move(fullState))
{
}

juce::File PresetManager::getPresetDirectory() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("UltimaVox").getChildFile("VST-Engine")
        .getChildFile("Presets").getChildFile("User");
}

juce::String PresetManager::makeSafeFilename(const juce::String& name) const {
    juce::String safe;
    for (const auto ch : name) {
        if (juce::CharacterFunctions::isLetter(ch) || juce::CharacterFunctions::isDigit(ch) || ch == ' ' || ch == '-' || ch == '_')
            safe += ch;
    }
    return safe.isEmpty() ? "Unnamed" : safe;
}

bool PresetManager::serializeToXml(juce::XmlElement &xml, const PresetKind kind,
                                   const EngineType engine) {
  xml.setAttribute("version", currentPresetVersion);
  xml.setAttribute("type", kind == PresetKind::sound ? "sound" : "full");
  if (kind == PresetKind::sound)
    xml.setAttribute("engine", engine == EngineType::kick ? "kick" : "bass");
  xml.createNewChildElement("name")->addTextElement(currentPresetName);

  // Sound preset: engine sound parameters ONLY (psy-bass + kick, issue #11
  // PHASE 5) - no sequence, no global MIDI/generator state. Kick parameters
  // are appended into the same PARAMETERS element with their own IDs; the
  // loader applies per-ID and skips missing entries, so pre-kick v1 presets
  // load unchanged and keep the live kick values. Full preset: complete
  // APVTS state (which includes the global MIDI/generator settings) plus
  // the canonical sequence - all Phase 0-5 state required by #11.
  auto *paramsEl = xml.createNewChildElement("parameters");
  if (kind == PresetKind::full && fullStateCallbacks.captureComplete) {
    const auto project = fullStateCallbacks.captureComplete();
    if (!project.isValid())
      return false;
    xml.setAttribute("format", "rack-project-v5");
    paramsEl->addChildElement(project.createXml().release());
    return true;
  }
  auto state = store.copyState();

  if (kind == PresetKind::sound) {
    auto *paramsTree = paramsEl->createNewChildElement("PARAMETERS");
    if (engine == SoundEngine::bass || engine == SoundEngine::legacyCombined)
      for (int i = 0; i < vstengine::core::numSoundParameterIds; ++i) {
        const auto child = state.getChildWithProperty(
            "id", juce::var(vstengine::core::soundParameterIds[i]));
        if (child.isValid())
          paramsTree->addChildElement(child.createXml().release());
      }
    if (engine == SoundEngine::kick || engine == SoundEngine::legacyCombined)
      for (int i = 0; i < vstengine::core::numKickSoundParameterIds; ++i) {
        const auto child = state.getChildWithProperty(
            "id", juce::var(vstengine::core::kickSoundParameterIds[i]));
        if (child.isValid())
          paramsTree->addChildElement(child.createXml().release());
      }
  } else {
    paramsEl->addChildElement(state.createXml().release());
    xml.createNewChildElement("sequence")
        ->addTextElement(encodeSequence(sequence));
    if (fullStateCallbacks.capture) {
      const auto extra = fullStateCallbacks.capture();
      if (extra.isValid())
        xml.createNewChildElement("parts")->addChildElement(
            extra.createXml().release());
    }
  }
  return true;
}

bool PresetManager::deserializeFromXml(const juce::XmlElement& sourceXml,
                                       const PresetKind expectedKind,
                                       const EngineType expectedEngine)
{
    if (!sourceXml.hasTagName("VstEnginePreset"))
        return false;

    // Version dispatch / migration: rewrite the preset to the current schema
    // or reject it cleanly (unsupported future version, missing version).
    juce::XmlElement migrated("VstEnginePreset");
    if (!migrateToCurrentVersion(sourceXml, migrated))
        return false;
    const juce::XmlElement& xml = migrated;

    const auto typeAttr = xml.getStringAttribute("type");
    if (typeAttr != "sound" && typeAttr != "full")
        return false;
    if ((expectedKind == PresetKind::sound) != (typeAttr == "sound"))
        return false; // preset kind mismatch: fail safely

    // Parse everything into locals first; live plugin state is only touched
    // once the whole preset has been validated.
    juce::String newCurrentName = currentPresetName;
    if (auto* nameEl = xml.getChildByName("name")) {
        const auto n = nameEl->getAllSubText();
        if (n.isNotEmpty())
            newCurrentName = n;
    }

    if (typeAttr == "full" && fullStateCallbacks.restoreComplete) {
      if (xml.getStringAttribute("format") != "rack-project-v5")
        return false;
      const auto *parameters = xml.getChildByName("parameters");
      const auto *project =
          parameters ? parameters->getChildByName("PARAMETERS") : nullptr;
      if (!project)
        return false;
      const auto restored = juce::ValueTree::fromXml(*project);
      if (!restored.isValid() || !fullStateCallbacks.restoreComplete(restored))
        return false;
      currentPresetName = newCurrentName;
      return true;
    }

    // Full presets carry the sequence; Sound presets never touch it.
    vstengine::sequence::Sequence restoredSequence;
    juce::ValueTree restoredFullState;
    if (typeAttr == "full") {
        auto* seqEl = xml.getChildByName("sequence");
        if (seqEl == nullptr)
            return false;
        if (!decodeSequence(seqEl->getAllSubText(), restoredSequence))
            return false; // corrupt sequence: fail safely
        if (auto* partsElement = xml.getChildByName("parts")) {
            const auto* stateElement = partsElement->getFirstChildElement();
            if (stateElement == nullptr || !fullStateCallbacks.restore)
                return false;
            restoredFullState = juce::ValueTree::fromXml(*stateElement);
            if (!restoredFullState.isValid())
                return false;
        }
    }

    auto* paramsEl = xml.getChildByName("parameters");
    if (paramsEl == nullptr)
        return false;
    auto* stateEl = paramsEl->getChildByName("PARAMETERS");
    if (stateEl == nullptr)
        return false;
    const juce::ValueTree restored = juce::ValueTree::fromXml(*stateEl);
    if (!validateParameterTree(restored))
        return false;

    if (typeAttr == "sound") {
        bool hasBass = false;
        bool hasKick = false;
        for (int i = 0; i < restored.getNumChildren(); ++i) {
            const auto id = restored.getChild(i).getProperty("id").toString();
            for (const auto* candidate : vstengine::core::soundParameterIds)
                hasBass = hasBass || id == candidate;
            for (const auto* candidate : vstengine::core::kickSoundParameterIds)
                hasKick = hasKick || id == candidate;
        }

        EngineType storedEngine = EngineType::none;
        const auto engineAttr = xml.getStringAttribute("engine");
        if (engineAttr == "bass") storedEngine = EngineType::bass;
        else if (engineAttr == "kick") storedEngine = EngineType::kick;
        else if (engineAttr.isNotEmpty()) return false;
        else storedEngine = hasBass && hasKick ? EngineType::legacyCombined
                          : hasKick ? EngineType::kick
                          : hasBass ? EngineType::bass : EngineType::none;

        if (storedEngine == EngineType::none)
            return false;
        if (storedEngine == EngineType::bass && (!hasBass || hasKick))
            return false;
        if (storedEngine == EngineType::kick && (!hasKick || hasBass))
            return false;
        if (expectedEngine != EngineType::none && storedEngine != expectedEngine)
            return false;

        auto newState = store.copyState();
        bool appliedParameter = false;
        if (storedEngine == EngineType::bass
            || storedEngine == EngineType::legacyCombined) {
            for (int i = 0; i < vstengine::core::numSoundParameterIds; ++i) {
                const auto id = juce::var(vstengine::core::soundParameterIds[i]);
                const auto src = restored.getChildWithProperty("id", id);
                if (src.isValid()) {
                    auto dst = newState.getChildWithProperty("id", id);
                    if (dst.isValid()) {
                        dst.copyPropertiesFrom(src, nullptr);
                        appliedParameter = true;
                    }
                }
            }
        }
        if (storedEngine == EngineType::kick
            || storedEngine == EngineType::legacyCombined) {
            for (int i = 0; i < vstengine::core::numKickSoundParameterIds; ++i) {
                const auto id = juce::var(vstengine::core::kickSoundParameterIds[i]);
                const auto src = restored.getChildWithProperty("id", id);
                if (src.isValid()) {
                    auto dst = newState.getChildWithProperty("id", id);
                    if (dst.isValid()) {
                        dst.copyPropertiesFrom(src, nullptr);
                        appliedParameter = true;
                    }
                }
            }
        }
        if (!appliedParameter)
            return false;
        store.replaceState(std::move(newState));
    } else {
        // Merge validated v1 fields into current APVTS shape. Older v1 Full
        // presets may lack later parameters; they must not erase them.
        auto newState = store.copyState();
        int appliedParameters = 0;
        for (int i = 0; i < restored.getNumChildren(); ++i) {
            const auto source = restored.getChild(i);
            auto destination = newState.getChildWithProperty(
                "id", source.getProperty("id"));
            if (destination.isValid()) {
                destination.copyPropertiesFrom(source, nullptr);
                ++appliedParameters;
            }
        }
        if (appliedParameters == 0)
            return false;
        if (restoredFullState.isValid()
            && !fullStateCallbacks.restore(restoredFullState))
            return false;
        store.replaceState(std::move(newState));
        sequence = restoredSequence;
    }

    currentPresetName = newCurrentName;
    return true;
}

PresetManager::OperationResult PresetManager::saveSoundPreset(
    const EngineType engine, const juce::String& name)
{
  const std::scoped_lock lock(stateMutex);
  if (const auto validation = validateName(name); !validation)
    return validation;
  if (engine != SoundEngine::bass)
    return fail(Error::wrongEngine,
                "Kick content is not active in Vox Electronic Engine");
  const auto previousName = currentPresetName;
  currentPresetName = name;
  juce::XmlElement xml("VstEnginePreset");
  if (!serializeToXml(xml, PresetKind::sound, engine)) {
    currentPresetName = previousName;
    return fail(Error::invalidPreset, "Project capture failed");
  }
    const auto result = writePresetFile(xml, name);
    if (!result)
        currentPresetName = previousName;
    return result;
}

PresetManager::OperationResult PresetManager::saveFullPreset(
    const juce::String& name)
{
  const std::scoped_lock lock(stateMutex);
  if (const auto validation = validateName(name); !validation)
    return validation;
  const auto previousName = currentPresetName;
  currentPresetName = name;
  juce::XmlElement xml("VstEnginePreset");
  if (!serializeToXml(xml, PresetKind::full)) {
    currentPresetName = previousName;
    return fail(Error::invalidPreset, "Project capture failed");
  }
    const auto result = writePresetFile(xml, name);
    if (!result)
        currentPresetName = previousName;
    return result;
}

PresetManager::OperationResult PresetManager::writePresetFile(
    const juce::XmlElement& xml, const juce::String& name)
{
    if (!presetDirectory.exists()) {
        const auto result = presetDirectory.createDirectory();
        if (result.failed())
            return fail(Error::cannotWrite, "cannot create preset directory: "
                        + result.getErrorMessage());
    }
    const auto safeName = makeSafeFilename(name);
    juce::File file(presetDirectory.getChildFile(safeName + ".xml"));
    const auto categoryKey = [](const juce::XmlElement& element) {
        if (element.getStringAttribute("type") == "full")
            return juce::String("full");
        const auto engine = element.getStringAttribute("engine");
        return engine.isNotEmpty() ? engine : juce::String("sound");
    };
    if (file.existsAsFile()) {
        const auto existing = juce::parseXML(file.loadFileAsString());
        if (existing != nullptr && categoryKey(*existing) != categoryKey(xml))
            file = presetDirectory.getChildFile(
                safeName + " [" + categoryKey(xml) + "].xml");
    }
    juce::TemporaryFile temporary(file);
    if (!temporary.getFile().replaceWithText(xml.toString()))
        return fail(Error::cannotWrite, "cannot write preset");
    if (!temporary.overwriteTargetFileWithTemporary())
        return fail(Error::cannotWrite, "cannot replace preset");
    currentPresetName = name;
    return ok();
}

PresetManager::OperationResult PresetManager::loadPresetFile(
    const juce::File& file, const PresetKind expectedKind,
    const EngineType expectedEngine)
{
    if (!file.existsAsFile())
        return fail(Error::fileNotFound, "preset file not found");
    if (file.getSize() < 0 || static_cast<std::uint64_t>(file.getSize()) >
                                  vstengine::core::maxSerializedProjectBytes)
      return fail(Error::invalidPreset,
                  "preset exceeds the serialized project budget");
    const juce::String content = file.loadFileAsString();
    if (content.isEmpty())
        return fail(Error::invalidPreset, "preset file is empty");
    std::unique_ptr<juce::XmlElement> xml(juce::parseXML(content));
    if (!xml || !xml->hasTagName("VstEnginePreset"))
        return fail(Error::invalidPreset, "invalid preset XML");
    const auto version = xml->getIntAttribute("version", 0);
    if (version > currentPresetVersion)
        return fail(Error::unsupportedVersion, "unsupported preset version");
    if (version < 1)
        return fail(Error::invalidPreset, "invalid preset version");
    const auto type = xml->getStringAttribute("type");
    const auto expectedType = expectedKind == PresetKind::sound ? "sound" : "full";
    if (type != expectedType)
        return fail(Error::wrongKind, "preset type does not match operation");
    if (!deserializeFromXml(*xml, expectedKind, expectedEngine)) {
        if (expectedKind == PresetKind::sound
            && expectedEngine != EngineType::none)
            return fail(Error::wrongEngine, "preset engine does not match target");
        return fail(Error::invalidPreset, "invalid preset data");
    }
    return ok();
}

PresetManager::OperationResult PresetManager::loadSoundPreset(
    const EngineType engine, const juce::String& name)
{
  const std::scoped_lock lock(stateMutex);
  for (const auto &entry : getPresets())
    if (entry.source == PresetSource::user && entry.name == name &&
        entry.kind == PresetKind::sound && entry.engine == engine)
      return loadPresetFile(entry.file, PresetKind::sound, engine);
  const auto safeName = makeSafeFilename(name);
  juce::File file(presetDirectory.getChildFile(safeName + ".xml"));
  return loadPresetFile(file, PresetKind::sound, engine);
}

PresetManager::OperationResult PresetManager::loadSoundPreset(
    const juce::String& name)
{
  const std::scoped_lock lock(stateMutex);
  const auto safeName = makeSafeFilename(name);
  juce::File file(presetDirectory.getChildFile(safeName + ".xml"));
  return loadPresetFile(file, PresetKind::sound);
}

PresetManager::OperationResult PresetManager::loadFullPreset(
    const juce::String& name)
{
  const std::scoped_lock lock(stateMutex);
  for (const auto &entry : getPresets())
    if (entry.source == PresetSource::user && entry.name == name &&
        entry.kind == PresetKind::full)
      return loadPresetFile(entry.file, PresetKind::full);
  const auto safeName = makeSafeFilename(name);
  juce::File file(presetDirectory.getChildFile(safeName + ".xml"));
  return loadPresetFile(file, PresetKind::full);
}

juce::StringArray PresetManager::getSoundPresetNames() const
{
  const std::scoped_lock lock(stateMutex);
  juce::StringArray names;
  for (const auto &entry : getPresets())
    if (entry.source == PresetSource::user && entry.kind == PresetKind::sound)
      names.addIfNotAlreadyThere(entry.name);
  return names;
}

juce::StringArray PresetManager::getSoundPresetNames(const EngineType engine) const
{
  const std::scoped_lock lock(stateMutex);
  juce::StringArray names;
  for (const auto &entry : getPresets())
    if (entry.source == PresetSource::user && entry.kind == PresetKind::sound &&
        entry.engine == engine)
      names.addIfNotAlreadyThere(entry.name);
  return names;
}

juce::StringArray PresetManager::getFullPresetNames() const
{
  const std::scoped_lock lock(stateMutex);
  juce::StringArray names;
  for (const auto &entry : getPresets())
    if (entry.source == PresetSource::user && entry.kind == PresetKind::full)
      names.addIfNotAlreadyThere(entry.name);
  return names;
}

juce::Array<PresetManager::PresetEntry> PresetManager::getPresets() const
{
  const std::scoped_lock lock(stateMutex);
  juce::Array<PresetEntry> entries;
  for (const auto &preset : factoryPresets)
    entries.add({preset.name,
                 PresetSource::factory,
                 PresetKind::sound,
                 SoundEngine::bass,
                 {}});

  if (!presetDirectory.exists())
    return entries;

  for (const auto &file :
       presetDirectory.findChildFiles(juce::File::findFiles, false, "*.xml")) {
    auto xml = juce::parseXML(file.loadFileAsString());
    if (xml == nullptr || !xml->hasTagName("VstEnginePreset") ||
        xml->getIntAttribute("version", 0) < 1 ||
        xml->getIntAttribute("version", 0) > currentPresetVersion)
      continue;
    auto *nameEl = xml->getChildByName("name");
    if (nameEl == nullptr || nameEl->getAllSubText().trim().isEmpty())
      continue;

    const auto type = xml->getStringAttribute("type");
    if (type == "full") {
      entries.add({nameEl->getAllSubText(), PresetSource::user,
                   PresetKind::full, SoundEngine::none, file});
      continue;
    }
    if (type != "sound")
      continue;

    bool hasBass = false;
    bool hasKick = false;
    if (auto *parameters = xml->getChildByName("parameters"))
      if (auto *state = parameters->getChildByName("PARAMETERS"))
        forEachXmlChildElement(*state, child) {
          const auto id = child->getStringAttribute("id");
          for (const auto *candidate : vstengine::core::soundParameterIds)
            hasBass = hasBass || id == candidate;
          for (const auto *candidate : vstengine::core::kickSoundParameterIds)
            hasKick = hasKick || id == candidate;
        }
    if (!hasBass && !hasKick)
      continue;
    EngineType engine = EngineType::none;
    const auto engineAttr = xml->getStringAttribute("engine");
    if (engineAttr == "bass")
      engine = EngineType::bass;
    else if (engineAttr == "kick")
      engine = EngineType::kick;
    else if (engineAttr.isEmpty())
      engine = hasBass && hasKick ? EngineType::legacyCombined
               : hasKick          ? EngineType::kick
                                  : EngineType::bass;
    else
      continue;
    if ((engine == EngineType::bass && (!hasBass || hasKick)) ||
        (engine == EngineType::kick && (!hasKick || hasBass)))
      continue;
    // Kick sound files remain on disk for migration/export, but are not
    // active Vox Electronic Engine content after checkpoint 7A.
    if (engine == EngineType::kick || engine == EngineType::legacyCombined)
      continue;
    entries.add({nameEl->getAllSubText(), PresetSource::user, PresetKind::sound,
                 engine, file});
  }
    return entries;
}

PresetManager::OperationResult PresetManager::renamePreset(
    const juce::String& oldName, const juce::String& newName)
{
  const std::scoped_lock lock(stateMutex);
  if (isFactoryName(oldName))
    return fail(Error::readOnly, "factory presets are read-only");
  for (const auto &entry : getPresets())
    if (entry.source == PresetSource::user && entry.name == oldName)
      return renamePreset(entry, newName);
  return fail(Error::fileNotFound, "preset file not found");
}

PresetManager::OperationResult PresetManager::renamePreset(
    const PresetEntry& entry, const juce::String& newName)
{
  const std::scoped_lock lock(stateMutex);
  if (entry.isReadOnly() || isFactoryName(entry.name))
    return fail(Error::readOnly, "factory presets are read-only");
  if (const auto validation = validateName(newName); !validation)
    return validation;
  if (entry.file.getParentDirectory() != presetDirectory)
    return fail(Error::invalidPreset, "preset path is outside user directory");
  if (!entry.file.existsAsFile())
    return fail(Error::fileNotFound, "preset file not found");

  const auto newFile =
      presetDirectory.getChildFile(makeSafeFilename(newName) + ".xml");
  if (newFile.existsAsFile() && newFile != entry.file)
    return fail(Error::alreadyExists, "preset name already exists");

  auto xml = juce::parseXML(entry.file.loadFileAsString());
  if (xml == nullptr || !xml->hasTagName("VstEnginePreset"))
    return fail(Error::invalidPreset, "invalid preset XML");
  auto *nameEl = xml->getChildByName("name");
  if (nameEl == nullptr)
    return fail(Error::invalidPreset, "preset name is missing");
  nameEl->deleteAllTextElements();
  nameEl->addTextElement(newName);

  juce::TemporaryFile temporary(newFile);
  if (!temporary.getFile().replaceWithText(xml->toString()) ||
      !temporary.overwriteTargetFileWithTemporary())
    return fail(Error::cannotWrite, "cannot write renamed preset");
  if (newFile != entry.file && !entry.file.deleteFile()) {
    newFile.deleteFile();
    return fail(Error::cannotWrite, "cannot remove old preset file");
  }
    currentPresetName = newName;
    return ok();
}

PresetManager::OperationResult PresetManager::deletePreset(
    const juce::String& name)
{
  const std::scoped_lock lock(stateMutex);
  if (isFactoryName(name))
    return fail(Error::readOnly, "factory presets are read-only");
  for (const auto &entry : getPresets())
    if (entry.source == PresetSource::user && entry.name == name)
      return deletePreset(entry);
  return fail(Error::fileNotFound, "preset file not found");
}

PresetManager::OperationResult PresetManager::deletePreset(
    const PresetEntry& entry)
{
  const std::scoped_lock lock(stateMutex);
  if (entry.isReadOnly() || isFactoryName(entry.name))
    return fail(Error::readOnly, "factory presets are read-only");
  if (entry.file.getParentDirectory() != presetDirectory)
    return fail(Error::invalidPreset, "preset path is outside user directory");
  if (!entry.file.existsAsFile())
    return fail(Error::fileNotFound, "preset file not found");
  if (!entry.file.deleteFile())
    return fail(Error::cannotWrite, "cannot delete preset");
  if (currentPresetName == entry.name)
    currentPresetName.clear();
  return ok();
}

juce::StringArray PresetManager::getFactoryPresetNames() const {
  const std::scoped_lock lock(stateMutex);
  juce::StringArray names;
  for (const auto &preset : factoryPresets)
    names.add(preset.name);
  return names;
}

juce::StringArray PresetManager::getFactoryPresetNames(
    const EngineType engine) const
{
  const std::scoped_lock lock(stateMutex);
  juce::StringArray names;
  if (engine == EngineType::bass)
    for (const auto &preset : factoryPresets)
      names.add(preset.name);
  return names;
}

PresetManager::OperationResult PresetManager::loadPreset(
    const PresetEntry& entry)
{
  const std::scoped_lock lock(stateMutex);
  if (entry.source == PresetSource::factory)
    return loadFactoryPreset(entry.engine, entry.name);
  if (entry.file.getParentDirectory() != presetDirectory)
    return fail(Error::invalidPreset, "preset path is outside user directory");
  return loadPresetFile(entry.file, entry.kind, entry.engine);
}

PresetManager::OperationResult PresetManager::loadFactoryPreset(
    const EngineType engine, const juce::String& name) {
  const std::scoped_lock lock(stateMutex);
  // Build a current-schema sound preset from plain parameter values. Values are
  // normalized with each parameter's own range so the resulting XML is
  // identical in format to a user-saved Sound preset.
  auto buildPresetXml = [this](const EngineType target, const char *presetName,
                               const FactorySoundParam *params, int numParams) {
    juce::XmlElement xml("VstEnginePreset");
    xml.setAttribute("version", currentPresetVersion);
    xml.setAttribute("type", "sound");
    xml.setAttribute("engine", target == EngineType::kick ? "kick" : "bass");
    xml.createNewChildElement("name")->setText(presetName);
    auto *paramsEl = xml.createNewChildElement("parameters");
    auto *paramsTree = paramsEl->createNewChildElement("PARAMETERS");

    for (int i = 0; i < numParams; ++i) {
      auto *paramEl = paramsTree->createNewChildElement("PARAM");
      paramEl->setAttribute("id", params[i].id);
      paramEl->setAttribute(
          "value",
          juce::String(store.convertTo0to1(params[i].id, params[i].value), 8));
    }

    return xml;
  };

  if (engine == EngineType::bass)
    for (const auto &preset : factoryPresets) {
      if (name != preset.name)
        continue;
      const auto xml = buildPresetXml(engine, preset.name, preset.params.data(),
                                      static_cast<int>(preset.params.size()));
      return deserializeFromXml(xml, PresetKind::sound, engine)
                 ? ok()
                 : fail(Error::invalidPreset, "invalid factory preset");
    }

    return fail(Error::fileNotFound, "factory preset not found");
}

PresetManager::OperationResult PresetManager::loadFactoryPreset(
    const juce::String& name)
{
  const std::scoped_lock lock(stateMutex);
  for (const auto &preset : factoryPresets)
    if (name == preset.name)
      return loadFactoryPreset(EngineType::bass, name);
  return fail(Error::fileNotFound, "factory preset not found");
}

PresetManager::OperationResult PresetManager::validateName(
    const juce::String& name) const
{
    const auto trimmed = name.trim();
    if (trimmed.isEmpty() || trimmed != name || makeSafeFilename(name) != name)
        return fail(Error::invalidName,
                    "invalid preset name; use letters, digits, spaces, '-' or '_'");
    if (isFactoryName(name))
        return fail(Error::readOnly, "factory preset names are reserved");
    return ok();
}

bool PresetManager::isFactoryName(const juce::String& name) const
{
    for (const auto& preset : factoryPresets)
        if (name == preset.name)
            return true;
    return false;
}

} // namespace vstengine
