# Release signing of native .vins packages (offline, owner-held key)

Owner policy (#78 §5):
- **Key.** The release publisher key is an RSA-3072 key generated **offline under the owner's control**.
- **Private key.** It stays with the owner, encrypted and offline, on hardware-backed storage where available. It never goes into a chat, the repository, CI secrets, the shared self-hosted runners or an agent worktree.
- **Public key.** The public key and its key id are pinned into the production Rack and published.

Until the key is provisioned, release is **BLOCKED**: a release-candidate configure stops without it, and there is no development-key fallback.

`.vins` publisher signing proves who published an instrument package to the Rack. It is **not** Windows Authenticode. Signing the VST3 and the DLLs with Authenticode is a separate release decision.

## 1. One time: generate the key (owner, offline machine)

```sh
# Private key, encrypted with a passphrase. Never leaves this machine.
openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:3072 -aes-256-cbc -out release.key
# Public key and its modulus.
openssl pkey -in release.key -pubout -out release.pub.pem
openssl rsa -pubin -in release.pub.pem -modulus -noout > release.modulus
# Key id (first field) and size, from any Rack build tree:
vox_vins_pack keyid release.modulus
```

Then:
- **Publish the public key.** Add `release.pub.pem` to the repository at `cmake/publishers/release.pub.pem` (a public key only), and publish the key id with the release notes.
- **Back up the private key.** Keep `release.key` and its passphrase in owner-held backups only.

## 2. Every release: build the candidate (any machine, no secrets)

```sh
cmake -S . -B build-rc -DCMAKE_BUILD_TYPE=Release \
  -DVOX_PRODUCTION_BUILD=ON -DVOX_RELEASE_PUBLIC_KEY=cmake/publishers/release.pub.pem
cmake --build build-rc --config Release
```

The result:
- **The VST3** pins only the release public key.
- **Native instruments** are written unsigned to `build-rc/release-candidates/<name>.unsigned.vins`.
- **Graph packages** carry no code and need no signature.

The package tests need the development keys, so they run in the development build of the **same commit**. Its Windows CI must be green.

## 3. Sign (owner, offline machine)

Copy each `*.unsigned.vins` to the offline machine together with `vox_vins_pack` from the same commit. Then:

```sh
vox_vins_pack sign-request VoxFullOnDrums.unsigned.vins manifest.json
#   prints the package, its publisher key id and the manifest SHA-256
openssl dgst -sha256 -sign release.key -out manifest.sig manifest.json
vox_vins_pack attach VoxFullOnDrums.unsigned.vins manifest.sig release.modulus VoxFullOnDrums.vins
vox_vins_pack verify VoxFullOnDrums.vins release.modulus
```

What each step checks:
- **`sign-request`** checks the candidate: a valid archive with a native manifest that is not yet signed.
- **`attach`** verifies the signature against the public key **before** writing anything. A wrong key, a manifest that names another publisher, or an already signed package is refused.
- **What is signed.** The signature covers the exact `manifest.json` bytes, which pin every member's SHA-256. The member files themselves are unchanged.

## 4. Assemble and attribute the release

1. Place the signed `.vins` files in `vox music/instruments/` next to the release-candidate VST3 (`vox music/Vox Electronic Engine.vst3`).
2. Record in the release notes:
   - the commit;
   - the release key id;
   - the SHA-256 of every signed package (printed by `attach`).
3. Before publishing, verify on a clean machine: the Rack loads every instrument and reports no publisher or signature diagnostics.

## Test coverage

`vins_offline_signing` (CTest, development build) runs the whole flow with the build's untrusted test key standing in for the release key:
- unsigned candidate;
- `sign-request`;
- openssl signature;
- `attach`;
- `verify`.

It also checks that the following are refused and that nothing is written:
- a signature made with another key;
- another public key;
- re-signing a signed package;
- verifying with the wrong key.

The `vins_production_gate_*` tests check that a release candidate without the public key is refused, that a private-key input is refused, and that the public-key configuration is accepted.
