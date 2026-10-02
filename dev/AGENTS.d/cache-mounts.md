---
paths:
  - dev/Containerfile
invariant: Apt cache mounts without cleanup; ccache mount with CCACHE_DIR; dockerfile:1.7 syntax; vmaf user uid/gid 2000.
---
<!-- markdownlint-disable MD013 -->
# BuildKit cache mount pattern (ADR-0923)

Containerfile uses BuildKit cache mounts to accelerate rebuilds.
Three invariants must hold on every modification:

1. **apt cache mounts pair with no apt-lists cleanup.** Every
   `RUN apt-get install ...` line MUST be prefixed with:

   ```dockerfile
   RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
       --mount=type=cache,target=/var/lib/apt,sharing=locked \
       apt-get update && apt-get install -y --no-install-recommends ...
   ```

   Trailing `&& rm -rf /var/lib/apt/lists/*` cleanup MUST NOT be
   re-added — with cache mount, lists never make it into image
   layer; re-adding cleanup defeats cache.
2. **ccache mount pairs with `CCACHE_DIR=...`.** Every meson / ninja
   / cmake C/C++ compile step MUST be wrapped with ccache cache
   mount plus matching `CCACHE_DIR` env hint. Step running as `vmaf`
   user -> mount needs `uid=2000,gid=2000` (build-deps stage pins
   user identity per ADR-0603); step running as root -> point cache
   at `/root/.cache/ccache`. Shared `id=ccache-dev-mcp` /
   `id=ccache-dev-mcp-vmaf` markers serialise concurrent BuildKit
   workers against same cache, MUST stay consistent across steps
   sharing cache pool. RUN that configures then builds -> export
   `CCACHE_DIR` before both commands; assignment attached only to
   `cd` doesn't reach Meson or Ninja.
3. **`# syntax=docker/dockerfile:1.7`** at top of file enables
   `--mount=type=cache` parsing — do not remove or downgrade
   directive.
4. **`vmaf` user uid/gid pin.** User created with
   `useradd --uid 2000 --gid 2000` in build-deps stage so
   `--mount=...,uid=2000,gid=2000` cache mounts resolve to same
   identity that runs build. Preserve explicit uid/gid pin on any
   modification to user-creation step.
