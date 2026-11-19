# VOX FX — Wave B Room Cuts (review only)

Wave A 120 + Wave B 56 = **176 candidates, zero approved**, with 124 more candidates to 300 and another 300 to 600. Nothing in this tranche is installed as approved Factory content.

| Cue | Role / mode | Arrangement slot |
|---|---|---|
| Sill Knock | Impact / One Shot | Dry, close drop marker with a late side click |
| Helix Crown | Riser / 8-bar Transition | D-major rising line before the lead |
| Dust Shutter | Fall / 2-bar Transition | Keyless, narrowing wash over an occupied mix |
| Split Iris | Sweep / Loop | Cyclic upper-mid acid color in a breakdown |
| Paper Comet | Zap / Gate | Brittle call and short noisy answer in a busy fill |
| Skew Buckle | Rhythm / 4-bar Sequence | Swung percussion with room for automation in bar three |
| Frost Gallery | Atmosphere / Continuous | Glassy hold under a bright arp |
| Latch Glint | Stinger / One Shot | Centered closing call followed by an open fifth |

These cues use built-in procedural sources only. The authoring deliberately favors dry anchors, fewer layers and less masking over baking gain into the reference. The independent event shapes, source/filter structures and timing do not rely on seed, key or overall gain changes for uniqueness. Exact recipe and audio fingerprints are a technical duplicate check, not an auditory finding.

[56-cue manifest](evidence/wave-b-fiftysix-manifest.tsv) · [Host bases](evidence/wave-b-fiftysix-settings.tsv) · [168 multirate checks](evidence/wave-b-fiftysix-metrics.tsv) · [Arrangement notes](evidence/wave-b-fiftysix-creative-brief.tsv) · [Raw levels](evidence/wave-b-fiftysix-levels.tsv)

- 56 unique IDs and 56 distinct 48 kHz WAV SHA-256s. All 56 rendered at 44.1/48/96 kHz and reproduced their 48 kHz reference after portable decode with the same host bases. The prior 48 WAVs, presets, manifest/settings/metrics rows and original two source WAVs are unchanged.
- New cues across 24 rate checks: peak .00347483–.01870680, max DC .00001784, min mono/stereo RMS ratio .95885589, max last-5-ms peak .00019586. Raw 48 kHz peaks are −46.453 to −34.812 dBFS, and active RMS −63.529 to −47.896 dBFS (20 ms windows, gate 30 dB below maximum window power; not LUFS). Dust Shutter is the quietest by this RMS metric. The finite Loop/Continuous/Gate references do not qualify sustained playback or seams.

## Real-mix gain-staging check

Start with the raw, unnormalized clip at unity in the documented slot. Compare the same arrangement without and with FX at fixed monitoring level. If the cue is masked, audition a documented clip or FX-bus trim (for example, +6 dB steps, checking peak headroom at each step). Keep the raw WAV unchanged. Write down arrangement context, active trim, mono result, and whether it reads without covering the kick, lead or next transient. If a cue only works with disruptive trim, mark it for redesign rather than accepting or peak-normalizing it. Apply this method especially to prior low-level Tangent Iris and Glass Estuary. No owner arrangements were available here, and no gains or cues have been approved.

## Listening package and rebuild

`artifacts/vox-fx-wave-b-room-cuts-review.zip` (7,009,875 bytes, SHA-256 `ab0c84ea73c12a99c68cd7cffda822d1bd566d2f82219bb225b257d3acb62bcb`) contains **eight** incremental WAVs and portable presets plus cumulative metadata, not all 56 recordings. [22 pinned members](evidence/wave-b-fiftysix-members.sha256) were verified and the ZIP rebuilt byte-identically. Durable transfer needs an authorized private destination; do not publish these audio files to public Git or a public deployment.

Build the pinned exporter as in [Windows processor triage](WINDOWS_PROCESSOR_TRIAGE_20261119.md) and use a fresh export directory:

```bash
build/export-wave-b-stage build/wave-b-56 --room-cuts
# Python with NumPy 2.2.6
python docs/ultima-vox-music/fx/evidence/measure-wave-b-crossings.py build/wave-b-56 --room-cuts
python docs/ultima-vox-music/fx/evidence/package-wave-b-review.py build/wave-b-56 build/vox-fx-wave-b-room-cuts-review.zip --room-cuts
```
