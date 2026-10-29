# Vox Demo Synth in Vox Rack — Cubase test guide (Windows x64)

Install by copying files: no installer, scripts or environment variables.
CTest results do not count as DAW acceptance. Record the result of every step
in the table at the end.

**Signing.** CI builds sign the native `.vins` packages with a development
publisher key that is generated in the CI build tree. A package loads only in
the Rack from the **same CI run**; `build-info.txt` names the key. The real
release key is not set up yet (release blocker, #77 gate 3).

## 0. Downloads (one CI run, same commit)

| Artifact | Contents |
| --- | --- |
| `Vox-Music-Install-Windows` | `vox music\Vox Electronic Engine.vst3` plus `vox music\instruments\VoxDemoSynth.vins` and `VoxDemoSynthGraph.vins`, already in the install layout |
| `Vox-Demo-Synth-Cubase-Guide` | this guide, `vox-demo-synth-test-phrase.mid`, the graph smoke guide, `build-info.txt` (commit, publisher key, SHA-256 of the Rack and of each `.vins`) |

## 1. Install (once)

1. Close Cubase.
2. Remove any older `Vox Electronic Engine.vst3` from
   `C:\Program Files\Common Files\VST3\`, including copies directly in that
   folder. Cubase must find exactly one.
3. Copy the folder `vox music` from `Vox-Music-Install-Windows` to
   `C:\Program Files\Common Files\VST3\`. The result:
   ```
   C:\Program Files\Common Files\VST3\vox music\Vox Electronic Engine.vst3
   C:\Program Files\Common Files\VST3\vox music\instruments\VoxDemoSynth.vins
   C:\Program Files\Common Files\VST3\vox music\instruments\VoxDemoSynthGraph.vins
   ```
4. Confirm you are testing this build:
   `Get-FileHash "C:\Program Files\Common Files\VST3\vox music\Vox Electronic Engine.vst3\Contents\x86_64-win\Vox Electronic Engine.vst3"`
   The hash must equal "Rack SHA-256" in `build-info.txt`, and it must not
   change in any later step. The same check works for each `.vins` file.
5. Start Cubase.

The Rack reads only `vox music\instruments` next to its own VST3. On first
use it copies the verified module to
`%LOCALAPPDATA%\Vox Music\Cache` and loads it from there.

## 3. Cubase procedure

| # | Action | Expected |
| --- | --- | --- |
| 1 | *Studio → VST Plug-in Manager* | "Vox Electronic Engine" listed, no error |
| 2 | Add a **new** Instrument track with Vox Electronic Engine (no saved project, no track preset); open its editor | Rack shows **16 empty slots**; the header shows "Empty slot" and "Choose an instrument to activate this slot"; nothing sounds |
| 3 | Click empty **slot 1** (or right-click → *Load / replace instrument...*) → **Vox Demo Synth / Ultima Vox** | Slot 1 shows Vox Demo Synth on **CH1** (first free channel); the Macros page shows Wave, Gain, Attack, Decay, Sustain, Release, Cutoff, Resonance. Loading into slot 5 instead also gives CH1 |
| 4 | Set the track's MIDI channel to 1 (or Any with a channel-1 keyboard); play a few keys | Audible saw tone; pitch follows the keys; louder with harder velocity; key up starts a short release |
| 5 | Import `vox-demo-synth-test-phrase.mid` onto the track (channel 1, 120 BPM) and play | Bar 1: four rising notes, rising velocity. Bar 2: A3, B3 on top, back to A3 when B3 ends. Bar 3: short E3 held by sustain pedal until beat 4. Bar 4: G2 ends with a normal release on beat 3 (CC123) |
| 6 | Move each of the eight controls in the Rack editor while the phrase loops | Wave: Sine/Saw/Square timbre; Gain: level; Attack/Decay/Sustain/Release: envelope; Cutoff/Resonance: filter brightness/peak |
| 7 | Track automation: *Show Used Automation* / add lane → parameter list → **Slot 1 Cutoff** (id `slot01.p007`) and **Slot 1 Wave** (`slot01.p001`). Write a Cutoff sweep, switch to Read | Sound follows the lane; the Rack knob moves with it |
| 8 | Transport stop and the Rack's Panic (if the note hangs, use Cubase *Reset*) | Sound stops immediately; next note plays normally |
| 9 | Set distinct values (e.g. Wave Square, Cutoff 1500 Hz, Release 1 s). Save the project, quit Cubase, reopen the project | Slot 1 still Vox Demo Synth on CH1; all eight values identical; the phrase sounds the same |
| 10 | Second instance: another Instrument track with Vox Electronic Engine, Demo Synth in its slot 1, different Wave | Both tracks play independently with their own settings |
| 11 | Quit Cubase. Move `vox music\instruments\VoxDemoSynth.vins` out of the folder (e.g. to the Desktop). Reopen the project | Rack loads; slot 1 still shows Vox Demo Synth (missing), silent; no crash. Save the project once |
| 12 | Quit Cubase, move the file back, reopen | Sound and all eight values return |
| 13 | Compare the Rack binary hash with step 1.3 | Unchanged |

Parameter identities: slot N, endpoint K is `slotNN.pKKK`; Demo Synth uses
`p001` Wave, `p002` Gain, `p003` Attack, `p004` Decay, `p005` Sustain,
`p006` Release, `p007` Cutoff, `p008` Resonance. Cubase may show the generic
name ("Slot 1 Param 007") until the instrument is bound in that slot.

If a new track shows instruments already loaded, Cubase restored something:
a project, a track/VST preset ("Default" preset), or an older Rack build
(Rack builds up to commit `9131f50` preloaded Bass and Acid in slots 1–2; a
layout with four instruments comes from a saved state or an even older
build). Check the hash in step 1.3 and start from a new empty project.

## 4. Uninstall

- **One instrument:** delete its `.vins` file from `vox music\instruments`.
- **Everything:** delete the `vox music` folder, and optionally
  `%LOCALAPPDATA%\Vox Music\Cache`.

## 5. Results (fill in)

| Date | Cubase version | Windows | CI run | Steps passed | Notes |
| --- | --- | --- | --- | --- | --- |
| | | | | | |
