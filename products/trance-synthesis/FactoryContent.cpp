#include "FactoryContent.h"

#include <cstddef>

namespace vox::syn {

#include "FactoryData.inc"

static_assert (std::size (kFactoryPrograms) == static_cast<std::size_t> (factoryProgramCapacity));
static_assert (std::size (kFactoryPhrases) == static_cast<std::size_t> (factoryPhraseCapacity));
static_assert (factoryProgramsReady == factoryProgramCapacity);
static_assert (factoryPhrasesReady == factoryPhraseCapacity);

int factoryProgramCount() noexcept
{
    return static_cast<int> (std::size (kFactoryPrograms));
}

const FactoryProgram& factoryProgram (int index) noexcept
{
    return kFactoryPrograms[static_cast<std::size_t> (index)];
}

int factoryPhraseCount() noexcept
{
    return static_cast<int> (std::size (kFactoryPhrases));
}

const FactoryPhrase& factoryPhrase (int index) noexcept
{
    return kFactoryPhrases[static_cast<std::size_t> (index)];
}

} // namespace vox::syn
