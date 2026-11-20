VOX FX — Wave B staging / 24 review programs / 0 approved

Start with solo/*.wav (stereo 48 kHz PCM24). These are raw reference
renders, not loudness-normalized. Use monitoring gain or a gain-matched
mix comparison; they are not loudness approval or a factory install.

manifest.tsv provides cue intent, role, genre, mood, mode, BPM, bars/note,
key/scale, provenance and hashes. settings.tsv gives the host base values
needed for exact recreation; portable files intentionally leave receiving
host automation untouched. MIDI pitches, velocities, tempo and host bases
are not automatically applied when loading a portable .voxfx file.

Vellum Coil and Kiln Choir refer to external WAV identity only. Relink to
sources/rail-modes.wav and sources/ceramic-modes.wav respectively using the
included bytes. The two mono source studies are original procedural VOX
modal synth recipes, not third-party recordings or approved shipped stock.
The other twenty-two programs use built-in sources. Approval is pending
for all of the programs, including the previously staged sixteen.

metrics.tsv is the 44.1/48/96 kHz render audit, not a listening verdict.
The 48 kHz solo renders were repeated after portable restore with the host
bases restored; integer PCM bytes matched. See members.sha256 for integrity.
