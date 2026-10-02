---
paths:
  - scripts/ci/check-vcs-version-not-bare-sha.sh
  - core/include/meson.build
invariant: `vcs_tag`: no `--always`, explicit `fallback:`, `--match 'v*.*.*'`; `build.yml` checkout keeps `fetch-depth: 0`.
---
<!-- markdownlint-disable MD013 MD060 -->
# `check-vcs-version-not-bare-sha.sh` invariants

`core/include/meson.build` builds `VMAF_VERSION` from `git describe`, and
upstream Netflix/vmaf spells that call with `--always`. Fork deliberately
does not. With `--always`, git exits 0 even with no reachable `v*.*.*` tag,
prints bare abbreviated object name. Meson writes that into
`vcs_version.h` verbatim — so `vmaf --version`, JSON/XML `version` field
and `vmaf_version()` all report commit instead of version, on any shallow
checkout, tarball export, or worktree whose `.git` is a file.

Three properties load-bearing; this gate enforces each:

| Property | Why it matters |
| --- | --- |
| No `--always` in the `vcs_tag` command | It is what suppresses the non-zero exit that the fallback path depends on. |
| An explicit `fallback:` | Meson would default it to `meson.project_version()`, but the fallback *is* the tagless path here; spelling it out keeps the intent across meson upgrades. |
| `--match 'v*.*.*'` retained | Without it any tag in the repository can supply the version. |

Two things make defect easy to reintroduce, hard to notice. Conflicts
with upstream on every sync, so mechanical "take theirs" resolution restores
`--always`. Invisible until seven-character abbreviation happens
to contain no ASCII digit — about one commit in a thousand — only
condition `core/test/test_output.c::test_vmaf_version` can detect. Assume any
version-string failure on one leg environmental until checked
whether checkout could reach a tag.

`.github/workflows/build.yml` must therefore keep `fetch-depth: 0` on its
checkout: `git describe --long` needs both tag objects and commit
distance to them; `actions/checkout` default of 1 supplies neither.
