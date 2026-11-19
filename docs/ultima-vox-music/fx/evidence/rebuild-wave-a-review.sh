#!/usr/bin/env bash
# From repository root. NumPy 2.2.6 (Python 3), GCC, ZIP and GNU coreutils.
# Describes the archive structure; ZIP timestamp and compiler/library versions
# can affect archive/WAV hashes. Verify the original download with its SHA-256.
set -euo pipefail
out=${1:-build/wave-a-review}
mkdir -p "$out"
out=$(realpath "$out")
root="$out/wave-a"
if [[ -e "$root" ]]; then echo "Choose a fresh output directory: $root exists" >&2; exit 1; fi
mkdir "$root"
evidence=docs/ultima-vox-music/fx/evidence
cxx=${CXX:-g++}
"$cxx" -std=c++20 -O2 -Iproducts/fx tests/suite/FxFactoryCandidatesTests.cpp -o "$out/catalog"
"$out/catalog" "$root" > "$root/render-audit.txt"
"$cxx" -std=c++20 -O2 -Iproducts/fx -Ilibs/vox-preset "$evidence/export-wave-a-presets.cpp" \
  libs/vox-preset/StateContainer.cpp -o "$out/export-presets"
"$out/export-presets" "$root"
python "$evidence/measure-wave-a-context.py" "$root"
cp "$evidence/wave-a-120-package-readme.txt" "$root/README.txt"
(cd "$root" && find . -type f ! -name SHA256SUMS -print0 | LC_ALL=C sort -z | xargs -0 sha256sum > SHA256SUMS)
zipFile="$out/vox-fx-wave-a-120-review.zip"
(cd "$root" && find . -type f | LC_ALL=C sort | zip -X -6 -q "$zipFile" -@)
unzip -tq "$zipFile"
sha256sum "$zipFile"
