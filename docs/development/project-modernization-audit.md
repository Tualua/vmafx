# Project Modernization Audit

`scripts/dev/project_modernization_audit.py` scans the repository and local
planning files for modernization work that should become concrete PRs. It is a
read-only operator tool, not a CI gate.

Use it when the active backlog feels thin, after a large merge train, or before
starting a broad cleanup branch:

```bash
python3 scripts/dev/project_modernization_audit.py \
  --out-json .workingdir2/modernization/audit.json \
  --out-md .workingdir2/modernization/audit.md
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

`blocked=true` means the matched line contains a dependency phrase such as
`upstream`, `manual access`, `legal`, `model weights`, or `stability window`.
That flag is a triage hint only. Revalidate the dependency before deleting or
deferring the row.

The audit intentionally does not update `.workingdir2/OPEN.md` or
`.workingdir2/BACKLOG.md`. Those files remain the editorial state of record:
run the audit, copy the real findings into the state files, then pick the next
PR from that cleaned list.

## Narrow Sweeps

Limit the scan to one area while preparing a focused branch:

```bash
python3 scripts/dev/project_modernization_audit.py \
  --scan-root tools/vmaf-tune \
  --scan-root docs/usage \
  --out-md .workingdir2/modernization/vmaf-tune.md
```

Override state files when reviewing an archived planning note:

```bash
python3 scripts/dev/project_modernization_audit.py \
  --state-file .workingdir2/OPEN.md \
  --state-file docs/state.md
```

## Reproducer

```bash
python3 -m pytest scripts/dev/test_project_modernization_audit.py -q
```
