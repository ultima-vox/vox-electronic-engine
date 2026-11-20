# VOX FX — Wave B counterpoints, review-only

Wave A remains **120 / 0 approved**; Wave B remains separate staging and now contains **32 / 0 approved**. There are **152 cumulative review candidates**; 148 more are needed for the 300-program milestone. The 600-program target and listening/release approval are unchanged. No staging recipe enters the factory/browser registry here.

| Cue | Role / mode | Arrangement intent |
|---|---|---|
| Bronze Dovetail | Impact / One Shot | Low-mid strike and offset reflections inside a short kickless gap |
| Saffron Vectors | Riser / 8-bar Transition | Opens after four bars rather than masking an F-major chord line |
| Cinder Lattice | Fall / 4-bar Transition | Keyless successive band decay to leave space around an arpeggio |
| Cobalt Sluice | Sweep / 1-bar Transition | Near-to-far formant sweep behind a center lead |
| Pulse Quartz | Zap / Gate | Playable acid-response chirp, with release during the gaps |
| Quiet Escapement | Rhythm / seconds Loop | A restrained tick and reverse return underneath straight hats |
| Lunar Crib | Atmosphere / 2-bar Loop | Expanding upper bed that leaves the pulsing bass register free |
| Mirror Inlet | Stinger / One Shot | Wide fifth followed by centered late octave before a melodic hook |

Each new recipe has independently authored layers and trajectories, processing, motion and arrangement purpose. The two Loop cues are Wave B's first; Wave B now represents **all six** play modes. The clip references for held modes contain a finite pass, not a manual held-performance verification. The creative brief records revision, review schema, dayphase, energy, listening question and rights-review status per new cue. No new third-party assets were introduced.

[Manifest](evidence/wave-b-thirtytwo-manifest.tsv) · [creative brief](evidence/wave-b-thirtytwo-creative-brief.tsv) · [host bases](evidence/wave-b-thirtytwo-settings.tsv) · [96 render checks](evidence/wave-b-thirtytwo-metrics.tsv) · [synthetic mix metrics](evidence/wave-b-thirtytwo-context-metrics.tsv) · [member hashes](evidence/wave-b-thirtytwo-members.sha256) · [archive SHA-256](evidence/wave-b-thirtytwo-archive.sha256)

**Audio checks:** 32 × 44.1/48/96 kHz plus 32 deterministic byte-identical 48 kHz repeats from decoded `.voxfx` with the recorded host bases restored. Peak .00121820–.02314925, maximum absolute DC .00048405, minimum mono/stereo RMS .92416610, and maximum final-5-ms peak .00007761. All earlier 24 solo WAVs and portable files plus two original external source WAVs match their published SHA-256s. Unique structures are checked against A and B without relying on key/bar changes alone; this does not establish auditory distinctiveness.

**Listening material:** Eight with/without FX pairs use original synthetic backing, not a finished owner mix. Matching bed, explicit per-cue monitor trim (+18.619 to +24 dB), and at least 5.627 dBFS peak headroom. The trims are not written into Program state or reference solos. Review raw level and suitability in real arrangements before approval.

`artifacts/vox-fx-wave-b-thirtytwo-cue-review.zip` is 53,004,529 bytes, SHA-256 `4066018e9dc82c51c94407c01f1d4489b8735333ffb4ca82f23f59d6f05b69f4`. It contains the 32 solo WAVs and presets, two source WAVs, 16 A/B context files and metadata. The packager reproduced it byte-for-byte and all members were verified. No audio binaries are checked into the public repository. A durable private upload still requires an authorized private destination.

### Rebuild

Compile [the exporter](evidence/export-wave-b-stage.cpp) using the pinned JUCE command in the [triage handoff](WINDOWS_PROCESSOR_TRIAGE_20261119.md), then run:

```bash
build/export-wave-b-stage build/wave-b-32 --counterpoints
# Python with NumPy 2.2.6
python docs/ultima-vox-music/fx/evidence/measure-wave-b-counterpoints.py build/wave-b-32
python docs/ultima-vox-music/fx/evidence/package-wave-b-review.py build/wave-b-32 build/wave-b-32-review.zip --counterpoints
```

Omitting `--counterpoints` keeps the prior 24-cue export/packaging path. ZIP reconstruction rejects member drift or existing output paths.
