# VOX Drums: acoustic sample licence inventory and cross-stream dependency map

Status: **research record, 2026-11-14.** Nothing has been downloaded or added to the repository.
TL decision #136 comment 6880518298 §2 governs this work:

- assets need verifiable redistribution **and** commercial rights: CC0 preferred, CC BY with
  full attribution;
- no NC, personal-use-only, ambiguous or ripped material;
- until a licensed, credible kit exists, Rock/Metal content is **NOT RELEASE-READY**.

## 1. Candidates checked (licence pages read on 2026-11-14)

| Asset | Author | Licence (primary source) | Instruments | Fit | Obligations |
|---|---|---|---|---|---|
| **CrocellKit** v1.1 | DrumGizmo team; kit of the Danish metal band Crocell, recorded at JBOSound (Jacob Olsen) during the album *Relics* (2018) | **CC BY 4.0**: "The CrocellKit drumkit is released under the Creative Commons Attribution 4.0 International License." (drumgizmo.org/wiki/doku.php?id=kits:crocellkit) | 1 kick, 2 rack toms, 2 floor toms, 1 snare, 1 hi-hat, 3 crashes, 1 ride, 2 chinas, 2 splashes; 15 mic channels | **Metal** (Heavy, Symphonic, Nordic) | Attribution wherever performer credits appear; state the changes (downmix, selection, trimming) |
| **DRSKit** v2.1 | DrumGizmo team with Jes Eiler (DRSDrums) | **CC BY 4.0**: "The DRSKit drumkit source files are released under the Creative Commons Attribution 4.0 International License." (drumgizmo.org/wiki/doku.php?id=kits:drskit) | 1 kick, 1 rack tom, 2 floor toms, 1 snare, hi-hat (Paiste 602), 2 crashes, 1 ride; 13 mic channels | **Rock** (Hard Rock) | as CrocellKit |
| **Big Rusty Drums** v1.100 | Karoryfer Samples | **CC0 1.0**: the repository `github.com/sfzinstruments/karoryfer.big-rusty-drums` carries the CC0 1.0 Universal legal code | 24" kick, 14"×8" snare, 4 toms (incl. a 22" kick used as floor tom), hi-hat, 2 rides, 3 crashes/china, cymbal stack | **Rock** (alternative, "oversized") | none (CC0); attribution given as courtesy |
| Unruly Drums v1.100 | Karoryfer Samples | **CC0 1.0** (repository `karoryfer.unruly-drums`) | kick, 3 snares, hi-hat, crash, ride, woodblock; no toms | indie/lo-fi; **not** rock/metal (no toms) | none |
| MuldjordKit | DrumGizmo team | CC BY 4.0 according to the SFZ Instruments catalogue; **not yet read on the primary page** | — | possible rock alternative | to verify |
| Salamander Drumkit | Alexander Holm | CC BY-SA 3.0 (SFZ catalogue) | — | **rejected**: share-alike obligations on redistributed derivatives | — |
| Virtuosity Drums | Versilian Studios / Karoryfer | CC0 (secondary sources only, KVR) | jazz kit, played with sticks | not a rock/metal kit; licence file not read | — |

Not used: Freesound packs (licence varies per file), "free" commercial packs with their own
EULAs, and anything whose licence is only stated by a third party.

## 2. Proposed sources (pending download, measurement and listening)

- **Metal kits:** CrocellKit (CC BY 4.0).
- **Rock kit:** DRSKit (CC BY 4.0), or Big Rusty Drums (CC0) if listening prefers it.

Required per asset at download time:

- source URL, version, retrieval date;
- SHA-256 of the original archive (DrumGizmo publishes MD5: CrocellKit 1.1
  `fa2be0f847bcd8ddef3830c1523690d3`, DRSKit 2.1 `8c4d4b61ad9d354b3b845edd5da9c133`);
- SHA-256 of every derived file shipped;
- a NOTICE entry, for example:

  > "CrocellKit by the DrumGizmo team (drumgizmo.org), recorded from the drums of Crocell, licensed under CC BY 4.0
  > (https://creativecommons.org/licenses/by/4.0/). Changes: multi-microphone channels mixed to stereo, velocity
  > layers selected and trimmed, converted to 48 kHz/24-bit."

### Processing plan (after AG and the content step)

1. Mix the multi-microphone recordings to stereo per instrument (close mics + overheads +
   room), with a fixed, documented mix.
2. Keep 3–4 velocity layers per instrument and 2–3 round robins where they exist. Normalise
   per layer without changing relative levels.
3. Trim to a decay threshold and add short fades.
4. **Size budget:** the plugin's factory content is offline. A target of under 150 MB per kit
   is to be confirmed against the current `.vins` and package limits.
5. **Pad map** (#137 §2.3), roles stored in `DROL`; kits marked release-ready only after the
   owner's listening review.

### Open risks

- **Size:** the source kits are 2–6 GB, multi-mic, 24-bit; the derived kits must be far smaller.
- **Mix quality:** a fixed stereo mix of multi-mic recordings needs a listening pass.
- The **manifest format** for licences and attribution (factory content manifest #136 gate 2)
  is not yet defined.
- **Until steps 1–5 are done, Rock and Metal kits and their factory content stay NOT
  RELEASE-READY.**

## 3. Cross-stream dependency map

| Stream | Item | State | Depends on | Feeds |
|---|---|---|---|---|
| Base | #125 `drm/production-candidate` @ `c7c29bb` | unpublished development build, not a compatibility target | — | everything |
| Generator fix | #129 Ambient lanes + style audit tests | Draft, CI green | #125 | merged **into** #138 |
| Roles engine | #132 `DROL` v1 | Draft, CI green; engine/state only | #125 | merged **into** #138; roles v2 per #137 §2 next |
| Roles UI | #133 | open | native UI (#127, #135) | the earlier eight-page work is behaviour donor only |
| MIDI drag | #134 gesture fix | Draft/HOLD, CI green | #125 | final integrated DAW acceptance |
| Layout state | #130 `DLAY` (simplified §14) | AG review | #125 | native UI |
| UI reference | #135 hardware reference | docs | #127 owner sign-off | native component library (separate stream) |
| Styles design | #137 | AG review of §§2, 3, 10, 11 | #136 | roles v2, meters/tuplets, import, `DGEN` v2/`DSTL`, 16 styles, presets |
| Style engine | #138 foundation | Draft, CI pending | #129 + #132 (merged in) | every later generator step |
| Assets | this document | research | #136 §2 | Rock/Metal kits (release-gated) |

**One integration point:** a combined candidate branch from #125 + #129 + #132 + #134 + #138
(+ later steps), assembled only on an explicit integration decision. Then one final Windows CI
and one Cubase/REAPER acceptance. No merge to `main` without the owner's authorisation.
