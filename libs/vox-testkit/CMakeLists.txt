# vox_testkit: test-only helpers (never linked into a plugin).
#  - GoldenRender: WAV writer, render metrics, golden manifests.
#  - probe/: the realtime allocation probe, byte-identical to the reviewed
#    legacy probe at f9c8bc6 (PROVENANCE.csv). It counts allocation calls of
#    the probing thread only; a probed audio path must reach exactly 0.
add_library(vox_testkit STATIC GoldenRender.cpp GoldenRender.h)
target_include_directories(vox_testkit PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(vox_testkit PUBLIC cxx_std_20)

set(VOX_SUITE_PROBE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/probe CACHE INTERNAL "")
set(VOX_SUITE_ALLOCATION_PROBE OFF CACHE INTERNAL "")
if(VOX_SANITIZE_THREAD)
  set(VOX_SUITE_ALLOCATION_PROBE sanitizer CACHE INTERNAL "")
elseif(UNIX AND NOT APPLE AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  set(VOX_SUITE_ALLOCATION_PROBE linux CACHE INTERNAL "")
elseif(WIN32 AND MSVC)
  set(VOX_SUITE_ALLOCATION_PROBE windows CACHE INTERNAL "")
endif()

# Adds the probe to a test executable; defines VOX_TEST_INTERPOSE_MALLOC when
# the platform has one (Linux glibc interposer, Windows IAT hooks, TSan hooks).
function(vox_suite_add_allocation_probe target)
  if(VOX_SUITE_ALLOCATION_PROBE STREQUAL "linux")
    target_sources(${target} PRIVATE ${VOX_SUITE_PROBE_DIR}/AllocationProbeLinux.c)
    target_link_options(${target} PRIVATE -rdynamic)
  elseif(VOX_SUITE_ALLOCATION_PROBE STREQUAL "sanitizer")
    target_sources(${target} PRIVATE ${VOX_SUITE_PROBE_DIR}/AllocationProbeSanitizer.c)
    target_compile_definitions(${target} PRIVATE VOX_PROBE_SANITIZER=1)
  elseif(VOX_SUITE_ALLOCATION_PROBE STREQUAL "windows")
    target_sources(${target} PRIVATE ${VOX_SUITE_PROBE_DIR}/AllocationProbeWindows.c)
  else()
    return()
  endif()
  target_compile_definitions(${target} PRIVATE VOX_TEST_INTERPOSE_MALLOC=1)
  target_include_directories(${target} PRIVATE ${VOX_SUITE_PROBE_DIR})
endfunction()
