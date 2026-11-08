# Suite tests: CTest label "suite". Run with: ctest -L suite

add_executable(vox_preset_tests StateContainerTests.cpp)
target_link_libraries(vox_preset_tests PRIVATE vox_preset)
add_test(NAME vox_preset_tests COMMAND vox_preset_tests)

add_executable(suite_identity_tests IdentityTests.cpp)
target_include_directories(suite_identity_tests PRIVATE ${juce_SOURCE_DIR}/modules)
target_link_libraries(suite_identity_tests PRIVATE juce::juce_core)
target_compile_definitions(suite_identity_tests PRIVATE
  JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0
  VOX_ID_MANUFACTURER="${VOX_SUITE_MANUFACTURER_CODE}"
  VOX_ID_SYN_CODE="${VOX_SUITE_syn_PLUGIN_CODE}" VOX_ID_SYN_CID="${VOX_SUITE_syn_CID}"
  VOX_ID_DRM_CODE="${VOX_SUITE_drm_PLUGIN_CODE}" VOX_ID_DRM_CID="${VOX_SUITE_drm_CID}"
  VOX_ID_FX_CODE="${VOX_SUITE_fx_PLUGIN_CODE}" VOX_ID_FX_CID="${VOX_SUITE_fx_CID}")
add_test(NAME suite_identity_tests COMMAND suite_identity_tests)

set(identity_expected "")
set(bundles "")
foreach(key IN LISTS VOX_SUITE_PRODUCTS)
  list(APPEND identity_expected
    "${VOX_SUITE_${key}_PLUGIN_CODE}|${VOX_SUITE_${key}_BUNDLE_ID}|${VOX_SUITE_${key}_CID}|${VOX_SUITE_${key}_NAME}")
endforeach()
add_test(NAME suite_identity_doc
  COMMAND ${CMAKE_COMMAND} "-DDOC=${PROJECT_SOURCE_DIR}/docs/ultima-vox-music/IDENTITY.md"
          "-DEXPECTED=${identity_expected}" -P ${PROJECT_SOURCE_DIR}/cmake/suite/CheckIdentityDoc.cmake)

add_test(NAME suite_boundaries
  COMMAND ${CMAKE_COMMAND} "-DROOT=${PROJECT_SOURCE_DIR}" -P ${PROJECT_SOURCE_DIR}/cmake/suite/CheckBoundaries.cmake)

foreach(key IN LISTS VOX_SUITE_PRODUCTS)
  set(product ${VOX_SUITE_${key}_TARGET})
  set(test suite_shell_tests_${key})
  add_executable(${test} SuiteShellTests.cpp)
  target_link_libraries(${test} PRIVATE ${product})
  # The plugin shared-code library already contains the JUCE modules; the
  # test only needs their headers (same pattern as the legacy shell tests).
  target_include_directories(${test} PRIVATE ${juce_SOURCE_DIR}/modules ${PROJECT_SOURCE_DIR}/libs/vox-ui)
  # Another product's state tag, for the cross-product rejection test.
  list(FIND VOX_SUITE_PRODUCTS ${key} index)
  math(EXPR other "(${index} + 1) % 3")
  list(GET VOX_SUITE_PRODUCTS ${other} otherKey)
  target_compile_definitions(${test} PRIVATE
    VOX_TEST_PRODUCT_NAME="${VOX_SUITE_${key}_NAME}"
    VOX_TEST_STATE_TAG="${VOX_SUITE_${key}_STATE_TAG}"
    VOX_TEST_FOREIGN_TAG="${VOX_SUITE_${otherKey}_STATE_TAG}")
  add_test(NAME ${test} COMMAND ${test})

  list(APPEND bundles
    "$<TARGET_FILE_DIR:${product}_VST3>/../..|${VOX_SUITE_${key}_CID}|${VOX_SUITE_${key}_NAME}")
endforeach()

# moduleinfo.json is written into each bundle by JUCE's VST3 manifest helper
# after the plugin is linked; it carries the class ID a DAW will see.
add_test(NAME suite_moduleinfo
  COMMAND ${CMAKE_COMMAND} "-DBUNDLES=${bundles}" -P ${PROJECT_SOURCE_DIR}/cmake/suite/CheckModuleInfo.cmake)

get_property(suite_tests DIRECTORY PROPERTY TESTS)
set_tests_properties(${suite_tests} PROPERTIES LABELS suite)
