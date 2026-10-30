# Configure Rack #80 with -DCMAKE_PROJECT_INCLUDE=<absolute path to this file>
# and -DVOX_BASS_RACK_TEST_SOURCE=<absolute path to BassNativeRackTests.cpp>.
# The target is deferred until Rack has declared VstEngine and fetched JUCE.
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR AND NOT DEFINED VOX_BASS_RACK_INJECTION_ADDED)
  set(VOX_BASS_RACK_INJECTION_ADDED TRUE)
  function(vox_add_bass_rack_test)
    if(NOT EXISTS "${VOX_BASS_RACK_TEST_SOURCE}")
      message(FATAL_ERROR "VOX_BASS_RACK_TEST_SOURCE must name BassNativeRackTests.cpp")
    endif()
    add_executable(bass_native_rack_tests "${VOX_BASS_RACK_TEST_SOURCE}")
    target_include_directories(bass_native_rack_tests PRIVATE
      "${CMAKE_SOURCE_DIR}/apps/vst3" "${CMAKE_SOURCE_DIR}/libs"
      "${CMAKE_BINARY_DIR}/VstEngine_artefacts/JuceLibraryCode" "${juce_SOURCE_DIR}/modules")
    target_link_libraries(bass_native_rack_tests PRIVATE VstEngine)
    if(DEFINED VOX_BASS_SOUND_TEST_SOURCE)
      if(NOT EXISTS "${CMAKE_SOURCE_DIR}/libs/instrument/SoundLayout.h")
        message(FATAL_ERROR "Bass SOUND validation requires the canonical #89 layout contract")
      endif()
      add_executable(bass_sound_layout_tests "${VOX_BASS_SOUND_TEST_SOURCE}")
      get_filename_component(vox_bass_module_test_dir "${VOX_BASS_SOUND_TEST_SOURCE}" DIRECTORY)
      target_include_directories(bass_sound_layout_tests PRIVATE
        "${CMAKE_SOURCE_DIR}/libs" "${vox_bass_module_test_dir}/../../../libs")
      target_link_libraries(bass_sound_layout_tests PRIVATE vst_vins)
    endif()
  endfunction()
  cmake_language(DEFER CALL vox_add_bass_rack_test)
endif()
