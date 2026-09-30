juce_add_gui_app(vox_ui_showcase
  PRODUCT_NAME "VOX UI Showcase")

target_sources(vox_ui_showcase PRIVATE Main.cpp)

target_link_libraries(vox_ui_showcase PRIVATE
  vox_ui
  juce::juce_gui_extra)

target_compile_definitions(vox_ui_showcase PRIVATE
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0)

# Visual Gate capture must not modify editor_smoke_tests. Defer target creation
# until the top-level directory has finished defining the VstEngine target.
function(vox_add_visual_gate_capture)
  add_executable(vox_ui_visual_gate_capture VisualGateCapture.cpp)
  target_include_directories(vox_ui_visual_gate_capture PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/vst3
    ${CMAKE_SOURCE_DIR}/libs
    ${CMAKE_BINARY_DIR}/VstEngine_artefacts/JuceLibraryCode
    ${juce_SOURCE_DIR}/modules)
  target_link_libraries(vox_ui_visual_gate_capture PRIVATE VstEngine)
  target_compile_features(vox_ui_visual_gate_capture PRIVATE cxx_std_20)
endfunction()

cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL vox_add_visual_gate_capture)
