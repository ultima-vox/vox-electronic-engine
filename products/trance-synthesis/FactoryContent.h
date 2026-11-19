#pragma once

#include "FactoryBank.h"

namespace vox::syn {

struct FactoryProgram {
    const char* id;
    const char* name;
    const char* style;
    const char* role;
    const char* purpose;
    ProgramCategory category;
    const float* values;
};

struct FactoryPhrase {
    const char* id;
    const char* name;
    const char* style;
    const char* role;
    const char* purpose;
    ProgramCategory category;
    int ticksPerQuarter;
    const PhraseNote* notes;
    int noteCount;
};

[[nodiscard]] int factoryProgramCount() noexcept;
[[nodiscard]] const FactoryProgram& factoryProgram (int index) noexcept;
[[nodiscard]] int factoryPhraseCount() noexcept;
[[nodiscard]] const FactoryPhrase& factoryPhrase (int index) noexcept;

} // namespace vox::syn
