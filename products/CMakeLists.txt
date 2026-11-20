# One juce_add_plugin target per product. Identity comes only from
# cmake/suite/SuiteIdentity.cmake.

function(vox_add_suite_plugin key)
  set(target ${VOX_SUITE_${key}_TARGET})
  set(dir ${CMAKE_CURRENT_SOURCE_DIR}/${VOX_SUITE_${key}_DIR})

  juce_add_plugin(${target}
    VERSION ${VOX_SUITE_VERSION}
    COMPANY_NAME "${VOX_SUITE_COMPANY_NAME}"
    BUNDLE_ID ${VOX_SUITE_${key}_BUNDLE_ID}
    PLUGIN_MANUFACTURER_CODE ${VOX_SUITE_MANUFACTURER_CODE}
    PLUGIN_CODE ${VOX_SUITE_${key}_PLUGIN_CODE}
    PRODUCT_NAME "${VOX_SUITE_${key}_NAME}"
    FORMATS VST3
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES "Instrument")

  target_sources(${target} PRIVATE
    ${dir}/Processor.cpp ${dir}/Processor.h
    ${CMAKE_CURRENT_SOURCE_DIR}/common/SuiteProcessor.h
    ${CMAKE_CURRENT_SOURCE_DIR}/common/SuiteEditor.h)
  target_include_directories(${target} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR} ${dir})
  target_compile_definitions(${target} PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    VOX_SUITE_STATE_TAG="${VOX_SUITE_${key}_STATE_TAG}")
  target_link_libraries(${target}
    PRIVATE juce::juce_audio_utils vox_ui
    PUBLIC vox_preset juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive- /FS)
  endif()
endfunction()

# VOX Drums kit engine (DRM-03): C++, no legacy ABI.
add_library(vox_drums_engine STATIC
  drums/engine/KitEngine.cpp drums/engine/KitEngine.h
  drums/engine/SendFx.cpp drums/engine/SendFx.h
  drums/engine/BarTiming.cpp drums/engine/BarTiming.h drums/engine/BarTiming.inl)
target_include_directories(vox_drums_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/drums)
target_compile_features(vox_drums_engine PUBLIC cxx_std_20)
set_target_properties(vox_drums_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)

foreach(key IN LISTS VOX_SUITE_PRODUCTS)
  vox_add_suite_plugin(${key})
endforeach()

target_sources(${VOX_SUITE_drm_TARGET} PRIVATE
  drums/DrumsParameters.cpp drums/DrumsParameters.h
  drums/DrumsEditor.cpp drums/DrumsEditor.h
  drums/DrumsWorkspace.cpp drums/DrumsWorkspace.h drums/WorkspaceModel.h
  drums/DrumsLayoutState.cpp drums/DrumsLayoutState.h
  drums/WorkspaceLibrary.cpp drums/WorkspaceLibrary.h
  drums/DrumsDockZone.cpp drums/DrumsDockZone.h
  drums/DrumsDial.h
  drums/DrumsFader.h
  drums/DrumsRolesPanel.cpp drums/DrumsRolesPanel.h
  drums/PadControlBindings.h drums/PadControlSurface.cpp drums/PadControlSurface.h
  drums/SampleLibrary.cpp drums/SampleLibrary.h
  drums/KitStore.cpp drums/KitStore.h
  drums/PatternStore.cpp drums/PatternStore.h drums/SongModel.cpp drums/Generator.cpp drums/Generator.h drums/PadRoles.cpp drums/PadRoles.h drums/FactoryContent.cpp drums/FactoryContent.h drums/Presets.cpp drums/Presets.h drums/DrumsPages.cpp drums/DrumsPages.h drums/PatternExport.cpp drums/PatternExport.h
  drums/PatternEditor.cpp drums/PatternEditor.h
  drums/StyleDefinition.cpp drums/StyleDefinition.h drums/StyleRegistry.cpp drums/StyleRegistry.h
  drums/StyleLibrary.cpp drums/StyleLibrary.h)
target_link_libraries(${VOX_SUITE_drm_TARGET} PRIVATE vox_drums_engine juce::juce_cryptography)

# Factory style definitions (issue 136): the JSON files in drums/styles are
# built into the plugin as text, in this order, and parsed by the same
# parser an imported style will use. Reconfigures when a file changes.
set(VOX_DRUMS_FACTORY_STYLES full-on goa house techno trap breaks ambient
    uplifting-trance anthem-trance dark-psy melodic-trance hard-rock heavy-metal symphonic-metal nordic-metal
    tech-house drum-and-bass melodic-drum-and-bass melodic-techno progressive-trance dream-trance hard-trance symphonic-trance)
set(_vox_styles_inc "${CMAKE_CURRENT_BINARY_DIR}/drums/FactoryStyles.inc")
set(_vox_styles_text "// Generated from products/drums/styles/*.json. Do not edit.\n")
foreach(_style IN LISTS VOX_DRUMS_FACTORY_STYLES)
  set(_file "${CMAKE_CURRENT_SOURCE_DIR}/drums/styles/${_style}.json")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_file}")
  file(READ "${_file}" _json)
  string(APPEND _vox_styles_text "R\"voxstyle(${_json})voxstyle\",\n")
endforeach()
file(CONFIGURE OUTPUT "${_vox_styles_inc}" CONTENT "${_vox_styles_text}" @ONLY)
target_include_directories(${VOX_SUITE_drm_TARGET} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/drums")
