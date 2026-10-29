# vox_add_vins(<target> OUTPUT <file.vins> ID <id> VERSION <x.y.z> NAME <name>
#   VENDOR <vendor> INSTRUMENTS <id>... KIND graph|native
#   [GRAPH <member> <file>] [MODULE_TARGET <shared library target>]
#   [KEY <private pem> MODULUS <modulus file>] [MEMBERS <member>=<file>...])
# Adds an ALL target that builds the package with cmake/MakeVins.cmake.
function(vox_add_vins target)
  cmake_parse_arguments(V "" "OUTPUT;ID;VERSION;NAME;VENDOR;KIND;MODULE_TARGET;KEY;MODULUS"
    "INSTRUMENTS;GRAPH;MEMBERS" ${ARGN})
  set(members ${V_MEMBERS})
  set(depends vox_vins_pack)
  set(extra "")
  if(V_KIND STREQUAL "graph")
    list(GET V_GRAPH 0 graph_member)
    list(GET V_GRAPH 1 graph_file)
    list(APPEND members "${graph_member}=${graph_file}")
    list(APPEND depends "${graph_file}")
    list(APPEND extra "-DGRAPH_MEMBER=${graph_member}")
  else()
    if(NOT VOX_NATIVE_PLATFORM)
      message(FATAL_ERROR "vox_add_vins: no native platform for this build")
    endif()
    set(module_member "module/${VOX_NATIVE_PLATFORM}/$<TARGET_FILE_NAME:${V_MODULE_TARGET}>")
    list(APPEND members "${module_member}=$<TARGET_FILE:${V_MODULE_TARGET}>")
    list(APPEND depends ${V_MODULE_TARGET})
    if(NOT V_KEY)
      set(V_KEY ${VOX_PUBLISHER_KEY})
      set(V_MODULUS ${VOX_PUBLISHER_MODULUS_FILE})
    endif()
    list(APPEND extra "-DMODULE_PLATFORM=${VOX_NATIVE_PLATFORM}" "-DMODULE_MEMBER=${module_member}"
      "-DOPENSSL=${VOX_OPENSSL}" "-DKEY=${V_KEY}" "-DMODULUS_FILE=${V_MODULUS}")
  endif()
  list(JOIN members "|" member_text)
  list(JOIN V_INSTRUMENTS "," instrument_text)
  add_custom_command(OUTPUT ${V_OUTPUT}
    COMMAND ${CMAKE_COMMAND} "-DTOOL=$<TARGET_FILE:vox_vins_pack>" "-DOUTPUT=${V_OUTPUT}"
      "-DID=${V_ID}" "-DVERSION=${V_VERSION}" "-DNAME=${V_NAME}" "-DVENDOR=${V_VENDOR}"
      "-DKIND=${V_KIND}" "-DINSTRUMENTS=${instrument_text}" "-DMEMBERS=${member_text}" ${extra}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/MakeVins.cmake
    DEPENDS ${depends} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/MakeVins.cmake
    VERBATIM)
  add_custom_target(${target} ALL DEPENDS ${V_OUTPUT})
endfunction()
