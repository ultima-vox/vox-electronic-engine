#pragma once

#include "FactoryBank.h"

namespace vox::syn {

struct FactoryProgram {
    const char* id;
    const char* name;
    const char* style;
    const char* variant;
    const char* purpose;
    FactoryRole role;
    int engineA;
    int engineB;
    const char* mood;
    int auditionNote;
    const float* values; // frozenParameterCount floats. Not the full live catalog.
};

// Extended timbre for one candidate. Not part of the frozen 75-wide row.
// Defaults mean the VA unison, wavetable and mod layer are off.
// Psy Bass and Acid candidates stay on these defaults.
struct FactoryFeature {
    float unison = 0.0f;
    float detune = 0.0f;
    float spread = 0.0f;
    float wtLevel = 0.0f;
    float wtPos = 0.0f;
    float fm = 0.0f;
    float ring = 0.0f;
    float osc3Level = 0.0f;
    float osc3Semi = 0.5f;
    int modSource = 0;
    int modDest = 0;
    float modAmount = 0.5f;
    int extraRoute = -1;
    int extraSource = 0;
    int extraDest = 0;
    float extraAmount = 0.5f;
};

struct FactoryPhrase {
    const char* id;
    const char* name;
    const char* style;
    const char* pattern;
    const char* purpose;
    FactoryRole role;
    int ticksPerQuarter;
    int lengthTicks;
    const PhraseNote* notes;
    int noteCount;
};

[[nodiscard]] int factoryProgramCount() noexcept;
[[nodiscard]] const FactoryProgram& factoryProgram (int index) noexcept;
[[nodiscard]] const FactoryFeature& factoryFeature (int index) noexcept;
[[nodiscard]] int factoryPhraseCount() noexcept;
[[nodiscard]] const FactoryPhrase& factoryPhrase (int index) noexcept;

} // namespace vox::syn
