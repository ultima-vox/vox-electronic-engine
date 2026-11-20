# Windows GitHub Actions runner failover

Development-only workflow change; **FX #156 is Draft/HOLD/NO MERGE**.

## Setup

Add repository Actions secret **`RUNNER_STATUS_TOKEN`**, a fine-grained token restricted to `ultima-vox/vox-electronic-engine` with repository **Administration: read** permission and implicit Metadata: read. Ordinary `GITHUB_TOKEN` cannot list repository self-hosted runners. Do not put this token in files, variables, comments or logs. Use short expiration and rotate periodically. Untrusted fork PRs do not receive secrets.

## Route selection

An eligible workflow starts only a lightweight `ubuntu-24.04` `choose-windows-runner` job. With working credentials it asks the paginated GitHub repository runner API for `online`, `busy=false` runners labeled `self-hosted` and `windows`. If one is available, it routes the Windows job to `['self-hosted','windows']`. If none are available, it routes the **same single job** to GitHub-hosted `windows-2022`. The job summary records which and why. GitHub-hosted runs may incur usage charges and need to download all pinned dependencies afresh.

Missing secret, HTTP 403, network/API errors are **not proof of runner outage**: in those cases the router warns and retains the original self-hosted route. Automatic failover is not operational until the secret is configured. Existing eligibility is preserved: the FX #156 Draft heavy suite still needs an explicit added `[RUN_FX_SUITE]` marker (or separate `[RUN_FX_PROCESSOR]` triage marker). Do not start Windows Release/CTest/Steinberg validator without owner authorization.

## Scope of guarantee

This is **dispatch-time** routing only. Runner availability may change in the gap between inventory and job assignment, and there is no automatic queued-job watchdog, no mid-build migration, and no double job execution. A self-hosted failure after a job starts is still a failed job; separate recovery would need additional orchestration and permission. GitHub-hosted Windows does not see any self-hosted files. The read-only Windows PowerShell 5.1 preflight in [#164](https://github.com/ultima-vox/vox-electronic-engine/issues/164) must be performed on the intended actual self-hosted runner.
