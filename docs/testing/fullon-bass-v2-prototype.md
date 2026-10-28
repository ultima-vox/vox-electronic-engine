# Full-on Bass ABI V2 prototype — 28 October 2026

Status: experimental, **not production and not a Windows package**. The module
wraps the existing `FullOnBassInstance` and 13 named plain controls, with explicit
pool indices 0–12. The shared header was read from PR #67 commit `e46129b`; it
is not vendored or modified. The ABI and loader remain provisional. Sound state
is the existing canonical 52-byte version 1 format.

Build option: `VOX_BUILD_FULLON_BASS_MODULE=ON`; set `VOX_MODULE_ABI_V2_ROOT`
to the `libs` directory in a checkout of the Rack-owned ABI header. This target
has not been configured or compiled on Windows in this environment.

Local experiment: Linux x86-64, GCC 13.3, JUCE 9.0.1, glibc 2.39. The shared
library and an independent `dlopen` harness were compiled and run. The harness
checks identity and 13 unique indices, all 13 parameter state isolation, invalid
parameter rejection, two-instance isolation, canonical state round trip,
identical initial audio, actual audible stereo, sample-offset parameter edit,
reset and panic. A Linux interposer from #67 observed no malloc/calloc/realloc
in 1000 warmed process calls with MIDI NoteOn and a parameter change. It does
not prove absence of every allocation method or blocking lock. `nm -D` shows
only `voxModuleEntryV2` exported. This is not the actual Rack adapter test.

Not done: signed Windows x64 DLL, approved package format, UI and content,
Rack loading/removal/reinstall, Windows CMake/full CTest, exact Bass rerender
root cause, comprehensive automation/MIDI/saturation conformance, sound-quality
and DAW/owner acceptance. Retain draft status. No production discovery or trust
policy is implemented here.

## Windows CI preparation

The repository workflow now includes an additional `windows-fullon-bass-v2`
job targeting `[self-hosted, windows]`. It checks out the shared proposed ABI
from PR #67 at `e46129be249bb4e07161b898ba2e659ea6bb075b`, configures the
opt-in Bass DLL target, builds and runs the dynamic ABI smoke test, and uploads
the DLL as an **experimental** artifact. It does not alter the main VST3 job
or publish an installer. Workflow YAML and runner labels were parsed locally;
Windows compilation, actual runner execution and artifact creation remain
unverified until the branch is published and GitHub Actions runs it.
