#include "preset/PresetManager.h"
#include <cstdlib>
#include <iostream>

namespace {
int testsRun = 0;
int testsPassed = 0;
#define REQUIRE(condition, message)                                           \
    do {                                                                       \
        ++testsRun;                                                            \
        if (!(condition)) {                                                    \
            std::cerr << "FAILED: " << message << " line " << __LINE__ << '\n'; \
            std::exit(EXIT_FAILURE);                                           \
        }                                                                      \
        ++testsPassed;                                                         \
    } while (false)

using Manager = vstengine::PresetManager;

// Stand-in for the plugin's single project-state authority.
struct Project {
    juce::ValueTree live { "PARAMETERS" };
    int restores = 0;

    Manager::SceneCallbacks callbacks()
    {
        return { [this] { return live.createCopy(); },
                 [this](const juce::ValueTree& tree) {
                     if (!tree.hasType("PARAMETERS")
                         || static_cast<int>(tree.getProperty(
                                "stateSchemaVersion", 0)) != 5)
                         return false;
                     live = tree.createCopy();
                     ++restores;
                     return true;
                 } };
    }
};

bool containsName(const Manager& manager, const juce::String& name)
{
    for (const auto& entry : manager.getPresets())
        if (entry.name == name)
            return true;
    return false;
}

void writeFile(const juce::File& file, const juce::XmlElement& xml)
{
    REQUIRE(file.replaceWithText(xml.toString()), "fixture written");
}
} // namespace

int main()
{
    const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                         .getChildFile("VoxRackScenePresetTests");
    dir.deleteRecursively();
    REQUIRE(dir.createDirectory(), "test directory created");

    Project project;
    project.live.setProperty("stateSchemaVersion", 5, nullptr);
    project.live.setProperty("marker", "first", nullptr);
    Manager manager(project.callbacks(), dir);

    REQUIRE(manager.getPresets().isEmpty(), "no factory content is invented");

    REQUIRE(manager.saveFullPreset("Scene A"), "scene saves");
    REQUIRE(manager.getCurrentPresetName() == "Scene A", "saved name current");
    const auto file = dir.getChildFile("Scene A.xml");
    const auto saved = juce::parseXML(file.loadFileAsString());
    REQUIRE(saved != nullptr
                && saved->getIntAttribute("version")
                       == Manager::currentPresetVersion
                && saved->getStringAttribute("type") == "full"
                && saved->getStringAttribute("format") == Manager::sceneFormat,
            "scene file uses the current explicit format");

    project.live.setProperty("marker", "second", nullptr);
    REQUIRE(manager.loadFullPreset("Scene A"), "scene loads");
    REQUIRE(project.live.getProperty("marker").toString() == "first",
            "scene restores through the project authority");

    REQUIRE(!manager.saveFullPreset(" bad"), "untrimmed name rejected");
    REQUIRE(manager.saveFullPreset("Scene A").wasOk(), "same scene overwrites");

    // Anything that is not the current scene format is neither listed nor
    // loadable: old sound presets, v1 full presets and future versions.
    juce::XmlElement sound("VstEnginePreset");
    sound.setAttribute("version", 2);
    sound.setAttribute("type", "sound");
    sound.setAttribute("engine", "bass");
    sound.createNewChildElement("name")->setText("Old Sound");
    sound.createNewChildElement("parameters")
        ->createNewChildElement("PARAMETERS");
    writeFile(dir.getChildFile("Old Sound.xml"), sound);

    juce::XmlElement v1("VstEnginePreset");
    v1.setAttribute("version", 1);
    v1.setAttribute("type", "full");
    v1.createNewChildElement("name")->setText("Old Full");
    v1.createNewChildElement("parameters")->createNewChildElement("PARAMETERS");
    v1.createNewChildElement("sequence")->addTextElement("AAAA");
    writeFile(dir.getChildFile("Old Full.xml"), v1);

    auto future = *saved;
    future.setAttribute("version", Manager::currentPresetVersion + 1);
    future.getChildByName("name")->deleteAllTextElements();
    future.getChildByName("name")->addTextElement("Future");
    writeFile(dir.getChildFile("Future.xml"), future);

    REQUIRE(!containsName(manager, "Old Sound"), "sound preset not listed");
    REQUIRE(!containsName(manager, "Old Full"), "v1 full preset not listed");
    REQUIRE(!containsName(manager, "Future"), "future version not listed");
    const auto before = project.restores;
    REQUIRE(manager.loadFullPreset("Old Sound").error
                == Manager::Error::invalidPreset,
            "sound preset rejected");
    REQUIRE(manager.loadFullPreset("Old Full").error
                == Manager::Error::unsupportedVersion,
            "v1 full preset rejected");
    REQUIRE(manager.loadFullPreset("Future").error
                == Manager::Error::unsupportedVersion,
            "future version rejected");
    REQUIRE(project.restores == before, "rejected files never restore state");
    REQUIRE(manager.saveFullPreset("Old Sound").error
                == Manager::Error::alreadyExists,
            "saving never overwrites a non-scene file");

    // The project authority decides; a refused restore is a failed load.
    auto stale = *saved;
    stale.getChildByName("parameters")
        ->getChildByName("PARAMETERS")
        ->setAttribute("stateSchemaVersion", 4);
    stale.getChildByName("name")->deleteAllTextElements();
    stale.getChildByName("name")->addTextElement("Stale");
    writeFile(dir.getChildFile("Stale.xml"), stale);
    project.live.setProperty("marker", "kept", nullptr);
    REQUIRE(manager.loadFullPreset("Stale").error
                == Manager::Error::invalidPreset,
            "refused project restore fails the load");
    REQUIRE(project.live.getProperty("marker").toString() == "kept",
            "failed load leaves live state unchanged");

    REQUIRE(manager.renamePreset("Scene A", "Scene B"), "scene renames");
    REQUIRE(!containsName(manager, "Scene A") && containsName(manager, "Scene B"),
            "rename replaces the entry");
    REQUIRE(manager.deletePreset("Scene B"), "scene deletes");
    REQUIRE(!containsName(manager, "Scene B"), "deleted scene gone");
    REQUIRE(manager.loadFullPreset("Scene B").error
                == Manager::Error::fileNotFound,
            "deleted scene cannot load");

    dir.deleteRecursively();
    std::cout << "Preset tests passed (" << testsPassed << "/" << testsRun
              << ")\n";
    return EXIT_SUCCESS;
}
