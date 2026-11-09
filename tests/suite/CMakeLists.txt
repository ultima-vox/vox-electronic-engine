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


# DRM-00: golden corpus of the legacy 8-pad Drums module at f9c8bc6 (owner
# decision D3). The module and its C ABI header are byte-identical, test-only
# copies (PROVENANCE.csv); no product or library may link this target
# (suite_boundaries).
add_library(vox_legacy_drums_reference STATIC
  legacy-reference/drums-f9c8bc6/VoxFullOnDrums.c)
target_include_directories(vox_legacy_drums_reference PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/legacy-reference/drums-f9c8bc6)
set_target_properties(vox_legacy_drums_reference PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
if(UNIX)
  target_link_libraries(vox_legacy_drums_reference PUBLIC m)
endif()

add_executable(suite_drums_legacy_golden_tests DrumsLegacyGoldenTests.cpp)
target_link_libraries(suite_drums_legacy_golden_tests PRIVATE vox_legacy_drums_reference vox_testkit)
add_test(NAME suite_drums_legacy_golden_tests
  COMMAND suite_drums_legacy_golden_tests
          ${CMAKE_CURRENT_SOURCE_DIR}/golden/drums-legacy-f9c8bc6-v1.csv
          --parameters ${PROJECT_SOURCE_DIR}/docs/ultima-vox-music/drums/legacy-parameters-f9c8bc6.csv)

# DRM-01: VOX Drums product tests (parameter contract, editor bindings,
# sound, state, realtime).
add_executable(suite_drums_product_tests DrumsProductTests.cpp)
target_link_libraries(suite_drums_product_tests PRIVATE ${VOX_SUITE_drm_TARGET})
target_include_directories(suite_drums_product_tests PRIVATE ${juce_SOURCE_DIR}/modules ${PROJECT_SOURCE_DIR}/libs/vox-ui)
vox_suite_add_allocation_probe(suite_drums_product_tests)
add_test(NAME suite_drums_product_tests
  COMMAND suite_drums_product_tests ${PROJECT_SOURCE_DIR}/products/drums/parameters.csv)

# DRM-01 parity: the plugin against the DRM-00 reference module. The only
# other test allowed to drive the legacy C ABI (suite_boundaries).
add_executable(suite_drums_parity_tests DrumsParityTests.cpp)
target_link_libraries(suite_drums_parity_tests PRIVATE ${VOX_SUITE_drm_TARGET} vox_legacy_drums_reference)
target_include_directories(suite_drums_parity_tests PRIVATE ${juce_SOURCE_DIR}/modules ${PROJECT_SOURCE_DIR}/libs/vox-ui)
add_test(NAME suite_drums_parity_tests COMMAND suite_drums_parity_tests)

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
  # Products with a sound engine (others must stay silent).
  if(key STREQUAL "drm")
    target_compile_definitions(${test} PRIVATE VOX_TEST_HAS_ENGINE=1)
  endif()
  add_test(NAME ${test} COMMAND ${test})

  list(APPEND bundles
    "$<TARGET_FILE_DIR:${product}_VST3>/../..|${VOX_SUITE_${key}_CID}|${VOX_SUITE_${key}_NAME}")
endforeach()

# Mini-host: loads every built bundle through JUCE's VST3 host (DRM-01).
juce_add_console_app(suite_vst3_host_tests PRODUCT_NAME "suite_vst3_host_tests")
target_sources(suite_vst3_host_tests PRIVATE Vst3HostTests.cpp)
target_compile_definitions(suite_vst3_host_tests PRIVATE
  JUCE_PLUGINHOST_VST3=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
  VOX_TEST_COMPANY="${VOX_SUITE_COMPANY_NAME}")
target_link_libraries(suite_vst3_host_tests PRIVATE
  juce::juce_audio_processors juce::juce_recommended_config_flags)
set(host_bundles "")
foreach(key IN LISTS VOX_SUITE_PRODUCTS)
  add_dependencies(suite_vst3_host_tests ${VOX_SUITE_${key}_TARGET}_VST3)
  set(engine 0)
  set(level "-")
  if(key STREQUAL "drm")
    set(engine 1)
    set(level "Kick Level")
  endif()
  list(APPEND host_bundles
    "$<TARGET_FILE_DIR:${VOX_SUITE_${key}_TARGET}_VST3>/../..|${VOX_SUITE_${key}_NAME}|${engine}|${level}")
endforeach()
add_test(NAME suite_vst3_host_tests COMMAND suite_vst3_host_tests ${host_bundles})

# moduleinfo.json is written into each bundle by JUCE's VST3 manifest helper
# after the plugin is linked; it carries the class ID a DAW will see.
add_test(NAME suite_moduleinfo
  COMMAND ${CMAKE_COMMAND} "-DBUNDLES=${bundles}" -P ${PROJECT_SOURCE_DIR}/cmake/suite/CheckModuleInfo.cmake)


# Review 6724525306 P2: the pinned-dependency check. Strict mode refuses a
# wrong commit and an unverifiable source; non-strict refuses a wrong commit
# and only warns for an unverifiable one.
foreach(case strict-pinned strict-wrong-commit strict-no-metadata relaxed-no-metadata relaxed-wrong-commit)
  add_test(NAME suite_verify_juce_${case}
    COMMAND ${CMAKE_COMMAND} "-DROOT=${PROJECT_SOURCE_DIR}" -DCASE=${case}
            "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/verify-fixtures" "-DJUCE_DIR=${juce_SOURCE_DIR}"
            -DCOMMIT=${VOX_JUCE_COMMIT} -P ${CMAKE_CURRENT_SOURCE_DIR}/VerifyPinnedSourceCase.cmake)
endforeach()
set_tests_properties(suite_verify_juce_strict-wrong-commit suite_verify_juce_strict-no-metadata
                     suite_verify_juce_relaxed-wrong-commit PROPERTIES WILL_FAIL TRUE)

get_property(suite_tests DIRECTORY PROPERTY TESTS)
set_tests_properties(${suite_tests} PROPERTIES LABELS suite)
