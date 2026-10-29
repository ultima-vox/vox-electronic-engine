#include "PresetManager.h"
#include "core/ResourceLimits.h"

namespace vstengine {

namespace {
using Error = PresetManager::Error;
using OperationResult = PresetManager::OperationResult;

OperationResult ok() { return {}; }

OperationResult fail(const Error error, const juce::String& message)
{
    return { error, message };
}

// The project tree of a current-format scene file, or null.
const juce::XmlElement* sceneProject(const juce::XmlElement& xml)
{
    if (!xml.hasTagName("VstEnginePreset")
        || xml.getIntAttribute("version", 0)
               != PresetManager::currentPresetVersion
        || xml.getStringAttribute("type") != "full"
        || xml.getStringAttribute("format") != PresetManager::sceneFormat)
        return nullptr;
    const auto* parameters = xml.getChildByName("parameters");
    return parameters != nullptr ? parameters->getChildByName("PARAMETERS")
                                 : nullptr;
}

juce::String sceneName(const juce::XmlElement& xml)
{
    const auto* name = xml.getChildByName("name");
    return name != nullptr ? name->getAllSubText() : juce::String{};
}
} // namespace

PresetManager::PresetManager(SceneCallbacks scene, juce::File directory)
    : sceneCallbacks(std::move(scene)), presetDirectory(std::move(directory))
{
    if (!presetDirectory.exists())
        presetDirectory.createDirectory();
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

PresetManager::OperationResult PresetManager::saveFullPreset(
    const juce::String& name)
{
  const std::scoped_lock lock(stateMutex);
  if (const auto validation = validateName(name); !validation)
    return validation;
  if (!sceneCallbacks.capture)
    return fail(Error::invalidPreset, "Project capture failed");
  const auto project = sceneCallbacks.capture();
  if (!project.isValid())
    return fail(Error::invalidPreset, "Project capture failed");

  juce::XmlElement xml("VstEnginePreset");
  xml.setAttribute("version", currentPresetVersion);
  xml.setAttribute("type", "full");
  xml.setAttribute("format", sceneFormat);
  xml.createNewChildElement("name")->addTextElement(name);
  xml.createNewChildElement("parameters")
      ->addChildElement(project.createXml().release());

  if (!presetDirectory.exists()) {
    const auto result = presetDirectory.createDirectory();
    if (result.failed())
      return fail(Error::cannotWrite, "cannot create preset directory: " +
                                          result.getErrorMessage());
  }
  const juce::File file(
      presetDirectory.getChildFile(makeSafeFilename(name) + ".xml"));
  if (file.existsAsFile()) {
    const auto existing = juce::parseXML(file.loadFileAsString());
    if (existing == nullptr || sceneProject(*existing) == nullptr)
      return fail(Error::alreadyExists,
                  "a file that is not a Rack scene uses this name");
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
    const juce::File& file)
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
    if (xml->getIntAttribute("version", 0) != currentPresetVersion)
        return fail(Error::unsupportedVersion, "unsupported preset version");
    const auto* project = sceneProject(*xml);
    if (project == nullptr)
        return fail(Error::invalidPreset, "not a Rack scene preset");
    const auto restored = juce::ValueTree::fromXml(*project);
    if (!restored.isValid() || !sceneCallbacks.restore
        || !sceneCallbacks.restore(restored))
        return fail(Error::invalidPreset, "invalid preset data");
    const auto name = sceneName(*xml);
    if (name.isNotEmpty())
        currentPresetName = name;
    return ok();
}

PresetManager::OperationResult PresetManager::loadFullPreset(
    const juce::String& name)
{
  const std::scoped_lock lock(stateMutex);
  for (const auto &entry : getPresets())
    if (entry.name == name)
      return loadPresetFile(entry.file);
  return loadPresetFile(
      presetDirectory.getChildFile(makeSafeFilename(name) + ".xml"));
}

juce::StringArray PresetManager::getFullPresetNames() const
{
  const std::scoped_lock lock(stateMutex);
  juce::StringArray names;
  for (const auto &entry : getPresets())
    names.addIfNotAlreadyThere(entry.name);
  return names;
}

juce::Array<PresetManager::PresetEntry> PresetManager::getPresets() const
{
  const std::scoped_lock lock(stateMutex);
  juce::Array<PresetEntry> entries;
  if (!presetDirectory.exists())
    return entries;
  for (const auto &file :
       presetDirectory.findChildFiles(juce::File::findFiles, false, "*.xml")) {
    if (static_cast<std::uint64_t>(std::max<juce::int64>(0, file.getSize())) >
        vstengine::core::maxSerializedProjectBytes)
      continue;
    const auto xml = juce::parseXML(file.loadFileAsString());
    if (xml == nullptr || sceneProject(*xml) == nullptr)
      continue;
    const auto name = sceneName(*xml);
    if (name.trim().isEmpty())
      continue;
    entries.add({name, file});
  }
  return entries;
}

PresetManager::OperationResult PresetManager::renamePreset(
    const juce::String& oldName, const juce::String& newName)
{
  const std::scoped_lock lock(stateMutex);
  for (const auto &entry : getPresets())
    if (entry.name == oldName)
      return renamePreset(entry, newName);
  return fail(Error::fileNotFound, "preset file not found");
}

PresetManager::OperationResult PresetManager::renamePreset(
    const PresetEntry& entry, const juce::String& newName)
{
  const std::scoped_lock lock(stateMutex);
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
  if (xml == nullptr || sceneProject(*xml) == nullptr)
    return fail(Error::invalidPreset, "not a Rack scene preset");
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
  for (const auto &entry : getPresets())
    if (entry.name == name)
      return deletePreset(entry);
  return fail(Error::fileNotFound, "preset file not found");
}

PresetManager::OperationResult PresetManager::deletePreset(
    const PresetEntry& entry)
{
  const std::scoped_lock lock(stateMutex);
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

PresetManager::OperationResult PresetManager::loadPreset(
    const PresetEntry& entry)
{
  const std::scoped_lock lock(stateMutex);
  if (entry.file.getParentDirectory() != presetDirectory)
    return fail(Error::invalidPreset, "preset path is outside user directory");
  return loadPresetFile(entry.file);
}

PresetManager::OperationResult PresetManager::validateName(
    const juce::String& name) const
{
    const auto trimmed = name.trim();
    if (trimmed.isEmpty() || trimmed != name || makeSafeFilename(name) != name)
        return fail(Error::invalidName,
                    "invalid preset name; use letters, digits, spaces, '-' or '_'");
    return ok();
}

} // namespace vstengine
