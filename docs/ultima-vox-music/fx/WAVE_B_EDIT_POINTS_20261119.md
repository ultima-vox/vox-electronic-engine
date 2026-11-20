# VOX FX — Wave B Edit Points (review only)

**32 individually authored arrangement cues** extend Wave B from 80 to 112. With Wave A120, the [working matrix](evidence/fx-review-232-working-matrix.tsv) holds **232 review candidates and zero approvals**: 68 short of 300 and 368 short of 600. The 200 existing rows, including their reviewer fields, are unchanged. The programs are not installed in the plug-in, bundled for release, or uploaded.

| Role | New cues | Placement contrasts to review |
|---|---|---|
| Impact | Granite Pocket, Velvet Bulkhead, Cinder Docket, Porcelain Keel | Keyless snare-cut tick vs. a low chord-mute hit, held metal bloom and dry glass accent |
| Riser | Meridian Gauze, Hydraulic Seam, Garnet Breath, Arc Filing | Four-bar high-band lift, eight-bar keyless pressure, gate-controlled pickup and three short arcs |
| Fall | Satin Spall, Coal Canvas, Burnished Drop, Limestone Veil | Fifth after pad release, keyless dub exit, tight metallic drop and held grain tail |
| Sweep | Lattice Brush, Opal Switchback, Tin Cartouche, Brine Ribbon | Keyless offbeat pass, mid-fill notch, held loop seam and gated vowel |
| Zap | Cobalt Skip, Acetate Joint, Nickel Ligature, Cartridge Tick | Tritone flam pickup, acid handover, reverse grain into an octave chirp and keyless hard edit |
| Rhythm | Bronze Census, Balsa Relay, Quill Escapement, Parasol Catch | Two-bar eighths, four-bar sixteenths, one-bar 32nd triplets and three-bar eighth triplets |
| Atmosphere | Willow Diaphragm, Soot Gallery, Orchard Drift, Tallow Quay | Vocal bridge, keyless echo-room loop, fifth under an arp and a short held spoken-word pickup |
| Stinger | Ivory Ellipsis, Slate Semicolon, Celadon Aperture, Sundial Stamp | One-bar held-back chorus call, keyless cut, modal lead handover and tritone stop mark |

All use built-in synthesis. Each recipe is authored for a placement with its own instrument layers, timing, trajectories and Motion routes. Distinct presets and render hashes are technical checks, not claims about how they sound together. Transition previews use the fixed BPM and bar count in the sidecar; held and loop previews are finite and do not qualify steady-state joins or a DAW release tail.

## Technical checks

- All 112 Wave B programs passed the exporter: 336 rate checks at 44.1/48/96 kHz in PCM24 and byte-exact 48 kHz rerenders using decoded portable Program, Pattern and Motion with the same host bases. The audit verified all 162 previously pinned B files (80 WAVs, 80 portable files, two original sources), and byte-identical prefixes for all B80 tables. Across A and B the matrix has 232 unique IDs and 232 unique pinned WAV hashes; Wave A was not rerendered in this pass.
- The new 96 rate checks measured peaks .00110352–.02800346, maximum absolute channel DC .00012002, minimum mono/stereo RMS ratio .93357290 and maximum last-5-ms peak .00005054. New 48 kHz peak levels range from −58.650 to −31.056 dBFS; measured active RMS ranges from −77.473 to −46.755 dBFS (not LUFS). Twelve new cues fall below −60 dBFS active RMS; 82 across all 232. Start gain-staging checks with Meridian Gauze, Acetate Joint and Orchard Drift. No normalized variants were produced.
- [Manifest](evidence/wave-b-onehundredtwelve-manifest.tsv), [render settings](evidence/wave-b-onehundredtwelve-settings.tsv), [rate metrics](evidence/wave-b-onehundredtwelve-metrics.tsv), and [68 incremental SHA-256 pins](evidence/wave-b-onehundredtwelve-members.sha256) cover the 64 additions and four reports. Two independent audit outputs matched byte for byte; appending a byte to a copy of the Quill Escapement WAV produced a rejection and no report. The old B80 audit profile also reproduced its published metadata byte for byte.

## Listening and transfer

Keep the raw references at their exported levels. At matched monitoring gain, compare **Hydraulic Seam** behind a real lead, **Quill Escapement** over straight hats, **Willow Diaphragm** under a vocal, and **Ivory Ellipsis** into a chorus. Log the arrangement reference, any actual clip/bus trim, headroom, mono masking and the review decision. Use the 232-cue matrix for decisions rather than editing multiple sheets. The gain-staging queue is a technical triage flag, not a suggested trim or a listening verdict.

Use authorized real Cubase/REAPER arrangements, not synthetic backing. Private handoff is blocked until the owner names the durable destination and recipients. No new ZIP or audio transfer. Real host acceptance and rights sign-off remain pending.

## Reproduce locally

Catalog source `d569fdec34bf4f787cda32e7226645aa21588bb1`; build the [staged exporter](evidence/export-wave-b-stage.cpp) as in [B56](WAVE_B_ROOM_CUTS_20261119.md) with GCC 12.2 and JUCE `e18f7f506c0b96f2c738a0bcd7fe6467a5005ad8`. Use fresh ignored output directories and NumPy 2.2.6:

```bash
build/export-wave-b-stage build/wave-b-112 --edit-points
python docs/ultima-vox-music/fx/evidence/audit-fx-session-cuts.py \
  build/wave-b-112 build/wave-b-112-metadata --edit-points
```

The audit emits metadata only. Resolve logical `Wave-B/…` members under the local export root. Do not upload or package automatically.
