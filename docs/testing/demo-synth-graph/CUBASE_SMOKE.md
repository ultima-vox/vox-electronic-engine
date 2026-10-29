# Vox Demo Synth (Graph) — Cubase smoke test

What this checks: the graph-only Demo, installed as `VoxDemoSynthGraph.vins`
(no DLL, no code), appears in the Rack and plays in Cubase. The CTest parity
test (`graph_demo_parity_tests`) shows it renders bit for bit like the native
Demo. That is not proof of DAW behaviour. Record the result in the PR.

Nothing is built into the Rack: without the `.vins` file the instrument is not
offered.

## Setup

1. Install as in `CUBASE_TEST_GUIDE.md` sections 0–1, using the
   `Vox-Music-Install-Windows` artifact. It contains `VoxDemoSynthGraph.vins`
   in `vox music\instruments`.
2. Start Cubase. Create a new project and add an Instrument track with
   **Vox Electronic Engine**.

## Steps and expected result

| # | Action | Expected |
|---|---|---|
| 1 | Open the plugin editor. | 16 empty slots. |
| 2 | Slot 5 → instrument menu. | **Vox Demo Synth (Graph)** and **Vox Demo Synth** are listed. |
| 3 | Load it into slot 5. | Slot shows MIDI CH1 and 8 controls: Wave, Gain, Attack, Decay, Sustain, Release, Cutoff, Resonance. |
| 4 | Play keys on the track (CH1). | Mono saw synth; last-note priority; release tail on key up. |
| 5 | Move Cutoff, Resonance, Wave, Gain. | Audible change, no clicks on Gain/Cutoff moves. |
| 6 | Automate Cutoff (write, then read). | Playback follows the lane. |
| 7 | Hold sustain pedal, release keys, pedal up. | Notes hold, then release. |
| 8 | Save, close, reopen the project. | Slot 5 still the graph Demo, CH1, same values, same sound. |
| 9 | Quit Cubase, move `VoxDemoSynthGraph.vins` out of `instruments`, reopen; then move it back and reopen. | Without the file the slot is kept but silent (missing); with it the sound returns. |

Report: Cubase version, pass/fail per row, anything unexpected.

## Optional A/B against the native Demo

This uses the native `VoxDemoSynth.vins` from the same install folder. One Rack slot owns one MIDI channel, and the Rack
rejects a channel that another slot already owns. So the two instruments
can never both be on CH1 in one Rack. Use one of these two setups:

| # | Setup | Action | Expected |
|---|---|---|---|
| A1 | Two tracks. Add a second Instrument track with its own **Vox Electronic Engine**. Track 1: graph Demo in slot 1 (CH1). Track 2: Vox Demo Synth in slot 1 (CH1). | Same settings on both. Copy one MIDI clip (and any automation) to both tracks. Solo each in turn, or bounce both. | Same sound. The bounces should match: the two are designed to be identical. |
| A2 | One Rack. Graph Demo in slot 1 takes CH1. Vox Demo Synth in slot 2 takes CH2. | Same settings in both slots. Duplicate the same notes on CH1 and CH2 (two MIDI tracks routed to the Rack, or one clip with both channels). Mute one slot at a time. | Same sound from each slot. |
