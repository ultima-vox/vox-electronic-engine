# Full-on Drums (D1) in Vox Rack — Cubase test guide (Windows x64)

Development mode only: unsigned module, environment-variable package root,
no installer. CTest results are not DAW acceptance; mark every step below
unverified until run in Cubase.

## 0. Downloads (one CI run, same commit)

| Artifact | Contents |
| --- | --- |
| `Vox-Electronic-Engine-Windows` | `VstEngine_artefacts/Release/VST3/Vox Electronic Engine.vst3` (Rack) |
| `Vox-FullOn-Drums-Package-Windows` | `com.ultimavox.fullon-drums.voxinst/0.1.0/{manifest.json, module/x64/VoxFullOnDrums.dll}` |
| `Vox-FullOn-Drums-Cubase-Guide` | this guide, `install-drums-package.ps1`, `fullon-drums-145bpm-4bar.mid`, `build-info.txt` |

## 1. Pad map (one Rack slot, one MIDI channel, eight pads inside the kit)

| Pad | Sound | MIDI note | Endpoints (slot N) | Character control |
| --- | --- | --- | --- | --- |
| 1 | Kick | 36 (C1) | `pNNN` 001–005 | Click |
| 2 | Snare | 38 (D1) | 006–010 | Snappy (noise vs. tone) |
| 3 | Clap | 39 (D#1) | 011–015 | Tone (band centre) |
| 4 | Closed hat | 42 (F#1) | 016–020 | Tone (metal vs. noise, brightness) |
| 5 | Open hat | 46 (A#1) | 021–025 | Tone |
| 6 | Tom | 45 (A1) | 026–030 | Sweep (pitch drop) |
| 7 | Rim / perc | 37 (C#1) | 031–035 | Tone (tone vs. click) |
| 8 | Cymbal | 49 (C#2) | 036–040 | Tone |

Per pad, in order: Level, Tune (±12 st), Decay (T60, seconds), Pan, Character.
Kit controls: `p041` Kit Volume, `p042` Kick Pitch Env, `p043` Kick Pitch
Decay, `p044` Kick Drive, `p045` Kick Sub, `p046` Kick Tail, `p047` Hat Choke
(closed hat cuts open hat), `p048` Velocity Sens. `p049`–`p064` are reserved
for the D2 sequencer. Cubase note names may be one octave different depending
on its "MIDI note name" display preference; the numbers are authoritative.

Behaviour: one-shot voices (NoteOff and sustain are ignored), several pads
sound together (16 voices), a retriggered pad fades its previous hit in 3 ms,
CC120 stops at once, CC123 fades out within 50 ms, Rack Panic stops at once.

## 2. Install

1. Close Cubase. Copy `Vox Electronic Engine.vst3` to
   `C:\Program Files\Common Files\VST3\` and note its hash
   (`Get-FileHash "...\Contents\x86_64-win\Vox Electronic Engine.vst3"`); it
   must equal "Rack SHA-256" in `build-info.txt` and never change below.
2. Extract `Vox-FullOn-Drums-Package-Windows` to e.g. `C:\VoxDev\download`.
3. `powershell -ExecutionPolicy Bypass -File install-drums-package.ps1 -PackageZipDir C:\VoxDev\download`
   copies the package to
   `C:\VoxDev\instruments\com.ultimavox.fullon-drums.voxinst\0.1.0\`, checks
   its SHA-256 and sets the user variable
   `VOX_DEV_INSTRUMENT_ROOT=C:\VoxDev\instruments` (the Demo Synth package can
   live in the same root).
4. Start Cubase from the Start menu (sign out/in once if the kit is missing).

## 3. Cubase procedure

| # | Action | Expected |
| --- | --- | --- |
| 1 | Instrument track with Vox Electronic Engine; open the editor | Rack with 16 slots |
| 2 | Right-click **slot 1** → *Load / replace instrument...* → **Full-on Drums / Ultima Vox** | Slot 1 = Full-on Drums on **CH1**; the Macros page lists the 48 kit controls |
| 3 | Track MIDI channel 1; import `fullon-drums-145bpm-4bar.mid` (set project tempo 145) and play | Kick on every beat, 16th closed hats, off-beat open hats, snare + clap on 2 and 4, rim accents, tom fill in bar 4; nothing hangs at stop |
| 4 | Play pads from a keyboard or the piano roll (notes 36–49 above) | Each pad sounds; harder velocity is louder; several notes together all sound |
| 5 | Open hat (46) then closed hat (42) | Closed hat cuts the open hat; switch **Hat Choke** off → open hat keeps ringing |
| 6 | Move each pad's Level/Tune/Decay/Pan/Character and the kick controls | Only that pad changes (e.g. Kick Tune lowers the kick only) |
| 7 | Automation lane on **Slot 1 Kick Tune** (`slot01.p002`) and **Slot 1 Open Hat Decay** (`slot01.p023`); write, then Read | Sound follows the lanes |
| 8 | Stop, Rack Panic | Silence at once |
| 9 | Change several controls, save, quit Cubase, reopen | Same slot, channel, values and groove |
| 10 | *Export → Audio Mixdown* of the 4 bars (offline), twice | Two identical files with the groove |
| 11 | Second instrument track with another Rack + Full-on Drums | Plays independently |
| 12 | Quit Cubase, rename `...\com.ultimavox.fullon-drums.voxinst` to `.off`, reopen | Rack loads; slot 1 shows Full-on Drums missing and silent; save once |
| 13 | Quit, rename back, reopen | Groove and values return |
| 14 | Compare the Rack hash with step 2.1 | Unchanged |

## 4. Results (fill in)

| Date | Cubase version | Windows | CI run | Steps passed | Notes |
| --- | --- | --- | --- | --- | --- |
| | | | | | |
