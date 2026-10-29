#include "acid/AcidControlInventory.h"
#include "acid/AcidPatternDisplay.h"
#include <array>
#include <iostream>
#include <limits>
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
  std::array<unsigned, 4> typeCounts{};
  unsigned bipolarCount = 0;
  for (const auto &c : controls) {
    const auto metadata = acidControlMetadata(c);
    ++typeCounts[static_cast<std::size_t>(metadata.type)];
    bipolarCount += metadata.bipolar ? 1u : 0u;
    check(metadata.normalizedDefault >= 0.0f && metadata.normalizedDefault <= 1.0f,
          "control default lies in normalized domain");
    check(metadata.automatable, "every Acid control is independently automatable");
    if (c.engineIndex < AcidEngine::parameterCount)
      check(metadata.normalizedDefault == engine.parameter(c.id),
            "manifest default matches fresh engine");
    if (c.id == "waveform")
      check(metadata.type == AcidControlType::continuous, "oscillator waveform is continuous morph");
    if (c.id == "filter_mode" || c.id == "dist_mode" || c.id == "lfo1_wave" ||
        c.id == "lfo2_wave" || c.id == "lfo3_wave" || c.id == "step_rate")
      check(metadata.type == AcidControlType::choice && metadata.choiceCount == 4,
            "four-option selectors are identified");
    if (c.id == "mod_target" || c.id == "env2_target" || c.id == "lfo1_target" ||
        c.id == "lfo2_target" || c.id == "lfo3_target" || c.id == "step_target")
      check(metadata.type == AcidControlType::choice && metadata.choiceCount == 6,
            "modulation destinations have six options");
    if (c.id == "unison_voices")
      check(metadata.type == AcidControlType::integer && metadata.choiceCount == 4,
            "unison voices have four integer values");
    if (c.id == "lfo1_sync" || c.id == "lfo2_sync" || c.id == "lfo3_sync" ||
        c.engineIndex == AcidEngine::parameterCount)
      check(metadata.type == AcidControlType::boolean && metadata.choiceCount == 2,
            "sync and play flags are boolean");
    if (c.id.starts_with("step_0") || c.id.starts_with("step_1"))
      check(metadata.type == AcidControlType::continuous && metadata.bipolar,
            "step levels are bipolar continuous controls");
  }
  check(typeCounts == std::array<unsigned, 4>{60, 12, 1, 7},
        "all 80 control types are accounted for");
  check(bipolarCount == 19, "STEP levels, tune, fine and pan are bipolar");
  for (const auto &c : controls) {
    if (c.id == "legato" || c.id == "retrigger" || c.id == "vel_to_accent" || c.id == "slide_on_overlap") {
      const auto m = acidControlMetadata(c);
      const bool expected = c.id == "legato" ? engine.isLegatoEnabled()
          : c.id == "retrigger" ? engine.isRetriggerEnabled()
          : c.id == "vel_to_accent" ? engine.isVelToAccentEnabled()
          : engine.isSlideOnOverlapEnabled();
      check(m.normalizedDefault == (expected ? 1.0f : 0.0f), "flag default matches engine");
    }
  }
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
  check(acidDisplayMidiNote(127, -67) == 60,
        "sequence note display preserves valid large negative offsets");
  check(acidDisplayMidiNote(0, 48) == 48,
        "sequence note display preserves valid large positive offsets");
  check(acidDisplayMidiNote(48, std::numeric_limits<int>::max()) == 127 &&
        acidDisplayMidiNote(48, std::numeric_limits<int>::min()) == 0,
        "invalid extreme offsets saturate without signed overflow");
  // Existing DSP bins use floor(v * 3.99/5.99), not nearest choice.
  check(acidSelectorIndex(0.20f, 4) == 0, "four-way selector first bin");
  check(acidSelectorIndex(0.26f, 4) == 1, "four-way selector second bin");
  check(acidSelectorIndex(0.10f, 6) == 0, "six-way selector first bin");
  check(acidSelectorIndex(0.18f, 6) == 1, "six-way selector second bin");
  check(acidSelectorIndex(1.0f, 4) == 3 && acidSelectorIndex(1.0f, 6) == 5,
        "selector upper endpoint maps to last option");
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
