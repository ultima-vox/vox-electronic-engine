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

add_test(NAME suite_provenance
  COMMAND ${CMAKE_COMMAND} "-DROOT=${PROJECT_SOURCE_DIR}"
          "-DCSV=${PROJECT_SOURCE_DIR}/docs/ultima-vox-music/PROVENANCE.csv"
          -P ${PROJECT_SOURCE_DIR}/cmake/suite/CheckProvenance.cmake)

# Donor tests, byte-identical to f9c8bc6 (PROVENANCE.csv), now run against
# the vox_dsp copy.
add_executable(suite_effects_tests donor/EffectsChainTests.cpp)
target_link_libraries(suite_effects_tests PRIVATE vox_dsp)
add_test(NAME suite_effects_tests COMMAND suite_effects_tests)

add_executable(suite_effects_tail_policy_tests donor/EffectsTailPolicyTests.cpp)
target_link_libraries(suite_effects_tail_policy_tests PRIVATE vox_dsp)
add_test(NAME suite_effects_tail_policy_tests COMMAND suite_effects_tail_policy_tests)

# The allocation probe proves itself first (every allocation path on the
# probed thread is counted, other threads are not), then measures.
if(VOX_SUITE_ALLOCATION_PROBE)
  add_library(vox_suite_allocation_canary SHARED donor/AllocationCanary.c)
  set_target_properties(vox_suite_allocation_canary PROPERTIES PREFIX "" C_VISIBILITY_PRESET hidden)
  if(MSVC)
    set_property(TARGET vox_suite_allocation_canary PROPERTY
      MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  endif()
  add_executable(suite_allocation_probe_tests donor/AllocationProbeTests.cpp)
  vox_suite_add_allocation_probe(suite_allocation_probe_tests)
  add_dependencies(suite_allocation_probe_tests vox_suite_allocation_canary)
  if(WIN32)
    target_link_libraries(suite_allocation_probe_tests PRIVATE ole32)
  else()
    find_package(Threads REQUIRED)
    target_link_libraries(suite_allocation_probe_tests PRIVATE ${CMAKE_DL_LIBS} Threads::Threads)
  endif()
  add_test(NAME suite_allocation_probe_tests
    COMMAND suite_allocation_probe_tests $<TARGET_FILE:vox_suite_allocation_canary>)
endif()

add_executable(suite_primitives_tests PrimitivesTests.cpp)
target_link_libraries(suite_primitives_tests PRIVATE vox_dsp vox_testkit)
add_test(NAME suite_primitives_tests COMMAND suite_primitives_tests)

add_executable(suite_effects_golden_tests EffectsGoldenTests.cpp)
target_link_libraries(suite_effects_golden_tests PRIVATE vox_dsp vox_testkit)
vox_suite_add_allocation_probe(suite_effects_golden_tests)
add_test(NAME suite_effects_golden_tests
  COMMAND suite_effects_golden_tests ${CMAKE_CURRENT_SOURCE_DIR}/golden/effects-chain-v1.csv)

add_test(NAME suite_boundaries
  COMMAND ${CMAKE_COMMAND} "-DROOT=${PROJECT_SOURCE_DIR}" -P ${PROJECT_SOURCE_DIR}/cmake/suite/CheckBoundaries.cmake)

foreach(key IN LISTS VOX_SUITE_PRODUCTS)
  set(product ${VOX_SUITE_${key}_TARGET})
  set(test suite_shell_tests_${key})
  add_executable(${test} SuiteShellTests.cpp)
  target_link_libraries(${test} PRIVATE ${product})
  vox_suite_add_allocation_probe(${test})
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
