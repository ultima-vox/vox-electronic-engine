# Preserve the actual Windows FX failure before another qualification

Run on the existing x64 suite runner, on the exact failed checkout and binaries;
substitute the actual artifact ID/run and do not copy signed download links.
This is a diagnostic replay, not a second `[RUN_FX_SUITE]` dispatch, a CTest
replacement, or permission to alter the Draft/HOLD PR.

**Current incident: one test, not the entire suite.** The TL independently
identified `suite_fx_processor_tests` (73/74 passed) on the old `e08eb314`
source tree; the actually built commit was `54fe607f` with the same tree.
See [the source/binary evidence](windows-44479129114.txt). Preserve any original
Release EXE and link PDBs from that job before cleaning or rebuilding. On that
checkout, run:

```powershell
ctest --test-dir build-suite -C Release -R '^suite_fx_processor_tests$' --output-on-failure -VV
Get-Content build-suite\Testing\Temporary\LastTest.log
Get-Command cdb.exe,procdump64.exe,procdump.exe -ErrorAction SilentlyContinue
```

The helper `.github/scripts/Capture-FxProcessorException.ps1` discovers this
one Release test via CTest JSON and uses installed x64 CDB if available. Run it
from the matching runner checkout; if using the historical binary, copy only
the helper into that workspace or invoke it by absolute path (do not rebuild
or replace the original EXE first):

```powershell
& <path-to-helper> -BuildDir build-suite -OutputDir suite-diagnostic
# Optional: -CdbPath 'C:\path\to\x64\cdb.exe' -TimeoutSeconds 90
```

On future manually admitted #156 suite runs, CTest JUnit and text logs go in
`VOX-Suite-VST3/ctest/` even when CTest fails. A failed FX processor test triggers
only the bounded debugger replay; if present, dump, debugger log and matching
EXE/PDB go to the separate seven-day `VOX-FX-Processor-Diagnostic` artifact.
Treat it as private engineering evidence. No tool installation or registry
changes are attempted. This machinery is **not** authorization to dispatch
another broad suite before the targeted replay/diagnosis.

1. Preserve the original CTest output, the runner's
   `build-suite/Testing/Temporary/LastTest.log` and `LastTestsFailed.log`, and
   `VOX-Suite-VST3/manifest.txt` from the signed-in Actions UI or the runner.
   Record `git rev-parse HEAD`, run/job ID, runner OS and versions, and SHA-256 of
   the exact failed executable(s) and matching PDBs if available. A check-run
   annotation reporting exit code `1` does **not** identify a failing test or
   an SEH exception. The archive digest is **not** a VST3 binary hash.
2. If the workspace artifacts are from the same SHA, list test commands and
   working directories (not guessed executable paths), and save the list:

   ```powershell
   New-Item -ItemType Directory -Force suite-out\diagnostic | Out-Null
   ctest --test-dir build-suite -C Release -L suite --show-only=json-v1 |
     Set-Content suite-out\diagnostic\CTestCommands.json
   Copy-Item build-suite\Testing\Temporary\LastTest*.log suite-out\diagnostic\
   Get-Content build-suite\Testing\Temporary\LastTestsFailed.log
   ```

3. Replay **the failed test(s)** from the `command` and `WORKING_DIRECTORY`
   specified by CTest. If CTest reports a native exception, capture it with the
   Windows SDK's x64 `cdb.exe`, using matching PDBs. Do not replace a reproducible
   Release failure with a successful debug-only executable. Example after
   selecting `$test` from the JSON, and changing to its CTest working directory:

   ```powershell
   $command = @($test.command)
   $exe = $command[0]
   $argsToTest = @($command | Select-Object -Skip 1)
   # Use absolute paths without spaces for the CDB command payload below.
   $dump = 'C:\vox-diagnostic\failing-test.dmp'
   $log  = 'C:\vox-diagnostic\failing-test-cdb.log'
   New-Item -ItemType Directory -Force (Split-Path $dump) | Out-Null
   $handler = ".lastevent; .exr -1; .ecxr; kP; .dump /m $dump; q"
   $commands = "sxd -c2 `"$handler`" av; sxd -c2 `"$handler`" sov; sxd -c2 `"$handler`" eh; g"
   & $cdb -G -lines -logo $log -c $commands $exe @argsToTest
   ```

   `av`, `sov`, and `eh` cover access violations, stack overflow and C++
   exceptions, respectively, at second chance. If the original failure is a
   timeout or a different exception, take a hung-process dump or configure the
   observed exception code instead; don't infer an exception from a timeout.
   Keep the exception address/code, symbolized stack, the small dump (private
   engineering attachment), and the matching PDB/executable hashes together.
   If this exact binary does not reproduce, say so and retain the original
   log; rebuilding with symbols provides a different binary and must be labeled.
4. Root-cause before relaxing any test or labeling Windows PASS. Requalify
   exactly one integrated head with Release x64 CTest, Steinberg validator and
   the individual `VOX FX.vst3` binary SHA-256. The existing workflow trigger,
   marker and Drums/Synth checks do not need to change for steps 1–3.
