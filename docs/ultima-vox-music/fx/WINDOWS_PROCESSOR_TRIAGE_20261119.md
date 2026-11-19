# VOX FX — Windows P0 triage and Wave B asset rehearsal

**Authority:** [independent TL review, #156](https://github.com/ultima-vox/vox-electronic-engine/pull/156#issuecomment-7003120118). TL integrated `1ae534f6d49e5a24b72d05a3463147697138af85` into #156 and removed the consumed marker. This follow-up stays on `arena/20b960e7-vox-electronic-engine`. Do not merge, mark Ready, reset the marker or launch the broad qualification on the strength of this diagnostic staging.

> **Targeted run update:** [44488704936](https://github.com/ultima-vox/vox-electronic-engine/actions/runs/44488704936) completed. Historical Release/PDB build succeeded, the single-test step failed, and the CDB step was skipped because its expression lacked a failure status function. This does not establish another SEGFAULT or missing CDB. [Job and archive provenance](evidence/windows-44488704936.txt). Preserve the archive and inspect the raw CTest log before any new trigger.

## Confirmed facts, not a root-cause claim

- Run `44479129114`, job `135406798629`: Release x64 build succeeded; **73/74 CTests passed**. Only `suite_fx_processor_tests` (test #28) failed with CTest `SEGFAULT`; validator execution skipped. Physically built synthetic merge `54fe607f105bd689100461ab41d764aee003c4f9`, same tree `5d7064b9c4623aa3fa3a2d1a5e11ef7ffcc3ea01` as then-PR head `e08eb314f6da27f877c0544a836e3bf8aec6207c`. The artifact's `VOX FX.vst3` file SHA-256 is `894b974cc72c7c772e39be01ea0410723d818b9660cf74c931199b85ac1f748a`, verified by TL, not a release binary. [Updated evidence](evidence/windows-44479129114.txt).
- No exception code, symbolized stack or minidump was captured. Do not infer stack overflow, UI initialization, fixture timing or a processor bug. Sandbox has no Windows runtime/runner shell and the artifact contains neither the test executable/PDB nor the dump; diagnostic replay was **not executed on Windows**. Preserve the TL's downloaded artifact before the Actions copy expires on 22 November (UTC).

## What this follow-up makes observable

- For manually admitted FX PR #156 qualification only, the existing suite now retains CTest JUnit and the human log even on failure, and builds optimized Release test and FX shared-code symbols. Other suites' test/validator gates and the opt-in trigger are unchanged.
- Failure of the named FX processor test enables one bounded CDB replay using the existing Release command and working directory as reported by CTest. The helper uses an installed x64 debugger, not a download, service, registry edit or manual access token. It records binary provenance, exception/stack output and, if captured, a minidump with its EXE/PDB in a separate short-lived diagnostic artifact. A replay that does not reproduce is not a PASS. Phase markers in the processor test flush to stderr so CTest retains the last reached section when buffered stdout is lost.
- **Before any broad run:** runner administrator replays only `suite_fx_processor_tests` on the matching historical checkout/binary, collects the exception code, source/fixture frame, stack and dump per [targeted runbook](evidence/windows-exception-runbook.md). If only a symbolized rebuild is available, keep the historical original logs and label the rebuild separately. Only then fix the diagnosed cause and plan the single deliberate integrated qualification.

## Wave B progress without affecting Wave A

`Vellum Coil` (Dark Trance gate sweep) and `Kiln Choir` (Balearic continuous bed) are separate review-only rehearsals with authored modal WAVs, distinct grain trajectories and FMOD3 step/ADSR. The worker test exercises PCM import, portable save/restore of the SHA, a missing source, rejection of a different valid WAV, exact-byte source relink at a different path, and byte-identical offline output after relink. With an output directory argument the test also writes the two source WAVs, two solo WAVs and two `.voxfx` files. References in the export carry only a filename and hash so each recipient chooses the provided WAV using Relink; no temporary machine path is transmitted. Host FX wet/amount remain intentionally outside portable format.

Wave A stays **120 review-only / 0 approved**, with eight separate Wave B candidates staged (three tonal/keyless pairs plus these two asset cues). B=300/C=600 remain open, as do FX-T18, OS DPI/focus, licensed listening approval and final owner-only DAW acceptance. The original 96,509,858-byte ZIP still verifies against [SHA-256](evidence/wave-a-120-archive.sha256); it remains an Arena download, not a durable repo-accessible distribution channel. Transfer the unchanged ZIP to an authorized private share and verify the same SHA after upload; do not put a media blob in Git or publish a release to work around access.

## Executed checks

[Test logs](evidence/windows-triage-linux-daa2b76.txt), test/content revision
`daa2b7621b274283c73029f97e0bd8dfbf225c38`: pinned JUCE native processor and
runtime stress passed (2/2); linked worker plus ASan/UBSan passed (1/1 each).
YAML and new PowerShell syntax checked; a Linux CTest failure fixture verified
that its JSON and text/JUnit outputs match the discovery/preservation logic.
No Windows PowerShell/CDB runtime validation or full Linux CTest was possible.
The follow-up workflow-only refinement copies text logs in a separate step so
an abrupt test-step exit cannot bypass log preservation.

The two-cue Arena ZIP is `artifacts/vox-fx-wave-b-asset-rehearsal.zip` with
[archive SHA-256](evidence/wave-b-asset-rehearsal.sha256) and
[member SHA-256s](evidence/wave-b-asset-rehearsal-members.sha256).

## Follow-up after targeted run 44488704936

[Evidence and next runner action](evidence/windows-44488704936.txt). The
consumed marker is removed; #156 remains Draft on `b15a166`. The opt-in and
full Windows suite job are unchanged. The staging fix makes the debugger step's
status check explicit and preserves native CTest exit and an absolute JUnit
path; it fails instead of reporting success if no test matches. YAML and both
changed PowerShell blocks parsed locally. A Linux CTest fixture produced a
nonzero exit, JUnit and `LastTestsFailed.log` for one intentionally failing
named test, did not execute an unrelated test, and failed on zero matches.
This is not Windows workflow runtime validation or a processor fix.

The **eight** previously staged Wave B candidates now have a technical audit
and an Arena review bundle, `artifacts/vox-fx-wave-b-eight-cue-audit.zip`
(20,701,615 bytes; [archive hash](evidence/wave-b-eight-archive.sha256),
[member hashes](evidence/wave-b-eight-members.sha256)). The
[manifest](evidence/wave-b-eight-manifest.tsv) records original tonal/keyless
4/8/16-bar pairs, two independently authored sample-backed cues, source SHA,
portable section versions, a musical instruction and the solo SHA per entry.
The sample sources regenerated at the same two prior SHA values; no third-party
material is asserted, and no license or listening approval is implied.

At the `b15a166` product tree, the linked authoring tool checked eight portable
round trips, 24 solo renders at 44.1/48/96 kHz and eight PCM-identical repeats at
48 kHz. Peak `0.0012182…0.02314925`, max DC `0.00001786`, min mono/stereo RMS
`0.95644551`, final 5 ms silent for this matrix. These are solo technical
checks, not quality or full-mix acceptance. See [metrics](evidence/wave-b-eight-metrics.tsv)
and [host/audition sidecar](evidence/wave-b-eight-settings.tsv). Wave A stays
120/0, B stays at eight candidates in staging, targets 300/600 remain.

To reproduce with the JUCE pin from the existing
[Linux harness](evidence/run-linux-native-fx.sh) (Linux x86-64 GCC, run from root):

```bash
JUCE_MODULES=build/fx-native/deps/juce/modules
JUCE_OBJECTS=build/fx-native/deps/obj
g++ -std=c++20 -O2 -pthread -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 -DJUCE_USE_CURL=0 \
  -I"$JUCE_MODULES" -Iproducts/fx -Ilibs/vox-preset \
  docs/ultima-vox-music/fx/evidence/export-wave-b-stage.cpp libs/vox-preset/StateContainer.cpp \
  "$JUCE_OBJECTS"/juce_core*.o "$JUCE_OBJECTS"/juce_cryptography.o -ldl -lrt -l:libz.so.1 \
  -o build/export-wave-b-stage
build/export-wave-b-stage build/wave-b-stage
```
