# Builds one .vins package (cmake -P). See docs/architecture/VINS_PACKAGES.md.
# Inputs: TOOL (vox_vins_pack), OUTPUT, ID, VERSION, NAME, VENDOR, KIND
# (graph|native), INSTRUMENTS (comma separated), MEMBERS ("|" separated
# <member>=<file> pairs). graph: GRAPH_MEMBER. native: MODULE_PLATFORM,
# MODULE_MEMBER, OPENSSL, KEY (private PEM, read only by openssl) and
# MODULUS_FILE (its public modulus, for the publisher key id).
cmake_minimum_required(VERSION 3.22)
foreach(required TOOL OUTPUT ID VERSION NAME VENDOR KIND INSTRUMENTS MEMBERS)
  if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
    message(FATAL_ERROR "MakeVins: ${required} is required")
  endif()
endforeach()

get_filename_component(out_dir "${OUTPUT}" DIRECTORY)
set(staging "${OUTPUT}.staging")
file(REMOVE_RECURSE "${staging}")
file(MAKE_DIRECTORY "${staging}" "${out_dir}")

string(REPLACE "|" ";" member_list "${MEMBERS}")
set(member_args "")
foreach(pair IN LISTS member_list)
  list(APPEND member_args "member=${pair}")
endforeach()
string(REPLACE "," ";" instrument_list "${INSTRUMENTS}")
set(instrument_args "")
foreach(id IN LISTS instrument_list)
  list(APPEND instrument_args "instrument=${id}")
endforeach()

set(kind_args "")
if(KIND STREQUAL "graph")
  list(APPEND kind_args "graph=${GRAPH_MEMBER}")
elseif(KIND STREQUAL "native")
  execute_process(COMMAND "${TOOL}" keyid "${MODULUS_FILE}"
    OUTPUT_VARIABLE keyid_line RESULT_VARIABLE rc OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "MakeVins: cannot compute the publisher key id")
  endif()
  string(REGEX MATCH "^[0-9a-f]+" keyid "${keyid_line}")
  list(APPEND kind_args "publisher=${keyid}" "module=${MODULE_PLATFORM}=${MODULE_MEMBER}")
else()
  message(FATAL_ERROR "MakeVins: KIND must be graph or native")
endif()

execute_process(COMMAND "${TOOL}" manifest "${staging}/manifest.json"
    "id=${ID}" "version=${VERSION}" "name=${NAME}" "vendor=${VENDOR}" "kind=${KIND}"
    ${instrument_args} ${kind_args} ${member_args}
  RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "MakeVins: writing the manifest failed")
endif()
set(sig_args "")
if(KIND STREQUAL "native")
  execute_process(COMMAND "${OPENSSL}" dgst -sha256 -sign "${KEY}"
      -out "${staging}/manifest.sig" "${staging}/manifest.json"
    RESULT_VARIABLE rc OUTPUT_QUIET ERROR_QUIET)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "MakeVins: signing the manifest failed")
  endif()
  set(sig_args "sig=${staging}/manifest.sig")
endif()
execute_process(COMMAND "${TOOL}" zip "${OUTPUT}" "${staging}/manifest.json" ${sig_args}
    ${member_args}
  RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "MakeVins: writing the archive failed")
endif()
file(REMOVE_RECURSE "${staging}")
