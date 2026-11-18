# Arena: use repository-hosted agent skills without installing plugins

Arena may not expose built-in skill installation, slash commands or subagent tools. **Reading a SKILL.md is ordinary prompt-based instruction loading, not installation of native runtime features.** It can guide behavior only if the agent reads and follows it; not a guarantee enforced by Arena.

## Skills in this repo

| Path | Use | Source of adaptation |
|---|---|---|
| `.agents/skills/ultracode/SKILL.md` | planning, bounded work packets, integration and independent verification | https://github.com/ZimaAI/ultracode-skill/tree/main/ultracode |
| `.agents/skills/ub-mode/SKILL.md` | write honest failing tests first, fix root causes, preserve safe boundaries | https://github.com/unclebob/ub-mode/blob/master/SKILL.md |
| `.agents/skills/caveman/SKILL.md` | compress progress narration, **not** source code, evidence or security warnings | https://github.com/JuliusBrussee/caveman |

The three Markdown files are small **VOX adaptations**, not full upstream packages: no extra shell scripts, proxies, external API tokens, undisclosed models or background execution. Upstream skill versions are not automatically updated. Review upstream releases/licensing before mirroring more content.

## Starting a new Arena session

1. Open the repository branch containing these skill files (or merge/cherry-pick the approved skills-only commit into the assigned branch without rewriting others' work). The agent must verify which branch it actually has.
2. Ask the agent to read `AGENTS.md`, the three SKILL.md files and the active product spec. The latest GitHub PR comments and owner task control scope.
3. Verify explicit acknowledgement by requiring the agent to list **which files it read**, one actual action from each skill, and any unavailable host facility.
4. For VOX FX, continue **PR #156**, not a fresh feature PR; keep original assigned Arena branch if the host prohibits switching branches.
5. For a full-product assignment, do not stop after writing a plan or interim milestone. Commit code, validate and continue until the product candidate is complete; keep the PR Draft/HOLD for release.

### Reusable initial Arena prompt

```text
Repository: ultima-vox/vox-electronic-engine.
Before editing, read AGENTS.md and all three repository skill instructions:
.agents/skills/ultracode/SKILL.md
.agents/skills/ub-mode/SKILL.md
.agents/skills/caveman/SKILL.md
Also read agents/AGENT_CONTRACT.md and the current scoped product specification.

Use Ultracode's end-to-end workflow even when the host cannot spawn subagents
(fall back to sequential independent passes). Use UB-mode's reproducible tests,
root-cause repairs and clear safety boundaries. Use Caveman only for succinct
status: keep code, exact logs and QA evidence complete.

If these files are missing in your assigned branch, report the missing files
and inspect the GitHub skill-bridge PR/branch for their content; do not pretend
you loaded them. If necessary, ask the owner/TL to integrate the skill-bridge
documentation into your branch. Never install or execute unreviewed upstream
scripts.

For VOX FX continue Draft PR #156 end-to-end through FX-07. Do not pause
between milestones for TL approval. Keep the current assigned branch if fixed.
Do not merge/release, touch main, force-push or delete other work. Report exactly
what was read, how each skill changed your workflow, and unavailable host tools.
```

## Precedence and safety

**Owner/active product spec > root AGENTS.md > agents/AGENT_CONTRACT.md > local skill adaptations > third-party links**. Tool permissions and host system instructions take precedence over repository text.

Third-party `ub-mode` currently suggests pushing to the main line at completion. This is **expressly disabled** by VOX rules. No skill may grant missing capabilities, override a branch restriction, or demand automatic cleanup/merge. Caveman cannot abbreviate negative test evidence. Ultracode cannot create real subagents where Arena does not expose that tool.

## Verification after adopting

A genuinely adopted skills bridge should leave concrete evidence: exact read paths/commit, feature plan, tests first when feasible, changed files, before/after results, one integrated PR and short precise status. Merely saying “Ultracode enabled” without evidence is not acceptance.
