# DRM-00 — reference corpus of the legacy Drums engine

This corpus fixes the sound of the legacy 8-pad Drums module **before** any port, so that DRM-02 can show the ported engine sounds the same.

## Source (owner decision D3)

- **Donor:** `integration/rack-1.0` @ `f9c8bc642567ba0edc6b638b64c72f9dc6ccf2e1`, file `libs/drums/VoxFullOnDrums.c`, and the C ABI header `libs/instrument/abi/VoxModuleAbiV2.h`. The archive tag is `archive/rack-legacy-2026-11-08`.
- **Test-only copies.** Both files are copied byte-for-byte into `tests/suite/legacy-reference/drums-f9c8bc6/`. `PROVENANCE.csv` records them and `suite_provenance` checks them.
  - No product or library may link or name them; `suite_boundaries` enforces this.
  - The ABI is not a suite interface.
- **Not the #109 reference.** `tests/drums/reference/VoxFullOnDrumsV1.c` on #109 is frozen at `15456c6`, not at `f9c8bc6`.
  - Its SHA-256 is `23b135bc…`; the `f9c8bc6` file is `a7df1417…`.
  - It already contains the unreviewed optimisation `f7ed686`.
  - The suite therefore does not use it as its golden reference.

## Corpus (`suite_drums_legacy_golden_tests`)

The test drives the module through its ABI, the way the Rack host did. It uses the deterministic scenarios of the #109 render-equivalence harness, adapted from `be102f8:tests/drums/DrumsRenderEquivalenceTests.c`.

| Dimension | Values |
|---|---|
| Configuration | defaults, tweaked (tune, character, pan, all filter types, transients, sends, pad outputs, kick), samples (Sample/Layer with loop, reverse, region, normalize), dense (retrigger, stealing, chokes), internal (sequencer in Both mode), automation (mid-block parameter changes) |
| Sample rate | 44.1 / 48 / 96 kHz |
| Block | 64 / 128 / 512 |
| Outputs | stereo main + 9 stereo aux buses; a release-all in the middle of each render, so tails and restarts are covered |

That gives 54 scenarios and 540 bus rows in `tests/suite/golden/drums-legacy-f9c8bc6-v1.csv`. Each row holds peak, RMS, DC and spectral centroid per channel. The tolerances are those of `vox_testkit::compare`.

**Checks:**
- All output is finite.
- The main bus and the aux buses carry signal.
- Every render is **bit-identical when repeated**.
- Every render matches the manifest.

**Block-size invariance** (measured, then pinned in the test):
- With identical hits, renders in blocks of 64 and 512 are **bit-identical on every bus** for defaults, tweaked, samples, dense and internal, up to the release-all point.
- That flag acts at the end of the block that contains it, so where it lands depends on the block size by ABI design.
- Automation is applied per block and is excluded from this check.
- The corpus itself is still per block size, because the #109 harness seeds its hit generator with the block size.

**Listening:** `VOX_GOLDEN_OUT=<dir>` writes the main bus of each configuration at 48 kHz / 128 as WAV. The `VOX-Suite-VST3` CI artifact contains these files.

## Legacy parameter table

`docs/ultima-vox-music/drums/legacy-parameters-f9c8bc6.csv` lists all 187 parameters of the module: id, name, unit, group, type, choices, range, default, step, pool index and flags. The test regenerates the table and fails if the committed copy no longer matches.

DRM-01 maps these meanings to the new `drm.*` IDs (FOUND-00 §5.3). That map is used only for parity tests. **There is no import of Rack projects.**

## How DRM-02 uses the corpus

The ported engine renders the same 54 scenarios. They must match this manifest within tolerance, with bit-identical repeats and bit-identical block invariance. Any intended sound change needs a new, owner-approved manifest version (`-v2`) with before/after renders.
