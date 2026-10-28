# Assembles a development instrument package (cmake -P script):
#   ${PACKAGE_ROOT}/${PACK_ID}.voxinst/${PACK_VERSION}/manifest.json
#   ${PACKAGE_ROOT}/${PACK_ID}.voxinst/${PACK_VERSION}/module/${PACKAGE_ARCH}/<module file>
# Inputs: MODULE_FILE, PACKAGE_ROOT, PACK_ID, PACK_VERSION, PACKAGE_ARCH and
# INSTRUMENT_IDS (a ';' or ',' separated list). The SHA-256 is a corruption
# check only; this script does not sign anything.
cmake_minimum_required(VERSION 3.22)

foreach(required MODULE_FILE PACKAGE_ROOT PACK_ID PACK_VERSION PACKAGE_ARCH INSTRUMENT_IDS)
  if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
    message(FATAL_ERROR "MakeInstrumentPackage: ${required} is required")
  endif()
endforeach()
if(NOT EXISTS "${MODULE_FILE}")
  message(FATAL_ERROR "MakeInstrumentPackage: module not found: ${MODULE_FILE}")
endif()
if(NOT PACK_ID MATCHES "^[a-z0-9._-]+$")
  message(FATAL_ERROR "MakeInstrumentPackage: invalid PACK_ID '${PACK_ID}'")
endif()
if(NOT PACK_VERSION MATCHES "^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)$")
  message(FATAL_ERROR "MakeInstrumentPackage: PACK_VERSION must be MAJOR.MINOR.PATCH")
endif()

set(version_dir "${PACKAGE_ROOT}/${PACK_ID}.voxinst/${PACK_VERSION}")
get_filename_component(module_name "${MODULE_FILE}" NAME)
set(module_dir "${version_dir}/module/${PACKAGE_ARCH}")

# Rebuild this version from scratch so stale files never survive.
file(REMOVE_RECURSE "${version_dir}")
file(MAKE_DIRECTORY "${module_dir}" "${version_dir}/ui" "${version_dir}/content")
file(COPY_FILE "${MODULE_FILE}" "${module_dir}/${module_name}")
file(SHA256 "${module_dir}/${module_name}" module_sha256)
string(TOLOWER "${module_sha256}" module_sha256)

string(REPLACE "," ";" ids "${INSTRUMENT_IDS}")
set(ids_json "")
foreach(id IN LISTS ids)
  if(NOT id MATCHES "^[a-z0-9._-]+$")
    message(FATAL_ERROR "MakeInstrumentPackage: invalid instrument id '${id}'")
  endif()
  if(ids_json)
    string(APPEND ids_json ", ")
  endif()
  string(APPEND ids_json "\"${id}\"")
endforeach()

file(WRITE "${version_dir}/manifest.json" "{
  \"manifestVersion\": 1,
  \"packId\": \"${PACK_ID}\",
  \"packVersion\": \"${PACK_VERSION}\",
  \"abi\": {\"min\": 2, \"max\": 2},
  \"arch\": \"${PACKAGE_ARCH}\",
  \"module\": \"${module_name}\",
  \"sha256\": \"${module_sha256}\",
  \"instrumentIds\": [${ids_json}]
}
")
