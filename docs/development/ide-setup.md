<!-- markdownlint-disable MD013 MD060 -->
# IDE setup (VS Code + Zed + clangd)

Use this page to get working code navigation in VS Code or Zed. Both editors
use clangd as the C/C++ language server (VS Code disables Microsoft
IntelliSense explicitly), and clangd needs a Meson build directory that
contains `compile_commands.json`.

## Quick start

1. Configure one build directory with every backend your host has a toolchain
   for (see
   [below](#make-sure-the-build-directory-covers-every-backend-you-touch)).
2. Open the repository in VS Code or Zed.
3. Restart clangd so it re-reads `compile_commands.json`.
4. Open a backend file such as `core/src/feature/cuda/` or
   `core/src/feature/sycl/` and confirm there are no "undeclared identifier"
   errors.

## Which build directory each tool reads

The tools do not all read the same directory. Make sure the directory each one
reads exists, or configure both:

| Tool | Reads | Defined in |
|---|---|---|
| VS Code clangd | `core/build/compile_commands.json` | `.vscode/settings.json` (`--compile-commands-dir=core/build`) |
| VS Code C/C++ extension | `build/compile_commands.json` | `.vscode/c_cpp_properties.json` |
| Zed clangd | `build/compile_commands.json` | `.zed/settings.json` (`--compile-commands-dir=build`) |
| `make` targets | `core/build` | `BUILD_DIR` in the `Makefile` |
| `meson setup build core` | `build` | the command in `AGENTS.md` section 2 |

`meson setup build core` writes `build/`; a `make` build writes `core/build/`.
For VS Code, configure `core/build` (or change `--compile-commands-dir`
locally); for Zed, configure `build`.

## Make sure the build directory covers every backend you touch

`compile_commands.json` only contains entries for files that were actually
compiled. If the build directory was set up CPU-only, clangd has no include
paths for CUDA / SYCL headers and lights up every `VmafCudaBuffer` /
`sycl::queue` symbol as "undeclared identifier".

!!! note "Vulkan removed (ADR-0726)"
    The `enable_vulkan` option no longer exists. Do not pass
    `-Denable_vulkan=enabled`: Meson rejects an undeclared option with an
    `Unknown options` error and configuration fails. Warnings about `volk.h`,
    `vk_mem_alloc.h`, `VkInstance`, or `VkDevice` mean that the build directory
    predates the removal and should be reconfigured.

Configure the IDE build with every backend for which the host has a toolchain:

```bash
# CUDA + SYCL (full GPU IDE build)
source /opt/intel/oneapi/setvars.sh
CC=icx CXX=icpx meson setup build core \
    -Denable_cuda=true -Denable_sycl=true \
    -Denable_float=true
```

Then restart clangd so it re-reads `compile_commands.json`.

## If you need separate backend build directories

Do not turn the IDE build into an enable-only-one-backend directory. Use named
build directories alongside it:

```bash
# CUDA-only test build
meson setup build-cuda-test core \
    -Denable_cuda=true -Denable_sycl=false -Denable_float=true

# SYCL-only test build (icx/icpx required)
CC=icx CXX=icpx meson setup build-sycl-test core \
    -Denable_cuda=false -Denable_sycl=true -Denable_float=true
```

The cross-backend gate scripts under `scripts/ci/` accept a selected binary
through `--vmaf-binary`.

## Symptoms of a misconfigured build directory

- `unknown type name 'VmafCudaBuffer'` / `VmafCudaState` under
  `core/src/feature/cuda/`;
- `'sycl/sycl.hpp' file not found` under `core/src/feature/sycl/`; or
- a cascade of include-cleanliness warnings after clangd's first fatal error.

If clangd otherwise works, regenerate the compilation database with the
backend enabled before changing source includes.

## Podman dev container

`.devcontainer/podman/devcontainer.json` is a VS Code dev container that builds
its own image on your machine. Use it when you cannot pull the image of the
root `.devcontainer/devcontainer.json`: that one starts from
`ghcr.io/vmafx/vmafx-dev-mcp`, a private package
([ADR-1564](../adr/1564-dev-image-private-guard.md)). This one builds the
`gpu-sdks` stage of [`dev/Containerfile`](../../dev/Containerfile) from public,
pinned inputs, so it needs no registry login
([ADR-3520](../adr/3520-podman-dev-container-local-build.md)).

The container has CUDA (`nvcc`), oneAPI (`icx` / `icpx` on `PATH`, no
`setvars.sh` needed), the Intel NEO OpenCL and Level Zero runtimes with
`ocloc`, and ROCm (`hipcc`). CUDA and HIP code compiles everywhere. It runs only
on a GPU the container can reach, and the configuration passes `/dev/dri`
through, which covers Intel GPUs. Your workspace files stay owned by your host
user.

The image holds the CUDA toolkit and oneAPI, which may be used but not
redistributed. Keep it local: do not push it to a registry.

### Before the first start

1. Use rootless Podman and enable your user's Podman socket:

    ```bash
    systemctl --user enable --now podman.socket
    ```

    The container mounts it so `podman` inside drives the host's engine,
    which is what the `scripts/test/` helpers expect.

2. Point VS Code's Dev Containers extension at Podman. Set
   `"dev.containers.dockerPath": "podman"` in your user settings.

3. Make sure your user can open the GPU's render node, for example
   `/dev/dri/renderD128`. On most desktop distributions the logged-in user
   already can. Otherwise add the user to the `render` group.

### Open the repository in the container

Run **Dev Containers: Reopen in Container** and pick
**vmafx (podman, local GPU toolchains)**.

The first build takes over an hour. It pulls the ROCm image (about 21 GB), and
the finished image is about 28 GB. Later starts reuse the image. Run
**Dev Containers: Rebuild Container** after a change to the `gpu-sdks` stage or
to `build-config.env`. A change to the sources does not need a rebuild,
because the stage copies none.

The build uses `--format=docker`. Podman's default OCI format drops the
`SHELL` instruction, and every `RUN` step would then run under `/bin/sh`. To
build the image by hand:

```bash
podman build --format docker -f dev/Containerfile --target gpu-sdks \
    -t localhost/vmafx-devc:gpu-sdks .
```

Pass `--secret id=github_token,env=GITHUB_TOKEN` as well if the Intel NEO step
hits GitHub's API rate limit.

### Check the GPU and build

Inside the container, `sycl-ls` lists the Intel GPU through Level Zero and
OpenCL. On an Arc Pro B50 it shows:

```text
[opencl:gpu][opencl:1] Intel(R) OpenCL Graphics (discrete), Intel(R) Arc(TM) Pro B50 Graphics OpenCL 3.0 NEO  [26.35.39758.10]
[level_zero:gpu][level_zero:0] Intel(R) oneAPI Unified Runtime over Level-Zero V2, Intel(R) Arc(TM) Pro B50 Graphics 20.1.0 [1.17.39758+10]
```

Then configure the IDE build as described
[above](#make-sure-the-build-directory-covers-every-backend-you-touch). To
compile the SYCL kernels for your GPU only, rather than all 19 default AOT
targets, pass the target (here, Battlemage `bmg-g21`):

```bash
CC=icx CXX=icpx meson setup build core -Denable_sycl=true \
    -Dsycl_icpx_aot_targets=bmg-g21 -Db_lto=false
ninja -C build
python3 scripts/ci/run_meson_test.py -- -C build --suite sycl
```

Without `-Db_lto=false` the test executables do not link with the default
linker (`undefined reference to vmaf_engine_init`). The
[SYCL backend guide](../backends/sycl/overview.md) covers the compiler and
linker choices.

### Limits

- The container reaches Intel and AMD render nodes. It does not get NVIDIA
  devices or AMD's `/dev/kfd`. To run CUDA or HIP code on a GPU, use the
  [dev-MCP container](dev-mcp.md) with your GPU passed through.
- It has the toolchains only. `vmaf`, FFmpeg and the MCP server are not built
  into the image, so you build them from the workspace. The dev-MCP container
  ships them prebuilt.
- `--group-add=keep-groups` works only with a local Podman. If VS Code talks
  to Podman over a remote connection, remove that line from `runArgs`, and the
  render node must then be readable without the extra groups.

## Zed editor

The checked-in contract targets **Zed 1.18.1**
(`bebe92f469834a287f5a57ed78e8d51a918b8ada`). It was verified against that
exact upstream source revision and the current official Zed documentation on
2026-09-24. `.zed/` and `.vscode/` coexist; neither editor needs the other's
configuration.

### Project settings versus user settings

Zed parses `.zed/settings.json` with its restricted project-settings schema.
The repository therefore keeps only settings that can actually be applied to
this worktree:

| In `.zed/settings.json` (project) | Effect |
|---|---|
| clangd | Reads `build/compile_commands.json` and uses the repository's `.clang-tidy` policy. |
| File associations | CUDA, HIP, Metal, and Objective-C++ suffixes map to C++; Meson files map to Meson. |
| Edit predictions | Disabled for models, corpora, golden fixtures, `.workingdir/`, build output, and binary dataset formats. |
| File scanning | Keeps Zed's inherited defaults through the `"..."` entry and skips large build, model, and corpus artefacts. |
| `vmafx-mcp` context server | Starts the current Go MCP binary inside the dev container. |

These belong in the developer's user settings, not in the project:
`agent`, `agent_servers`, UI preferences, telemetry, and extension
installation. Project settings cannot install ACP agents or set native-agent
permissions, and keeping them out of the repository is a trust boundary:
cloning a project must not silently choose a provider, model, or permission
policy for a developer.

Zed opens new worktrees in Restricted Mode. Trust the worktree only after
reviewing `.zed/settings.json`; until then Zed deliberately will not launch the
project's language or MCP servers.

### Extensions

Install the Meson extension for the `Meson` file association. C, C++, Go,
Python, CodeLLDB, Debugpy, and Delve support are built into Zed or installed on
first use. The repository does not use `auto_install_extensions`: Zed reads
that key from user settings only.

### External agents (ACP)

Open `zed: acp registry` and install whichever external agents you use. The
current registry IDs for the team's three common clients are `claude-acp`,
`codex-acp`, and `gemini`. If a developer prefers a declarative user-level
manifest, this is the supported shape in `~/.config/zed/settings.json`:

```json
{
  "agent_servers": {
    "claude-acp": { "type": "registry" },
    "codex-acp": { "type": "registry" },
    "gemini": { "type": "registry" }
  }
}
```

Do not add provider model IDs here. External ACP agents own their
authentication,
subscription, model selection, and native permission policy. Zed's
`agent.tool_permissions` applies to the native Zed Agent and is user-global;
it is not a project-level substitute for an external agent's write mode.

### Dev container and MCP

For vmaf, vmaf-tune, AI, and MCP work, build the exact-source image first and
then start the service from Zed's task picker:

1. `Dev container: build exact source`
2. `Dev container: start`
3. `MCP: probe running container`

The context-server process is equivalent to:

```bash
docker exec -i vmaf-dev-mcp vmafx-mcp
```

The server is the Go binary from `cmd/vmafx-mcp` (ADR-1229). The old Python
`vmaf-mcp` command is retained only as a compatibility surface and is not the
IDE default. Current Zed custom MCP entries use `command` plus `args`; the old
`source: "custom"` field is not part of the 1.18.1 shape.

### Checked-in tasks

`.zed/tasks.json` keeps the three governance tasks introduced by the HISS
adoption and adds current, executable workflows:

| Task | Purpose |
|---|---|
| `Standards: Verify All` | Run the aggregate local governance gate. |
| `Standards: Audit` | Run the standards audit. |
| `Standards: Compile Context` | Verify generated standards context. |
| `Dev container: build exact source` | Build with the source-revision guard. |
| `Dev container: start` | Start only the primary `dev-mcp` service. |
| `Dev container: shell` | Open an interactive shell in the running container. |
| `Dev container: CPU fast gate` | Build in writable `/probes/zed-build-cpu` and run the fast suite. |
| `MCP: probe running container` | Exercise the running container and write its normal probe receipt. |
| `vmaf-tune: compare CPU smoke` | Run the current `vmaf-tune compare --src ...` CLI in the container. |
| `Netflix golden`, `Format check`, `Lint all` | Invoke canonical Make targets. |
| `pytest: current file` | Run the active Python test through the selected environment. |
| `ADR: claim selected slug` | Reserve an ADR number through the atomic allocator. |

The container mounts `/workspace` read-only, so the fast-gate task writes its
build directory under `/probes`; a task that tries to create `/workspace/build`
will fail by design.

### Checked-in debug scenarios

`.zed/debug.json` provides:

- CodeLLDB launch and attach scenarios for the `vmaf` CLI;
- CodeLLDB scenarios for `test_feature` in normal and ASan/UBSan builds;
- Debugpy for the active pytest file and the current `vmaftune.cli` module; and
- Delve for the Go `cmd/vmafx-mcp` server.

Zed only falls back to `.vscode/launch.json` when no `.zed/debug.json`
configurations exist, so the checked-in Zed list must remain complete enough
for the advertised workflows.

### Regression check

Run the focused contract after editing any `.zed/` file or this guide:

```bash
python3 -m pytest -q scripts/ci/tests/test_zed_project_config.py
```

The test rejects project-ignored `agent` / `agent_servers` keys, the retired
numbered workspace root, the deprecated Python MCP entrypoint, deleted helper
paths, the pre-package `vmaf-tune` module path, and loss of the governance
tasks.

### Authoritative Zed references

- [Project settings parser at the verified 1.18.1
  commit](https://github.com/zed-industries/zed/blob/bebe92f469834a287f5a57ed78e8d51a918b8ada/crates/settings_content/src/project.rs)
- [Settings-store project parser at the verified
  commit](https://github.com/zed-industries/zed/blob/bebe92f469834a287f5a57ed78e8d51a918b8ada/crates/settings/src/settings_store.rs)
- [External agents](https://zed.dev/docs/ai/external-agents)
- [Model Context Protocol](https://zed.dev/docs/ai/mcp)
- [Edit predictions](https://zed.dev/docs/ai/edit-prediction)
- [Tasks](https://zed.dev/docs/tasks)
- [Debugger](https://zed.dev/docs/debugger)

The older dated migration documents remain historical evidence only. Do not
copy their version-specific snippets into current configuration.
