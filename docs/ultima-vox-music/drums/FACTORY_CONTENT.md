# VOX Drums — factory content manifest

Everything shipped as factory content is **original and synthesized**. There are no
audio files and no third-party material.

| Item | What it is | Provenance |
|---|---|---|
| Kit "Psy Full-on" (family Psy / Full-on) | 16 pads: synthesis settings of the built-in models | table in `products/drums/FactoryContent.cpp` |
| Kit "House Techno" (family House / Techno) | as above | as above |
| Kit "Hip-hop Breaks" (family Hip-hop / Breaks) | as above | as above |
| 5 patterns with B variations per kit, one 16-bar song per kit | output of the generator for fixed settings and seeds | `factoryGenerator`, `factoryPatterns` in `FactoryContent.cpp`; `Generator.cpp` |
| Genre kits (PR 137 §5): Trance, Hard Trance, Dark Psy, Tech House, Melodic Techno, DnB, Rock (synth sketch), Metal (synth sketch) | 16 pads each from the built-in synth models, with their pad roles (`DROL`, §2.3 columns) | `genreKits()` in `FactoryContent.cpp` |
| One song preset per factory style (23) | Generate Song (5 slots with B variations, 32-bar timeline) with the style's own rules, its §4 defaults and seed `50000 + 101 × id`, on the role map of the style's kit | `factoryStyleGenerator`, `factoryStyleSong`, `factoryKitOfStyle` |

## Genre kits and style songs (PR 137 §5)

| Kit | Roles (pads 1–16) | Styles whose song is written for it | Release |
|---|---|---|---|
| Trance | §2.3 Trance column (ride 9, tom 2 10, tom 3 11) | Uplifting, Anthem, Melodic, Progressive, Dream, Symphonic Trance | listening pending |
| Hard Trance | Trance column | Hard Trance | listening pending |
| Dark Psy | default column, glitch percussion | Dark Psy | listening pending |
| Tech House | default column, bongo / conga / shaker percussion | Tech House | listening pending |
| Melodic Techno | default column with tom 2 on pad 9 (47) | Melodic Techno | listening pending |
| DnB | default column with a ride on pad 9 (51), ghost snare on pad 10 | Drum & Bass, Melodic DnB | listening pending |
| Rock (synth sketch) | §2.3 Rock column | Hard Rock | **NOT RELEASE-READY**: needs licensed acoustic samples |
| Metal (synth sketch) | §2.3 Metal column | Heavy, Symphonic, Nordic Metal | **NOT RELEASE-READY**: needs licensed acoustic samples |

- The original styles' songs use the original kits (Full-on and Goa: Psy Full-on; House, Techno
  and Ambient: House Techno; Trap and Breaks: Hip-hop Breaks) and the identity map.
- **Loading:** a genre kit sets its roles; an original kit sets the default layout. A **Factory
  Song** (TL decision on #147) loads its paired factory kit (pads, kit parameters, roles), the
  generator (the style and its defaults), the five patterns with B and the song as **one** load:
  all or nothing (`Presets::applyDocuments`), one Undo Load; the playback mode stays the user's.
  There is no song-only factory load. Rock/Metal songs are listed as NOT RELEASE-READY.
- **Pinned mappings (approved on #147):** DnB ride on pad 9 and ghost snare (percussion) on pad 10;
  Melodic Techno tom 2 on pad 9; snare-model percussion layers where §2.3 names them; Ambient's
  song on House Techno (interim).
- **Manifest:** `factory-content.csv` now has a `release` column: `listening-pending` for every
  electronic kit and its content (none was auditioned), `NOT-RELEASE-READY:needs-licensed-samples`
  for Rock and Metal. A genre kit's hash also covers its `DROL` section. The three original kits'
  hashes and contents are unchanged.
- **Not done:** no kit or song was auditioned; the per-style MIDI listening files (§8) come next.

- **Hashes:** `products/drums/factory-content.csv` holds the SHA-256 of every kit
  (its DPAD section and parameter values, and DROL for a genre kit), of each original kit's
  patterns (its DSEQ section) and song (DSNG), and of every style song's DSEQ and DSNG. `suite_drums_candidate_tests` recomputes and compares them.
  An intended change is committed with `VOX_GOLDEN_UPDATE=1`.
- **Rights:** the content was written for this product in this repository. No licence
  from a third party is involved. The repository has no licence file; the terms under
  which the product and its content are distributed are the owner's to state.
- **Samples:** none are shipped. User samples are referenced by path and content hash;
  a portable kit (`.voxkit`) carries the user's own files.
- **Not done:** the kits were not auditioned or tuned by ear by a person.

## Listening pack (PR 137 §8; TL review on #148)

Per style (23):

- `NN-<key>-0-preview.wav`: rendered offline through the style's factory kit (the Factory Song's
  main phrase, bars 9–16, then 1 s of tails; Internal sequencer, Song mode; 48 kHz, 16-bit, stereo).
  It is the plugin's unity output with no gain adjustment (TL P1 review on #148); the measured peak
  is listed in the README, and a preview above −1 dBFS, or silent, fails the test.
- `NN-<key>-1-song.mid`, `-2-pattern.mid`, `-3-meter.mid` (the style's first extra meter) or
  `-3-busy.mid`: the plugin's own MIDI export with the kit's notes, at the middle of the tempo hint.
  MIDI alone is not a listening verdict.

`README.md` is the review table (yes / no / why); `SHA256SUMS.txt` lists every file
(`sha256sum -c` format). `suite_drums_candidate_tests` always generates and checks everything in
memory. With `VOX_LISTENING_OUT=<new or empty folder>` it writes the files, checks every write,
requires exactly 69 MIDI + 23 WAV + README + SHA256SUMS, and writes `<folder>.zip`; it never
deletes or overwrites (a non-empty folder fails the test and nothing is written).

- **Windows CI:** the suite workflow writes the pack and uploads `VOX-Drums-Listening-Pack`
  (the ZIP and SHA256SUMS; kept 14 days).
- **Reproducible copy:** `tools/drums/make-listening-pack.ps1 -Build <dir> -Out <folder>` (Windows) or
  `tools/drums/make-listening-pack.sh <build-dir> <folder>` builds the test, writes the pack and
  verifies SHA256SUMS. Two runs give identical files (the ZIP container itself carries timestamps).
- Rock and Metal previews use the synth sketches: **NOT RELEASE-READY**. This is not the final DAW
  acceptance; nothing here establishes that a style sounds right.

## Gain staging (TL P1 review on #148; coverage per the TL review on #150)

**Finding:** at the old Kit Volume (0.6 in every kit) ten of the eleven factory kits peaked above
0 dBFS on Main with their own factory content (up to +7.09 dBFS, Metal), and the Rock and Metal
kits also on single output buses. The Main sum is linear; the excess came from the kits' output
gain (pad level × Kit Volume against the synth models). **Fix:** each factory kit's Kit Volume (its
output gain on every bus) is calibrated so that its worst case in the stress matrix below is about
−2 dBFS sample peak. No limiter, no DSP change, no routing change (Main FX only on Main; Kit Volume
still applies to every bus and stays automatable). The initial instance kit (`legacyKit`, the host
parameter defaults) is unchanged; it starts with no pattern.

**What is guaranteed, exactly:** the factory kits, at their stored Kit Volume and pad levels, with
the factory routing (all pads on Main) or the multi-out routing (pad n on Out 2 + n % 8), playing
the factory content and the stress matrix below, peak at or below **−1 dBFS sample peak** on every
active output bus at 44.1, 48 and 96 kHz, with every sample finite. It is **not** a true-peak
(inter-sample, oversampled) figure, which is not measured. It does **not** cover user-edited kits or
settings: any Kit Volume or pad level can be raised past full scale. The matrix is sampled (fixed
seeds), so the ~1 dB margin between the measured worst case (−2 dBFS) and the gate is the safety for
seeds and settings outside it.

**Gate** (`suite_drums_gain_staging_tests`, 8 ctests run in parallel):

- **Matrix** (`<rate> main|multi`, 44.1 / 48 / 96 kHz): per style on its factory kit, the 32-bar
  Factory Song; stress A (density, complexity, fills, groove, syncopation, variation and Seq Accent
  at 1) as a 64-bar song (seed 77 + id) and two 32-bar songs (seeds 1009 + 7·id, 4099 + 13·id);
  stress B (density, complexity, fills 1; groove, syncopation, variation 0.5; humanize 0; Seq Accent
  1) as two 32-bar songs (seeds 9973 + 3·id, 30011 + 11·id); and the original kits' own song.
  Multi-out runs the Factory Song and the first stress A song (each bus carries at most two pads).
- **`blocks`** (48 kHz): the stress A song of each kit rendered in blocks of 64, 333 and 512 frames
  is **sample-identical** on Main (the transport stops at the same frame), so the block size cannot
  change a peak; and the Factory Song looped over bars 25–32, played for 40 bars (across the loop
  point), stays under the limit.
- **`matched`** (48 kHz): each calibrated kit's output equals the legacy output (Kit Volume 0.6)
  times the volume ratio, sample for sample (max deviation ≈ 1e-7 of peak): the timbre is unchanged.
- **Report:** `VOX_GAIN_REPORT=<prefix>` writes `<prefix>-<mode>.csv` (kit, style, content, seed,
  rate, block, routing, bus, peak); a failed write fails the test. The Windows CI writes them into
  the suite artifact (`suite-out/gain`).

| Kit | Kit Volume | Worst Main / worst single output bus (dBFS sample peak), before | after (worst case) |
|---|---|---|---|
| Psy Full-on | 0.600 → 0.385 (−3.85 dB) | +1.62 / −3.42 | −2.01 / −7.25 (full-on stress B, 96 kHz) |
| House Techno | 0.600 → 0.407 (−3.37 dB) | +1.36 / −3.75 | −2.02 / −7.70 (techno stress A 64 bars, 96 kHz) |
| Hip-hop Breaks | 0.600 → 0.327 (−5.27 dB) | +1.74 / −1.57 | −2.01 / −6.93 (breaks stress A seed 4164, 48 kHz) |
| Trance | 0.600 → 0.337 (−5.01 dB) | +2.77 / −1.27 | −2.02 / −6.30 (anthem-trance stress A, 96 kHz) |
| Hard Trance | 0.600 → 0.402 (−3.48 dB) | +1.47 / −0.56 | −2.01 / −4.04 (hard-trance stress A 64 bars, 96 kHz) |
| Dark Psy | 0.600 → 0.562 (−0.57 dB) | −1.47 / −3.02 | −2.01 / −3.60 (dark-psy stress A, 48 kHz) |
| Tech House | 0.600 → 0.386 (−3.83 dB) | +1.83 / −2.38 | −2.00 / −5.68 (tech-house stress A 64 bars, 48 kHz) |
| Melodic Techno | 0.600 → 0.375 (−4.08 dB) | +1.90 / −1.38 | −2.02 / −4.97 (melodic-techno stress B, 96 kHz) |
| DnB | 0.600 → 0.406 (−3.39 dB) | +1.39 / −0.07 | −2.00 / −3.14 (melodic DnB stress A 64 bars, 96 kHz) |
| Rock (synth sketch) | 0.600 → 0.328 (−5.25 dB) | +3.24 / +1.27 | −2.00 / −3.97 (hard-rock Factory Song, 96 kHz) |
| Metal (synth sketch) | 0.600 → 0.208 (−9.20 dB) | +7.09 / +0.99 | −2.01 / −8.24 (nordic-metal stress B, 48 kHz) |

"Before" is the single-seed measurement of the first calibration round; "after" is the full matrix.

Worst Factory Song peak per style (Main, any rate), before → after, dBFS sample peak:

| Style | Before | After | Style | Before | After |
|---|---|---|---|---|---|
| Full-on / Psy | +0.50 | −3.35 | Heavy Metal | +3.77 | −5.43 |
| Goa | +1.62 | −2.23 | Symphonic Metal | +4.07 | −5.13 |
| House | −0.79 | −4.16 | Nordic Metal | +5.94 | −3.26 |
| Techno | −0.20 | −3.57 | Tech House | −0.00 | −3.84 |
| Trap | +0.10 | −5.17 | Drum & Bass | −2.48 | −5.87 |
| Breaks | +0.50 | −4.77 | Melodic Drum & Bass | +0.23 | −3.94 |
| Ambient | −2.69 | −6.06 | Melodic Techno | +1.90 | −2.18 |
| Uplifting Trance | +0.39 | −4.62 | Progressive Trance | +0.73 | −4.28 |
| Anthem Trance | +2.35 | −2.67 | Dream Trance | +1.82 | −3.19 |
| Dark Psy | −1.55 | −2.11 | Hard Trance | +0.97 | −2.51 |
| Melodic Trance | −0.33 | −5.34 | Symphonic Trance | +2.47 | −2.54 |
| Hard Rock | +3.24 | −2.00 | | | |

- **Hashes:** only the 11 kit hashes change (Kit Volume is in the kit's DPAD and DPRM); every
  pattern and song hash is unchanged. The previous manifest is kept as the comparison baseline in
  `tests/suite/golden/factory-content-before-gain-staging.csv`.
- **Listening previews** are the plugin's unity output (no gain adjustment); above −1 dBFS fails.
- **Not covered:** user kits and user settings; true-peak headroom; loudness or tonal balance
  between kits (listening).
