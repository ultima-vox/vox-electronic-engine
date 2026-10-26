add_library(vox_ui STATIC)

target_sources(vox_ui PRIVATE
  VoxLookAndFeel.cpp VoxLookAndFeel.h
  VoxComponents.h
  Tokens.h
  Typography.h
  components/VoxPanel.cpp components/VoxPanel.h
  components/VoxKnob.cpp components/VoxKnob.h
  components/VoxButton.cpp components/VoxButton.h
  components/VoxComboBox.cpp components/VoxComboBox.h
  components/VoxTabBar.cpp components/VoxTabBar.h
  components/VoxSectionHeader.cpp components/VoxSectionHeader.h)

target_include_directories(vox_ui PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/components)

target_link_libraries(vox_ui PUBLIC
  juce::juce_gui_basics
  juce::juce_graphics)

target_compile_features(vox_ui PUBLIC cxx_std_20)

option(VOX_UI_BUILD_SHOWCASE "Build VOX UI showcase" OFF)
if(VOX_UI_BUILD_SHOWCASE)
  add_subdirectory(showcase)
endif()
