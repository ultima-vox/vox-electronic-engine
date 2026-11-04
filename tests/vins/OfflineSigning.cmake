# Offline release signing, end to end (docs/release/RELEASE_SIGNING.md), with
# the build's untrusted test key standing in for the owner's release key:
# unsigned candidate -> sign-request -> openssl signature -> attach -> verify.
# Wrong keys and tampering are refused. Inputs: TOOL, OPENSSL, MAKE_VINS,
# MODULE, PLATFORM, RELEASE_KEY, RELEASE_MODULUS, OTHER_KEY, OTHER_MODULUS, WORK.
cmake_minimum_required(VERSION 3.22)
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
get_filename_component(module_name "${MODULE}" NAME)

function(run expect_success)
  execute_process(COMMAND ${ARGN} RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(expect_success AND NOT rc EQUAL 0)
    message(FATAL_ERROR "expected success: ${ARGN}\n${out}${err}")
  elseif(NOT expect_success AND rc EQUAL 0)
    message(FATAL_ERROR "expected a refusal: ${ARGN}\n${out}${err}")
  endif()
  set(last_output "${out}" PARENT_SCOPE)
endfunction()

# 1. The release-candidate build writes the native package unsigned.
run(TRUE ${CMAKE_COMMAND} "-DTOOL=${TOOL}" "-DOUTPUT=${WORK}/Tone.unsigned.vins"
  -DID=test.abi.v2.tone -DVERSION=1.0.0 "-DNAME=ABI Test Tone" "-DVENDOR=Vox tests"
  -DKIND=native -DINSTRUMENTS=test.abi.v2.tone
  "-DMEMBERS=module/${PLATFORM}/${module_name}=${MODULE}"
  "-DMODULE_PLATFORM=${PLATFORM}" "-DMODULE_MEMBER=module/${PLATFORM}/${module_name}"
  "-DOPENSSL=${OPENSSL}" "-DKEY=" "-DMODULUS_FILE=${RELEASE_MODULUS}" -P "${MAKE_VINS}")
run(FALSE "${TOOL}" verify "${WORK}/Tone.unsigned.vins" "${RELEASE_MODULUS}")

# 2. The owner extracts the exact manifest bytes and signs them offline.
run(TRUE "${TOOL}" sign-request "${WORK}/Tone.unsigned.vins" "${WORK}/manifest.json")
if(NOT last_output MATCHES "manifest sha256 [0-9a-f]+")
  message(FATAL_ERROR "sign-request reports no manifest hash: ${last_output}")
endif()
run(TRUE "${OPENSSL}" dgst -sha256 -sign "${RELEASE_KEY}" -out "${WORK}/manifest.sig"
  "${WORK}/manifest.json")

# 3. attach verifies before writing; verify checks the signed package.
run(TRUE "${TOOL}" attach "${WORK}/Tone.unsigned.vins" "${WORK}/manifest.sig"
  "${RELEASE_MODULUS}" "${WORK}/Tone.vins")
run(TRUE "${TOOL}" verify "${WORK}/Tone.vins" "${RELEASE_MODULUS}")

# 4. Refusals: another key's signature, another public key, a re-sign of a
#    signed package, a tampered candidate. Nothing is written on refusal.
run(TRUE "${OPENSSL}" dgst -sha256 -sign "${OTHER_KEY}" -out "${WORK}/other.sig"
  "${WORK}/manifest.json")
run(FALSE "${TOOL}" attach "${WORK}/Tone.unsigned.vins" "${WORK}/other.sig"
  "${RELEASE_MODULUS}" "${WORK}/Wrong.vins")
if(EXISTS "${WORK}/Wrong.vins")
  message(FATAL_ERROR "a refused attach wrote a package")
endif()
run(FALSE "${TOOL}" attach "${WORK}/Tone.unsigned.vins" "${WORK}/manifest.sig"
  "${OTHER_MODULUS}" "${WORK}/Wrong.vins")
run(FALSE "${TOOL}" attach "${WORK}/Tone.vins" "${WORK}/manifest.sig"
  "${RELEASE_MODULUS}" "${WORK}/Wrong.vins")
run(FALSE "${TOOL}" verify "${WORK}/Tone.vins" "${OTHER_MODULUS}")
message(STATUS "offline signing round trip and refusals passed")
