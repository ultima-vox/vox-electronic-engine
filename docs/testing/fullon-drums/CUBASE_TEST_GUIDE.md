# Full-on Drums (D1) in Vox Rack — Cubase test guide (Windows x64)

Development mode only: unsigned module, environment-variable package root,
no installer. CTest results are not DAW acceptance; mark every step below
unverified until run in Cubase.

## 0. Downloads (one CI run, same commit)

| Artifact | Contents |
| --- | --- |
| `Vox-Electronic-Engine-Windows` | `VstEngine_artefacts/Release/VST3/Vox Electronic Engine.vst3` (Rack) |
| `Vox-FullOn-Drums-Package-Windows` | `com.ultimavox.fullon-drums.voxinst/0.1.0/{manifest.json, module/x64/VoxFullOnDrums.dll}` |
| `Vox-FullOn-Drums-Cubase-Guide` | this guide, `install-drums-package.ps1`, `fullon-drums-145bpm-4bar.mid` (one track), `fullon-drums-145bpm-8-tracks.mid` (eight named part tracks), `make_groove_midi.py`, `build-info.txt` |

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
(closed hat cuts open hat), `p048` Velocity Sens. Sequencer: `p049` Seq Mode (Host MIDI / Internal /
Both), `p050` Seq Pattern, `p051` Swing, `p052` Accent, `p053` Fill, `p054`
Bars, `p055`–`p062` lane on/off (pad order), `p063` Steps. Cubase note names may be one octave different depending
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
| 1 | New Instrument track with Vox Electronic Engine (new project, no track preset); open the editor | Rack with **16 empty slots** |
| 2 | Click empty **slot 1** (or right-click → *Load / replace instrument...*) → **Full-on Drums / Ultima Vox** | Slot 1 = Full-on Drums on **CH1** (first free channel); the Macros page lists the 63 kit and sequencer controls |
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
| 15 | Delete the MIDI clip. Set **Seq Mode = Internal**, Seq Pattern = Full-on Basic, press Play at 145 BPM | The kit plays the pattern in time with Cubase; bar 4 has a tom fill |
| 16 | Loop bars 1–4, seek, change tempo, stop/start | Stays on the grid; downbeat replays on loop; no hits while stopped |
| 17 | Re-import the clip with Seq Mode = Internal | No doubled hits (incoming notes are ignored); Seq Mode = Both plays both |
| 18 | Swing 0.3, Accent 0, lane switches, Steps 8, other patterns | Audible changes as described |
| 19 | Save with Seq Mode = Internal, reopen | Same pattern, no duplicate beats |

## 4. One kit, eight MIDI tracks (Kick, Snare, Hats ... on separate tracks)

To edit each drum part on its own Cubase MIDI track, keep **one** Rack and
**one** Full-on Drums slot. Several MIDI tracks can send to the same plugin
instance on the same MIDI channel, and each track holds only its own pad's
note. This needs no extra Rack instance, no extra MIDI channel and no change
to the Rack rule that one slot owns one channel.

How the kit treats the merged input:
- Hits from different tracks that land on the same sample give the same
  result whatever order Cubase merges them in.
- The same pad on two tracks at the same moment plays once, at the louder
  velocity.
- A simultaneous closed and open hat leaves the open hat ringing.
- An all-off (CC120/123) at the same moment as a note stops what was already
  sounding, and the new note then plays.

### 4.1 Track layout

| Track | Pad | MIDI note | MIDI channel |
| --- | --- | --- | --- |
| Kick | Kick | 36 | the kit slot's channel (CH1 on a fresh Rack) |
| Rim | Rim / perc | 37 | same |
| Snare | Snare | 38 | same |
| Clap | Clap | 39 | same |
| Closed Hat | Closed hat | 42 | same |
| Tom | Tom | 45 | same |
| Open Hat | Open hat | 46 | same |
| Cymbal | Cymbal | 49 | same |

### 4.2 Setup steps

1. Load the Rack once: one Instrument track with Vox Electronic Engine,
   Full-on Drums in slot 1 (it takes **CH1**).
2. Set **Seq Mode = Host MIDI** (`p049`). This is the default. Internal
   would ignore the tracks, and Both would add the internal pattern.
3. Import `fullon-drums-145bpm-8-tracks.mid` (*File → Import → MIDI File*)
   and set the project tempo to 145. It is a type-1 file with eight named
   tracks. Each track is four bars on channel 1 with its own note.
   **Importing does not route the tracks to the Rack.** Cubase may create
   MIDI tracks that point at no instrument or at another one.
4. For each of the eight MIDI tracks, in the Inspector:
   - set **Output Routing** to the existing *Vox Electronic Engine*
     instance (the same one for all eight);
   - set **Channel** to **1** (the kit slot's channel).

   Select the eight tracks and hold the Cubase "apply to selected tracks"
   modifier to set them in one go (Alt+Shift on Windows). If that does not
   work in your version, set each track by hand.
5. Play. The result must equal the one-track groove,
   `fullon-drums-145bpm-4bar.mid`.
6. Save the arrangement as a **project template** (*File → Save as
   Template*), so the routing is done once and not in every project.

Menu names can differ slightly between Cubase versions. The routing itself
(eight MIDI tracks into one VST instrument input) is what this guide checks,
and it is **unverified until run in Cubase**.

### 4.3 Optional Cubase Drum Map (one Drum Editor view)

If you prefer one Drum Editor lane per pad on a single track:
1. Open *MIDI → Drum Map Setup*.
2. Create a new map with eight rows using the names and note numbers from
   4.1, with the same input and output note.
3. Assign the map to the track in the Inspector (*Drum Map*).

No `.drm` file is shipped: its format is not verified here, so set the map up
by hand as above.

### 4.4 Procedure

| # | Action | Expected |
| --- | --- | --- |
| 20 | Steps 4.2.1–4.2.5 | The eight tracks route to the one Rack; the kit plays the full groove; no doubled hits |
| 21 | Mute **Open Hat**, then solo **Kick** | Only the open hat disappears; then only the kick plays |
| 22 | Move the Snare track's notes a few ticks earlier and later | Only the snare moves |
| 23 | Copy the Kick part to a 9th track on the same channel | The kick does not get louder or flam (same-moment duplicate plays once) |
| 24 | Set one track's channel to 2 | That track's part stops (CH2 is not the kit's channel); back to 1 → it returns |
| 25 | Loop bars 1–4 and play several passes | Every pass is the same; nothing missing or doubled at the loop point |
| 26 | Stop with notes sounding; Rack Panic; CC120 or CC123 from a track | Silence as in steps 3.8 and 3.4 |
| 27 | Seq Mode = Internal with the eight tracks still playing | Only the internal pattern plays (tracks ignored); back to Host MIDI → the tracks play |
| 28 | Save, close, reopen the project (or the template) | Same eight tracks, same routing, same sound |

Not covered here: separate **audio** outputs per pad (stems) for the Cubase
mixer. Eight MIDI tracks do not create separate VST3 audio returns. That is
a separate future design.

## 5. Results (fill in)

| Date | Cubase version | Windows | CI run | Steps passed (1–28) | Notes |
| --- | --- | --- | --- | --- | --- |
| | | | | | |
