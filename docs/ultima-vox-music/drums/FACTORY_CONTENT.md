# VOX Drums — factory content manifest

Everything shipped as factory content is **original and synthesized**. There are no
audio files and no third-party material.

| Item | What it is | Provenance |
|---|---|---|
| Kit "Psy Full-on" (family Psy / Full-on) | 16 pads: synthesis settings of the built-in models | table in `products/drums/FactoryContent.cpp` |
| Kit "House Techno" (family House / Techno) | as above | as above |
| Kit "Hip-hop Breaks" (family Hip-hop / Breaks) | as above | as above |
| 5 patterns with B variations per kit, one 16-bar song per kit | output of the generator for fixed settings and seeds | `factoryGenerator`, `factoryPatterns` in `FactoryContent.cpp`; `Generator.cpp` |

- **Hashes:** `products/drums/factory-content.csv` holds the SHA-256 of every kit
  (its DPAD section and parameter values), of each kit's patterns (its DSEQ section)
  and of its song (DSNG). `suite_drums_candidate_tests` recomputes and compares them.
  An intended change is committed with `VOX_GOLDEN_UPDATE=1`.
- **Rights:** the content was written for this product in this repository. No licence
  from a third party is involved. The repository has no licence file; the terms under
  which the product and its content are distributed are the owner's to state.
- **Samples:** none are shipped. User samples are referenced by path and content hash;
  a portable kit (`.voxkit`) carries the user's own files.
- **Not done:** the kits were not auditioned or tuned by ear by a person.
