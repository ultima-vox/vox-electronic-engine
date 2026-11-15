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
  Every preview is written at the same fixed −6 dB so none clips; the plugin's own peak is listed in
  the README. A preview that would still clip, or is silent, fails the test.
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
