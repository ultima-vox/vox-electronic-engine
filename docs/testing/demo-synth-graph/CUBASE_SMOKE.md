# Vox Demo Synth (Graph) — Cubase smoke test

What this checks: the graph-only Demo (no DLL, no install step) appears in
the Rack and plays in Cubase. CTest parity (`graph_demo_parity_tests`) proves
it renders bit for bit like the native Demo; it does **not** prove DAW
behaviour. Record the result in the PR.

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
| 9 | Optional, with the native Demo package installed: load Vox Demo Synth into slot 6 with the same settings, set slot 6 to CH1 too. | Same sound (the two are designed to be identical). |

Report: Cubase version, pass/fail per row, anything unexpected.
