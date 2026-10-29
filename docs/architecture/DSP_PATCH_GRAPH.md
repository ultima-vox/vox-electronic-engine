# DSP patch graph — slice 1 (PROPOSED, #75)

Status: proposed, not frozen. Owner plan: issue #75. Scope: one instrument's
voice. This is **not** the Rack audio bus / FX graph.

## Model

A graph instrument is data (`graph.json`). The controller thread parses and
validates it, then compiles it into an immutable `CompiledPatch` with a fixed
evaluation order. The audio thread only steps that plan: no parsing,
allocation, locks or file access. The patch is exposed through the existing
`InstrumentProvider` / `InstrumentInstance` contract (`providerId`
`vox.graph`). Routing, the host pool, automation, state and the editor are
therefore unchanged. No DLL is involved.

Code: `libs/dspcore/Primitives.h` (DSP Core), `libs/dspcore/PatchGraph.*`
(spec, compiler, voice), `libs/dspcore/GraphInstrument.*` (JSON, provider).

## graph.json v1

| Field | Meaning |
|---|---|
| `graphVersion` | `1`. Any other value is rejected. |
| `instrument` | `id` (durable InstrumentId), `name`, `vendor`, `version`. |
| `voice` | `policy` `mono-last` (the only policy in slice 1); `ampEnvelope` = id of the env node that ends the voice. |
| `parameters[]` | `id`, `name`, `type` (`float`/`int`/`bool`/`choice`), `min`, `max`, `default`, `pool` (stable host pool index), optional `unit`, `group`, `choices`, `smoothing` (seconds, one-pole). |
| `nodes[]` | `id`, `type` (`block@version`), `params` (block param → parameter id), `inputs` (block input → `node.port`). |
| `output` | `node.port` of type audio. |

The parameter `id` and `pool` index are durable identity, exactly as for
native instruments: never renumber them.

## Blocks (slice 1)

Each block input has a domain. The **whole host range** `[min, max]` of the
parameter bound to it must lie inside that domain, so no automation value can
drive the DSP out of its stable region. The compiler enforces this; nothing is
clamped silently at run time.

| Block | Inputs | Output | Params and domains |
|---|---|---|---|
| `env.adsr@1` | — | control | attack, decay, release in [0.001, 60] s; sustain in [0, 1] |
| `osc.blep@1` | — (pitch from the voice) | audio | wave in [0, 2] (0 sine, 1 saw, 2 square); int/choice only, no smoothing |
| `filter.svf.lp@1` | in: audio | audio | cutoff in [20, 20000] Hz (also limited to 0.45 × sample rate); resonance in [0, 1] |
| `amp.vca@1` | in: audio, env: control | audio | — (`in * env * velocity`) |
| `amp.gain@1` | in: audio | audio | gain in [0, 4] |

A block version is a behaviour contract. A changed algorithm or a widened
domain is a new version (`@2`); `@1` keeps rendering the same samples.

## Validation (compile errors carry a reason)

Each of these is a compile error:

- An unknown graph version, voice policy, block or block version.
- A duplicate node id, parameter id or pool index.
- An invalid parameter: its id, its range, a default outside the range, or smoothing over 1 s.
- An int, bool or choice parameter with a non-integral min, max or default.
- A bool parameter whose range is not 0..1.
- A choice parameter that does not have min 0 and max = number of labels - 1.
- A choice parameter with an empty label or a non-string label.

Parameters are checked in floating point, so a value such as `max: 1e38` is
rejected; it is never converted to an integer type.
- A parameter range outside the domain of the block input it is bound to.
- A continuous parameter bound to an integral input.
- An unknown parameter reference.
- A missing, dangling or wrongly typed input, such as control into audio.
- A cycle.
- A non-audio output.
- A missing or non-envelope `ampEnvelope`.
- More than 64 nodes.
- JSON over 256 KiB.

The parser rejects any number before it is converted to a C++ type that cannot
hold it:

| Field | Allowed values |
|---|---|
| `graphVersion` | integer in 0..1000000 |
| `instrument.version` | integer in 1..2^32-1 |
| `pool` | integer in 0..32767 (then checked against the slot pool size) |
| `min`, `max`, `default` | within ±FLT_MAX |

## Voice semantics (slice 1)

Mono, last-note priority with a held-key stack; sustain pedal (CC64), CC120
(sound off, immediate), CC123 (notes off, normal release); velocity scales
linearly. Per sample: smoothed parameters, then envelopes; if the amplitude
envelope is idle the voice outputs 0 and resets filter state; otherwise the
remaining nodes run in compiled order. Identical to the native Vox Demo Synth,
which is the parity oracle (`graph_demo_parity_tests`, bit for bit).

## Delivery

Slice 1 embeds the graph Demo in the plugin as a **temporary factory test
graph** (`VOX_FACTORY_GRAPHS` in CMake → `generated/FactoryGraphs.h`). This
lets it be tested in a DAW before a loader exists. It has no `.vins` package.

This is not the shipping path. The owner's final requirement (#73/#75) is a
single graph-only `.vins` archive next to the VST3, loaded with the same
`loadPatch`. Once that verified archive loader exists, the embedded demo stops
being a shipping assumption and is removed. Out of scope here: polyphony, modulation matrix,
FX blocks, sample playback, a graph editor UI.
