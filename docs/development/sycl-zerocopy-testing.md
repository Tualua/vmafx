# Testing SYCL zero-copy input on an Intel GPU

`scripts/test/sycl-dev-container.sh` builds libvmaf and FFmpeg from the current
worktree inside the SYCL toolchain image and runs meson tests or any command on
the Intel GPU (`/dev/dri`). Use it for the QSV zero-copy path of the
`libvmaf_sycl` filter, which needs the oneVPL GPU runtime and a QSV-enabled
FFmpeg build.

It is a separate launcher, not `vmaf-dev-mcp`, for the reasons recorded in
[ADR-1595](../adr/1595-sycl-zerocopy-fail-loud-twin-routing.md): master has no
`scripts/test/lib/container-rt.sh` / `Containerfile.vmafx` yet, the work needs
the oneVPL runtime and the QSV build dependencies that
`localhost/vmafx:build-ocloc` carries, and it publishes no artifacts.

## Modes

| Mode | What it does |
|---|---|
| `libvmaf` | Configures (first run) and builds libvmaf with SYCL (SPIR-V JIT, no AOT targets), installs it into the cache prefix and copies the public headers FFmpeg's `check_pkg_config libvmaf` probes. |
| `ffmpeg` | Copies the image's FFmpeg checkout, resets it to `FFMPEG_TAG` from `build-config.env`, applies `ffmpeg-patches/series.txt` with `git am --3way` (stops with `PATCH FAILED: <name>` on a conflict), configures with QSV, VAAPI and `--enable-libvmaf-sycl`, builds and installs. Run `libvmaf` first. |
| `test [args...]` | `meson test -C <cache>/build --print-errorlogs` with your arguments, for example `test --suite sycl`. |
| `exec <cmd...>` | Runs a command with the built `ffmpeg` and libvmaf on `PATH` / `LD_LIBRARY_PATH`. |

No mode, or an unknown one, prints usage and exits 2; a missing container
runtime exits 127.

## Environment

| Variable | Default | Meaning |
|---|---|---|
| `SYCL_DEV_IMAGE` | `localhost/vmafx:build-ocloc` | Toolchain image. |
| `SYCL_DEV_CC` | `icx` | C compiler for the libvmaf build (master's SYCL test executables need a compiler that links `-fsycl`). |
| `SYCL_DEV_TIMEOUT` | `5400` | Wall-clock cap in seconds for one run (killed with SIGKILL). |
| `YUV_DIR` | `<main checkout>/python/test/resource/yuv` | Fixtures, mounted read-only at `/yuv`. |
| `HOST_REPO` | the main checkout | Host path of the main checkout. Inside a devcontainer the container runtime is the host's, so `-v` sources must be host paths; every mount under the main checkout is rewritten to the same suffix under `HOST_REPO`. |
| `CONTAINER_RT` | `podman`, then `docker` | Container CLI. |

Each run mounts the worktree at `/work`, passes `--device /dev/dri` and sets
`UR_L0_USE_IMMEDIATE_COMMANDLISTS=0`.

## Cache layout

Everything lives under `<worktree>/.cache/sycl-dev/` (git-ignored):
`build/` (meson), `prefix/` (libvmaf install), `ffmpeg-src/` and
`ffmpeg-prefix/`. Builds are incremental; delete the directory to start over.

## Reproducer: a CPU extractor on zero-copy input fails loudly

```bash
scripts/test/sycl-dev-container.sh libvmaf
scripts/test/sycl-dev-container.sh ffmpeg
scripts/test/sycl-dev-container.sh exec bash -c '
  cd /tmp
  for s in hrc00:ref hrc01:dis; do
    ffmpeg -hide_banner -loglevel error -y -f rawvideo -pix_fmt yuv420p -s 576x324 \
      -i /yuv/src01_${s%%:*}_576x324.yuv -pix_fmt nv12 -c:v hevc_qsv \
      -profile:v main -global_quality 22 ${s##*:}.mp4
  done
  Q="-init_hw_device vaapi=va0:/dev/dri/renderD128 -init_hw_device qsv=qr@va0 -init_hw_device qsv=qd@va0"
  ffmpeg -hide_banner $Q -hwaccel qsv -hwaccel_output_format qsv -hwaccel_device qd -i dis.mp4 \
    -hwaccel qsv -hwaccel_output_format qsv -hwaccel_device qr -i ref.mp4 \
    -lavfi "[0:v][1:v]libvmaf_sycl=feature=name=psnr" -f null -'
```

The run exits non-zero and logs `feature extractor 'psnr' runs on the CPU and
needs host pictures, which zero-copy input does not provide; register its SYCL
twin 'psnr_sycl' instead`. The same command without `feature=name=psnr` prints
a VMAF score. Each decoder input has its own QSV device (`qr`, `qd`), as
[the SYCL overview](../backends/sycl/overview.md) requires.

## What runs where

The zero-copy end-to-end runs are local / container only: the self-hosted
Arc A380 CI runner has no FFmpeg or oneVPL. CI carries the device unit tests
instead (`--suite sycl`, for example `test_sycl_zerocopy_guards`), which
emulate the VA import by writing the shared upload slots directly.
