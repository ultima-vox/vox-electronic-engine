# Vox Demo Synth (Graph) — Cubase smoke test

What this checks: the graph-only Demo (no DLL, no install step) appears in
the Rack and plays in Cubase. CTest parity (`graph_demo_parity_tests`) proves
it renders bit for bit like the native Demo; it does **not** prove DAW
behaviour. Record the result in the PR.

**Temporary delivery.** In this PR the graph Demo is a *factory test graph*
embedded in the Rack binary. It has no `.vins` package yet. The final
installation path is one graph-only `.vins` archive next to the VST3
(#73/#75). The embedded copy is removed as a shipping assumption once the
verified archive loader exists. Passing this smoke test does not complete
#73.

## Setup

1. From this PR's Windows CI run download `Vox-Electronic-Engine-Windows`
   (the plugin) and `Vox-Demo-Synth-Cubase-Guide` (this guide plus
   `build-info.txt`, whose "Head commit" must be this PR's head).
2. Close Cubase. Replace `C:\Program Files\Common Files\VST3\Vox Electronic Engine.vst3`
   with the artifact's bundle. No instrument package is needed.
3. Start Cubase, rescan if asked. New project, add an Instrument track with
   **Vox Electronic Engine**.

## Steps and expected result

| # | Action | Expected |
|---|---|---|
| 1 | Open the plugin editor. | 16 empty slots. |
| 2 | Slot 5 → instrument menu. | **Vox Demo Synth (Graph)** is listed (next to Vox Demo Synth if its package is installed). |
| 3 | Load it into slot 5. | Slot shows MIDI CH1 and 8 controls: Wave, Gain, Attack, Decay, Sustain, Release, Cutoff, Resonance. |
| 4 | Play keys on the track (CH1). | Mono saw synth; last-note priority; release tail on key up. |
| 5 | Move Cutoff, Resonance, Wave, Gain. | Audible change, no clicks on Gain/Cutoff moves. |
| 6 | Automate Cutoff (write, then read). | Playback follows the lane. |
| 7 | Hold sustain pedal, release keys, pedal up. | Notes hold, then release. |
| 8 | Save, close, reopen the project. | Slot 5 still the graph Demo, CH1, same values, same sound. |

Report: Cubase version, pass/fail per row, anything unexpected.

## Optional A/B against the native Demo

This needs the native Vox Demo Synth package, installed as in
`CUBASE_TEST_GUIDE.md`. One Rack slot owns one MIDI channel, and the Rack
rejects a channel that another slot already owns. So the two instruments
can never both be on CH1 in one Rack. Use one of these two setups:

| # | Setup | Action | Expected |
|---|---|---|---|
| A1 | Two tracks. Add a second Instrument track with its own **Vox Electronic Engine**. Track 1: graph Demo in slot 1 (CH1). Track 2: Vox Demo Synth in slot 1 (CH1). | Same settings on both. Copy one MIDI clip (and any automation) to both tracks. Solo each in turn, or bounce both. | Same sound. The bounces should match: the two are designed to be identical. |
| A2 | One Rack. Graph Demo in slot 1 takes CH1. Vox Demo Synth in slot 2 takes CH2. | Same settings in both slots. Duplicate the same notes on CH1 and CH2 (two MIDI tracks routed to the Rack, or one clip with both channels). Mute one slot at a time. | Same sound from each slot. |
