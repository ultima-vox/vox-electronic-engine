#pragma once
#include "common/UiComponents.h"
#include "match/KickBassMatch.h"

namespace vstengine::ui {
class MatchPanel final : public juce::Component {
public:
    struct Callbacks {
        std::function<vstengine::match::MatchReport()> analyze;
        std::function<void(const vstengine::match::MatchAdjustments&)> apply;
    };
    MatchPanel (juce::AudioProcessorValueTreeState&, Callbacks);
    void paint (juce::Graphics&) override; void resized() override;
    void refreshCurrentValues() { updateLabels(); }
private:
    void analyze(); void apply(); void updateLabels();
    juce::AudioProcessorValueTreeState& state;
    Callbacks callbacks;
    juce::TextButton analyzeButton { "ANALYZE" }, applyButton { "APPLY" };
    juce::Label actionTitle, measuredTitle, recommendedTitle, currentTitle, status;
    std::array<juce::Label, 7> measured;
    std::array<juce::Label, 4> recommended, current;
    vstengine::match::MatchReport report {};
    bool hasReport {};
};
} // namespace vstengine::ui
