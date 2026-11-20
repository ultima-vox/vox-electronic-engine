# Free Delay under Amount automation — development evidence

**Draft / HOLD / NO MERGE.** Product change `830a3ad1946a567346deca725d5b792506eb9999`, based on TL-integrated `6f8a5c2`. The existing free **Delay** amount controls both tap length (50–500 ms) and feedback (0–75%). The old path rounded the slewed length to a whole sample, so even a smoothly routed Amount produced full-sample jumps in the read head.

The new read head fades into fractional linear interpolation when the target Amount changes, then fades back to the original integer tap after the target stays unchanged for at least 5 ms and the smoothed tap is within .01 sample of it. Each fade takes 5 ms. The rounded position is one endpoint of the same interpolation cell, so blending the address only needs the existing interpolation read. No new buffer, host parameter, preset schema, effect type, or reported latency. **Tempo Delay** retains its own BPM-synced eighth-note tap and two-head crossfade. Feedback safety limits, silence cap, structural slot crossfades, Save and drag contracts are unchanged.

## Evidence and limitations

The production-rate first-echo fixture feeds stereo opposite-polarity sines at 0.075 × sample rate. It compares steady sweep output to an analytic continuous sine at the actual smoothed tap, before feedback arrives. Relative mean-square error vs reference (rounded model → interpolated measured):

| Rate | Whole-sample tap | Moving fractional tap |
|---|---:|---:|
| 44.1 kHz | −17.52 dB | −34.00 dB |
| 48 kHz | −17.63 dB | −34.10 dB |
| 96 kHz | −17.38 dB | −33.89 dB |

This is ~16.5 dB less staircase error in that fixture, not an alias-rejection spec or an assertion that delay glides have no pitch shift. Linear interpolation still attenuates high frequencies on fractional taps. Drive/Wavefold aliasing, wider automation/listening cases, and real host behavior are open.

- Nine fixed free-Delay renders (44.1/48/96 kHz × Amount 0/.43/1, a 200 ms stereo burst followed by silence from a cold start) are byte-identical against `6f8a5c2`. Reproduce with [the renderer](evidence/render-fixed-delay-pins.cpp), compiled C++20 `-O2` against each revision's `products/fx`, on little-endian GCC x86-64; compare both output directories and [hashes](evidence/free-delay-static-f32-20261119.sha256). All nine [historical Glitch WAV](evidence/glitch-legacy-wav-20261119.sha256) hashes still match. No media was committed or transferred, and the 232 review-only/0-approved catalog is untouched. Existing sessions with **moving** free Delay Amount intentionally render differently.
- The mixed four-slot **Drive → Wavefold → free Delay → Reverb** fixture uses a real LFO-to-Amount route. At all three rates the 97/512-frame partitions match exactly with positive measurable stereo tail energy, bounded peaks, and a final half-second below −100 dBFS. It runs alongside the previous Tempo Delay variant. The moving-tap fixture has a scoped no-allocation assertion when the interposer is available.

- **48/48 Linux executables** at `830a3ad`: [45 standalone](evidence/free-delay-inventory-20261119.txt) + [three pinned-JUCE native](evidence/free-delay-native-20261119.txt). [ASan/UBSan](evidence/free-delay-sanitized-20261119.txt) passed Effects and SoundGraph. All eight prior callback/float-WAV fixtures remain exact; a ninth live/offline case with LFO-routed free Delay passed over 581,280 frames, with maximum absolute difference `1.65135345e-38` (near float underflow, not byte-exact). Loaded FX-T18 is not inferred from the native harness.

**Not qualified:** FX-T18 loaded wall/CPU deadlines, nonlinear listening/aliasing, native Windows Source focus and drag, Windows VST3 and Cubase/REAPER transport and project reopen. Intended Windows runner read-only PowerShell 5.1 / actual three-bundle inventory needs an authorized operator; not a request to dispatch or build. Windows engineering qualification and the later owner DAW round remain separate.
