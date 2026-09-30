juce_add_gui_app(vox_ui_showcase
  PRODUCT_NAME "VOX UI Showcase")

target_sources(vox_ui_showcase PRIVATE Main.cpp)

target_link_libraries(vox_ui_showcase PRIVATE
  vox_ui
  juce::juce_gui_extra)

target_compile_definitions(vox_ui_showcase PRIVATE
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0)
