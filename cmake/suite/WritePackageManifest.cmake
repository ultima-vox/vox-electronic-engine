# cmake -D... -P WritePackageManifest.cmake
# Writes the manifest installed next to the three VST3 bundles. Hashes are of
# the built bundle files. The installer exe hash is recorded by CI after NSIS
# links, because this file is an input to that installer.

if(NOT DEFINED CONFIG OR NOT DEFINED BINDIR OR NOT DEFINED OUT
    OR NOT DEFINED GIT_SHA OR NOT DEFINED JUCE OR NOT DEFINED VERSION)
  message(FATAL_ERROR "WritePackageManifest missing CONFIG, BINDIR, OUT, GIT_SHA, JUCE or VERSION")
endif()

set(keys syn drm fx)
set(lines
  "package: ULTIMA VOX MUSIC"
  "version: ${VERSION}"
  "file_name: ULTIMA-VOX-MUSIC-${VERSION}-win64"
  "products: VOX Drums, VOX FX, VOX Trance Synthesis"
  "install_root: \$PROGRAMFILES64/Common Files/VST3/Ultima Vox"
  "git_sha: ${GIT_SHA}"
  "juce: ${JUCE}"
  "nsis_makensis: ${NSIS}"
  "authenticode: unsigned; CI records Get-AuthenticodeSignature after the exe is linked"
  "human_approved: 0"
  "release_accepted: no"
  "legacy_rack_package: no"
  "identities:")

foreach(key IN LISTS keys)
  if(NOT DEFINED VOX_PKG_${key})
    message(FATAL_ERROR "missing VOX_PKG_${key}")
  endif()
  string(REPLACE "|" ";" fields "${VOX_PKG_${key}}")
  list(LENGTH fields nfields)
  if(NOT nfields EQUAL 4)
    message(FATAL_ERROR "VOX_PKG_${key} must be name|bundle|cid|directory")
  endif()
  list(GET fields 0 name)
  list(GET fields 1 bundle_id)
  list(GET fields 2 cid)
  list(GET fields 3 directory)
  if(NOT EXISTS "${directory}")
    message(FATAL_ERROR "bundle directory missing: ${directory}")
  endif()
  list(APPEND lines "  ${name} | ${bundle_id} | ${cid}")
  file(GLOB_RECURSE files LIST_DIRECTORIES false "${directory}/*")
  if(NOT files)
    message(FATAL_ERROR "bundle directory is empty: ${directory}")
  endif()
  list(SORT files)
  foreach(path IN LISTS files)
    file(SHA256 "${path}" hash)
    file(RELATIVE_PATH relative "${directory}" "${path}")
    string(REPLACE "\\" "/" relative "${relative}")
    list(APPEND file_lines "${hash}  ${name}.vst3/${relative}")
  endforeach()
endforeach()

list(APPEND lines "files:")
list(APPEND lines ${file_lines})
list(JOIN lines "\n" text)
file(WRITE "${OUT}" "${text}\n")
message(STATUS "Wrote ${OUT}")
