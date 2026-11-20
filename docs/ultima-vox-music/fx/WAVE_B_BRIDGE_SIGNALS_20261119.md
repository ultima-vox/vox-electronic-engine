# VOX FX — Wave B bridge signals, review-only

**Wave A 120 + Wave B 40 = 160 review candidates, zero approved.** These eight incremental cues are not installed into the factory registry. The 300-program milestone still needs 140, then a further 300 for Wave C. The prior Wave B 32 package remains unchanged.

| New cue | Role / mode | Arrangement intent |
|---|---|---|
| Slate Clutch | Impact / One Shot | Dry strike and short woody after-tick between bass phrases |
| Lantern Parallax | Riser / 4-bar Transition | Sparse D-minor bridge, third enters only in the latter half |
| Vapour Splice | Riser / 4-bar Transition | Keyless alternate when the existing chords already carry the rise |
| Chalk Eddy | Fall / 8-bar Transition | Restrained grain return behind the bass rhythm |
| Silt Compass | Sweep / Continuous | Held shallow formant rotation without a repeated attack |
| Fraction Spark | Zap / Gate | Short acid-laser response that leaves the next subdivision open |
| Hex Repeater | Rhythm / 2-bar Sequence | Sparse triplet percussion with an empty downbeat in the second bar |
| Amber Branch | Stinger / One Shot | Lead ending accent and soft split fifth, not a next-kick hit |

The eight recipes have independent sources, trajectories, timing, post FX and motion. The two bridge alternates are not transpositions or seed variants; they share a slot and tempo, but their source designs differ. The stage exporter rejects exact structure duplicates against prior A+B entries, independent of key/bar differences. This cannot establish perceptual distinction or approval.

[40-cue manifest](evidence/wave-b-forty-manifest.tsv) · [host bases](evidence/wave-b-forty-settings.tsv) · [120 render checks](evidence/wave-b-forty-metrics.tsv) · [new cue brief](evidence/wave-b-forty-creative-brief.tsv) · [raw levels](evidence/wave-b-forty-levels.tsv) · [member hashes](evidence/wave-b-forty-members.sha256) · [archive SHA](evidence/wave-b-forty-archive.sha256)

- 40 × 44.1/48/96 kHz. 40 decoded `.voxfx` round trips reproduced their 48 kHz solos with the recorded host bases, byte-for-byte. All 32 previous solo WAVs and portable presets and both original external source WAVs were rechecked against their published SHA-256s. No new external sources. Overall peak .00121820–.02314925, max DC .00048405, min mono/stereo RMS .92416610, final-5-ms peak ≤.00007761.
- The **new eight** have raw peaks from **−53.115 to −38.146 dBFS**, and measured active-window stereo RMS from **−64.020 to −53.059 dBFS** (20 ms windows, threshold 30 dB below each clip's maximum window power). These unchanged levels are quiet and require gain-staging review; the RMS metric is not LUFS or perceived loudness.
- The bridge comparison provides the **same** synthetic 132 BPM D-minor bed alone, with Lantern Parallax, and with Vapour Splice. Added signals are matched at −38.940 dBFS by that active-window RMS metric (+24.000 and +18.497 dB trim respectively); peak is −6.911 dBFS in both mixes. Trims are not applied to raw or preset files. Listen in real arrangements to assess placement, masking and audible balance; no owner mix was available this pass.

`artifacts/vox-fx-wave-b-bridge-signals-review.zip` (17,041,038 bytes, SHA-256 `04c9d1937e9563166ad70253664caf2cd7c8d750043545d828e99cb02e4fdd22`) contains eight incremental WAVs and `.voxfx` files, three bridge WAVs, and cumulative metadata for 40. It does **not** duplicate the previous 32 audio files. All 25 pinned members were verified; the ZIP rebuild was byte-identical. Audio is not in public Git; a durable private upload needs an authorized private destination.

## Rebuild

Use the pinned [export build command](WINDOWS_PROCESSOR_TRIAGE_20261119.md) and a fresh output directory:

```bash
build/export-wave-b-stage build/wave-b-40 --signals
# Python with NumPy 2.2.6
python docs/ultima-vox-music/fx/evidence/measure-wave-b-signals.py build/wave-b-40
python docs/ultima-vox-music/fx/evidence/package-wave-b-review.py build/wave-b-40 build/vox-fx-wave-b-bridge-signals-review.zip --signals
```
