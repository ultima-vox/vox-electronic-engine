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
[[nodiscard]] int factoryPhraseCount() noexcept;
[[nodiscard]] const FactoryPhrase& factoryPhrase (int index) noexcept;

} // namespace vox::syn
