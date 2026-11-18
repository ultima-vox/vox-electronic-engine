---
name: ub-mode
description: "Use root-cause-first TDD, clean boundaries, deterministic evidence and safe refactoring for VOX C++/JUCE changes."
---
# UB-mode — VOX/Arena safe adaptation

Source model: https://github.com/unclebob/ub-mode/blob/master/SKILL.md. This is an **adaptation**, not a verbatim copy, and intentionally removes upstream powers conflicting with VOX branch protection.

## Engineering loop
1. **Make it work:** reproduce the failure or user-visible missing behavior, design the smallest meaningful test, see it fail for the expected reason, then write the bounded change. If the environment cannot execute it, mark `NOT_RUN` — never claim TDD evidence that did not occur.
2. **Make it right:** fix the root cause instead of hiding errors with broad guards or relaxed tolerances. Keep one decision in one place, preserve stable host IDs/schema, avoid duplicate mutable state and keep forbidden states unrepresentable at API boundaries.
3. **Follow practice:** commit coherent units with meaningful evidence, maintain verification logs and do a final regression run. Refactor only after behavior is pinned by tests. Keep status honest and concise.

## VOX-specific hard requirements
- Realtime `processBlock`: no locks, I/O, dynamic alloc/free, UI work or unbounded loops. Analyze ownership and DSP lifetime across parameter/state handoff.
- Windows x64/JUCE 9 + Steinberg validator and exact-head CI matter more than Linux-only syntax checks; never declare host compatibility from mocks or a silent shell.
- Audit 44.1/48/96 kHz, 64/128/512 frame sizes, MIDI offsets, state project restore, invalid inputs, deterministic audio and sample license provenance as applicable.
- Optional external quality tools (mutation testing, complexity, duplication) may run **only if actually available and reviewed**. Do not invent tool names or download/execute outside packages without authorization.
- **Safety override:** NEVER push/merge directly to `main` as a default, NEVER delete worktrees/files/branches without approved scope, never force-push shared branches. Upstream suggestions to push to the main line or reclaim disk do NOT apply.
- Keep implementing approved product stages despite interim HOLD; HOLD only prohibits merge and release until acceptance.
