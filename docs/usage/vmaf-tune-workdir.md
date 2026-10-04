<!-- markdownlint-disable MD013 MD060 -->
# `vmaf-tune` scratch directory and environment variables

`vmaf-tune` writes decoded reference YUV files and intermediate encodes to
a scratch directory. Point it at a volume with enough free space before
running `compare`, `tune-per-shot` or `ladder` on long sources. The
overview is in [vmaf-tune.md](vmaf-tune.md).

## Choose the scratch directory

The directory is resolved in this order:

1. The `--workdir PATH` flag, accepted by `compare`, `tune-per-shot` and
   `ladder`.
2. The `VMAFTUNE_WORKDIR` environment variable.
3. The operating-system default, typically `/tmp`.

([ADR-0598](../adr/0598-vmaftune-workdir-relocation.md))

!!! note
    A 634-second 1080p60 source decodes to about 118 GB of raw YUV420p.
    Set `VMAFTUNE_WORKDIR` when `/tmp` is a small `tmpfs`. In the
    `vmaf-dev-mcp` container it is pre-set to `/probes/vmaftune-work`,
    the 435 GB `/probes` bind mount.

## Environment variables

| Variable | Default | Effect |
|----------|---------|--------|
| `VMAFTUNE_WORKDIR` | OS default (typically `/tmp`) | Parent of every per-run scratch directory. Overridden by `--workdir`. |
| `VMAFTUNE_VAAPI_DEVICE` | unset | VA-API DRI render node for Intel QSV hardware-device initialisation. The `--vaapi-device` flag of `compare` takes precedence; without either, the first Intel render node under `/sys/class/drm` is used. Ignored for non-QSV encoders ([ADR-0641](../adr/0641-dev-container-encoder-probe-hardening.md)). |
| `VMAFTUNE_SALIENCY_FALLBACK_OK` | unset | Set to `1` to accept a plain encode when an encoder has no saliency ROI dispatch. Equivalent to `--saliency-fallback-plain`; see [saliency-aware](vmaf-tune-saliency-aware.md). |

## Disk-space safeguards

`compare` and `tune-per-shot` run bisects inside a thread pool. Without
a cap, every codec thread decodes the full reference in parallel: a
three-codec compare against a 110 GB source would peak at 330 GB, which
overflowed a 420 GB volume in the BBB v13 failure
([ADR-0577](../adr/0577-vmaftune-bisect-concurrency-cap-and-aggressive-cleanup.md)).
Three
safeguards are on by default:

1. **Decode concurrency cap.** `--max-concurrent-decodes 1` serialises
   reference-YUV decodes across all threads, so peak disk use stays at
   one YUV regardless of how many codecs run in parallel. Raise the cap
   on hosts with large volumes and fast disks. The flag exists on
   `compare`, `tune-per-shot` and `ladder`.
2. **Aggressive cleanup.** After each bisect (one codec and target-VMAF
   pair) its decoded reference YUV is deleted. Per-iteration `.mkv` and
   `.decoded.yuv` files are deleted as soon as scoring finishes, so
   scratch use is bounded by the active bisect.
3. **Mid-run disk check.** Before each iteration's decode, `vmaf-tune`
   compares `shutil.disk_usage` against twice the estimated YUV size.
   When the volume is too full the bisect fails fast with an error that
   names the codec and target VMAF, instead of an opaque ffmpeg rc=228
   (ENOSPC) and a corrupt output JSON.

To reproduce the failure in a test, monkeypatch `shutil.disk_usage` to
return low free space and pass a container source. Both checks fire
before any real decode.

## See also

- [Overview](vmaf-tune.md)
- [Compare](vmaf-tune-compare.md), [per-shot](vmaf-tune-per-shot.md),
  [ladder](vmaf-tune-ladder.md)
