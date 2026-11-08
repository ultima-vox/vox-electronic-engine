# ULTIMA VOX MUSIC: build selection and dependency pinning

## Two separate builds in one tree

| Configuration | What it builds | CI |
|---|---|---|
| `-DVOX_BUILD_SUITE=ON -DVOX_BUILD_LEGACY=OFF` (**default**) | The three standalone VST3 plugins, `libs/vox-*` and `tests/suite`. CTest label `suite`. | `.github/workflows/suite.yml` |
| `-DVOX_BUILD_LEGACY=ON -DVOX_BUILD_SUITE=OFF` | The archived Vox Electronic Engine (Rack shell) and its tests, unchanged | `.github/workflows/build.yml` |
| both ON | Both builds side by side. Local use only; no CI job runs this configuration. | — |
| both OFF | Configure fails with "Nothing to build" | — |

Each CI job builds and tests exactly one configuration, so a failure in one never fails the other. The only target both configurations share is `libs/vox-ui`. Suite tests forbid suite code from including legacy headers (`suite_boundaries`).

**Transition policy.** The suite is the product, so the default is suite-only. The legacy build stays reproducible on request until the owner approves the archive procedure (FOUND-00 §2). After that it remains available behind `VOX_BUILD_LEGACY=ON` for donor and reference work.

## Pinned dependencies

| Dependency | Pin | Where |
|---|---|---|
| JUCE 9.0.1 | commit `e18f7f506c0b96f2c738a0bcd7fe6467a5005ad8` | `CMakeLists.txt` (`VOX_JUCE_COMMIT`), both workflows |
| Steinberg VST3 SDK, used only for `validator` in CI | `v3.8.0_build_66` = `9fad9770f2ae8542ab1a548a68c1ad1ac690abe0` | `suite.yml` |

`vox_verify_pinned_source()` (`cmake/suite/VerifyPinnedSource.cmake`) checks the JUCE source at configure time. Both CI workflows configure with **`-DVOX_STRICT_DEPENDENCIES=ON`**.

| Source | Default (`OFF`, local development) | `ON` (CI and release) |
|---|---|---|
| Git checkout at the pinned commit | accepted | accepted |
| Git checkout at another commit | **fails** | **fails** |
| Not verifiable (no git, no `.git` metadata) | warning: "NOT verified, not acceptable for CI or release" | **fails** |

The behaviour is covered by the CTest entries `suite_verify_juce_{strict-pinned, strict-wrong-commit, strict-no-metadata, relaxed-no-metadata, relaxed-wrong-commit}`.

On the self-hosted runner, `.github/scripts/Use-PinnedCheckout.ps1` provides each pinned commit from the runner tool cache:
- it fetches a copy, with retries, only when the cache does not have one;
- it refuses a copy at another commit or with local changes;
- it uses long paths, which the VST3 SDK needs on Windows.
