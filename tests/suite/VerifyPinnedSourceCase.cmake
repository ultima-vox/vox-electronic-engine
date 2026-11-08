# cmake -DROOT=<repo> -DCASE=<case> -DWORK=<dir> -DJUCE_DIR=<dir> -DCOMMIT=<sha> -P VerifyPinnedSourceCase.cmake
# Drives vox_verify_pinned_source() against prepared fixtures. The CTest
# entries mark the cases that must fail with WILL_FAIL.
include(${ROOT}/cmake/suite/VerifyPinnedSource.cmake)
find_package(Git REQUIRED)

set(fixture "${WORK}/${CASE}")
file(REMOVE_RECURSE "${fixture}")
file(MAKE_DIRECTORY "${fixture}")

if(CASE MATCHES "wrong-commit")
  # A real git checkout at a different commit.
  execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${fixture}" init -q)
  execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${fixture}" -c user.name=vox -c user.email=vox@invalid
                          commit -q --allow-empty -m fixture)
  set(dir "${fixture}")
elseif(CASE MATCHES "no-metadata")
  # Sources without .git (e.g. an unpacked archive).
  file(WRITE "${fixture}/CMakeLists.txt" "# not a checkout\n")
  set(dir "${fixture}")
else()
  set(dir "${JUCE_DIR}")
endif()

if(CASE MATCHES "^strict")
  set(strict ON)
else()
  set(strict OFF)
endif()
vox_verify_pinned_source(JUCE "${dir}" "${COMMIT}" ${strict})
message(STATUS "case ${CASE}: accepted")
