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
| Factory definitions of all 23 styles | `products/drums/styles/*.json` | built in as text by CMake; parsed by the same parser an imported style will use |
| Interpreter | `products/drums/Generator.cpp` (`styleHit`) | one code path for every style; the switch on `Style` is gone |
| Test reference | `tests/suite/GeneratorReference.cpp` | the switch-based generator, test-only, never linked into the plugin |

- **Ids:**
  - 0–6 keep the meaning of the original styles (the `Style` enum and `DGEN` v1 are unchanged
    in this step);
  - 7–22 are the 16 new styles, in the order of issue #136;
  - ids are never reused.
- A style is **available** only when it has a definition. Since `drm/styles-16` all 23 have one
  (see "The 16 new styles" below); an unknown id is still refused and changes nothing.

## Definition (internal, version 1, not frozen)

```
{ "format": "vox-drums-style", "version": 1, "key": "house", "name": "House",
  "rules": [ { "part": "kick", "at": [1, 5, 9, 13], "velocity": 0.95, "accent": true },
             { "part": "closedHat", "at": [2, 4, 6, 8, 10, 12, 14, 16], "kind": "ghost",
               "chance": "0.1 + 0.7 * density", "velocity": 0.4 }, … ],
  "probability": { "value": "90 - 50 * complexity", "min": 30, "max": 100, "except": ["kick"] },
  "fill": { "cycle": ["clap", "snare", "snare"], "ratchet": 2 } }
```

- **`part`:** a generator part (roles v2, PR #137 §2): `kick`, `snare`, `clap`, `closedHat`,
  `openHat`, `tom1`, `rim`, `crash`, `perc1`–`perc8`, `ride`, `tom2`, `tom3` (19 keys; a style
  declares at most 16 of them in `parts`).
- **`at`:** 16th positions 1–16 of a 4/4 bar.
- **`kind`:**
  - `hit` sets the hit, optionally gated by `chance`;
  - `ghost` fills only an empty position and, at complexity > 0.5, may make it a 60 % step.
- **`when`:** conditions such as `"density > 0.15"`.
- **`bars`:** `all`, `first`, `odd`, `even` or `last` (bar 1, 3; 2, 4; the pattern's last bar).
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
| `parts` | yes | PR #137 §2.4. `required`: a list where each item is a part, or a list of 2+ alternatives of which one is enough (e.g. `["clap","snare"]`); `optional`: a list of parts. At most 16 parts, 8 of them percussion; no part twice. Every part used by a rule, the fill or an assertion must be declared. Used for the Generator notices only; not in `contentHash`. |
| `rules[]` | yes | as above, plus `meter` (default `4/4`, must be listed in `meters`) and, for tuplets, `beats` + `subdivision` (`triplet` in 4/4 and 3/4, `duplet` in 6/8 and 12/8) |
| `probability`, `fill` | `fill` yes | as above |
| `variation` | no | `keep`: the parts a Variation B keeps (default kick, snare, clap) |
| `song` | no | `breakWithoutKick` (default true) |
| `assertions[]` | no | `part`, `meter`, `always` / `never` positions: **self-consistency checks only, not genre or quality** |

- **Roles v2 (pre-release, PR #137 §2):**
  - the keys `tom` and `cymbal` became `tom1` and `crash`; `ride`, `tom2` and `tom3` were added;
  - the canonical text of the factory styles changed (`documentHash`); their `contentHash` and
    generated output did not.
  - The seven factory styles declare as `required` the parts they always play (a rule without a
    chance, condition or bar restriction), and the other parts they use as `optional`.
- **Positions** are stored as a 32-bit mask:
  - straight rules: 1–16 in 4/4, 1–12 in 3/4 and 6/8, **1–24 in 12/8**;
  - tuplet rules: cells 1–6 (triplet) or 1–4 (duplet) within each listed beat.
- **Generation (PR #137 §10.6, `drm/style-meters`):**
  - the generator runs the rules of the pattern's meter: 4/4, and 3/4, 6/8, 12/8 when the style
    lists them;
  - a straight cell that starts on a 16th of the meter's grid gets that position's hit;
  - **tuplet rules:** a part with a tuplet rule for a beat gets that beat in triplets (in 4/4
    and 3/4) or duplets (in 6/8 and 12/8) in its lane, with the cells from those rules (cell
    *k* at the 16th level). This holds when the rule's bars and conditions apply, no cell of the
    beat is locked, and the resolution has tuplets (4/4 at 64ths has none).
  - Other beats of a generated lane go back to straight. Tuplet cells are not swung.
  - **Refusal:** a pattern in a meter the style does not define is refused and nothing changes.
    `generatorRefusal()` gives the reason ("the style is written for 4/4, 12/8, not 3/4"), and
    GENERATE shows it.
  - The fill takes the last 2–8 sixteenths of the meter's bar. Assertions are checked per meter.
  - 4/4 output is byte-identical to before: R4 digests, the 43,044-result parity and the pinned
    hashes are unchanged.
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

## The 16 new styles (`drm/styles-16`, PR #137 §3, §2.4, §4, §10.6)

- **Source:** `products/drums/styles/<key>.json`, one per style, in registry order (ids 7–22).
  Each implements its §3 table: the rules ("Always"/"Sometimes"), the `parts` of §2.4, the
  defaults of §4, the meters of §10.6, and `assertions` for its "Never" lines.
- **Schema additions** (both in `contentHash` only when not the default, so the 7 original
  hashes are unchanged):
  - `bars`: `even` and `last`;
  - `fill.maxLength` 2–8 (default 8) and `fill.maxVelocity` 0.1–0.95 (default 0.95), e.g. Melodic
    Trance's short, soft fill.
- **Meters:** Hard Rock and Heavy Metal 4/4 + 12/8; Symphonic Metal 4/4 + 3/4 + 12/8; Nordic Metal
  4/4 + 6/8 + 12/8; Dream and Symphonic Trance 4/4 + 3/4; every other style 4/4 only.
- **Triplet rolls** (beat 4 of the last bar, with fills > 0.3 and complexity > 0.5): Uplifting,
  Anthem, Hard Rock, Heavy Metal, Symphonic Metal, Nordic Metal, Drum & Bass, Symphonic Trance.
- **Assertion check:** `checkStyleAssertions` now generates with a role map that gives every
  declared part a pad of its own, so assertions on ride, tom 2 and tom 3 are checked.
- **Tests** (`suite_drums_generator_style_tests`, `testNewStyles`), on the genre's role map
  (§2.3), with the style's defaults:
  - all 23 styles: assertions hold; every style × meter × resolution generates a valid pattern;
  - genre distinctions from §3: Uplifting vs Anthem snare 5/13; Melodic ≤ 0.9 velocity;
    Progressive rolling percussion, open hat only on 15; Dream ride on every 8th; Hard Trance 16th
    hats, kick off the beat only in fills; Symphonic Trance three toms; Dark Psy vs Full-on clap
    and vs Techno hats, ratchets in ≥ 30 % of bars; House vs Tech House pickup, Tech House ≥ 4
    percussion positions; Melodic Techno no hat on the beats; Breaks vs DnB kick ghosts; Melodic
    DnB ≤ 1 snare ghost and a ride; Trap snare on 9; Heavy Metal vs House kicks/clap; Hard Rock ≤ 4
    kicks; Symphonic Metal double-bass/half-time bars; Nordic tom 3 on the beats; the meters each
    style accepts and refuses; the exact list of styles with triplet rolls;
  - hashes of all 23 pinned (`StyleHashes.inc`); export → parse → generate byte-identical for all
    23; the unknown-style test uses ids 23, 100 and 255.
- **Limits:** these tests check the written rules, not whether a style sounds right. Genre and
  sound acceptance is a listening/DAW check by the owner. Rock and Metal stay **NOT RELEASE-READY**
  until licensed acoustic samples exist.

Not yet done:

- `DGEN` v2 (style by UUID) and `DSTL` (embedded user style). Their exact state layout is
  committed before the code.
- Import UI, library folder, collision handling.

## State layout: `DGEN` v2 and `DSTL` v1 (exact; committed before the code)

Pre-release policy (#136): there is **no `DGEN` v1 decoder**. A development-build state with
`DGEN` v1 is rejected like any unsupported version. All integers are little-endian.

### `DGEN` v2: generator settings, 58 bytes

| Offset | Size | Field | Rule |
|---|---|---|---|
| 0 | 4 | `u32 version` = 2 | anything else rejects |
| 4 | 16 | style UUID, the 32 hex digits of the canonical text in order (byte *i* = digits 2*i*, 2*i*+1) | a registry UUID (factory style, ids 0–22, available or not), or the id of the state's `DSTL` |
| 20 | 32 | 8 × `f32`: density, groove, syncopation, swing, humanize, complexity, fills, variation | finite, 0–1 |
| 52 | 4 | `u32 seed` | any |
| 56 | 2 | `u16 laneLocks` | any |

- Size other than 58 → rejected.
- An unknown UUID with no matching `DSTL` → rejected: "unknown generator style".

### `DSTL` v1: the user style the generator uses, embedded for portability

| Offset | Size | Field | Rule |
|---|---|---|---|
| 0 | 4 | `u32 version` = 1 | anything else rejects |
| 4 | 32 | SHA-256 of the text bytes (= `documentHash`) | must match |
| 36 | 4 | `u32 length` *n* | 1 – 65,536 |
| 40 | *n* | the style's canonical UTF-8 text (`style::serialize`) | must parse, and must serialize back to exactly these bytes |

- **At most one `DSTL`.** It is written only when the generator style is a user style.
- **The `DSTL` id must equal the `DGEN` UUID.** A `DSTL` whose id is a factory UUID, or that
  `DGEN` does not reference, is rejected.
- The embedded copy is used on restore, whatever the local library holds.
- Any rejection leaves the live state unchanged (transactional restore).
- **Presets:** a Song preset carries `DGEN` and, for a user style, `DSTL`. Loading one replaces
  both. A preset without `DSTL` removes the project's `DSTL`.
- **Budget:** `DSTL` ≤ 65,576 bytes + section header. This must be measured by the boundary
  fixture (#130 T21); `productStateReserve()` is reviewed with that number.

### Implemented and tested (`suite_drums_style_engine_tests`, `suite_drums_candidate_tests`)

- **`DGEN` v2:** encode and decode; all 23 registry styles round-trip, available or not.
  - The candidate tests reject: v1, an unknown version, an unknown UUID, NaN and negative
    amounts, truncated and trailing bytes.
- **`DSTL` v1:**
  - a user style generates in the processor;
  - the project saves `DGEN` + `DSTL`;
  - a **fresh instance with an empty library** restores it, saves the same bytes and generates
    identically;
  - a factory style writes no `DSTL`.
- **`DSTL` rejected atomically** (live state unchanged):
  - unknown version, wrong hash, changed text;
  - truncated text or header;
  - non-canonical text with a valid hash;
  - a style the generator does not reference;
  - an embedded factory UUID;
  - a user UUID without `DSTL`;
  - a `DSTL` beside a factory-style `DGEN`.
- **Song presets:** they bring the user style along. A preset with a factory style removes the
  project's `DSTL`.
- **Mutation check (local):** removing the canonical-text check, or the "DSTL not used by
  DGEN" check, fails the corresponding tests.
- **Not yet:** the library folder, Import/Export UI and the library-level UUID collisions
  (Replace / Keep both). They come with the import feature.
