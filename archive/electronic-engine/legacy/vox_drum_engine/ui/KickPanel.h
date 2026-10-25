#pragma once
#include "common/UiComponents.h"

namespace vstengine::ui {

class KickPanel final : public juce::Component {
public:
    explicit KickPanel (juce::AudioProcessorValueTreeState&);
    void resized() override;

private:
    ParameterSection pitch { "PITCH" };
    ParameterSection body { "BODY" };
    ParameterSection transient { "TRANSIENT" };
    ParameterSection output { "DRIVE / OUTPUT" };
};

} // namespace vstengine::ui
