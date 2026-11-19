# VOX FX — Wave B Crossings (review only)

Wave A 120 + Wave B 48 = **168 candidates, zero approved**. A further 132 candidates are needed to reach 300, then another 300 to reach 600. This tranche does not modify the factory registry or make a listening or legal approval claim.

| Cue | Role / mode | Placement to audition |
|---|---|---|
| Granite Hinge | Impact / One Shot | Drop marker with grain scrape after the kick |
| Bell Current | Riser / 2-bar Transition | F-major bell lift handing off to the lead |
| Serpent Return | Fall / 4-bar Transition | Descending Goa acid with gaps around the kick |
| Tangent Iris | Sweep / Loop | Repeated low-motion filter movement in a progressive break |
| Quartz Fissure | Zap / Gate | Brittle high answer whose end follows note release |
| Checker Mire | Rhythm / 3-bar Sequence | Uneven triplet percussion and a third-bar pickup for a scene change |
| Glass Estuary | Atmosphere / Continuous | Glassy held texture under piano or voice |
| Brass Arc | Stinger / One Shot | A-minor call and side answer after an exposed hook |

The sound structures were authored per arrangement slot: sources and layer timing, trajectories, effect chains, and modulation. Exact structure fingerprints prohibit trivial variants by key/bar length; they do not prove perceptual distinction. Sources are built-in procedural designs; there are no new external samples.

[48-cue manifest](evidence/wave-b-fortyeight-manifest.tsv) · [Host bases](evidence/wave-b-fortyeight-settings.tsv) · [144 rate checks](evidence/wave-b-fortyeight-metrics.tsv) · [Arrangement notes](evidence/wave-b-fortyeight-creative-brief.tsv) · [Raw levels](evidence/wave-b-fortyeight-levels.tsv)

- 48 unique preset IDs and 48 different 48 kHz audio SHA-256s. All 48 portable files survived decode/re-encode and 48 kHz reference rendering with recorded host bases. The exporter completed 144 checks at 44.1/48/96 kHz. The prior 40 manifest/settings/metrics rows and all 40 WAVs/presets plus both sources are unchanged.
- For the 24 new rate checks: peak .00118709–.02103544; max DC .00004380; min mono/stereo RMS ratio .87731014; max last-5-ms peak .00010777. These are technical checks, not a quality sign-off.
- New raw 48 kHz peaks range from −58.231 to −33.541 dBFS; active RMS ranges from −75.062 to −43.430 dBFS (20 ms windows with a relative 30 dB power gate, not LUFS). Tangent Iris (−75.062 active RMS) and Glass Estuary (−66.051) need particular gain-staging and masking review. No render was normalized, and no actual arrangement was available for this stage. Finite Loop/Gate/Continuous previews do not replace testing the held behavior in a DAW.

`artifacts/vox-fx-wave-b-crossings-review.zip` (4,239,844 bytes; SHA-256 `9a18b9b616266ef652b886adf901fe58ccd3ac831f7b0c1a9a6b4d6237aa742e`) holds eight incremental raw WAVs and `.voxfx` files with cumulative evidence, **not** 48 WAVs. [22 pinned members](evidence/wave-b-fortyeight-members.sha256) were verified and the ZIP rebuilt byte-identically. Private durable delivery is still outstanding; do not upload listening audio to public Git.

## Rebuild

Build the pinned exporter as in [Windows processor triage](WINDOWS_PROCESSOR_TRIAGE_20261119.md) and use a fresh export directory:

```bash
build/export-wave-b-stage build/wave-b-48 --crossings
# Python with NumPy 2.2.6
python docs/ultima-vox-music/fx/evidence/measure-wave-b-crossings.py build/wave-b-48
python docs/ultima-vox-music/fx/evidence/package-wave-b-review.py build/wave-b-48 build/vox-fx-wave-b-crossings-review.zip --crossings
```
