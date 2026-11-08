# cmake -DROOT=<repo> -P CheckBoundaries.cmake
# Suite code must not include legacy Rack / instrument ABI / .vins headers or
# the legacy plugin shell (06_CLAUDE_EXECUTION_BRIEF.md §1, FOUND-00 §3.2).
set(dirs products libs/vox-preset libs/vox-dsp libs/vox-testkit tests/suite)
set(forbidden
  "rack/" "instrument/" "vins/" "native/" "VoxModuleAbi" "InstrumentAbi"
  "apps/vst3" "PluginProcessor.h" "PluginEditor.h")
set(violations "")
foreach(dir IN LISTS dirs)
  file(GLOB_RECURSE files "${ROOT}/${dir}/*.h" "${ROOT}/${dir}/*.hpp" "${ROOT}/${dir}/*.c" "${ROOT}/${dir}/*.cpp")
  foreach(file IN LISTS files)
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
if(violations)
  list(JOIN violations "\n" text)
  message(FATAL_ERROR "Suite code includes legacy headers:\n${text}")
endif()
message(STATUS "suite boundaries: ok")
