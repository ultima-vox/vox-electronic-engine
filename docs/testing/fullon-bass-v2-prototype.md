# Full-on Bass V2 adapter: experimental test path

The existing adapter wraps `FullOnBassInstance`, 13 independent controls and its
canonical 52-byte v1 state. It is a test and reference path, not a decision to
ship a native Bass DLL. The owner requests a graph-first `.vins` decision after
Bass-specific sound and performance comparison; Rack owns production packaging.

The ABI header is owned by Rack and is not copied into Bass. Once the canonical
header is present at `libs/instrument/abi/VoxModuleAbiV2.h`, the opt-in
`VOX_BUILD_FULLON_BASS_MODULE` target uses it by default. Until the shared
integration lands, CI tests the adapter against the canonical header at Rack
#80 commit `5f3818dea1dd2e1971058010aafb3ff570822ac2` via an isolated test
checkout. This test dependency is not a production installation strategy.

The adapter validates V2 transport and forwards sample rate, tempo, PPQ and
playing state to Bass. Bass currently has no transport-driven note player; the
Rack scheduler remains the sole sequence authority. Discontinuity/loop semantics
and the graph/content handoff need the shared integration contract.

Windows x64 Release CI for recovered commit `32ad958` passed both the VST3 and
adapter jobs: https://github.com/ultima-vox/vox-electronic-engine/actions/runs/40751447890.
The real Rack processor integration test exercises the native module, MIDI,
host gain and state restoration; it is not Cubase or production acceptance.
A previous Linux interposer observed no malloc/calloc/realloc during 1000 warmed
module calls; that is not a complete proof against every allocation or lock.

Outstanding: signed production `.vins` through Rack's package flow, graph versus
native Bass parity and performance, final UI mapping, transport/automation and
content integration, full exact-head CI, owner sound approval and Cubase/REAPER
acceptance. Do not label this adapter production-ready.
