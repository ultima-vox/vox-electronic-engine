# VOX Drums — final DAW acceptance checklist (owner)

One pass, at the end. Hosts: Cubase 12–15 and REAPER 7.x, Windows x64, the VST3 from
the CI artifact of the PR's final HEAD (check the SHA-256 in the artifact manifest).
Record host version, sample rate, buffer size and the result of each line.
**DRM-08 stays on HOLD until every line is accepted.**

## A. Load and identity
- [ ] The plugin scans and loads as "VOX Drums" (instrument). No crash on insert, remove, duplicate track.
- [ ] A project saved with the previous build (DRM-04b) opens: same sound, same patterns, Pattern mode, automation still attached.

## B. Outputs
- [ ] The host offers Main and Out 2 … Out 9 (9 stereo outputs). Cubase: "Activate Outputs"; REAPER: build multichannel routing.
- [ ] With only Main active, every pad sounds on Main whatever its output setting; ROUTING shows "plays on Main".
- [ ] Activate Out 2–9. Route each pad to each output in turn: it appears on that channel only, not on Main.
- [ ] Deactivate an output while playing: no click burst, no crash; the pad returns to Main.
- [ ] Sends: a pad on Out 3 with Send 1/2 up: dry on Out 3, delay and reverb on Main.

## C. Mixer
- [ ] Level, pan, mute, solo of several pads behave as labelled; meters move with the sound and fall to zero in silence.
- [ ] Delay time follows the host tempo; feedback, tone, return, reverb size, damp, return are audible.
- [ ] Stop the transport during a long reverb: the tail ends naturally. Host "panic"/All Sound Off silences it at once.

## D. Sequencer and Song mode
- [ ] Pattern mode: Internal and Both play in sync with the host; Host MIDI mode plays only incoming notes.
- [ ] Song mode: a 16-, 32- and 64-bar song follows the host position; the timeline playhead matches the host bar.
- [ ] Seek, cycle/loop in the host, tempo change and tempo automation: playback stays on the grid, no stuck or doubled notes.
- [ ] Loop region in the plugin: plays the region; turning it off returns to the song.
- [ ] Edit while playing: place, delete, Make Unique, edit a bar in the grid, undo/redo: no dropouts.
- [ ] Switch Pattern/Song while playing.

## E. MIDI export
- [ ] Scope "Pattern": click saves a file; drag drops a clip on a MIDI/instrument track. 1–4 bars, correct length.
- [ ] Scope "Entire Song" at 16, 32, 64 bars: click and drag both give one clip of the full length; the last bar's notes are present; notes are editable; channel 10.
- [ ] Play the dropped clip into VOX Drums in **Host MIDI** mode: it sounds like the internal song, without doubled hits.
- [ ] Two drags in a row, and two plugin instances dragging: each clip is the right one.

## F. Generator
- [ ] Each style (Full-on/Psy, Goa, House, Techno, Trap, Breaks, Ambient) gives a recognisably different groove.
- [ ] Generate Pattern, Generate Song, Mutate Lanes, Fill, Variation A>B each change what plays; Undo restores.
- [ ] Locked steps and locked lanes survive every Generate.

## G. Presets and content
- [ ] The three factory kits load and sound distinct; their patterns and song load.
- [ ] Save and load a kit, a pad (onto another pad), a pattern, a song; Undo Load.
- [ ] A kit with user samples: export a portable kit, import it on a machine (or after moving the files): samples play.
- [ ] A kit whose sample file was deleted: loads, the pad shows the sample as missing, nothing else changes.
- [ ] Loading a broken or foreign file reports an error and changes nothing.

## H. State and automation
- [ ] Save the project, close the host, reopen: song, unique bars, generator settings and seed, outputs, mixer, page shown are as saved.
- [ ] Automate pad level, a send, solo, delay feedback: recorded with gestures, plays back, survives reopen.
- [ ] Host preset save/load and track freeze/render: the render equals realtime playback.

## I. Editor
- [ ] All eight pages at 100, 125, 150 and 200 % display scaling: nothing clipped or overlapping; resize to the minimum and a large size.
- [ ] Open and close the editor during playback: audio continues unchanged.

## J. Performance (owner's machine)
- [ ] A 64-bar song with all pads, sends and outputs at 44.1/48/96 kHz and buffers 64/128/512: no dropouts at the settings the owner uses; note the host's performance meter.

## K. Sound
- [ ] Sound quality of kits, delay and reverb is acceptable for release (subjective; owner's call).
