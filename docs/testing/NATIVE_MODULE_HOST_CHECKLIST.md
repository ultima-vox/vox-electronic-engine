# Native instrument module — manual host checklist (M0–M3)

Automated tests (CTest `module_abi_v2_tests`, `native_rack_tests`,
`editor_smoke_tests`) prove the processor boundary only. They do **not**
establish DAW behavior. Record each run below with host, version, OS, Rack
build (CI run id) and module artifact.

## Setup

1. Install the `Vox-Music-Install-Windows` artifact as described in
   [demo-synth/CUBASE_TEST_GUIDE.md](demo-synth/CUBASE_TEST_GUIDE.md):
   copy the `vox music` folder into `C:\Program Files\Common Files\VST3\`.
   Do not rebuild the VST3 for any later step, and note its file hash.
2. The ABI test tone is a test fixture (`test-vins/AbiTestTone.vins` in the
   build tree). It is not shipped. To check it in a DAW, copy that file into
   `vox music\instruments`. It loads only in a Rack built in the same tree,
   because it is signed with that tree's development key.
3. No environment variables or other module roots exist.

## M0+ — Vox Demo Synth (playable instrument)

The Cubase procedure for the independent `VoxDemoSynth.dll` package
(`com.ultimavox.demo-synth`, 8 controls) is in
[demo-synth/CUBASE_TEST_GUIDE.md](demo-synth/CUBASE_TEST_GUIDE.md); CI ships it
as the `Vox-Demo-Synth-Cubase-Guide` artifact.

## M0 — Rack + test DLL

| # | Step | Expected |
| --- | --- | --- |
| 1 | Insert Vox Rack on an instrument track; open the editor | Slot catalog lists "ABI Test Tone" |
| 2 | Load it into an empty slot | Slot gets the next free MIDI channel; Macros page shows Gain, Octave, Mute, Wave controls |
| 3 | Play notes on that channel | Audible tone; NoteOff stops it; other channels silent |
| 4 | Move Gain/Octave/Wave/Mute in the Rack UI | Sound follows; host shows the matching `slotNN.p001..p004` changes |
| 5 | Write host automation on `slotNN.p001` and play back | Gain follows the lane |
| 6 | Save project, close DAW, reopen | Same slot, channel, values and sound |
| 7 | Two Rack instances on two tracks, each with the module | Both play independently |
| 8 | Unset the environment variable, reopen the project | Slot shows the instrument as missing, silent; resave keeps it; restoring the variable brings it back |
| 9 | Compare Rack VST3 file hash before/after steps 2–8 | Unchanged |

## M1 / M2 — real instrument DLLs (after the instrument owner ships them)

Repeat M0 with Bass.dll (13 controls) and Acid.dll (80 controls on the
selected test pool), plus: transport sync (start/stop/seek/loop with tempo
automation), NoteOff/Release tails, preset/pattern content once the content
boundary exists.

## M3 — installation and host acceptance

Install/remove/reinstall packages without touching the Rack, missing-module
preservation, replacement confirmation with automation inhibit/rearm,
latency/tail reporting, Cubase 12–15 and REAPER (VST3 parameter list,
automation read/touch/latch/write, project reopen, offline render).

## Results

| Date | Host/version | OS | Rack CI run | Module | Steps passed | Notes |
| --- | --- | --- | --- | --- | --- | --- |
| | | | | | | |
