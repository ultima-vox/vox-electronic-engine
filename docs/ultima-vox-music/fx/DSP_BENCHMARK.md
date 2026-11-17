# FX DSP-only benchmark — local preliminary snapshot

Linux, Intel Xeon 2.60 GHz, GCC 12.2, `-O2`. Each line covers 100 consecutive
128-sample `NoiseRiser::render` calls on eight events with four enabled layers
and the indicated family. Trigger, preparation, plugin wrapper, UI, state,
DAW and operating-system deadline handling are excluded. The initial 0.13–
0.29 seconds only are sampled. No warmup or real-time priority was used.
These noisy wall-clock measurements are neither qualification nor a CPU gate.

```text
44100 riser 8 events x 4 layers: p50 60.68 p95 78.84 p99 88.49 max 98.10 us
44100 downlifter 8 events x 4 layers: p50 95.56 p95 120.88 p99 149.63 max 175.06 us
44100 shepard 8 events x 4 layers: p50 397.35 p95 438.73 p99 473.91 max 640.05 us
44100 impact 8 events x 4 layers: p50 134.50 p95 150.73 p99 170.94 max 185.91 us
44100 whoosh 8 events x 4 layers: p50 120.66 p95 135.62 p99 158.28 max 169.95 us
44100 laser 8 events x 4 layers: p50 102.84 p95 123.35 p99 171.24 max 356.89 us
44100 squelch 8 events x 4 layers: p50 130.58 p95 168.01 p99 176.51 max 221.81 us
44100 metallic 8 events x 4 layers: p50 75.92 p95 93.89 p99 100.82 max 105.78 us
44100 texture 8 events x 4 layers: p50 153.32 p95 172.62 p99 185.39 max 194.17 us
44100 formant 8 events x 4 layers: p50 273.07 p95 299.72 p99 307.40 max 310.37 us
48000 riser 8 events x 4 layers: p50 57.38 p95 69.88 p99 76.29 max 77.17 us
48000 downlifter 8 events x 4 layers: p50 90.72 p95 105.28 p99 110.19 max 115.05 us
48000 shepard 8 events x 4 layers: p50 372.51 p95 516.44 p99 572.02 max 621.77 us
48000 impact 8 events x 4 layers: p50 115.00 p95 150.56 p99 165.38 max 180.54 us
48000 whoosh 8 events x 4 layers: p50 105.17 p95 123.26 p99 138.04 max 142.85 us
48000 laser 8 events x 4 layers: p50 90.83 p95 110.35 p99 130.36 max 132.68 us
48000 squelch 8 events x 4 layers: p50 114.97 p95 178.70 p99 197.85 max 205.45 us
48000 metallic 8 events x 4 layers: p50 66.88 p95 80.71 p99 104.55 max 111.75 us
48000 texture 8 events x 4 layers: p50 131.49 p95 155.75 p99 173.25 max 184.84 us
48000 formant 8 events x 4 layers: p50 234.62 p95 264.12 p99 265.84 max 267.90 us
96000 riser 8 events x 4 layers: p50 50.55 p95 60.88 p99 66.56 max 67.90 us
96000 downlifter 8 events x 4 layers: p50 78.90 p95 91.83 p99 96.78 max 108.19 us
96000 shepard 8 events x 4 layers: p50 316.48 p95 343.20 p99 363.62 max 414.21 us
96000 impact 8 events x 4 layers: p50 115.39 p95 187.70 p99 195.83 max 221.00 us
96000 whoosh 8 events x 4 layers: p50 100.01 p95 114.57 p99 126.14 max 142.75 us
96000 laser 8 events x 4 layers: p50 90.40 p95 106.88 p99 114.05 max 122.39 us
96000 squelch 8 events x 4 layers: p50 112.29 p95 127.52 p99 141.04 max 152.22 us
96000 metallic 8 events x 4 layers: p50 66.90 p95 97.56 p99 117.45 max 406.53 us
96000 texture 8 events x 4 layers: p50 122.68 p95 138.10 p99 159.89 max 161.71 us
96000 formant 8 events x 4 layers: p50 232.74 p95 251.97 p99 260.31 max 261.10 us
checksum 7345.049750, bound failures 0
```
