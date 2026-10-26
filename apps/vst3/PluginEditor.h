#pragma once

#include "PluginProcessor.h"
#include "ui/AcidPanel.h"
#include "ui/BassPanel.h"
#include "ui/GlobalHeader.h"
#include "vox-ui/Tokens.h"
#include "vox-ui/VoxLookAndFeel.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace vstengine::vst3 {

class VstEngineAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                            private juce::Timer {
public:
    explicit VstEngineAudioProcessorEditor(VstEngineAudioProcessor& processor);
    ~VstEngineAudioProcessorEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;

private:
    struct RackSlotDisplay {
        juce::String number;
        juce::String title;
        juce::String subtitle;
        juce::String channel;
        std::string instrumentId;
        juce::Colour waveColour;
        bool active { false };
    };

    void timerCallback() override;
    void selectRackSlot(std::size_t slotIndex);
    void updateActiveSlotView();
    void randomizeGlobalSeed();
    void triggerPanic();

    void drawTopHeaderBar(juce::Graphics& g, juce::Rectangle<float> bounds) const;
    void drawInstrumentRack(juce::Graphics& g, juce::Rectangle<float> bounds) const;
    void drawBottomKeyboardBar(juce::Graphics& g, juce::Rectangle<float> bounds) const;

    VstEngineAudioProcessor& processorRef_;
    vox_ui::VoxLookAndFeel lookAndFeel_;

    std::size_t selectedSlotIndex_ { 1 }; // Default selected slot: 02 Acid (index 1)
    std::uint32_t globalSeed_ { 1234u };

    // Top Global Bar Controls
    juce::ComboBox projectPresetBox_;
    juce::TextButton saveProjectBtn_ { "Save" };
    juce::Label seedValueLabel_;
    juce::TextButton randomSeedBtn_ { "🎲" };
    juce::Slider masterOutputKnob_;
    juce::TextButton panicButton_ { "Panic" };
    juce::TextButton settingsButton_ { "Settings" };

    // Instrument Panels inside the Rack Workspace
    std::unique_ptr<ui::BassPanel> bassPanel_;
    std::unique_ptr<ui::AcidPanel> acidPanel_;

    // Bottom Bar Controls (KEYBOARD / CHORDS / SCALE)
    juce::TextButton keyboardTabBtn_ { "KEYBOARD" };
    juce::TextButton chordsTabBtn_ { "CHORDS" };
    juce::TextButton scaleTabBtn_ { "SCALE" };
    juce::ComboBox velocityCurveBox_;
    juce::TextButton midiLearnBtn_ { "MIDI Learn" };

    // Cached layout areas
    juce::Rectangle<float> topBarBounds_;
    juce::Rectangle<float> rackPanelBounds_;
    juce::Rectangle<float> moduleWorkspaceBounds_;
    juce::Rectangle<float> bottomBarBounds_;
    std::array<juce::Rectangle<float>, 16> slotCardBounds_ {};

    std::array<RackSlotDisplay, 16> slots_ {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VstEngineAudioProcessorEditor)
};

} // namespace vstengine::vst3
