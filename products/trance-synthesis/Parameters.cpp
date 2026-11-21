#include "Parameters.h"

namespace vox::syn {

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (const auto& entry : parameterCatalog)
    {
        if (entry.choice)
        {
            layout.add (std::make_unique<juce::AudioParameterChoice> (
                juce::ParameterID { entry.id, 1 }, entry.name,
                juce::StringArray { "Psy Bass", "Acid", "VA" },
                juce::roundToInt (entry.initial)));
        }
        else
        {
            layout.add (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { entry.id, 1 }, entry.name,
                juce::NormalisableRange<float> { entry.minimum, entry.maximum },
                entry.initial));
        }
    }
    return layout;
}

} // namespace vox::syn
