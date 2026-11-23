# Windows x64 package for the three ULTIMA VOX MUSIC plugins.
#
# This is not the legacy Rack installer (that one lives in the VOX_BUILD_LEGACY
# block and is named "Vox Electronic Engine"). A combined suite+legacy configure
# refuses to emit this package so the two artifacts cannot be mixed.
#
# Default install location, when the user does not pass NSIS /D=:
#   $PROGRAMFILES64/Common Files/VST3/Ultima Vox/
# Hosts scan that VST3 directory recursively. Bundle IDs and CIDs are the
# frozen values in SuiteIdentity.cmake; this file does not change them.
#
# The installer is unsigned. Nothing here sets factoryProgramsHumanApproved.

option(VOX_SUITE_PACKAGE "Package VOX Drums, VOX FX and VOX Trance Synthesis (Windows NSIS + ZIP)" OFF)
if(NOT VOX_SUITE_PACKAGE)
  return()
endif()

if(NOT WIN32)
  message(FATAL_ERROR
    "VOX_SUITE_PACKAGE is the Windows x64 suite installer. "
    "This host is ${CMAKE_SYSTEM_NAME}.")
endif()
if(VOX_BUILD_LEGACY)
  message(FATAL_ERROR
    "VOX_SUITE_PACKAGE cannot be combined with VOX_BUILD_LEGACY. "
    "Refusing to configure so a Vox Electronic Engine package is not produced.")
endif()

find_program(VOX_MAKENSIS makensis)
if(NOT VOX_MAKENSIS)
  message(FATAL_ERROR
    "VOX_SUITE_PACKAGE requires makensis on PATH (CI pins NSIS 3.13). "
    "A ZIP-only result is not the suite installer.")
endif()

if(NOT VOX_PACKAGE_GIT_SHA)
  execute_process(
    COMMAND git rev-parse HEAD
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    OUTPUT_VARIABLE VOX_PACKAGE_GIT_SHA
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _vox_git_rc)
  if(NOT _vox_git_rc EQUAL 0 OR VOX_PACKAGE_GIT_SHA STREQUAL "")
    message(FATAL_ERROR
      "VOX_SUITE_PACKAGE requires a git SHA. Pass -DVOX_PACKAGE_GIT_SHA= or configure from a git checkout.")
  endif()
endif()

set(VOX_SUITE_PACKAGE_FILE_NAME "ULTIMA-VOX-MUSIC-${VOX_SUITE_VERSION}-win64")
set(_vox_manifest "${CMAKE_BINARY_DIR}/suite-package-manifest.txt")

set(_vox_manifest_cmd
  ${CMAKE_COMMAND}
  -DCONFIG=$<CONFIG>
  -DBINDIR=${CMAKE_BINARY_DIR}
  "-DGIT_SHA=${VOX_PACKAGE_GIT_SHA}"
  "-DJUCE=${VOX_JUCE_COMMIT}"
  "-DVERSION=${VOX_SUITE_VERSION}"
  "-DNSIS=${VOX_MAKENSIS}")
foreach(key IN LISTS VOX_SUITE_PRODUCTS)
  list(APPEND _vox_manifest_cmd
    "-DVOX_PKG_${key}=${VOX_SUITE_${key}_NAME}|${VOX_SUITE_${key}_BUNDLE_ID}|${VOX_SUITE_${key}_CID}|${CMAKE_BINARY_DIR}/products/${VOX_SUITE_${key}_TARGET}_artefacts/$<CONFIG>/VST3/${VOX_SUITE_${key}_NAME}.vst3")
endforeach()
list(APPEND _vox_manifest_cmd -DOUT=${_vox_manifest} -P "${CMAKE_CURRENT_LIST_DIR}/WritePackageManifest.cmake")

add_custom_target(vox_suite_package_manifest ALL
  COMMAND ${_vox_manifest_cmd}
  DEPENDS ${VOX_SUITE_syn_TARGET} ${VOX_SUITE_drm_TARGET} ${VOX_SUITE_fx_TARGET}
  VERBATIM
  COMMENT "Writing ULTIMA VOX MUSIC suite package manifest")
add_dependencies(vox_suite_package_manifest
  ${VOX_SUITE_syn_TARGET} ${VOX_SUITE_drm_TARGET} ${VOX_SUITE_fx_TARGET})

foreach(key IN LISTS VOX_SUITE_PRODUCTS)
  install(DIRECTORY
    "${CMAKE_BINARY_DIR}/products/${VOX_SUITE_${key}_TARGET}_artefacts/$<CONFIG>/VST3/${VOX_SUITE_${key}_NAME}.vst3"
    DESTINATION "."
    COMPONENT VoxSuite
    PATTERN "*.pdb" EXCLUDE
    PATTERN "*.ilk" EXCLUDE)
endforeach()
install(FILES "${_vox_manifest}"
  DESTINATION "."
  RENAME "SUITE-MANIFEST.txt"
  COMPONENT VoxSuite)

set(CPACK_PACKAGE_NAME "ULTIMA VOX MUSIC")
set(CPACK_PACKAGE_VENDOR "Ultima Vox")
set(CPACK_PACKAGE_VERSION "${VOX_SUITE_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
  "VOX Drums, VOX FX and VOX Trance Synthesis")
set(CPACK_PACKAGE_FILE_NAME "${VOX_SUITE_PACKAGE_FILE_NAME}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Ultima Vox")
set(CPACK_PACKAGE_INSTALL_REGISTRY_KEY "ULTIMA VOX MUSIC")
set(CPACK_INSTALL_CMAKE_PROJECTS
  "${CMAKE_BINARY_DIR};${PROJECT_NAME};VoxSuite;/")
set(CPACK_GENERATOR "NSIS;ZIP")
set(CPACK_NSIS_DISPLAY_NAME "ULTIMA VOX MUSIC")
set(CPACK_NSIS_PACKAGE_NAME "ULTIMA VOX MUSIC")
set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
set(CPACK_NSIS_MODIFY_PATH OFF)
set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES64/Common Files/VST3")
set(CPACK_PACKAGE_CHECKSUM SHA256)
set(CPACK_NSIS_HELP_LINK "https://github.com/ultima-vox/vox-electronic-engine")
set(CPACK_NSIS_URL_INFO_ABOUT "https://github.com/ultima-vox/vox-electronic-engine")
include(CPack)
