# VOX FX — Wave B interludes, review staging only

Wave A remains **120 for review, zero approved**. Wave B now holds **24 separately staged** cues (16 preserved, 8 new); **144 cumulative review programs / 0 approved**, not the Wave B target of 300 or the Wave C target of 600. None of this separate B staging enters the Wave A browser. No factory release or distribution approval is implied.

| Cue | Role / mode | Arrangement slot |
|---|---|---|
| Oxide Clapper | Impact / One Shot | Corroded center strike and a delayed plate for a kickless cut |
| Apricot Gantry | Riser / Transition, 2 bars | Fifth enters on the second bar after filtered chords |
| Brine Veil | Fall / Transition, 2 bars | Keyless falling hiss under the return of percussion |
| Pewter Vane | Sweep / Gate | Cross-stereo formant between vocal phrases, with authored release |
| Ice Caliper | Zap / One Shot | High offbeat zap answered by a reverse grain |
| Braided Ratchet | Rhythm / Sequence, 1 bar | Asymmetric stutter with ending ratchet against straight kick |
| Moth Radius | Atmosphere / Continuous | Held, slow stereo drift for a breakdown bed |
| Opal Signal | Stinger / One Shot | Two-stage tonic-to-fifth accent between vocal phrases |

Each cue has its own authored layers, contours, timing, filters, FX and two motion routes, with held ADSR, grain shape or trigger steps where relevant. The review exporter rejects duplicate IDs/names and exact Program structures even after clearing key and bar duration, across the entire current A+B collection; the staged WAV hashes are also unique. This exact-match audit is not a perceptual similarity or mix review.

[Manifest](evidence/wave-b-twentyfour-manifest.tsv) · [host settings](evidence/wave-b-twentyfour-settings.tsv) · [three-rate metrics](evidence/wave-b-twentyfour-metrics.tsv) · [member SHA-256](evidence/wave-b-twentyfour-members.sha256) · [ZIP SHA-256](evidence/wave-b-twentyfour-archive.sha256)

`artifacts/vox-fx-wave-b-twentyfour-cue-review.zip` (27,101,126 bytes) contains 24 raw stereo 48 kHz PCM24 references, 24 portable presets, the two original external source WAVs for Vellum Coil and Kiln Choir, settings, metrics and listening/relink instructions. The ZIP is a generated review artifact, not a published product or a Git binary. The 16 pre-existing renders and preset files and both external source WAVs are byte-identical to the comparison export from `514e96a`. The 16 reference WAV hashes also match the earlier saved Wave B manifest. All 120 Wave A solo WAVs were regenerated on this source and matched their published reference SHA-256s.

- 72 solo renders at 44.1/48/96 kHz; 24 portable round trips and byte-identical 48 kHz repeats from decoded state, with receiving-host base values reapplied.
- Peak .00121820–.02314925; maximum absolute DC .00013003; minimum mono/stereo RMS .92416610; maximum final-5-ms peak .00007761. These do not certify gain staging, musical distinctiveness or arrangement fitness.
- For reproduction use the pinned JUCE toolchain and exporter command in [Windows triage / Wave B staging](WINDOWS_PROCESSOR_TRIAGE_20261119.md). The exporter now audits and emits all 24.
- Owner listening, broader in-mix checks, the remaining 156 cues to reach 300, and then 300 more to reach 600 are open. No Windows or DAW qualification is inferred from this review package.

To rebuild the listening ZIP from the export directory without committing any audio binaries:

```bash
python3 docs/ultima-vox-music/fx/evidence/package-wave-b-24.py build/wave-b-stage build/vox-fx-wave-b-twentyfour-cue-review.zip
```

The packager checks SHA-256 for each pinned member and reports whether the compressed ZIP also matches the original byte-for-byte (zlib version can change the compressed bytes). Transfer the result only via authorized private storage; the repository is public, so neither this script nor the review manifest publishes the audio archive.
