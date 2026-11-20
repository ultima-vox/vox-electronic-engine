# VOX FX — Wave B Session Cuts (review only)

**24 individually authored cues** take Wave B from 56 to 80. With A120 that is **200 review candidates, zero human-approved**: 100 short of 300 and 400 short of 600. No factory or user-preset installation, package reissue, public audio or release approval. The archived A120+B56 WAVs and ZIP remain as pinned; the larger group is a local export and a reproducible metadata update.

| Role | New cues | What to compare in a real arrangement |
|---|---|---|
| Impact | Brass Pinhole, Rail Paper, Undertow Hinge | Kick follow-through vs. a keyless edit knock vs. a low modal stop marker |
| Riser | Aperture Run, Canal Pressure, Spark Ladder | Eight-bar harmonic lift, 16-bar keyless pressure under a lead, and a fast two-beat pickup |
| Fall | Ink Residue, Copper Silt, Sunken Reel | Keyless vocal space, gated percussion handover, and an eight-bar major-color descent |
| Sweep | Sinter Loop, Shale Skate, Ochre Sluice | Held grain loop, keyless drum-fill scrape, and a gated vowel against a lead |
| Zap | Ion Splinter, Lacquer Eel, Ice Fulcrum | Short flick, held acid glide, and reverse-grain pickup in an uneven fill |
| Rhythm | Gantry Claves, Moss Switch, Foam Meter | Two-bar swung sixteenths, four-bar triplet ghosts, and a one-bar pitched answer |
| Atmosphere | Fennel Haze, Ash Vestibule, Quartz Porch | Sustained vowel bed, keyless room loop and a held fifth under an arp |
| Stinger | Tallow Mark, Flint Apostrophe, Saffron Beacon | Modal handover, keyless stop punctuation and a one-bar pitched call |

New modes: 9 One Shot, 5 Transition, 3 Gate, 3 Sequence, 2 Loop and 2 Continuous. Each cue has its own part placement, recipe, motion and timing; these are not seed, gain or transposition variants. All 24 use built-in synthesis; no new external source WAVs.

## Provenance and technical audit

- The pinned exporter checks structural uniqueness after removing key and bar-length differences, produces PCM24 at 44.1/48/96 kHz and checks portable re-encoding. For every one of the 80 B cues the 48 kHz render reproduced byte-exactly from the decoded Program, Pattern and Motion with the same host bases. The 24 additions contribute 72 of the 240 rate checks. The older 56 B WAVs, 56 portable files and two source WAVs matched all 114 published pins. Earlier B56 manifest/settings/metrics rows are unchanged.
- Among the 72 new rate checks: peak .00246871–.02031517, largest absolute channel DC .00002196, minimum mono/stereo RMS ratio .91558944, largest last-5-ms peak .00008118. These checks do not qualify sustained playback or seams. New 48 kHz peak levels are −51.608 to −33.844 dBFS; active RMS is −69.083 to −46.485 dBFS (not LUFS). Seven new cues are below −60 dBFS active RMS, including Saffron Beacon. The raw references are not normalized.
- [Manifest](evidence/wave-b-eighty-manifest.tsv), [render settings](evidence/wave-b-eighty-settings.tsv), [multirate metrics](evidence/wave-b-eighty-metrics.tsv) and [52 SHA-256 pins](evidence/wave-b-eighty-members.sha256) cover the 48 new WAV/preset members and four new metadata files. Two audits yielded identical metadata; a corrupted new WAV was refused without emitting reports. Exact WAV hash checks span all 200 matrix entries, not just Wave B. Distinct bytes do not establish musical distinctness or approval.

## One listening sheet

Use the **[200-cue working matrix](evidence/fx-review-200-working-matrix.tsv)** for decisions. Its first 176 rows are identical to the prior sheet, including all review fields; the older sheet is now a baseline, not a second review path. The old ZIP does not contain the new cues. A playing host, fixed monitor level and actual owner arrangement are still needed. Start with the earlier Tangent Iris, Glass Estuary and Dust Shutter and the new Aperture Run, Gantry Claves and Saffron Beacon. At unity, log where each cue reads or masks; if needed, test and record the clip/bus trim in the mix, not a change to the original recording. Capture arrangement reference, headroom, mono masking and reviewer decision in the one sheet. `review-queue` sorts the gain-staging workload; it does not recommend a trim or imply rejection/approval.

Durable transfer is still blocked on the owner naming the authorized private destination and recipients. Use real arrangement references, not synthetic backing or public audio. No new ZIP was made.

## Reproduce locally

Catalog and audit source: `92bc14d1ad86cecd87c22c7232fcf1a61fb82eb0`. Compile the same [Wave B staged exporter](evidence/export-wave-b-stage.cpp) using the pinned JUCE core/cryptography, `vox_preset` and GCC 12.2 as for [B56](WAVE_B_ROOM_CUTS_20261119.md); the new profile is `--session-cuts`. Use fresh output directories and NumPy 2.2.6:

```bash
build/export-wave-b-stage build/wave-b-80 --session-cuts
python docs/ultima-vox-music/fx/evidence/audit-fx-session-cuts.py build/wave-b-80 build/wave-b-80-metadata
```

The audit emits metadata only. The `Wave-B/…` references in the review sheet are logical member names: locally resolve them under the B80 export root. Do not upload or repack automatically.
