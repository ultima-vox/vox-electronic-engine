# VOX FX WAV/MIDI Save — create-only (owner decision implemented)

The [owner decision](https://github.com/ultima-vox/vox-electronic-engine/pull/156#issuecomment-7033084795) superseded the earlier open overwrite proposal. Spec source `700f5fc2762d76710d0c74b82c6c814111b59566`, §§12/20.3. This is a separate contract from `.voxfx` User preset updates and prepared native drag. **Draft / HOLD / NO MERGE**. No Windows opt-in or dispatch.

## What the user sees

Save WAV and Save MIDI ask for a **new** name. A chosen existing path or a late competing file produces **“A file or folder already exists at this path. Choose another name or location.”** No numbered suffix, automatic retry, replacement, or hidden second render. Cancel/reopen, immutable capture, progress and one-result semantics are unchanged. An explicit new Save can use a different filename. The native chooser is not asked to confirm replacement; platform wording does not control publication.

## Publication and threat boundary

- `Destination::observe` classifies, without opening the contents, before WAV render or MIDI encode. A job that saw an occupied name remains rejected if the incumbent goes away later. A missing parent or blocked state returns failure, not a fallback. Dangling leaf links and directories are occupied, not treated as writable targets.
- `FileExport::writeBytesTo` exclusively creates a staging directory beside the requested target (`0700` on POSIX and inherited ACL on Windows). It checks the final payload length after the stream closes, then claims publication against cancellation. The **last** namespace operation is POSIX `link(2)` or Win32 `MoveFileExW` with flags `0` (no replace, no copy). There is no WAV/SMF code path to `rename` replacement even when a second writer wins after observation. The private stage is removed on normal completion, conflict or cancellation. A filesystem that cannot perform the no-replace operation fails closed; crash durability, hostile parent rebinding, uncontrolled ACLs/concurrent stage tampering, network and cloud behavior are outside the guarantee. Windows alternate-stream paths are rejected at admission.
- `.voxfx` editing keeps the old behavior under a distinct, extension-checked preset publisher. It is not callable by the audio/MIDI export worker. Prepared drag publishes to its own scratch path and records ownership only on successful publication. Existing retry/lifetime rules retain already-offered payloads; no new eviction or claim of host import success.

## Controlled Windows checklist (later, explicitly authorized)

Run `suite_fx_file_export_tests` as part of exact-head CTest. The Windows branch adds exclusive native handles (including on a competitor that arrives after staging starts), an ADS attempt against an incumbent, and an actual dangling reparse leaf. The fixture checks old sizes/hashes and cleanup; the reparse check logs **unexercised** if creating links is unavailable. Repeat on a prepared local NTFS fixture with symlink permissions rather than qualifying a skip. Unicode and spaced filenames, readonly, occupied directories, same/different incumbents and late publication are covered in the normal suite. Test native chooser selecting an occupied path then explicitly choosing a different filename; check the error, keyboard/focus and no auto-rename. Exercise Win32 OLE and editable SMF/WAV import, Drop Copy vs reference, bounce, editor close and unload in Cubase/REAPER. Verify `.voxfx` editing separately and confirm after unload that the DAW can still read an offered drag file. Do not use real project folders or evict offered files.

Optional fixture executable commands, **only on the authorized Windows test machine** after provisioning test-only local roots. Use ASCII parent paths (normal automated fixtures cover Unicode) and an exact final component `vox-fx-failure-fixture`:

```powershell
& $FileExportTestExe --win-failure-fixture denied 'C:\Disposable FX Tests\vox-fx-failure-fixture'
& $FileExportTestExe --win-failure-fixture low-space 'X:\vox-fx-failure-fixture'
```

- **ACL:** give a non-elevated test identity directory listing/read access but deny creating subdirectories; retain recovery access for the fixture owner. The executable first requires an independent `CreateDirectoryW` to fail with `ERROR_ACCESS_DENIED`, then requires both WAV and SMF to fail without changing the directory listing. Also test an unreadable preexisting leaf; have the fixture owner record its size and SHA-256 before and after the refusal.
- **Low space:** use a disposable volume or quota, never the runner/build/project volume. Size availability below the generated WAV and SMF payloads (the executable reports unexercised modes, with nonzero exit, if too much space remains). This checks safe publication failure and scratch cleanup, not the specific failed write API. Record separately whether staging creation failed or whether the filesystem allowed staging and then failed the write; collect the latter separately if the volume did not produce it. Also exercise cross-volume/no-replace support on an independently controlled fixture. Do not create or fill a volume from this executable.

## Linux evidence

Original create-only tested source **`7a986221ec386bd3571a061d13d9d4415396c9a2`**, based on owner policy `700f5fc2762d76710d0c74b82c6c814111b59566`. The follow-up evidence commit is documentation/logs only. Debian x86-64, GCC 12.2, JUCE `e18f7f506c0b96f2c738a0bcd7fe6467a5005ad8`.

| Check | Result |
|---|---|
| [Offscreen processor/editor](evidence/create-only-native-20261119.txt) | Exit 0. WAV cancellation during active render and editor recreation/unload, MIDI cancellation during encode, injected collisions and distinct-name retry for WAV/SMF, prepared drag replay/cleanup, parameter IDs and state. All **seven** online/offline WAV fixtures had zero sample difference. |
| [Native WAV and SMF publication](evidence/create-only-native-20261119.txt) | Exit 0. New WAV: 21,650 bytes, SHA-256 `263ee0ae0e7826eaef86e20a8f35da1b7820c5df03d6b8a7455dfe7ece92b526`. New SMF: 51 bytes, SHA-256 `f43d09a540cb14cef8ee5029b1d575ec7e7574580c05d476221b13f8464eb84b`. Existing equal/different, late competitor, removed incumbent, denied parent and dangling link, readonly/directory, cancel before publication, cross-device failure and stage cleanup checked. |
| [Optimized and ASan/UBSan focused tests](evidence/create-only-sanitizers-20261119.txt) | All four runs exited 0 (file export and preset library in each build; linked JUCE objects uninstrumented). Existing `.voxfx` update passes; no new IDs or state/serialization format changes. |
| [Routed callback diagnostic](evidence/create-only-native-20261119.txt) | 8,640 paired callbacks, 0 wall/CPU misses in this run, zero scoped allocations and wrapped locks. Not loaded or exact-head Windows/DAW proof; historical misses remain, **FX-T18 open**. |

Reproduce the native run with `VOX_FX_TIMING_THREAD_CPU=1 taskset -c 1 bash docs/ultima-vox-music/fx/evidence/run-linux-native-fx.sh build/fx-converge/native`. This checks Linux filesystem behavior and injected chooser decisions, not Win32 chooser wording, actual host import or low-space Win32 cases. The test compares the competing file's length and SHA after releasing a write barrier and waiting for the actual native publication call to reject the destination. Source inspection confirms that there is no second admission check substituting for that call.

The [TL follow-up run](evidence/tl-followup-native-20261119.txt) repeated Linux native processor/export and all seven parity cases at `6d8513e1308a61097df4cd7deca0a22f845952e0`, with no failures. This does not exercise the new Windows-only lock, ADS, reparse, ACL or low-space branches.
