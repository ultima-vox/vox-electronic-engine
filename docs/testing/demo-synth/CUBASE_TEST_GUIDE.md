# Vox Demo Synth in Vox Rack — Cubase test guide (Windows x64)

Development mode only: unsigned module, environment-variable package root, no
installer. CTest results do not count as DAW acceptance. Record the result of
every step in the table at the end.

## 0. Downloads (one CI run, same commit)

| Artifact | Contents |
| --- | --- |
| `Vox-Electronic-Engine-Windows` | `VstEngine_artefacts/Release/VST3/Vox Electronic Engine.vst3` (Rack) |
| `Vox-Demo-Synth-Package-Windows` | `com.ultimavox.demo-synth.voxinst/1.0.0/{manifest.json, module/x64/VoxDemoSynth.dll}` |
| `Vox-Demo-Synth-Cubase-Guide` | this guide, `install-demo-package.ps1`, `vox-demo-synth-test-phrase.mid`, `build-info.txt` (commit, Rack and module SHA-256) |

## 1. Install the Rack (once)

1. Close Cubase.
2. Copy the folder `Vox Electronic Engine.vst3` to
   `C:\Program Files\Common Files\VST3\` (replace an older copy).
3. Note the Rack binary hash:
   `Get-FileHash "C:\Program Files\Common Files\VST3\Vox Electronic Engine.vst3\Contents\x86_64-win\Vox Electronic Engine.vst3"`
   It must equal the "Rack SHA-256" in `build-info.txt` and must not change in
   any later step.

## 2. Install the Demo Synth package

1. Extract `Vox-Demo-Synth-Package-Windows` to a folder, for example
   `C:\VoxDev\download`.
2. Run in PowerShell:
   ```
   powershell -ExecutionPolicy Bypass -File install-demo-package.ps1 -PackageZipDir C:\VoxDev\download
   ```
   This copies the package to
   `C:\VoxDev\instruments\com.ultimavox.demo-synth.voxinst\1.0.0\`, checks the
   DLL SHA-256 against the manifest and sets the user environment variable
   `VOX_DEV_INSTRUMENT_ROOT=C:\VoxDev\instruments`.
3. Start Cubase **after** this (from the Start menu). If the instrument does
   not appear in step 3, sign out and in once so Cubase inherits the variable.

Manual alternative: copy the folder yourself and set the variable in
*System Properties → Environment Variables → User variables*.

## 3. Cubase procedure

| # | Action | Expected |
| --- | --- | --- |
| 1 | *Studio → VST Plug-in Manager* | "Vox Electronic Engine" listed, no error |
| 2 | Add an Instrument track with Vox Electronic Engine; open its editor | Rack shows 16 slots; slots 1–2 hold the built-in instruments |
| 3 | Right-click **slot 1** → *Load / replace instrument...* → **Vox Demo Synth / Ultima Vox** | Slot 1 shows Vox Demo Synth on **CH1**; the Macros page shows Wave, Gain, Attack, Decay, Sustain, Release, Cutoff, Resonance |
| 4 | Set the track's MIDI channel to 1 (or Any with a channel-1 keyboard); play a few keys | Audible saw tone; pitch follows the keys; louder with harder velocity; key up starts a short release |
| 5 | Import `vox-demo-synth-test-phrase.mid` onto the track (channel 1, 120 BPM) and play | Bar 1: four rising notes, rising velocity. Bar 2: A3, B3 on top, back to A3 when B3 ends. Bar 3: short E3 held by sustain pedal until beat 4. Bar 4: G2 ends with a normal release on beat 3 (CC123) |
| 6 | Move each of the eight controls in the Rack editor while the phrase loops | Wave: Sine/Saw/Square timbre; Gain: level; Attack/Decay/Sustain/Release: envelope; Cutoff/Resonance: filter brightness/peak |
| 7 | Track automation: *Show Used Automation* / add lane → parameter list → **Slot 1 Cutoff** (id `slot01.p007`) and **Slot 1 Wave** (`slot01.p001`). Write a Cutoff sweep, switch to Read | Sound follows the lane; the Rack knob moves with it |
| 8 | Transport stop and the Rack's Panic (if the note hangs, use Cubase *Reset*) | Sound stops immediately; next note plays normally |
| 9 | Set distinct values (e.g. Wave Square, Cutoff 1500 Hz, Release 1 s). Save the project, quit Cubase, reopen the project | Slot 1 still Vox Demo Synth on CH1; all eight values identical; the phrase sounds the same |
| 10 | Second instance: another Instrument track with Vox Electronic Engine, Demo Synth in its slot 1, different Wave | Both tracks play independently with their own settings |
| 11 | Quit Cubase. Rename `C:\VoxDev\instruments\com.ultimavox.demo-synth.voxinst` to `...voxinst.off`. Reopen the project | Rack loads; slot 1 still shows Vox Demo Synth (missing), silent; no crash. Save the project once |
| 12 | Quit Cubase, rename the folder back, reopen | Sound and all eight values return |
| 13 | Compare the Rack binary hash with step 1.3 | Unchanged |

Parameter identities: slot N, endpoint K is `slotNN.pKKK`; Demo Synth uses
`p001` Wave, `p002` Gain, `p003` Attack, `p004` Decay, `p005` Sustain,
`p006` Release, `p007` Cutoff, `p008` Resonance. Cubase may show the generic
name ("Slot 1 Param 007") until the instrument is bound in that slot.

## 4. Uninstall

Delete `C:\VoxDev\instruments\com.ultimavox.demo-synth.voxinst` and remove the
user variable `VOX_DEV_INSTRUMENT_ROOT`.

## 5. Results (fill in)

| Date | Cubase version | Windows | CI run | Steps passed | Notes |
| --- | --- | --- | --- | --- | --- |
| | | | | | |
