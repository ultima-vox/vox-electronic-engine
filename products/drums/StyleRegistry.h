#pragma once

// VOX Drums style registry (issue 136): the one authoritative list of the
// generator's styles. Exactly 23 canonical styles with stable ids that are
// never reused or renumbered; House appears once. Ids 0-6 keep the meaning
// of the original styles (Style enum, DGEN v1); 7-22 are the 16 new styles
// in the order of issue 136.
//
// A style is `available` when it has a declarative definition the
// generator can run. In this foundation step only the original seven do;
// the sixteen new ones get their rules after the Architecture Guard review
// of PR 137 (§3), so they are listed but not offered for generation.
//
// Message thread only. The factory definitions are parsed once, on first
// use, by the same parser an imported style will use.

#include "StyleDefinition.h"

namespace vox::drums::style {

inline constexpr int registrySize = 23;

struct StyleInfo {
    int id = 0;               // stable, 0..22
    const char* key = "";     // stable text id, e.g. "dark-psy"
    const char* name = "";    // shown to the user
    const char* group = "";   // browser grouping
    int tempoMin = 120, tempoMax = 130;   // recommended BPM range; information only, never sent to the host
};

// All 23, in id order.
const std::array<StyleInfo, registrySize>& registry() noexcept;
const StyleInfo* infoById (int id) noexcept;
const StyleInfo* infoByKey (const juce::String& key) noexcept;

// The parsed factory definition of `id`, or nullptr when the style has no
// definition yet (not available for generation).
const Definition* factoryDefinition (int id);
bool available (int id);

// A factory style that fails to parse is a build defect: the error text of
// the first failure ("" when all parsed). Checked by the tests.
juce::String factoryParseError();

} // namespace vox::drums::style
