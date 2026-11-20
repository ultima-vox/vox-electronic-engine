# Factory content ledger

The installed tables are original candidates for VOX Trance Synthesis. No third-party preset bank, sample or MIDI file was copied.

| Count | Value |
|---|---|
| Installed programs | 600 |
| Candidate programs | 600 |
| Human-approved programs | 0 |
| Installed phrases | 120 |
| Human-approved phrases | 0 |

Primary roles follow the owner quota: Bass 120, Acid 90, Lead 110, Pluck/Keys 80, Pad/Atmos 90, Arp 75, Stab 35. Transition risers and impacts are not synth programs.

`ProgramCategory` ordinals stored in user bank UBNK are unchanged. Factory role is a separate field.

`COVERAGE.csv` is the role × style × engine matrix, including N/A cells. `midi/FP-*.mid` is SMF type 0 at 960 PPQ and 120 bpm. Policy A: the file stores the authored tick, note, velocity and duration. A slide does not lengthen that NoteOff. Native performance holds the gate until the next note and orders NoteOn before NoteOff at the join. The text event says slide and accent are absent. A render CSV is written when `VOX_FACTORY_AUDIT` points at a file. Human approval is not implied. The working phrase is a separate PHRS section, version 1. It does not change ENG1 parameter IDs or the UBNK layout. A project saved before PHRS loads with the factory phrase. A corrupt PHRS rejects the whole blob. The audio thread never builds the schedule: the control thread publishes a fixed snapshot, and stop releases only the notes the player itself started.


`tests/suite/fixtures/ubnk-b0f230f.voxstate` is a UBNK v1 blob in the layout introduced by `b0f230f`. Category ordinals in it are the pre-role-split values.

SYN-04 appends VA oscillator and filter ids after the frozen 75. Existing ids are not reordered. Factory rows stay 75 values wide; a loaded program leaves the new knobs at their defaults. Wavetable frames and unison are not in this build. Human approval is still 0.

