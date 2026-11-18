#!/usr/bin/env bash
# VOX Drums listening pack (PR 137 section 8), reproducible outside CI.
# Usage: tools/drums/make-listening-pack.sh <build-dir> <new-or-empty-output-folder>
# Builds suite_drums_candidate_tests, runs it with VOX_LISTENING_OUT and leaves
# <output-folder> (69 MIDI, 23 WAV, README.md, SHA256SUMS.txt) and <output-folder>.zip.
set -euo pipefail
build="${1:?build dir}"; out="${2:?output folder}"
src="$(cd "$(dirname "$0")/../.." && pwd)"
cmake -S "$src" -B "$build" -DCMAKE_BUILD_TYPE=Release -DVOX_BUILD_SUITE=ON
cmake --build "$build" --target suite_drums_candidate_tests --parallel
VOX_LISTENING_OUT="$(realpath -m "$out")" ctest --test-dir "$build" -R suite_drums_candidate_tests --output-on-failure
(cd "$out" && sha256sum -c SHA256SUMS.txt >/dev/null) && echo "verified: $out and $out.zip"
