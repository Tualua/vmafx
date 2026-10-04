# Project Modernization Audit

`scripts/dev/project_modernization_audit.py` scans the repository and local
planning files for modernization work that should become concrete PRs. It is a
read-only operator tool, not a CI gate.

Use it when the active backlog feels thin, after a large merge train, or before
starting a broad cleanup branch:

```bash
python3 scripts/dev/project_modernization_audit.py \
  --out-json .workingdir/evidence/modernization/audit.json \
  --out-md .workingdir/evidence/modernization/audit.md
```

The default scan covers curated source and human-facing docs roots, local state
files, AI script clusters, and `model/tiny/registry.json` smoke rows. Archived
scratch is skipped unless `--include-archives` is passed.

## Report Shape

The Markdown report contains:

- summary counts by area;
- top actionable findings ranked by severity;
- modernization clusters such as large `ai/scripts/train_*.py` families;
- blocked or deferred rows separated from immediately actionable work.

The JSON report carries the same data with stable finding IDs so local notes can
refer to one row even after the Markdown is regenerated.

## Reading Findings

The audit drops closeout prose and documented optional-build stubs; a live
`raise NotImplementedError(...)` or a bare `return -ENOSYS;` stays a
high-severity finding.

| Marker | Filtered when | Still ranked high when |
| --- | --- | --- |
| `NotImplementedError` | The text is a docstring saying a scaffold was replaced, an `except NotImplementedError` handler, or a custom exception class inheriting from it. | A live `raise NotImplementedError(...)` in Python source. |
| `-ENOSYS` | The line is a documented disabled-build contract: API docs, workflow comments or DNN fallback stubs describing optional-build behaviour. The same filter covers optional-backend contracts that name the compile-time guard (`HAVE_*`, `enable_*=false`), unavailable loader or runtime paths, or documented CPU fallback behaviour. | A bare `return -ENOSYS;` outside a documented contract, including a live unguarded one in a HIP/ROCm dual-path file (an `enable_hipcc=false` branch that returns `-ENOSYS` is a supported optional-runtime contract and is filtered). |
| Error-code translation | A helper maps a native `NotSupported` runtime code to POSIX `-ENOSYS`: that is error normalisation, not a missing implementation. | Not applicable. |
| Test doubles and stubs | Lines saying a unit test injects a stub, fake session or fake subprocess; ADR allocator stub-file references such as `docs/adr/NNNN-slug.md.stub`; Python type-stub package names; driver-stub environment diagnostics; comments pinning disabled-build stub signatures to the real ABI. | Not applicable. |

`blocked=true` means the matched line contains a dependency phrase such as
`upstream`, `manual access`, `legal`, `model weights`, or `stability window`.
That flag is a triage hint only. Revalidate the dependency before deleting or
deferring the row.

The audit intentionally does not update `.workingdir/OPEN.md` or
`.workingdir/BACKLOG.md`. Those files remain the editorial state of record:
run the audit, copy the real findings into the state files, then pick the next
PR from that cleaned list.

## Narrow Sweeps

Limit the scan to one area while preparing a focused branch:

```bash
python3 scripts/dev/project_modernization_audit.py \
  --scan-root tools/vmaf-tune \
  --scan-root docs/usage \
  --out-md .workingdir/evidence/modernization/vmaf-tune.md
```

Override state files when reviewing an archived planning note:

```bash
python3 scripts/dev/project_modernization_audit.py \
  --state-file .workingdir/OPEN.md \
  --state-file docs/state.md
```

## Reproducer

```bash
python3 -m pytest scripts/dev/test_project_modernization_audit.py -q
```
