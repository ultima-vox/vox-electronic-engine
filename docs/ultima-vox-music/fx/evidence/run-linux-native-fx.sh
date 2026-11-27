#!/usr/bin/env bash
# Focused native execution with the actual pinned JUCE sources. Run from the
# repo root on Debian 12 x86-64. Requires GCC/G++, Git, curl, tar and the system
# runtime sonames below; no X server is needed for the offscreen regression.
# This does not replace suite CMake/CTest, a VST3 wrapper run or actual DPI.
set -euo pipefail
out=${1:-build/fx-native-evidence}
mkdir -p "$out"
out=$(realpath "$out")
deps="$out/deps"
mkdir -p "$deps/obj" "$out/obj"
pin=e18f7f506c0b96f2c738a0bcd7fe6467a5005ad8
xproto=7eb4d684d7dd720021df8a6445ac61dbec886b38
if [[ ! -d "$deps/juce/.git" ]]; then
    git init -q "$deps/juce"
    git -C "$deps/juce" fetch --depth=1 https://github.com/juce-framework/JUCE.git "$pin"
    git -C "$deps/juce" checkout -q --detach FETCH_HEAD
fi
test "$(git -C "$deps/juce" rev-parse HEAD)" = "$pin"
if [[ ! -d "$deps/xsrc/.git" ]]; then
    git clone --depth=1 --filter=blob:none --sparse --no-checkout https://github.com/NetBSD/xsrc.git "$deps/xsrc"
    if [[ "$(git -C "$deps/xsrc" rev-parse HEAD)" != "$xproto" ]]; then
        git -C "$deps/xsrc" fetch --depth=1 origin "$xproto"
    fi
    git -C "$deps/xsrc" sparse-checkout set external/mit/xorgproto/dist/include
    git -C "$deps/xsrc" checkout -q --detach "$xproto"
fi
test "$(git -C "$deps/xsrc" rev-parse HEAD)" = "$xproto"
for spec in 'mirror/libX11 libX11-1.8.4 x11' 'freetype/freetype VER-2-12-1 freetype' 'ScuffleCloud/fontconfig-mirror 2.14.1 fontconfig'; do
    read -r repo tag dst <<<"$spec"
    if [[ ! -e "$deps/$dst/.complete" ]]; then
        mkdir -p "$deps/$dst"
        curl -fsSL "https://codeload.github.com/$repo/tar.gz/refs/tags/$tag" | tar xz --strip-components=1 -C "$deps/$dst"
        touch "$deps/$dst/.complete"
    fi
done
juce="$deps/juce/modules"
flags=(-std=c++20 -g0 -pthread -I"$juce" -I"$deps/x11/include" -I"$deps/xsrc/external/mit/xorgproto/dist/include"
       -I"$deps/freetype/include" -I"$deps/fontconfig"
       -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 -DJUCE_WEB_BROWSER=0 -DJUCE_USE_CURL=0 -DJUCE_PLUGINHOST_VST3=0
       -DJUCE_USE_XRANDR=0 -DJUCE_USE_XINERAMA=0 -DJUCE_USE_XCURSOR=0 -DJUCE_USE_XINPUT=0 -DJUCE_USE_XSHM=0)
for module in juce_core juce_cryptography juce_events juce_data_structures juce_audio_basics juce_graphics juce_gui_basics juce_gui_extra juce_audio_processors_headless juce_audio_processors; do
    for src in "$juce/$module/"*.cpp; do
        n=${src##*/}; dst="$deps/obj/${n%.cpp}.o"
        echo "JUCE $n"; if [[ ! -f "$dst" ]]; then g++ "${flags[@]}" -O1 -c "$src" -o "$dst"; fi
    done
done
for n in Sheenbidi lunasvg; do
    dst="$deps/obj/juce_graphics_$n.o"
    if [[ ! -f "$dst" ]]; then gcc -O2 -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 -I"$juce" -c "$juce/juce_graphics/juce_graphics_$n.c" -o "$dst"; fi
done
prod=('-DJucePlugin_Name="VOX FX"' '-DVOX_SUITE_STATE_TAG="VXFX"' -DVOX_TEST_INTERPOSE_MALLOC=1 -DVOX_FX_LOCK_PROBE=1
      -Iproducts -Iproducts/fx -Ilibs/vox-preset -Ilibs/vox-ui -Ilibs/vox-testkit/probe)
gcc -O2 -c libs/vox-testkit/probe/AllocationProbeLinux.c -o "$out/probe.o"
gcc -O2 -c tests/suite/FxLockProbeLinux.c -o "$out/lock.o"
objects=()
for src in products/fx/{Processor,FxEditor,LayerPanel,CurveEditor}.cpp products/fx/engine/FileCommit.cpp libs/vox-ui/VoxLookAndFeel.cpp libs/vox-ui/components/*.cpp libs/vox-preset/StateContainer.cpp; do
    n=${src##*/}; dst="$out/obj/${n%.cpp}.o"
    echo "VOX $src"; g++ "${flags[@]}" "${prod[@]}" -O2 -c "$src" -o "$dst"; objects+=("$dst")
done
wrap=(-Wl,--wrap=pthread_mutex_lock -Wl,--wrap=pthread_mutex_trylock -Wl,--wrap=pthread_mutex_timedlock
      -Wl,--wrap=pthread_rwlock_rdlock -Wl,--wrap=pthread_rwlock_tryrdlock
      -Wl,--wrap=pthread_rwlock_wrlock -Wl,--wrap=pthread_rwlock_trywrlock
      -Wl,--wrap=pthread_spin_lock
      -Wl,--wrap=pthread_cond_wait -Wl,--wrap=pthread_cond_timedwait
      -Wl,--wrap=sem_wait -Wl,--wrap=sem_timedwait)
libs=(-ldl -lrt -l:libz.so.1 -l:libX11.so.6 -l:libXext.so.6 -l:libfreetype.so.6 -l:libfontconfig.so.1 -l:libpng16.so.16 -l:libjpeg.so.62)
git rev-parse HEAD
# FxAutomationTests also takes products/fx/parameters.csv, the frozen contract.
for n in FxProcessorTests FxRuntimeStressTests FxFileExportTests FxAutomationTests; do
    g++ "${flags[@]}" "${prod[@]}" -O2 -c "tests/suite/$n.cpp" -o "$out/obj/$n.o"
    g++ -pthread "${objects[@]}" "$out/obj/$n.o" "$deps"/obj/*.o "$out/probe.o" "$out/lock.o" "${wrap[@]}" "${libs[@]}" -o "$out/$n"
    if [[ "$n" == FxAutomationTests ]]; then
        (ulimit -s 1024; "$out/$n" products/fx/parameters.csv)
    else
        (ulimit -s 1024; "$out/$n")
    fi
done
