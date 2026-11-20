# VOX FX 1.0 — project and preset compatibility

*Draft user notes; development checkpoint, not a released build.*

**Keep a backup of your original project or `.voxfx` preset if you may reopen it with an older VOX FX build.** The current build opens older Programs and uses the original Glitch sound. If you change a Glitch layer's slice grid, read direction or gates and save, the project or preset can contain a newer Program format (FPRG v8). A build that only reads v7 or earlier cannot open that newer Program. This also applies to a saved A/B slot containing the edit. There is no automatic downgrade that discards your settings. To reopen an edited project, use a build that supports v8; to return to an older build, reopen the backup you made before editing.

In **Source / Key**, select a Glitch layer to edit its slices. Changes affect notes triggered after the edit. **Reset slices** restores that layer's original Glitch settings and sound. Programs with default Glitch settings on every layer continue to use the existing Program format, but resetting the current sound does not rewrite any already-saved A/B slots or preset files.
