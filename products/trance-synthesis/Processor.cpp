#include "Processor.h"

namespace vox::syn {

// Accent: Electric Violet (05_ULTIMA_VOX_UI_JUCE_SKIN.md §3).
SynthesisProcessor::SynthesisProcessor()
    : vox::suite::SuiteProcessor ({ JucePlugin_Name,
                                    vox::preset::makeTag (VOX_SUITE_STATE_TAG),
                                    1,
                                    juce::Colour::fromString ("FF9668F2") })
{
}

} // namespace vox::syn

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new vox::syn::SynthesisProcessor();
}
