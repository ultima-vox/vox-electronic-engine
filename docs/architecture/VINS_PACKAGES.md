# .vins instrument packages (#73)

**Status: PROPOSED.** This is the implementation of the owner spec in #73: one
instrument is one `.vins` file. The format is frozen at the pre-release
contract freeze, together with the 64/128-parameter pool decision (#77 gate 5).

It replaces the `.voxinst` folders, the `VOX_DEV_INSTRUMENT_ROOT` and
`VOX_DEV_NATIVE_MODULES` variables, the PowerShell install scripts and the graph
Demo that was built into the plugin. None of these exist any more.

## Install layout (Windows)

```text
C:\Program Files\Common Files\VST3\vox music\
  Vox Electronic Engine.vst3
  instruments\
    VoxDemoSynth.vins
    VoxDemoSynthGraph.vins
    ...
```

To install an instrument:
1. Copy its `.vins` file into `instruments\`.
2. Restart the DAW.

To uninstall it, delete the file. No scripts or environment variables are
needed, and adding or removing an instrument never changes the VST3.

How the Rack finds packages:
- It looks only in the `instruments` folder next to its own VST3. It finds its
  own binary through `GetModuleHandleExW` / `dladdr`, not through the DAW
  executable or the current directory.
- Only regular `*.vins` files in that folder are loaded; subfolders are not
  scanned.
- The scan is bounded: at most 1024 folder entries and 64 packages.
- Symlinks and junctions are refused, both for the folder and for the files.
- Rejected packages are listed in the Rack's diagnostics.

## Archive

A `.vins` is a ZIP-compatible file, but the Rack reads only a strict subset of
ZIP. The details are in `libs/instrument/vins/VinsZip.h`.

**Container rules**
- Stored or deflate members only.
- Not allowed: ZIP64, encryption, multiple disks, symlinks and directory
  entries.
- Member data runs contiguously from offset 0 to the central directory. Each
  local header must match its central directory record.
- Every member is checked against its CRC-32.
- Each member decompresses into exactly its declared size.

**Limits**

| Limit | Value |
|---|---|
| Archive size | 64 MiB |
| Number of members | 256 |
| Size of one member | 48 MiB |
| Total uncompressed size | 128 MiB |
| Compression ratio | 200:1, for members over 1 MiB |

**Member names**
- Portable relative paths: `[A-Za-z0-9._-]` segments separated by `/`.
- No `.` or `..` segments, no absolute paths, no `\`, no drive letters.
- Names must be unique ignoring ASCII case.

The whole file is read into memory once. Validation and extraction both use
that same buffer, so the file cannot change between being checked and being
used.

## manifest.json (schema 1)

```json
{
  "vins": 1,
  "packageId": "com.ultimavox.demo-synth",
  "version": "1.0.0",
  "name": "Vox Demo Synth",
  "vendor": "Ultima Vox",
  "executionKind": "native",
  "instruments": ["com.ultimavox.demo-synth"],
  "native": {
    "abi": 2,
    "publisher": "<16 hex: SHA-256 of the publisher modulus>",
    "modules": {"win-x64": "module/win-x64/VoxDemoSynth.dll"}
  },
  "files": {"module/win-x64/VoxDemoSynth.dll": "<sha256>"}
}
```

**Common rules**
- Keys outside this schema are rejected.
- `files` lists every member except `manifest.json` and `manifest.sig`, with
  each member's SHA-256. The archive must contain exactly the listed members.

**Graph packages**
- `executionKind: "graph"`, plus a `graph` key naming the member that holds
  the patch (see `DSP_PATCH_GRAPH.md`).
- A graph package must not have a `native` section.
- It lists exactly one instrument, and that id must equal the id inside the
  patch.

**Native packages**
- `executionKind: "native"`.
- The `native` section gives the ABI (2), the publisher key id and one module
  member per platform. Platforms are `win-x64` and `linux-x64`.

**Identifiers**
- `packageId` and instrument ids use `[a-z0-9._-]`, up to 128 characters.
- `version` is MAJOR.MINOR.PATCH, with no leading zeros.
- If two packages in the folder share a package id or an instrument id, all of
  them are rejected.

## Trust

SHA-256 hashes show that the archive is intact. They do not show who made it.

Native code is loaded only if both of these hold:
1. `manifest.sig` is a valid **RSA-3072+ PKCS#1 v1.5 SHA-256** signature over
   the exact bytes of `manifest.json`.
2. It was made with a publisher key that was **pinned into this Rack build at
   compile time**.

The Rack contains only the verification code (`VinsTrust.cpp`, built on
`juce::BigInteger`). It rebuilds the whole expected signature block and
compares it byte for byte; nothing in the signed data is parsed. Private keys
never enter the Rack, the repository or the logs.

Graph packages hold no executable code, so they need no signature. The DSP Core
compiler validates them instead.

**Build keys (`cmake/VinsPublisher.cmake`)**
- **Development (default, CI):** a key generated once inside the build tree signs native packages while packaging. Packages it signs load only in Racks built from that same tree, and both `build-info.txt` (which starts with "BUILD TYPE: DEVELOPMENT") and the Rack label it as a development key.
- **Release candidate:** configure with `-DVOX_PRODUCTION_BUILD=ON -DVOX_RELEASE_PUBLIC_KEY=<owner's public PEM>`.
  - The Rack then pins **only** that public key.
  - Native packages are written **unsigned** to `release-candidates/<name>.unsigned.vins`, for the owner to sign offline ([RELEASE_SIGNING](../release/RELEASE_SIGNING.md)).
  - Without the public key, configure stops: the release is BLOCKED.
- **Private keys never enter a build.** `VOX_PUBLISHER_PRIVATE_KEY` is refused by every configuration. No private key goes into the repository, CI secrets, the shared runners, agent worktrees or logs.
- **Release blocker (#77 gate 3):** the owner still has to generate and provision the release key (procedure in RELEASE_SIGNING). `.vins` publisher signing is not Windows Authenticode; signing the VST3 and DLLs with Authenticode is a separate release decision.

## Cache and loading

A native module is never loaded directly from the ZIP file.

**Extraction**
- The verified members are copied to
  `%LOCALAPPDATA%\Vox Music\Cache\<packageId>\<version>-<manifest hash>\`.
  On Linux the path is `$XDG_CACHE_HOME/vox-music/...`.
- Files are written to a staging folder first, then moved into place with an
  atomic rename.
- The module is loaded with the restricted DLL search of the V2 connector.

**Reusing a cache entry**
- An existing entry is used only if it holds exactly the listed files with the
  right hashes, and no links.
- Otherwise it is rebuilt.
- On Windows, an entry whose module is currently loaded cannot be replaced.
  That case is reported as a diagnostic.

**Rules for loaded modules**
- Modules are never unloaded while the process runs.
- The module's instrument ids must equal the manifest's ids.

All of this runs on the controller thread. The audio callback never touches
the file system, the ZIP data or the cache.

## Building packages

There is one CMake function:

```cmake
vox_add_vins(<target> KIND graph|native OUTPUT <file.vins> ID ... VERSION ...
             NAME ... VENDOR ... INSTRUMENTS ...
             GRAPH <member> <file> | MODULE_TARGET <shared library target>)
```

`cmake/MakeVins.cmake` builds each package in three steps:
1. `vox_vins_pack manifest` writes `manifest.json` with the member hashes.
2. For native packages, `openssl dgst -sha256 -sign` signs the manifest.
3. `vox_vins_pack zip` writes a deterministic stored ZIP.

## Tests

`vins_package_tests` covers:

| Area | Cases |
|---|---|
| ZIP reader | A round trip; bad member names (zip-slip, absolute, `\`, drive letters); duplicates; truncated and padded archives; CRC errors; bzip2; encryption; symlinks; ZIP64; size, count and total limits; deflate data that lies about its size; ratio bombs. |
| Signatures | A valid signature; a changed manifest, changed signature, short signature or signature ≥ modulus all fail; a key under 3072 bits is refused; a foreign publisher's signature fails. |
| Manifest schema | Version, identifier and kind checks; unknown keys and platforms are rejected. |
| Package loading | Graph and native packages together; no folder; Unicode and spaces in the path. Rejected: unsigned, foreign key, edited manifest, patched DLL, extra member, missing member, no manifest, a renamed DLL, a bad graph, a graph with a native section, a wrong graph id. In every rejected case nothing is extracted. |
| Conflicts and folder | Duplicate package ids and duplicate instrument ids reject everyone involved; folders and symlinks named `*.vins` are skipped. |
| Cache and discovery | Cache reuse and planted-file handling; how the `instruments` folder path is worked out from the plugin path; the Rack catalogue. |

`demo_synth_tests` and `graph_demo_parity_tests` load the real
`VoxDemoSynth.vins` and `VoxDemoSynthGraph.vins` packages from the build.

`vins_fuzz` is a deterministic mutation fuzzer. Its seeds are the real
packages built in the tree. It mutates whole archives, manifests, signatures
and graphs, and feeds them to `readZip`, `parseManifest`, `verifySignature`
and the graph compiler, checking the reader's invariants on every accepted
archive.
- CTest runs 20,000 iterations on every platform.
- A local ASan/UBSan campaign ran 400,000 iterations with no crash or
  invariant violation.
- libFuzzer is not available in this environment, so this harness takes its
  place for now.

`vins_production_gate_*` checks that a production configure without the
owner's key fails, and that one with the key passes.
