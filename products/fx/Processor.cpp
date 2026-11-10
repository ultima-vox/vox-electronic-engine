#include "Processor.h"

namespace vox::fx {

// Accent: Aqua (05_ULTIMA_VOX_UI_JUCE_SKIN.md §3).
FxProcessor::FxProcessor()
    : vox::suite::SuiteProcessor ({ JucePlugin_Name,
                                    vox::preset::makeTag (VOX_SUITE_STATE_TAG),
                                    1,
                                    juce::Colour::fromString ("FF16CDBA") })
{
}

} // namespace vox::fx

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new vox::fx::FxProcessor();
}
