#include "Processor.h"

namespace vox::drums {

// Accent: Copper (05_ULTIMA_VOX_UI_JUCE_SKIN.md §3).
DrumsProcessor::DrumsProcessor()
    : vox::suite::SuiteProcessor ({ JucePlugin_Name,
                                    vox::preset::makeTag (VOX_SUITE_STATE_TAG),
                                    1,
                                    juce::Colour::fromString ("FFEF805D") })
{
}

} // namespace vox::drums

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new vox::drums::DrumsProcessor();
}
