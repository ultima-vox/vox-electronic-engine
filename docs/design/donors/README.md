# VOX MUSIC — original Arena React/Vite donor sources

This is an **isolated data-drop branch**, not an implementation branch. **Do not merge it into `main` or the FX/Drums C++ integration branch** merely to distribute these inputs.

## Original files expected

The owner's three original uploaded inputs must be stored **without editing/repacking** at:

| Path | SHA-256 (original) |
|---|---|
| `docs/design/donors/vox-drums-vsti-interface-design.zip` | `00ff2b89b28d16f2c907a059fdd073f8d37e072714181675c3991ced5929ba63` |
| `docs/design/donors/vox-design-system-specification.zip` | `309332025553297b140231db789af730a8c835a69490a6b44f2d0bda749034c1` |
| `docs/design/donors/VOX_MUSIC_DESIGN_SYSTEM_TZ_2026-11-18.md` | `c8d17664efef3d14a3c31ac769efae8a225d0e63ce03f34b1b21e7d60a3a272d` |

**UPLOAD PENDING:** files listed above are not present until the owner runs the included `publish-vox-donors.ps1` from the provided transfer bundle and pushes this branch. The presence of this README or its PR does **not** mean the ZIP files were uploaded.

## Usage by the Arena developer

When the files exist at the paths above:

```bash
git fetch origin design/vox-react-donors-20261118
git ls-tree -r --name-only FETCH_HEAD docs/design/donors/
git show FETCH_HEAD:docs/design/donors/vox-drums-vsti-interface-design.zip > /tmp/vox-drums-vsti-interface-design.zip
git show FETCH_HEAD:docs/design/donors/vox-design-system-specification.zip > /tmp/vox-design-system-specification.zip
git show FETCH_HEAD:docs/design/donors/VOX_MUSIC_DESIGN_SYSTEM_TZ_2026-11-18.md > /tmp/VOX_MUSIC_DESIGN_SYSTEM_TZ_2026-11-18.md
sha256sum /tmp/vox-*.zip /tmp/VOX_MUSIC_DESIGN_SYSTEM_TZ_2026-11-18.md
```

GitHub branch file links will be:
- [Drums Vite donor ZIP](./vox-drums-vsti-interface-design.zip)
- [Design System Vite donor ZIP](./vox-design-system-specification.zip)
- [Design System TZ](./VOX_MUSIC_DESIGN_SYSTEM_TZ_2026-11-18.md)

Review the **current completed** Arena project at `apps/vox-music-prototype/` before adopting donor code. The agent has already reported a working single-project prototype with tests and browser preview. Do not discard or blindly overwrite it. Produce a **delta audit** against the original donors, migrate only missing valuable interactions, and rerun the build and tests. Keep React/Vite assets isolated from native VST3/C++ modules.

Safety: archive files are untrusted source code; inspect package scripts and dependencies before executing `npm install` or other commands.
