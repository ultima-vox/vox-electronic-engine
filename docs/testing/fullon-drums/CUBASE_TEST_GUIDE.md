# Full-on Drums in Vox Rack — Cubase / REAPER test guide (Windows x64)

You install by copying files: no installer, scripts or environment variables.
CTest results do not count as DAW acceptance, so mark every step below as
unverified until it has been run in Cubase or REAPER. Section 8 is the
result matrix: mark each row PASS, FAIL or NOT RUN with what you observed.

The native `.vins` packages are signed with the CI run's development
publisher key, so a package loads only in the Rack from the same CI run.
`build-info.txt` names the key.

## 0. Downloads (one CI run, same commit)

| Artifact | Contents |
| --- | --- |
| `Vox-Music-Install-Windows` | `vox music\Vox Electronic Engine.vst3` and `vox music\instruments\VoxFullOnDrums.vins`, plus the Demo packages. This is the install layout |
| `Vox-FullOn-Drums-Cubase-Guide` | this guide, `fullon-drums-145bpm-4bar.mid` (one track), `fullon-drums-145bpm-8-tracks.mid` (eight named part tracks), `make_groove_midi.py`, `build-info.txt` |

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
Both), `p050` Swing, `p051` Accent, `p052`–`p059` lane on/off (pad order). The
pattern itself is Rack content (a fresh kit holds Full-on Basic), not a
parameter. Cubase note names may be one octave different depending
on its "MIDI note name" display preference; the numbers are authoritative.

Behaviour: one-shot voices (NoteOff and sustain are ignored), several pads
sound together (16 voices), a retriggered pad fades its previous hit in 3 ms,
CC120 stops at once, CC123 fades out within 50 ms, Rack Panic stops at once.

## 2. Install

1. Close Cubase. Remove any older `Vox Electronic Engine.vst3` from
   `C:\Program Files\Common Files\VST3\`.
2. Copy the folder `vox music` from `Vox-Music-Install-Windows` into
   `C:\Program Files\Common Files\VST3\`. Afterwards this file must exist:
   `...\VST3\vox music\instruments\VoxFullOnDrums.vins`.
3. Check the hash:
   `Get-FileHash "...\VST3\vox music\Vox Electronic Engine.vst3\Contents\x86_64-win\Vox Electronic Engine.vst3"`.
   It must equal "Rack SHA-256" in `build-info.txt` and must not change in
   any later step.
4. Start Cubase.

Uninstall: close the DAW and delete `C:\Program Files\Common Files\VST3\vox music\`.
Nothing else is written outside the project files (no registry entries,
installers or environment variables). A later build installs the same way;
do not mix `.vins` files from another CI run (their development key differs,
and the Rack refuses them).

Keep **Settings (gear) → MIDI source = Auto** for every step. That menu is
the Rack-wide generator switch for sequence instruments; *Internal
patterns* there drops host MIDI for **all** slots, including the kit. The
kit's own sequencer mode is `p049` Seq Mode.

## 3. Cubase procedure

| # | Action | Expected |
| --- | --- | --- |
| 1 | New Instrument track with Vox Electronic Engine (new project, no track preset); open the editor | Rack with **16 empty slots** |
| 2 | Click empty **slot 1** (or right-click → *Load / replace instrument...*) → **Full-on Drums / Ultima Vox** | Slot 1 = Full-on Drums on **CH1** (first free channel). The header shows `Slot 1 | CH1` and Channel **CH1**. The **SOUND** page shows ten sections (Kick … Sequencer) with all 59 kit and sequencer controls, each with its current value under it (no empty value boxes) |
| 3 | Track MIDI channel 1; import `fullon-drums-145bpm-4bar.mid` (set project tempo 145) and play | Kick on every beat, 16th closed hats, off-beat open hats, snare + clap on 2 and 4, rim accents, tom fill in bar 4; nothing hangs at stop |
| 4 | Play pads from a keyboard or the piano roll (notes 36–49 above) | Each pad sounds; harder velocity is louder; several notes together all sound |
| 5 | Open hat (46) then closed hat (42) | Closed hat cuts the open hat; switch **Hat Choke** off → open hat keeps ringing |
| 6 | Move each pad's Level/Tune/Decay/Pan/Character and the kick controls | Only that pad changes (e.g. Kick Tune lowers the kick only) |
| 7 | Automation lane on **Slot 1 Kick Tune** (`slot01.p002`) and **Slot 1 Open Hat Decay** (`slot01.p023`); write, then Read | Sound follows the lanes |
| 8 | Stop, Rack Panic | Silence at once |
| 9 | Change several controls, save, quit Cubase, reopen | Same slot, channel, values and groove |
| 10 | *Export → Audio Mixdown* of the 4 bars (offline), twice | Two identical files with the groove |
| 11 | Second instrument track with another Rack + Full-on Drums | Plays independently |
| 12 | Quit Cubase, move `vox music\instruments\VoxFullOnDrums.vins` out of the folder, reopen | Rack loads; slot 1 shows Full-on Drums missing and silent; save once |
| 13 | Quit, move the file back, reopen | Groove and values return |
| 14 | Compare the Rack hash with step 2.1 | Unchanged |
| 15 | Delete the MIDI clip. Set **Seq Mode = Internal** (the kit holds Full-on Basic), press Play at 145 BPM | The kit plays the pattern in time with Cubase; bar 4 has a tom fill |
| 16 | Loop bars 1–4, seek, change tempo, stop/start | Stays on the grid; downbeat replays on loop; no hits while stopped |
| 17 | Re-import the clip with Seq Mode = Internal | No doubled hits (incoming notes are ignored); Seq Mode = Both plays both |
| 18 | Swing 0.3, Accent 0, lane switches; on the **Pattern** page choose another factory pattern, click steps on/off, drag a step's velocity, shift-click an accent, set 8 steps / 2 bars | Audible changes as described; edits play from the next beat without restarting the bar |
| 18a | With slot 1 selected: **Settings (gear) → Export selected pattern MIDI...**, save the `.mid`. Import it into Cubase (*File → Import → MIDI File*), route its eight lane tracks to the Rack on the kit's channel (section 4.2), set Seq Mode = Host MIDI | The exported lanes play exactly the edited pattern of step 18 (same hits, velocities and accents; no doubled hits). Export is in the Settings menu, not on the Pattern page |
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
- A simultaneous closed and open hat: with Hat Choke on, the closed hat
  cuts the open hat, whichever track comes first. With Hat Choke off, both
  ring.
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
| 29 | Put a Closed Hat note on the same tick as an Open Hat note (separate tracks) | With Hat Choke on, the closed hat cuts the open hat (try both track orders). With Hat Choke off, both ring |

Not covered here: separate **audio** outputs per pad (stems) for the Cubase
mixer. Eight MIDI tracks do not create separate VST3 audio returns. That is
a separate future design.

## 5. Rack features with the kit (Cubase)

**Starting state for steps 30–36** (restore it if a step changed it):
- slot 1 = Full-on Drums on **CH1**, Seq Mode = Host MIDI, factory pattern
  Full-on Basic; MIDI track "Kit" with `fullon-drums-145bpm-4bar.mid`, output
  to the Rack, channel 1;
- slot 2 = **Vox Demo Synth** on **CH2**; MIDI track "Chord" with a sustained
  chord over the 4 bars, output to the same Rack, channel 2;
- Settings → MIDI source = Auto; tempo 145; loop bars 1–4.

Steps 30–36 all act on the kit in **slot 1**. Only step 37 moves it.

| # | Action | Expected |
| --- | --- | --- |
| 30 | Settings → *Rack audio routing / sends / ducking...*: **Kick event source** = slot 1 (Full-on Drums), Note 36, Attack 1 ms, Hold 40 ms, Release 120 ms; **Duck depth** of slot 2 = 0.8. Play | The chord dips on every kick, starting with the kick itself (no lag) |
| 31 | Mute the "Kit" track in Cubase; set slot 1 Seq Mode = **Internal**; play | The chord still dips on every internal kick |
| 32 | Unmute the "Kit" track; keep Seq Mode = Internal; play | The chord dips only on the kicks you hear (the ignored host notes do not duck). Then set Seq Mode = Host MIDI again |
| 33 | Right-click slot 1 → **Mute**; play; then unmute | Muted: the kit is silent and the chord no longer dips. Unmuted: both return |
| 34 | Right-click slot 2 → **Solo**; play; then un-solo | Solo: only the chord plays (the kit and its sends are silent). Un-solo: all return |
| 35 | Slot 1 **Return 1** send = 0.5 (Return 1 = Delay, ON); play, then stop the transport; set the send back to 0 | Delay echoes of the kit. At stop the kit's own voices stop at once (the Rack resets its instruments on stop), while the Return 1 echoes ring out and fade. Panic silences both |
| 36 | Edit the slot 1 pattern (step 18) and set Seq Mode = Internal; **Settings → Export selected pattern MIDI...**; import the file, route its eight lane tracks to the Rack on channel 1 (section 4.2), mute the "Kit" track; bounce the Internal version offline (*Export → Audio Mixdown*); then Seq Mode = Host MIDI and bounce again | The two bounces sound the same (same hits, velocities, accents). Afterwards delete the imported lane tracks and unmute "Kit" |

**Step 37 changes the slot order** and is the last Rack step. Start from the
state above.

| # | Action | Expected |
| --- | --- | --- |
| 37a | While playing, right-click slot 1 → **Unload instrument** | The kit stops; nothing hangs; the chord keeps playing. Slot 1 shows Empty and OFF (its channel is released) |
| 37b | Load Full-on Drums into **slot 3** | The kit takes the first free channel, **CH1** (the header shows `Slot 3 | CH1`), so the "Kit" track on channel 1 plays it with no routing change. The kit starts from its defaults (Seq Mode = Host MIDI) |
| 37c | Routing window: Kick event source = **slot 3**; slot 2 Duck depth still 0.8; play | The chord dips on every kick exactly as in step 30: ducking does not depend on the kit sitting before or after the chord slot |
| 37d | Optional: unload slot 3 and load the kit into slot 1 again, source = slot 1 | Back to the starting state |

## 6. REAPER (same build, same files)

REAPER scans `C:\Program Files\Common Files\VST3\` by default
(*Options → Preferences → Plug-ins → VST*; *Re-scan* if the Rack does not
appear).

| # | Action | Expected |
| --- | --- | --- |
| R1 | New track → *FX* → **VST3i: Vox Electronic Engine (Ultima Vox)** | Rack with 16 empty slots |
| R2 | Load Full-on Drums in slot 1; insert `fullon-drums-145bpm-4bar.mid` on the track (tempo 145); play | The groove plays (track MIDI goes to the Rack on channel 1) |
| R3 | Eight-track layout: insert `fullon-drums-145bpm-8-tracks.mid` as separate tracks; on each, remove its own FX and add a **send** (Routing → *MIDI: All → channel 1*, audio send off) to the Rack track | The eight tracks play the full groove into the one kit |
| R4 | Steps 5, 6, 15–19 and 30–37 above, REAPER equivalents (same starting state) | As described there |
| R5 | Automation: track envelope for **Slot 1 Kick Tune** and **Slot 1 Open Hat Decay** (*Envelopes* button); write, then Read | Sound follows the envelopes; the parameter names and values show (e.g. `Slot 1 Kick Tune`, a value in semitones) |
| R6 | *File → Render* (offline) twice; save, close, reopen | Identical renders; same slot, channel, values and pattern |
| R7 | Move `VoxFullOnDrums.vins` out, reopen; move it back, reopen | As steps 12–13 |

## 7. Not in this milestone

Separate audio outputs per pad, a CPU / profiling view and a MIDI inspector
are not part of this build. The `.vins` packages are **development-signed**
(owner test only); a production build needs the owner's offline release key.

## 8. Results (fill in)

| Date | DAW + version | Windows | CI run | Rack SHA-256 | Notes |
| --- | --- | --- | --- | --- | --- |
| | | | | | |

| Steps | What it checks | Cubase | REAPER | Observed / log |
| --- | --- | --- | --- | --- |
| 1–2 | Scan, `.vins` discovery and trust, 16 empty slots, SOUND page values, header channel | NOT RUN | NOT RUN | |
| 3–5, 20–29 | Playable kit, 8 pads, multi-track input, choke | NOT RUN | NOT RUN | |
| 6–7 | 59 controls, host automation | NOT RUN | NOT RUN | |
| 8, 26 | Stop, Panic, CC120 / CC123 | NOT RUN | NOT RUN | |
| 9, 19, 28 | Save / reopen | NOT RUN | NOT RUN | |
| 10 | Offline bounce, repeatable | NOT RUN | NOT RUN | |
| 12–14 | Missing / reinstalled package | NOT RUN | NOT RUN | |
| 15–18 | Internal pattern, transport, edits while playing | NOT RUN | NOT RUN | |
| 18a, 36 | MIDI export → reimport parity | NOT RUN | NOT RUN | |
| 30–32, 37c | Same-block kick ducking, Host / Internal, slot order | NOT RUN | NOT RUN | |
| 33–35, 37a–b | Mute / solo, FX send tails, unload / reload | NOT RUN | NOT RUN | |
