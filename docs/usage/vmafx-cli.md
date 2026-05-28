# `vmafx` — modernized CLI reference

`vmafx` is a thin alias for the `vmaf` binary that activates modernized
defaults on invocation. It is installed as a symlink to `vmaf` in the same
`bindir`; the binary detects the `vmafx` basename at startup and adjusts its
behavior accordingly (ADR-0690).

> **Relationship to `vmaf`.** `vmafx` and `vmaf` share one binary on disk.
> Every flag documented in [`cli.md`](cli.md) is also accepted by `vmafx`.
> The only differences are the defaults described on this page. To use legacy
> defaults with `vmafx`, pass `--precision=legacy` explicitly.

## Modernized defaults

| Behavior | `vmaf` default | `vmafx` default |
|---|---|---|
| Output precision | `%.6f` (6 decimal places, matches upstream Netflix) | `%.17g` (IEEE-754 round-trip lossless, `--precision=max`) |
| Backend selection | auto (CUDA > Vulkan > SYCL > CPU) | same — auto is the default for both |
| Startup banner | `VMAF version <V>` | `VMAFX version <V> (precision=max)` |
| `--version` output | `<version string>` | `VMAFX <version string> (auto-backend, precision=max)` |

The backend auto-selection priority (CUDA > Vulkan > SYCL > CPU) applies to
both `vmaf` and `vmafx` and was established by ADR-0498. No difference exists
at the backend level.

## Why `--precision=max` is the vmafx default

The Netflix-upstream `%.6f` precision preserves bit-for-bit compatibility with
the three golden-data test pairs in `python/test/`. This is essential for the
`vmaf` binary, which participates in the golden-data gate.

`vmafx` targets new workflows that consume VMAF scores programmatically and
benefit from the full IEEE-754 double range. `%.17g` outputs the minimum
number of significant digits required for exact round-trip through `strtod`,
avoiding silent truncation when scores are stored in JSON, CSV, or database
tables. See [precision.md](precision.md) for full details (ADR-0119).

## Quick start

```shell
# .y4m pair — precision=max applied automatically
vmafx --reference ref.y4m --distorted dist.y4m

# Equivalent vmaf invocation with explicit flag
vmaf --reference ref.y4m --distorted dist.y4m --precision=max

# Opt back into legacy %.6f precision via explicit flag
vmafx --reference ref.y4m --distorted dist.y4m --precision=legacy

# Check which version and defaults are active
vmafx --version
# Output: VMAFX 3.x.y-lusoris.N (auto-backend, precision=max)
```

## netflix-compat mode

To reproduce the exact numerical output of `vmaf` (and upstream Netflix/vmaf),
use either:

```shell
# Option 1: invoke vmaf directly
vmaf --reference ref.y4m --distorted dist.y4m

# Option 2: invoke vmafx with explicit legacy precision
vmafx --reference ref.y4m --distorted dist.y4m --precision=legacy
```

Both produce `%.6f`-formatted scores and satisfy the golden-data gate.

## AI tool aliases

Three companion Python tools also ship `vmafx-*` aliases alongside their
existing `vmaf-*` names. Both names invoke the same callable:

| `vmaf-*` name | `vmafx-*` alias | Package |
|---|---|---|
| `vmaf-train` | `vmafx-train` | `ai/` (hatch package `vmaf-train`) |
| `vmaf-tune` | `vmafx-tune` | `tools/vmaf-tune/` (hatch package `vmaf-tune`) |
| `vmaf-mcp` | `vmafx-mcp` | `mcp-server/vmaf-mcp/` (hatch package `vmaf-mcp`) |

Install any package with `pip install -e <path>` to get both names. The
aliases are console_scripts entries pointing to the same Python callables;
no behavior difference exists between `vmaf-train` and `vmafx-train`.

## Smoke test

After installation, confirm the symlink and default banner:

```shell
# Confirm vmafx resolves to the vmaf binary
ls -la $(which vmafx)
# -> ... vmafx -> vmaf

# Version string shows VMAFX identity and defaults
vmafx --version
# -> VMAFX 3.x.y-lusoris.N (auto-backend, precision=max)

# Or inside the dev container:
docker exec vmaf-dev-mcp vmafx --version
```

## See also

- [`cli.md`](cli.md) — full flag reference for all options accepted by both
  `vmaf` and `vmafx`.
- [`precision.md`](precision.md) — detailed explanation of `--precision` modes
  (ADR-0119).
- [ADR-0690](../adr/0690-vmafx-binary-and-ai-aliases.md) — decision record for
  the symlink implementation and argv[0] detection mechanism.
- [ADR-0686](../adr/0686-vmafx-rebrand-aggressive-modernization.md) — VMAFX
  rebrand umbrella ADR.
