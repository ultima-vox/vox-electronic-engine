# VOX Drums — final DAW acceptance checklist (owner)

One pass, at the end. Hosts: Cubase 12–15 and REAPER 7.x, Windows x64, the VST3 from
the CI artifact of the final integrated release-candidate HEAD (check the SHA-256
in the artifact manifest).
Record host version, sample rate, buffer size and the result of each line.
**DRM-08 stays on HOLD until every line is accepted.**

## Candidate and evidence index

**Candidate:** the future completed release candidate staged through
`integration/vox-drums-1.0` ([integration dashboard #141](https://github.com/ultima-vox/vox-electronic-engine/pull/141)).
The present development HEAD is **not** automatically that candidate. Do not use an
old constituent PR artifact or a current unfinished SHA as final acceptance evidence.
After the complete product passes qualification, record the exact integrated commit,
Windows build/suite run URLs, artifact ID and SHA-256 in the release-candidate report.
Use the `VOX Drums.vst3` bundle from that exact commit's `VOX-Suite-VST3` artifact;
`manifest.txt` must identify the commit, run, environment and SHA-256 of every file.
Install only that bundle; check `VOX Drums.vst3\Contents\x86_64-win\VOX Drums.vst3`
against the manifest before testing.

**Evidence record (fill before the single owner DAW pass):** candidate SHA; Windows
build and suite run URLs/results/test counts; validator reports; artifact ID and
manifest hashes; factory content/licence and listening sign-off; 96 kHz RT report.
Record Cubase/REAPER versions, Windows/DPI, sample rates, buffer sizes and outcomes
against this same artifact. Do not start an intermediate manual acceptance round.

The table below specifies **required evidence**, not tests claimed to have passed.
Tests are in `tests/suite/`; run them on the final candidate and attach the actual
results. Existing interim editor tests do not prove the new workspace is implemented.

| Section | Required automated evidence on final candidate | Left to the manual pass |
|---|---|---|
| A Load | Steinberg validator; `suite_vst3_host_tests`; product identity and transactional state tests | real host scan and reopen of the candidate's host project |
| B Outputs | `DrumsCandidateTests` `testOutputs`: 9 buses, each alone, disabled bus on Main bit-identically, sparse layouts, oversized blocks | the host's own output activation and routing UI |
| C Mixer | `testOutputs`: level, pan, mute, solo, meters, sends, returns, tails, All Sound Off; `testReviewFindings`: attack after unmute, no echo after All Sound Off, reported tail | how it sounds; host panic |
| D Song | `DrumsKitEngineTests` `testSongScheduler`, `testSongLoopRegion`; `testSongParity`: 64 bars at 44.1/48/96 kHz x 64/333/512, tempo changes, seek, loop, stop, mode switch | host transport, cycle, tempo track |
| E MIDI export | `testSongParity`, `testExport`: playback equals the file within 1 tick, click and drag byte-identical, End of Track at 16/32/64 bars | drop onto a host track; the host's import |
| F Generator | style engine/registry and generator regressions for all 23 factory styles, reproducibility, locks, undo; per-style musical rules and listening-pack manifest | recognisable genre, musical usefulness and variation |
| G Presets | preset/factory regressions: Kit, Pattern and Song content for all 23 styles, roles, Undo Load, malformed files, missing samples, portability and factory hashes | file dialogs, paired genre kits/songs and a second machine |
| H State | `testState`, `testReviewFindings`: round trip, malformed sections, unknown sections kept or the restore refused, history ends at a restore | host project save/reopen, automation lanes, freeze |
| I Editor | native dockable JUCE workspace [#127](https://github.com/ultima-vox/vox-electronic-engine/issues/127): layout/state, SELECT vs AUDITION, real bindings/telemetry, gestures and DPI captures; new workspace tests required | actual host scaling, controls, inspector, mixer and drag-to-DAW |
| J Performance | allocation probe (0 allocations); `suite_drums_callback_bench` table in the CI log and artifact | dropouts on the owner's machine and buffer |
| K Sound | none | entirely manual |

**Content release gate:** verify per-file redistribution rights/provenance for every
bundled sample and complete genre listening sign-off. [FACTORY_CONTENT.md](FACTORY_CONTENT.md)
currently marks electronic content listening pending and Rock/Metal synth sketches
**NOT RELEASE-READY**: licensed acoustic content must be sourced and verified before
release. A generated listening pack or green CI is not a listening verdict. Gain tests
measure sample peaks only for their finite factory matrix; they do not establish true
peak, arbitrary user-kit safety or sound quality.

## A. Load and identity
- [ ] The plugin scans and loads as "VOX Drums" (instrument). No crash on insert, remove, duplicate track.
- [ ] Save and reopen a host project made with this final candidate: same sound, patterns, Song/Pattern mode and attached automation. Unpublished development-build compatibility is not a release requirement.

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
- [ ] Pattern export and its explicit drag handle: save a file and drop a clip on a MIDI/instrument track. 1–4 bars, correct length. A click/select does not start an accidental drag.
- [ ] Entire Song export at 16, 32, 64 bars: file export and the Song drag handle both give one clip of the full length; the last bar's notes are present; notes are editable; channel 10.
- [ ] Play the dropped clip into VOX Drums in **Host MIDI** mode: it sounds like the internal song, without doubled hits.
- [ ] Two drags in a row, and two plugin instances dragging: each clip is the right one.

## F. Generator
- [ ] All **23 factory styles** in the current style registry have recognisable, musically useful genre rules; verify the complete registry, including Trance variants, Rock and Metal, rather than only the original seven.
- [ ] Each style's factory Pattern and 16/32/64-bar generated Song use the expected roles and paired genre kit; variations/fills remain musically coherent. Record listening verdicts rather than inferring them from MIDI or gain tests.
- [ ] Generate Pattern, Generate Song, Mutate Lanes, Fill, Variation A>B each change what plays; Undo restores.
- [ ] Locked steps and locked lanes survive every Generate.

## G. Presets and content
- [ ] Factory **Kit, Pattern and Song** content covers all 23 styles and loads coherently with the correct genre kit and roles; original and genre kits sound appropriate and distinct.
- [ ] Every bundled sample has verified redistribution rights and provenance. Rock/Metal licensed acoustic content and electronic-genre listening are signed off; synth sketches or "listening pending" entries do not pass.
- [ ] Save and load a kit, a pad (onto another pad), a pattern, a song; Undo Load.
- [ ] A kit with user samples: export a portable kit, import it on a machine (or after moving the files): samples play.
- [ ] A kit whose sample file was deleted: loads, the pad shows the sample as missing, nothing else changes.
- [ ] Loading a broken or foreign file reports an error and changes nothing.

## H. State and automation
- [ ] Save the project, close the host, reopen: song, unique bars, generator settings and seed, outputs, mixer, selected objects and committed workspace layout are as saved. User workspaces and project layout remain distinct.
- [ ] Automate pad level, a send, solo, delay feedback: recorded with gestures, plays back, survives reopen.
- [ ] Host preset save/load and track freeze/render: the render equals realtime playback.

## I. Native dockable JUCE workspace — issue #127
- [ ] Genuine native JUCE workspace with the approved custom hardware-inspired finish; no shipping HTML/WebView or unchanged stock skin. The former editor is not accepted as the release UI.
- [ ] At 1040×700, 1280×720, 1440×900 and 1600×1000 logical sizes and Windows 100/125/150/200 % scaling: readable hit targets, no clipped grid or overlapping controls; constrained layouts degrade predictably.
- [ ] One Pad Rack switches 4×4 grid/list; **SELECT does not audition**. Explicit audition triggers the intended pad without changing unrelated selection/state.
- [ ] Supported side panels dock, tab, resize, hide and restore; one or two central editors split/maximise safely; named workspaces and Reset Workspace restore the expected layout.
- [ ] Exactly one bottom mixer, with collapsed/compact/expanded modes, resizes the central workspace rather than covering it; real meters match sound and fall to zero in silence.
- [ ] Inspector follows the canonical pad/step/Song context, pins the specific object correctly and refreshes after restore. Visible controls and host automation stay in sync with real processor state.
- [ ] In the actual host, verify Tab and Shift-Tab delivery, focus visibility and scrolling without stealing transport shortcuts.
- [ ] In the actual host, pattern/Song drag handles produce editable MIDI without accidental audition, pattern edits or duplicate notes; verify the single-click vs drag gesture and two instances.
- [ ] Open and close the editor during playback: audio continues unchanged.

## J. Performance (owner's machine)
- [ ] A 64-bar song with all pads, sends and outputs at 44.1/48/96 kHz and buffers 64/128/512: no dropouts at the settings the owner uses; note the host's performance meter.

## K. Sound
- [ ] Sound quality of kits, delay and reverb is acceptable for release (subjective; owner's call).
