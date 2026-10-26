add_library(vst_ui STATIC)

target_sources(vst_ui
    PRIVATE
        AcidPanel.cpp
        AcidPanel.h
        BassPanel.cpp
        BassPanel.h
        GlobalHeader.cpp
        GlobalHeader.h
        MainNavigation.cpp
        MainNavigation.h
        PresetBrowser.cpp
        PresetBrowser.h
        StepSequencer.cpp
        StepSequencer.h
        ZoneRangeEditor.cpp
        ZoneRangeEditor.h
)

target_include_directories(vst_ui
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(vst_ui
    PUBLIC
        vst_core
        vst_vox_ui
        vst_acid
        vst_bass
        vst_sequence
        vst_preset
        vst_rack
        juce::juce_gui_basics
        juce::juce_audio_processors
        juce::juce_audio_basics
)
