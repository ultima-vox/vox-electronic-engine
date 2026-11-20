# VOX FX — Wave B performance tranche and P0 evidence boundary

**Scope:** work on `arena/20b960e7-vox-electronic-engine`, independent of the
PR head `5b438c568d20aad03109b63bae5c6d134f7871b2`. No DSP changes, host
parameter/identity changes, workflow edit, marker change, Ready or merge.

## Windows P0 — reproducible test failure, no verified cause yet

- [TL review](https://github.com/ultima-vox/vox-electronic-engine/pull/156#issuecomment-7005328273)
  independently retrieved the old targeted archive: `suite_fx_processor_tests`
  really SEGFAULTed after 2.60 s on the symbolized historical `e08eb314`
  rebuild, matching the **test identity and CTest failure label** from the
  first 73/74 full run. That does not establish the same exception/fault site.
  [Historical evidence and the matching EXE/PDB hashes](evidence/windows-44488704936.txt).
- New targeted run `44497593788` is complete. Historical build passed, single
  test failed, debugger step **ran** and reported success, provenance/upload
  passed, full suite skipped. A successful debugger step is not a guarantee
  of a dump/symbolized stack. [Verified job/retention and retrieval status](evidence/windows-44497593788.txt).
- [Archive #14516426278](https://github.com/ultima-vox/vox-electronic-engine/actions/runs/44497593788/artifacts/14516426278)
  expires 22 Nov 09:34:02 UTC. Both log and file-download redirects return EOF
  from Arena. [PR request](https://github.com/ultima-vox/vox-electronic-engine/pull/156#issuecomment-7005782904)
  asks TL to preserve it and supply `LastTest.log`, the CDB diagnostic summary
  and exception/stack excerpt (or confirmation of absent output). No speculative
  engine/test fix. Once available, diagnose at the observed source/fixture frame
  and validate the fix before a deliberate integrated full Windows suite and
  Steinberg pass. FX-T17/T18 and owner-only DAW gate remain open.

## Wave B — 16 staged, not admitted to factory

Eight performance cues add two contrasting choices per previously unrepresented
role: impact (break accent versus kick-safe top accent), zap (held Goa chirp
versus reverse granular/laser answer), rhythm (two-bar triplets versus four-bar
ghost accents and ratchets), stinger (one-bar harmonic pickup versus punctuation
on a silent beat). Each has an authored arrangement/gesture in the manifest;
no seed permutations. The prior three tonal/keyless pairs and two asset-backed
cues are preserved. Wave A remains **120 review-only, zero approved**. Stage B
and C goals remain **300/600**, not a claim of completion or release scope change.

[Manifest](evidence/wave-b-sixteen-manifest.tsv) · [host settings](evidence/wave-b-sixteen-settings.tsv) ·
[multi-rate metrics](evidence/wave-b-sixteen-metrics.tsv) · [members SHA-256](evidence/wave-b-sixteen-members.sha256).
Arena bundle `artifacts/vox-fx-wave-b-sixteen-cue-review.zip`, **23,270,729 bytes**,
[archive hash](evidence/wave-b-sixteen-archive.sha256). Contains 16 portable
presets, 16 PCM24 stereo reference WAVs, the two original modal source WAVs
(with hashes), settings, metrics and review guidance. It is not a durable
external distribution link. No Wave A preset/preview files changed.

## Executed checks for the tranche

- Compiled `export-wave-b-stage.cpp` with GCC C++20 `-O2`, pinned JUCE
  `e18f7f506c0b96f2c738a0bcd7fe6467a5005ad8` core/cryptography and StateContainer;
  ran at 1 MiB stack. [Build command](WINDOWS_PROCESSOR_TRIAGE_20261119.md#follow-up-after-targeted-run-44488704936)
  is unchanged; use a new output directory for the expanded tool.
- **48** 44.1/48/96 kHz renders and **16** byte-matched 48 kHz repeats using
  the decoded portable Program/Pattern/Motion and restored receiving-host values.
  Sequence FPAT payloads matched and kept 2/4 bars respectively. First eight
  manifest rows (including solo hashes) and both original asset hashes matched
  the previous review. No new WAV sources were required by the second tranche.
- Peak range `0.00121820…0.02314925`, max DC `0.00013003`, min mono/stereo RMS
  `0.93671943`, max final 5 ms peak `0.00007761`. These are solo checks (including
  tails), not musical loudness/real-mix or listening and licensing approval.
  Archive integrity and all file checksums verified. No native or full-suite
  rerun on this staging-only tranche; old native results remain at their SHAs.
