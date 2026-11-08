# cmake -DROOT=<repo> -DCSV=<PROVENANCE.csv> -P CheckProvenance.cmake
# Every file copied from legacy sources must still be byte-identical to the
# reviewed source (SHA-256 recorded at port time). An intended change to a
# ported file updates its row in the same reviewed commit, with before/after
# golden evidence.
file(STRINGS "${CSV}" rows)
list(POP_FRONT rows)   # header
set(errors "")
set(count 0)
foreach(row IN LISTS rows)
  string(REPLACE "," ";" fields "${row}")
  list(LENGTH fields n)
  if(NOT n EQUAL 4)
    list(APPEND errors "malformed row: ${row}")
    continue()
  endif()
  list(GET fields 0 destination)
  list(GET fields 1 source)
  list(GET fields 2 commit)
  list(GET fields 3 expected)
  if(NOT EXISTS "${ROOT}/${destination}")
    list(APPEND errors "${destination}: missing")
    continue()
  endif()
  file(SHA256 "${ROOT}/${destination}" actual)
  if(NOT actual STREQUAL expected)
    # A checkout may have written this text file with CRLF (Windows
    # core.autocrlf on a working tree created before .gitattributes pinned
    # eol=lf: git does not rewrite files whose blob is unchanged). The
    # repository content is LF; every donor is stored without CR (git
    # ls-files --eol: i/lf). Normalising CRLF to LF therefore recovers exactly
    # the repository bytes, and any real edit still changes the hash.
    file(READ "${ROOT}/${destination}" content)
    string(REPLACE "\r\n" "\n" lf "${content}")
    string(SHA256 normalized "${lf}")
    if(normalized STREQUAL expected)
      message(STATUS "${destination}: identical to ${source}@${commit} after CRLF->LF (checkout line endings)")
      set(actual "${expected}")
    endif()
  endif()
  if(NOT actual STREQUAL expected)
    list(APPEND errors "${destination}: SHA-256 ${actual} differs from ${source}@${commit} (${expected})")
  endif()
  math(EXPR count "${count} + 1")
endforeach()
if(errors)
  list(JOIN errors "\n" text)
  message(FATAL_ERROR "${text}")
endif()
message(STATUS "provenance: ${count} files identical to their reviewed sources")
