# VOX Drums declarative style engine (issue #136)

Status: **foundation implemented on `drm/style-engine`; formats not frozen.**

- Design: PR #137 §11, revision with the TL task order (§7).
- The on-disk import format, `DGEN` v2 and the embedded `DSTL` wait for the Architecture Guard
  review of PR #137 §§2, 3, 10, 11.

## What exists

| Part | Where | Notes |
|---|---|---|
| Style model, formula language, strict JSON parser, content hash | `products/drums/StyleDefinition.{h,cpp}` | message thread only |
| Registry of exactly 23 styles | `products/drums/StyleRegistry.{h,cpp}` | stable ids 0–22, keys, names, groups, tempo hint ranges (information only, never sent to the host) |
| Factory definitions of the 7 original styles | `products/drums/styles/*.json` | built in as text by CMake; parsed by the same parser an imported style will use |
| Interpreter | `products/drums/Generator.cpp` (`styleHit`) | one code path for every style; the switch on `Style` is gone |
| Test reference | `tests/suite/GeneratorReference.cpp` | the switch-based generator, test-only, never linked into the plugin |

- **Ids:**
  - 0–6 keep the meaning of the original styles (the `Style` enum and `DGEN` v1 are unchanged
    in this step);
  - 7–22 are the 16 new styles, in the order of issue #136;
  - ids are never reused.
- A style is **available** only when it has a definition. In this step that is the original 7.
  The 16 new styles are listed but not offered for generation. They get their rules after the
  Architecture Guard review of PR #137 §3.

## Definition (internal, version 1, not frozen)

```
{ "format": "vox-drums-style", "version": 1, "key": "house", "name": "House",
  "rules": [ { "part": "kick", "at": [1, 5, 9, 13], "velocity": 0.95, "accent": true },
             { "part": "closedHat", "at": [2, 4, 6, 8, 10, 12, 14, 16], "kind": "ghost",
               "chance": "0.1 + 0.7 * density", "velocity": 0.4 }, … ],
  "probability": { "value": "90 - 50 * complexity", "min": 30, "max": 100, "except": ["kick"] },
  "fill": { "cycle": ["clap", "snare", "snare"], "ratchet": 2 } }
```

- **`part`:** a generator part, the role slots of #132: kick … cymbal, perc1–perc8.
- **`at`:** 16th positions 1–16 of a 4/4 bar.
- **`kind`:**
  - `hit` sets the hit, optionally gated by `chance`;
  - `ghost` fills only an empty position and, at complexity > 0.5, may make it a 60 % step.
- **`when`:** conditions such as `"density > 0.15"`.
- **`bars`:** `all`, `first` or `odd`.
- **`salt`:** selects an independent random draw.
- **`ratchet`:** an optional chance-gated choice of value.
- **Formulas:**
  - only numbers, the eight generator settings, `+ - *` and parentheses;
  - evaluated left to right with normal precedence, in double precision;
  - no functions, no scripts.
- **Limits:**
  - 64 KiB of text, 512 rules, 64 per part, 64 tokens per formula, nesting depth 8,
    4 conditions per rule;
  - every rejection names its field (e.g. `rules[0].at[0]: 17 is outside 1..16`);
  - nothing is changed on failure.

## Evidence (`suite_drums_style_engine_tests`)

- **Registry:**
  - exactly 23 unique ids, keys and names in the #136 order;
  - House once;
  - the 7 originals available, the 16 new ones not;
  - every factory definition parses.
- **Byte parity** with the switch-based generator:
  - results for Pattern (over a pre-filled pattern with a locked step and lane locks),
    Variation A>B, Fill (bar 1 and last bar) and Mutate Lanes (4 masks);
  - 4 role maps: identity, remapped, duplicate + missing, shuffled;
  - 4 seeds × 4 densities × 3 complexities × 2 syncopations × 2 fills × 2 variations ×
    4 grids;
  - plus Songs of 16/32/64 bars;
  - **43,044 results per style, all identical**.
- **Hashes:** SHA-256 of a deterministic binary form, pinned per factory style
  (`tests/suite/StyleHashes.inc`).
- **Parser:**
  - 21 rejection classes with their field text;
  - size, depth and per-part limits;
  - 4,000 random mutations: no crash, about 40 ms in total.
- **Formulas:** bit-identical to the equivalent C++ expressions.
- **Mutation check (local):** moving one Goa position makes the pinned hash and 18,468 of
  43,044 parity results fail.
- The existing suites still pass:
  - `suite_drums_generator_style_tests` (#129 grammar matrix);
  - `suite_drums_pad_roles_tests` (digests with the #129 integration);
  - candidate, product and others: ctest 31/31.

## Not in this step

- The 16 new styles' rules, roles v2, meters, tuplets, import UI, `DGEN` v2, `DSTL`, presets,
  factory kits. They come after the AG review (#137 §7).
- Generation runs on the message thread only. Parsing and evaluation never run in
  `processBlock`.

## Canonical `.voxstyle` v1 schema (branch `drm/style-schema`; AG-1 PASS, review 5906187802)

One schema, parser and AST for factory and user styles. It extends the flat-rule form above.
Formulas are unchanged.

| Key | Required | Content |
|---|---|---|
| `format`, `version` | yes | `"vox-drums-style"`, `1` |
| `id` | yes | lower-case UUID. Factory styles have fixed UUIDs in the registry; the numeric ids 0–22 give order only. |
| `key`, `name` | yes | key `[a-z0-9-]{1,48}`; name 1–64 characters |
| `meta` | no | `author`, `group` (≤ 64 characters each), `tempo {min, max}` 20–400 (information only) |
| `meters` | yes | unique list from `4/4`, `3/4`, `6/8`, `12/8`; **must contain `4/4`** |
| `defaults` | no | the 8 settings, 0–1 ("Style defaults") |
| `rules[]` | yes | as above, plus `meter` (default `4/4`, must be listed in `meters`) and, for tuplets, `beats` + `subdivision` (`triplet` in 4/4 and 3/4, `duplet` in 6/8 and 12/8) |
| `probability`, `fill` | `fill` yes | as above |
| `variation` | no | `keep`: the parts a Variation B keeps (default kick, snare, clap) |
| `song` | no | `breakWithoutKick` (default true) |
| `assertions[]` | no | `part`, `meter`, `always` / `never` positions: **self-consistency checks only, not genre or quality** |

- **Positions** are stored as a 32-bit mask:
  - straight rules: 1–16 in 4/4, 1–12 in 3/4 and 6/8, **1–24 in 12/8**;
  - tuplet rules: cells 1–6 (triplet) or 1–4 (duplet) within each listed beat.
- **Generation:** only straight 4/4 rules run until the shared timing engine (PR #137 §10.5a)
  exists. The other rules are validated, kept and serialized exactly.
- **Canonical serializer** (`style::serialize`):
  - sorted keys, no spaces outside formulas;
  - formulas in canonical token form;
  - numbers in their shortest round-trip form (`std::to_chars`).
  - parse → serialize → parse is exact.
- **Hashes:**
  - `contentHash`: the generation-relevant data;
  - `documentHash`: the canonical text, used for library and project identity.
- **Runnable fixture:** `tests/suite/fixtures/example-user-style.voxstyle` uses every field,
  including 12/8 rules up to position 23, tuplet and duplet rules, and assertions.
- **Tests added:**
  - every new rejection class;
  - 12/8 positions 17–24 and tuplet cells kept exactly;
  - every factory style and the fixture round-trip byte-exactly;
  - **factory export → parse → generate**, byte-identical (Pattern, Variation, Song);
  - the fixture generates and its own assertions hold;
  - a contradicted assertion is reported;
  - `variation.keep` and `song.breakWithoutKick` are honoured;
  - 23 unique registry UUIDs.

Not yet done:

- `DGEN` v2 (style by UUID) and `DSTL` (embedded user style). Their exact state layout is
  committed before the code.
- Import UI, library folder, collision handling.
