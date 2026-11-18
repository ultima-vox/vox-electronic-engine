# GitHub Actions: automatic Windows runner selection

**Configuration date:** 2026-11-17. Workflow implementation: `.github/workflows/choose-windows-runner.yml`, reused by `.github/workflows/suite.yml` and `.github/workflows/build.yml`.

## Behavior on each new workflow execution

1. A small `ubuntu-24.04` GitHub-hosted **selector** job queries GitHub's repository runner API. This job runs even when the four Windows self-hosted machines are online.
2. If at least one repository Windows runner is **online** and carries labels `self-hosted` and `windows`, build/test jobs run on `["self-hosted","windows"]`.
3. If all are offline, the API is inaccessible, or the credential is not configured, build/test jobs run on GitHub-hosted **`windows-2022`**. The workflow logs a warning/notice for credential/API fallback.
4. Selection is made once **per workflow run**, before scheduling the Windows job. It does not move or cancel a running job. An online but busy self-hosted runner is chosen and may queue; if it subsequently goes offline, an already-selected job does not automatically migrate.
5. Original CMake build, CTest, Windows validator and artifact publishing are preserved. **Callback timing/headroom measured on GitHub-hosted hardware is not equivalent to a dedicated self-hosted benchmark** and does not alone satisfy the production performance gate.

## One-time credential setup

The repository runner listing endpoint `GET /repos/ultima-vox/vox-electronic-engine/actions/runners` requires **repository Administration: Read** for a fine-grained personal access token or equivalent GitHub App access. The default workflow `GITHUB_TOKEN` cannot be given this permission through `permissions:` settings.

1. In GitHub user settings, create a **fine-grained personal access token** scoped to **only `ultima-vox/vox-electronic-engine`**. Under repository permissions, set **Administration → Read-only**. Give it an appropriate expiration, and rotate it before expiration. Alternative: GitHub App with repository Administration: Read access.
2. In this repository open **Settings → Secrets and variables → Actions → New repository secret**, name **`CI_RUNNER_STATUS_TOKEN`**, and store the generated token. **Never commit the token** or paste it in PR comments.
3. On a PR after the selection workflow has been merged, check the `choose_runner` job log for **`Runner selection: self-hosted`** while a labelled Windows runner is online; then check `Runner selection: github-hosted` when all are offline. No DSP change is needed.
4. If the secret is missing, expired or denied (e.g. fork PR), the selector **safely falls back to GitHub-hosted**. This is a functional fallback, but the intended **online → self-hosted** behavior cannot be confirmed until a permitted token is configured.

## Security and workflow scope

- The token is used **only by the no-checkout selection job**, not passed to the Windows build/test process. Its permissions are read-only and limited to this repository. Review workflows before merging code from untrusted collaborators and do not expose this secret through arbitrary unreviewed PR workflow changes. Fork pull requests do not receive repository Actions secrets.
- No new automatic **release**, `main` merge or deployment is authorized. The `build.yml` workflow still tests the historical legacy product; `suite.yml` tests the three independent suite VST3 products.
- After confirming auto-routing, close the temporary hosted fallback [PR #154](https://github.com/ultima-vox/vox-electronic-engine/pull/154); it is **not** to be merged. This removes duplicate expensive Windows suite runs.
- A true timeout-based failover if a previously online runner becomes unavailable **after** selection is not implemented. Such recovery would require a separate watchdog/rerun design and should not retry a DSP validation silently.

**Reference:** [GitHub REST API - self-hosted runners](https://docs.github.com/en/rest/actions/self-hosted-runners), [GitHub Actions runner selection](https://docs.github.com/en/actions/how-tos/write-workflows/choose-where-workflows-run/choose-the-runner-for-a-job).

## Verification after adding the secret

GitHub Actions evaluates repository secrets when a **new run starts**. Existing runs which selected hosted Windows before `CI_RUNNER_STATUS_TOKEN` was created keep their previous routing decision; they do not automatically reselect. Trigger a new PR synchronize run to verify the selector log. Expected when the repository's Windows self-hosted machines are still offline: `Runner selection: github-hosted ("windows-2022")` **without** the `CI_RUNNER_STATUS_TOKEN is not configured` notice. When self-hosted runners later return online, start another fresh run and expect `Runner selection: self-hosted (["self-hosted","windows"])`. Do not infer credential success from fallback alone if the log instead says `Cannot read repository runner status`.

## Live self-hosted verification (2026-11-17)

After the owner started the Windows machines, a new PR synchronization event must verify that `choose_runner` selects `self-hosted`. Earlier hosted-mode SUCCESS does not prove the online branch; CI results must be tied to the new SHA and selector logs. Do not claim all four runners online merely because one Windows job has begun.


## PR run queue control (2026-11-18)

Both `suite.yml` and `build.yml` now declare workflow-level GitHub Actions `concurrency` with a key that is stable per **PR number and workflow**, not per commit SHA. `cancel-in-progress: false` intentionally leaves an already running test uninterrupted while new pushes **replace an older pending run**. At most one running and one pending run of a given workflow/PR should survive under the new configuration. Separate PRs retain independent groups; GitHub may assign different runner resources.

Non-PR runs use the unique `github.run_id` and are **never coalesced**. This preserves every push-to-main and manually requested workflow run.

The legacy `build.yml` only runs for PRs targeting `main` (plus push to `main`), avoiding expensive Rack builds on every standalone FX/Drums PR. The `suite.yml` remains the standalone Windows x64 VST3 build/test/validator gate for all PRs and `next/standalone-suite` pushes.

**Compatibility:** These changes apply only after the workflow change is present in the workflow revision a PR run uses (after review/integration or explicit branch update). They do **not** retroactively prune queued runs from older workflow revisions, such as the existing long FX #156 backlog. For a one-time queue cleanup, cancel only superseded queued PR runs in Actions, preserving the *latest SHA*, currently running diagnostic jobs and required exact-head release evidence. Do not cancel another developer's in-progress work without coordination.

To inspect and selectively cancel **old queued** runs using authenticated GitHub CLI, not a repository token with unnecessary Administration permissions:

```powershell
gh run list -R ultima-vox/vox-electronic-engine -b arena/d9776bd9-vox-electronic-engine -L 50 --json databaseId,headSha,status,workflowName
# Inspect carefully; cancel only SUPERSEDED queued run IDs, never the newest head or a running test:
gh run cancel RUN_ID -R ultima-vox/vox-electronic-engine
```

GitHub Actions concurrency is **not** a hard real-time preemption mechanism: a self-hosted runner that has become Offline after selection can still strand the previously selected run. That is a separate runner-health/watchdog concern.
