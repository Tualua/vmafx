- Deleted the 26 Netflix release tags that VMAFx/vmafx had inherited (`v1.0.2`
  to `v1.5.3`, `v2.0.0` to `v2.3.1`, `v3.0.0`, `v1.3.6rc`, `v1.3.7rc`,
  `v3.0.0-rc`), each recorded first in
  `scripts/release/inherited-upstream-tags.json`. `go list -m -versions
  github.com/VMAFx/vmafx` on the repository now lists the fork's versions only;
  `proxy.golang.org` keeps its cache, so pin a version (`go get
  github.com/VMAFx/vmafx@v1.0.0-rc.2`) until its `@latest` shows one. New
  `scripts/release/delete-inherited-upstream-tags.py` (dry run by default) and
  a `pre-push` guard that refuses a Netflix tag or a tag name outside the fork's
  patterns; fetch `upstream` with `--no-tags` (ADR-1805).
