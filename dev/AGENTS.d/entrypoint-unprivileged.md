---
paths:
  - dev/scripts/dev-mcp-entrypoint.sh
  - dev/Containerfile
invariant: Entrypoint runs as `vmaf`; it changes a path's mode or owner only if the path is its own and the change is needed.
---
<!-- markdownlint-disable MD013 -->
# The entrypoint is unprivileged

- The image's `USER` is `vmaf` (uid 2000) and the entrypoint runs under
  `set -euo pipefail`. A `chmod` / `chown` on a root-owned path fails with
  EPERM and ends the container (restart loop, compose reports it unhealthy).
- Guard every such call: test first (`stat -c %a`, `[ -O path ]`), act only
  when the mode is wrong and the path is ours, or end the line with
  `2>/dev/null || true` when failure is acceptable.
- Why it matters: uutils coreutils (Ubuntu 26.04 base) changed between 0.8
  and 0.10. 0.8 skipped a `chmod` that would not change the mode; 0.10
  issues it. An unconditional `chmod 1777 /tmp` worked for months and broke
  with the base digest update of #1799.
- Check after a base image change: start the rebuilt image with
  `docker compose -f dev/docker-compose.yml up -d dev-mcp` and wait for
  `healthy`; a build that succeeds says nothing about the entrypoint.
