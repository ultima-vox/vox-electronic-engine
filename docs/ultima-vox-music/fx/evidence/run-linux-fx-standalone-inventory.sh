#!/usr/bin/env bash
# Remaining FX CTest source inventory, built directly on Linux (not CTest or
# a VST3 build). Run from repo root after run-linux-native-fx.sh fetched JUCE.
# The separately linked native script executes Processor/RuntimeStress/FileExport.
set -euo pipefail
native=${1:-build/fx-converge/native}
out=${2:-build/fx-converge/inventory}
mkdir -p "$out"
deps="$native/deps"
test "$(git -C "$deps/juce" rev-parse HEAD)" = e18f7f506c0b96f2c738a0bcd7fe6467a5005ad8
unset VOX_GOLDEN_OUT
export LC_ALL=C
python3 - <<'PY'
from pathlib import Path
import re
cmake = Path('tests/suite/CMakeLists.txt').read_text()
named = re.findall(r'^add_executable\(suite_fx_\w+ (Fx\w+\.cpp)\b', cmake, re.M)
for match in re.finditer(r'^foreach\(test (Fx\w+(?: Fx\w+)*)\)', cmake, re.M):
    named.extend(x+'.cpp' for x in match[1].split())
actual = {p.name for p in Path('tests/suite').glob('Fx*.cpp')}
if len(named) != len(actual) or set(named) != actual:
    raise SystemExit(f'FX CMake/source inventory discrepancy: {set(named)^actual}')
print(f'{len(named)} FX executables: native harness owns 3; this inventory runs {len(named)-3}')
PY
git rev-parse HEAD
g++ --version | sed -n '1p'
gcc -O2 -c libs/vox-testkit/probe/AllocationProbeLinux.c -o "$out/probe.o"
flags=(-std=c++20 -O2 -pthread -Iproducts/fx -Ilibs/vox-preset -Ilibs/vox-testkit/probe
       -I"$deps/juce/modules" -DVOX_TEST_INTERPOSE_MALLOC=1 -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
       -DJUCE_USE_CURL=0 -DJUCE_WEB_BROWSER=0)
g++ "${flags[@]}" -c libs/vox-preset/StateContainer.cpp -o "$out/state.o"
g++ "${flags[@]}" -c products/fx/engine/FileCommit.cpp -o "$out/filecommit.o"
passed=0; failed=0
for source in tests/suite/Fx*.cpp; do
    name=${source##*/}; name=${name%.cpp}
    args=()
    case "$name" in
        FxProcessorTests|FxRuntimeStressTests|FxFileExportTests) continue ;;
        FxPresetLibraryTests) args+=("$out/filecommit.o") ;;&
        FxPresetLibraryTests|FxSampleDecodeTests|FxSampleControllerTests)
            args+=("$deps/obj"/juce_core*.o "$deps/obj/juce_cryptography.o" -ldl -lrt -l:libz.so.1) ;;
    esac
    echo "=== $name ==="
    if g++ "${flags[@]}" "$source" "$out/state.o" "$out/probe.o" "${args[@]}" -o "$out/$name" &&
       (ulimit -s 1024; timeout 180 "$out/$name"); then
        echo "PASS $name"; ((++passed))
    else
        echo "FAIL $name"; ((++failed))
    fi
done
echo "Optimized standalone FX inventory: $passed passed, $failed failed"
((failed==0))
