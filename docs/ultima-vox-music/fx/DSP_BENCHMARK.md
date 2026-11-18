# FX DSP-only benchmark — local preliminary snapshot

Linux, Intel Xeon 2.60 GHz, GCC 12.2, `-O2`. Each line covers 100 consecutive
128-sample `NoiseRiser::render` calls on eight events with four enabled layers
and the indicated family. Trigger, preparation, plugin wrapper, UI, state,
DAW and operating-system deadline handling are excluded. The initial 0.13–
0.29 seconds only are sampled. No warmup or real-time priority was used.
Granular reaches the 64-grain pool cap; no user samples are included.
These noisy wall-clock measurements are neither qualification nor a CPU gate.

```text
44100 riser 8 events x 4 layers: p50 64.60 p95 78.53 p99 88.02 max 107.97 us
44100 downlifter 8 events x 4 layers: p50 103.15 p95 132.08 p99 177.47 max 185.13 us
44100 shepard 8 events x 4 layers: p50 434.38 p95 513.60 p99 647.69 max 662.27 us
44100 impact 8 events x 4 layers: p50 239.73 p95 299.07 p99 329.34 max 334.83 us
44100 whoosh 8 events x 4 layers: p50 144.78 p95 228.96 p99 232.96 max 247.59 us
44100 laser 8 events x 4 layers: p50 117.98 p95 145.54 p99 162.31 max 164.25 us
44100 squelch 8 events x 4 layers: p50 145.59 p95 173.97 p99 182.31 max 189.16 us
44100 metallic 8 events x 4 layers: p50 85.55 p95 102.61 p99 107.02 max 127.88 us
44100 texture 8 events x 4 layers: p50 173.09 p95 194.97 p99 201.43 max 210.56 us
44100 formant 8 events x 4 layers: p50 303.40 p95 334.56 p99 345.37 max 349.24 us
44100 glitch 8 events x 4 layers: p50 135.43 p95 160.89 p99 186.36 max 281.82 us
44100 granular 8 events x 4 layers: p50 112.32 p95 139.38 p99 150.69 max 153.42 us
48000 riser 8 events x 4 layers: p50 64.61 p95 81.55 p99 90.12 max 92.66 us
48000 downlifter 8 events x 4 layers: p50 103.01 p95 131.19 p99 138.57 max 143.94 us
48000 shepard 8 events x 4 layers: p50 383.24 p95 453.29 p99 466.94 max 555.04 us
48000 impact 8 events x 4 layers: p50 137.64 p95 214.71 p99 252.68 max 273.90 us
48000 whoosh 8 events x 4 layers: p50 134.62 p95 186.44 p99 278.12 max 286.91 us
48000 laser 8 events x 4 layers: p50 118.39 p95 145.60 p99 194.13 max 197.26 us
48000 squelch 8 events x 4 layers: p50 234.61 p95 264.40 p99 316.30 max 373.09 us
48000 metallic 8 events x 4 layers: p50 133.51 p95 164.34 p99 215.53 max 224.74 us
48000 texture 8 events x 4 layers: p50 233.74 p95 297.62 p99 398.56 max 452.36 us
48000 formant 8 events x 4 layers: p50 317.24 p95 473.35 p99 524.12 max 610.14 us
48000 glitch 8 events x 4 layers: p50 134.95 p95 153.13 p99 157.56 max 161.55 us
48000 granular 8 events x 4 layers: p50 151.19 p95 197.54 p99 232.69 max 241.02 us
96000 riser 8 events x 4 layers: p50 94.74 p95 117.95 p99 133.56 max 144.52 us
96000 downlifter 8 events x 4 layers: p50 151.94 p95 189.86 p99 203.61 max 209.99 us
96000 shepard 8 events x 4 layers: p50 418.68 p95 593.56 p99 680.01 max 752.04 us
96000 impact 8 events x 4 layers: p50 146.16 p95 165.09 p99 170.93 max 173.51 us
96000 whoosh 8 events x 4 layers: p50 126.70 p95 143.24 p99 150.77 max 158.17 us
96000 laser 8 events x 4 layers: p50 117.32 p95 138.62 p99 143.14 max 146.50 us
96000 squelch 8 events x 4 layers: p50 144.25 p95 164.96 p99 168.72 max 223.57 us
96000 metallic 8 events x 4 layers: p50 85.59 p95 107.52 p99 112.89 max 119.85 us
96000 texture 8 events x 4 layers: p50 161.88 p95 272.23 p99 293.96 max 306.63 us
96000 formant 8 events x 4 layers: p50 302.45 p95 340.67 p99 360.99 max 373.26 us
96000 glitch 8 events x 4 layers: p50 139.58 p95 154.44 p99 165.87 max 217.66 us
96000 granular 8 events x 4 layers: p50 112.23 p95 124.77 p99 127.29 max 133.20 us
checksum 8694.820750, bound failures 0
```

## Four shared slots stress snapshot

Same Linux Xeon/GCC -O2 environment; 400 consecutive 128-sample DSP calls
at 96 kHz, eight events, four Shepard layers per event and four identical
shared effects slots (0=bypass, 1=lowpass, 2=drive, 3=wavefold, 4=bitcrush,
5=ring, 6=gate). First ~0.53 s only; no host, plugin wrapper, UI or automation.
These are noisy wall-clock samples, not a deadline guarantee. Maxima approaching
the 1,333 µs nominal block period warrant profiling on the target Windows DAW.

```text
96k Shepard 8x4 + 4x effect 0: p50 474.33 p95 865.18 p99 1038.01 max 1064.06 us; checksum -0.249275
96k Shepard 8x4 + 4x effect 1: p50 443.63 p95 532.84 p99 755.44 max 901.32 us; checksum 0.338010
96k Shepard 8x4 + 4x effect 2: p50 445.38 p95 534.10 p99 652.04 max 769.64 us; checksum -24.707053
96k Shepard 8x4 + 4x effect 3: p50 448.97 p95 751.97 p99 809.69 max 984.31 us; checksum 12.919668
96k Shepard 8x4 + 4x effect 4: p50 428.99 p95 476.00 p99 496.31 max 517.65 us; checksum -0.332948
96k Shepard 8x4 + 4x effect 5: p50 416.85 p95 453.26 p99 473.08 max 591.04 us; checksum -0.662651
96k Shepard 8x4 + 4x effect 6: p50 425.13 p95 481.47 p99 501.91 max 549.97 us; checksum -0.089174

```
