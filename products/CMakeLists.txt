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

# VOX Drums engine (DRM-01): the reviewed legacy 8-pad module, compiled in
# statically from a byte-identical copy (PROVENANCE.csv) behind the private
# DrumEngine wrapper. Its entry point is renamed so it can never clash with
# another copy (the DRM-00 test reference). Replaced by DRM-03.
add_library(vox_drums_engine STATIC
  drums/engine/DrumEngine.cpp drums/engine/DrumEngine.h
  drums/engine/legacy8/VoxFullOnDrums.c)
target_include_directories(vox_drums_engine
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/drums
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/drums/engine/legacy8)
target_compile_definitions(vox_drums_engine PRIVATE voxModuleEntryV2=voxDrumsLegacy8EntryV2)
target_compile_features(vox_drums_engine PUBLIC cxx_std_20)
set_target_properties(vox_drums_engine PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON POSITION_INDEPENDENT_CODE ON)
if(NOT MSVC)
  target_link_libraries(vox_drums_engine PUBLIC m)
endif()

foreach(key IN LISTS VOX_SUITE_PRODUCTS)
  vox_add_suite_plugin(${key})
endforeach()

target_sources(${VOX_SUITE_drm_TARGET} PRIVATE
  drums/DrumsParameters.cpp drums/DrumsParameters.h
  drums/DrumsEditor.cpp drums/DrumsEditor.h)
target_link_libraries(${VOX_SUITE_drm_TARGET} PRIVATE vox_drums_engine)
