# Interactive SSH debugging on CI runners (tmate)

The `Builds` workflow includes an SSH debug step on all macOS
matrix legs. When a test step fails and the run was triggered manually via
`workflow_dispatch`, the step opens a tmate session that lets you SSH directly
into the GitHub-hosted macOS runner to run `lldb` on the crashing binary.

This capability was added after three speculative fix PRs (#1355, #1403, #1412)
failed to resolve the macOS SIGSEGV without direct access to the crash state.
See [ADR-0626](../adr/0626-macos-ci-tmate-debug-on-failure.md).

## How to trigger the SSH session

The step is gated on `github.event_name == 'workflow_dispatch'` — it does
**not** fire on regular PR pushes. You must trigger the workflow manually:

```bash
gh workflow run "Builds" \
  --ref feat/your-branch-name
```

Or from the GitHub UI: **Actions → Builds → Run workflow**.

## Finding the tmate URL in the logs

1. Open the Actions run that you triggered.
2. Select the failing macOS job (e.g. `macOS clang`).
3. Expand the **SSH debug session on test failure** step.
4. The step prints two lines:

   ```text
   SSH: ssh <session-id>@nyc1.tmate.io
   or: https://tmate.io/t/<session-id>
   ```

   Copy the `ssh` command and run it in your local terminal.

Access is restricted to the SSH public keys of the GitHub account that
triggered the workflow (`limit-access-to-actor: true`). Make sure your account
has at least one SSH public key registered at
<https://github.com/settings/keys>.

## What to debug once connected

The source tree is the runner's checkout directory,
`/Users/runner/work/vmafx/vmafx`
(`$GITHUB_WORKSPACE`). Test binaries are under `core/build/test/`.

### Typical session for a SIGSEGV in the test suite

1. Find which test binary triggered the crash:

    ```bash
    ls /Users/runner/work/vmafx/vmafx/core/build/test/
    ```

2. Attach `lldb` to the binary that crashed:

    ```bash
    lldb /Users/runner/work/vmafx/vmafx/core/build/test/test_output
    ```

3. Inside `lldb`, run it and inspect the crash when the SIGSEGV fires:

    ```text
    run
    bt          # full backtrace
    frame info  # current frame details
    p <var>     # inspect variables
    ```

### Useful environment flags

Apple's `MallocScribble` and `MallocGuardEdges` surface heap corruption that
the standard allocator hides:

```bash
MallocScribble=1 MallocGuardEdges=1 \
  lldb /Users/runner/work/vmafx/vmafx/core/build/test/test_output
```

`MALLOC_PERTURB_=198` (the value used to expose the ADR-0606 off-by-one) is
also useful:

```bash
MALLOC_PERTURB_=198 \
  lldb /Users/runner/work/vmafx/vmafx/core/build/test/test_output
```

## Session limits

| Limit | Value | Why |
| --- | --- | --- |
| Time to connect | 30 minutes (`connect-timeout-seconds: 1800`) | If you do not connect in that window the step exits and the job completes normally, with the failure status of the earlier test step. |
| Session length | Until you exit the shell or the runner's hard timeout (6 hours for macOS runners) | The session stays open while you are connected. |
| Who can connect | Only the actor who triggered the `workflow_dispatch` (`limit-access-to-actor: true`) | Access is tied to that account's SSH keys. |
| Cost | About 30 minutes of macOS runner time at the cap, roughly 2.40 USD at about 0.08 USD per minute (external price, unverified here) | Avoid leaving sessions idle. |

## When to remove this step

The `workflow_dispatch` gate makes the step a no-op on all regular PR pushes,
so it is safe to leave in place after the SIGSEGV is fixed. It has zero cost
on normal CI runs. Clean it up at your discretion once the macOS crash class is
fully resolved and you no longer need on-runner access.
