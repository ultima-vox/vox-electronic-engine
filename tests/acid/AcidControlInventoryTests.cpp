#include "acid/AcidControlInventory.h"
#include <array>
#include <iostream>
#include <set>
#include <string_view>

int main() {
  using namespace vstengine::acid;
  int failures = 0;
  auto check = [&](bool condition, const char *message) {
    if (!condition) {
      ++failures;
      std::cerr << "FAIL: " << message << '\n';
    }
  };
  const auto &controls = acidControlInventory;
  check(controls.size() == 80, "all independently automatable controls listed");
  std::set<std::string_view> ids;
  std::set<unsigned> poolIndices;
  std::array<bool, AcidEngine::parameterCount> engineFields{};
  AcidEngine engine;
  for (const auto &c : controls) {
    check(!c.id.empty() && ids.insert(c.id).second, "semantic IDs are unique");
    check(c.engineIndex <= AcidEngine::parameterCount,
          "engine index or exact play-flag sentinel is valid");
    check(c.poolIndex < 80 && poolIndices.insert(c.poolIndex).second,
          "explicit pool indices are unique and bounded");
    if (c.engineIndex < AcidEngine::parameterCount) {
      check(c.engineIndex != AcidEngine::playModeFlags &&
                !engineFields[c.engineIndex],
            "individual control maps to one unpacked field");
      engineFields[c.engineIndex] = true;
      check(c.id == AcidEngine::parameterIds[c.engineIndex],
            "semantic identity matches engine");
      const auto original = engine.parameter(c.id);
      check(engine.setParameter(c.id, 0.123f), "engine accepts inventory ID");
      check(engine.parameter(c.id) != original || original == 0.123f,
            "engine value changes");
      check(engine.setParameter(c.id, original), "restore original value");
    }
  }
  for (std::size_t i = 0; i < engineFields.size(); ++i)
    check(engineFields[i] == (i != AcidEngine::playModeFlags),
          "all unpacked engine fields covered");
  for (auto id : {"legato", "retrigger", "vel_to_accent", "slide_on_overlap"})
    check(ids.contains(id), "independent play flag present");
  for (const auto &c : controls)
    if (c.id == "legato" || c.id == "retrigger" || c.id == "vel_to_accent" ||
        c.id == "slide_on_overlap")
      check(c.engineIndex == AcidEngine::parameterCount,
            "play flag uses exact sentinel");
  const auto indexOf = [&](std::string_view id) {
    for (const auto &control : controls)
      if (control.id == id)
        return control.poolIndex;
    return 999u;
  };
  check(indexOf("waveform") == 0 && indexOf("step_01") == 57 &&
            indexOf("step_16") == 72 && indexOf("legato") == 76 &&
            indexOf("slide_on_overlap") == 79,
        "representative proposed semantic-to-index assignments are pinned");
  for (unsigned mask = 0; mask < 16; ++mask) {
    engine.setPlayModeOptions((mask & 1) != 0, (mask & 2) != 0, (mask & 4) != 0,
                              (mask & 8) != 0);
    check(engine.isLegatoEnabled() == ((mask & 1) != 0) &&
              engine.isRetriggerEnabled() == ((mask & 2) != 0) &&
              engine.isVelToAccentEnabled() == ((mask & 4) != 0) &&
              engine.isSlideOnOverlapEnabled() == ((mask & 8) != 0),
          "all play flag combinations remain independent");
  }
  std::cout << "Acid control inventory: " << failures << " failures\n";
  return failures != 0;
}
