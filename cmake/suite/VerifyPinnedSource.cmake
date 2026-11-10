# vox_verify_pinned_source(<name> <dir> <commit> <strict>)
#
# Checks that a dependency source tree is a git checkout at exactly <commit>.
#  - A checkout at another commit always fails.
#  - A source that cannot be verified (git missing, no .git metadata, git
#    error) fails when <strict> is true and only warns otherwise.
# CI and release builds configure with VOX_STRICT_DEPENDENCIES=ON; a developer
# may opt out explicitly with OFF (review 6724525306, P2). Usable at configure
# time and from cmake -P scripts.
function(vox_verify_pinned_source name dir commit strict)
  set(reason "")
  find_package(Git QUIET)
  if(NOT GIT_FOUND)
    set(reason "git was not found")
  elseif(NOT EXISTS "${dir}/.git")
    set(reason "${dir} has no git metadata")
  else()
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${dir}" rev-parse HEAD
      OUTPUT_VARIABLE head OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
      set(reason "git rev-parse failed in ${dir}")
    elseif(NOT head STREQUAL commit)
      message(FATAL_ERROR "${name} at ${dir} is '${head}', expected the pinned commit ${commit}")
    else()
      message(STATUS "${name} commit verified: ${head}")
      return()
    endif()
  endif()
  if(strict)
    message(FATAL_ERROR "${name} source cannot be verified (${reason}); "
                        "VOX_STRICT_DEPENDENCIES=ON refuses unverifiable sources")
  endif()
  message(WARNING "${name} source NOT verified (${reason}). Not acceptable for CI or release; "
                  "configure with -DVOX_STRICT_DEPENDENCIES=ON there.")
endfunction()
