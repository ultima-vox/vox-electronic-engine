---
name: caveman
description: "Use token-efficient status communication without shortening code, specifications, error logs, tests, identifiers or release evidence."
---
# Caveman — VOX/Arena compatible adaptation

Source model: https://github.com/JuliusBrussee/caveman (Caveman skill; source version not vendored here). **Only reporting style is compressed**. Not a networking proxy or an executable plugin.

## Always-on reporting rules
- Prefer one concise status summary instead of repeated progress messages. Report the PR, HEAD SHA, actual implemented behavior, tests and blockers.
- Preserve full commands, code, tool arguments, file paths, commit hashes, URLs, numbers, diagnostic errors, risk statements and negations.
- Do not compress requirements/specification, important user warnings, security decisions, licensing, state format or realtime constraints into ambiguous fragments.
- Keep useful GitHub PR status up to date, not verbose narration. Prefer one coherent handoff/resume point at the end of a session.
- Never represent missing evidence as `PASS`, and do not use aggressive abbreviations that obscure a `FAIL`/`HOLD`.
- Do not disable tests, drop acceptance gates or skip design verification to reduce token usage.

## Modes
Default: concise but human-readable. Ultra-compressed summaries are opt-in only at explicit request; no code simplification is authorized.

This skill does **not** install upstream Caveman scripts, shell wrappers, proxies or background agents.
