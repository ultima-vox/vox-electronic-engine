# cmake -DROOT=<repo> -P CheckBoundaries.cmake
# Suite code must not include legacy Rack / instrument ABI / .vins headers or
# the legacy plugin shell (06_CLAUDE_EXECUTION_BRIEF.md §1, FOUND-00 §3.2).
set(dirs products libs/vox-preset libs/vox-dsp libs/vox-testkit tests/suite)
set(forbidden
  "rack/" "instrument/" "vins/" "native/" "VoxModuleAbi" "InstrumentAbi"
  "apps/vst3" "PluginProcessor.h" "PluginEditor.h")
# Exceptions: the frozen legacy Drums reference (DRM-00) and the two tests
# that drive it through the legacy C ABI (golden corpus, DRM-01 parity;
# products and libraries must never reference it); and the transitional VOX Drums engine copy (DRM-01, FOUND-00
# K5 option a), whose C interface only DrumEngine.cpp sees. DRM-03 removes
# the second exception.
set(legacy_allowed "/tests/suite/legacy-reference/" "/tests/suite/DrumsLegacyGoldenTests.cpp"
  "/tests/suite/DrumsParityTests.cpp" "/tests/suite/DrumsKitParityTests.cpp"
  "/products/drums/engine/legacy8/" "/products/drums/engine/DrumEngine.cpp")
set(violations "")
foreach(dir IN LISTS dirs)
  file(GLOB_RECURSE files "${ROOT}/${dir}/*.h" "${ROOT}/${dir}/*.hpp" "${ROOT}/${dir}/*.c" "${ROOT}/${dir}/*.cpp")
  foreach(file IN LISTS files)
    set(allowed FALSE)
    foreach(path IN LISTS legacy_allowed)
      string(FIND "${file}" "${path}" at)
      if(NOT at EQUAL -1)
        set(allowed TRUE)
      endif()
    endforeach()
    if(allowed)
      continue()
    endif()
    file(STRINGS "${file}" includes REGEX "^[ \t]*#[ \t]*include")
    foreach(line IN LISTS includes)
      foreach(pattern IN LISTS forbidden)
        string(FIND "${line}" "${pattern}" at)
        if(NOT at EQUAL -1)
          list(APPEND violations "${file}: ${line}")
        endif()
      endforeach()
    endforeach()
  endforeach()
endforeach()
foreach(dir IN ITEMS products libs/vox-preset libs/vox-dsp libs/vox-testkit libs/vox-ui)
  file(GLOB_RECURSE files "${ROOT}/${dir}/*")
  foreach(file IN LISTS files)
    file(STRINGS "${file}" hits REGEX "legacy-reference|vox_legacy_drums_reference")
    if(hits)
      list(APPEND violations "${file}: references the test-only legacy reference")
    endif()
  endforeach()
endforeach()
if(violations)
  list(JOIN violations "\n" text)
  message(FATAL_ERROR "Suite code includes legacy headers:\n${text}")
endif()
message(STATUS "suite boundaries: ok")
