# AGENTS.md — dev/ (container infra)

Invariants for the `dev/` tree that agents must preserve across rebases and
follow-up PRs. See [Research-0135](../docs/research/0135-dev-mcp-container-stage-3-fix-2026-05-16.md)
for the diagnosis that established these.

## Containerfile invariants

### USER ordering and directory ownership (stage 3)

`WORKDIR` always creates directories as **root**, regardless of any previous
`USER` directive. After `COPY --chown=vmaf:vmaf . /dest/`, only the
*contents* are owned by `vmaf`; the destination directory itself is still
owned by root.

**Rule**: Any `RUN` step that executes as a non-root user and needs to
create a subdirectory inside a `WORKDIR`-created path must be preceded by:

```dockerfile
RUN chown <user>:<group> /parent /parent/dest
USER <user>
```

Do NOT rely on `COPY --chown` alone to make a directory writable — it does
not change the directory entry's owner, only file/subdirectory contents.

Violating this causes `meson setup build` (and any other tool that calls
`os.makedirs`) to fail with `PermissionError: [Errno 13] Permission denied`
at exactly the build-dir creation step, exit code 13.

### CUDA package names

- Use `cuda-toolkit` (the current unversioned meta-package).
- Do NOT install `libcuda1` (the runtime driver) — it must come from
  `nvidia-container-runtime` at run-time; baking it in shadows the host driver.
- Do NOT install `cuda-compiler` — this is a legacy alias that no longer
  exists in the NVIDIA CUDA channels; `cuda-toolkit` already provides `nvcc`.

### Intel oneAPI package name

- Use `intel-basekit` (unversioned meta-package).
- Do NOT use `intel-basekit-<year>.<quarter>` (e.g., `intel-basekit-2025.3`):
  Intel does not publish year-quarter-versioned meta-package names in
  `apt.repos.intel.com/oneapi`. Using the versioned name causes
  `E: Unable to locate package`.

### ROCm / HIP package names

- Use `rocm-hip-runtime-dev` (not `rocm-hip-sdk`).
- `rocm-hip-sdk` transitively installs `rccl` (multi-GPU collectives) which
  depends on `libdrm-amdgpu-amdgpu1` + `libdrm2-amdgpu` — packages absent
  from the ROCm 6.4 noble apt repo. libvmaf HIP kernels use one GPU per
  worker; rccl is not needed.

### SHELL / hadolint DL4006

- `SHELL ["/bin/bash", "-o", "pipefail", "-c"]` is set in the `gpu-sdks`
  stage and **inherited** by `libvmaf-build` and `dev-mcp`.
- hadolint does not track cross-stage SHELL inheritance. Any `RUN` step in
  a derived stage that contains a pipe will trigger DL4006 as a false positive.
  Suppress with `# hadolint ignore=DL4006` and note that SHELL is inherited.

### GPU backend exposure invariants (ADR-0514 / Research-0138)

These four constraints must survive every rebase. Each one corresponds to
a real container-side regression that hid a host GPU from libvmaf:

1. **`LD_LIBRARY_PATH` must include `${ONEAPI_ROOT}/tcm/latest/lib`.** The
   oneAPI level-zero UR adapter dlopens `libhwloc.so.15` at adapter-load
   time and that library lives only in `tcm/latest/lib` (not the
   `compiler/latest/lib` or `umf/latest/lib` paths the older env block
   covered). Dropping it silently breaks SYCL across every Intel GPU even
   if the device passthrough is otherwise correct.

2. **Do NOT set `VK_ICD_FILENAMES` or `VK_DRIVER_FILES` in the image.**
   The Vulkan loader's default search of `/etc/vulkan/icd.d/` +
   `/usr/share/vulkan/icd.d/` picks up both the NVIDIA Container
   Toolkit's run-time bind-mount AND the mesa intel/radeon/lavapipe
   ICDs. Pinning either env var to a single file (especially the prior
   `lvp_icd.x86_64.json`, which does not exist on disk) hides every
   real GPU. Operators that need to force a single ICD can set the
   env var at `docker exec` time per-invocation.

3. **`/dev/dri` is bind-mounted as a whole directory in
   `dev/docker-compose.yml` (ADR-0528).** Docker's `devices:` directive
   carries leaf device nodes but drops subdirectory entries such as
   `by-path/` and `by-id/`. The Intel compute-runtime discovers Arc GPUs
   through the udev-managed `pci-XXXX:YY:ZZ.W-render` symlinks inside
   `by-path/`; without them sycl-ls reports `Platforms: 0` even when
   `/dev/dri/renderD*` is visible. The former `/dev/dri/by-path`-only
   bind (ADR-0514) was vulnerable to PCI re-enumeration after reboot,
   suspend/resume, or GPU hotplug — the path would no longer exist and
   the container would fail to start. The fix mounts the stable
   `/dev/dri` directory itself (a kernel devtmpfs entry that is always
   present) and drops the separate `devices: /dev/dri:/dev/dri` entry
   (the bind-mount subsumes it). Only `/dev/kfd` remains under
   `devices:` (single leaf node, no subdirectory dependency).

4. **The build-time backend probe loop in stage 3 must stay green for
   `cpu` + `cuda` and `WARN`-but-not-`built without X support` for the
   GPU backends.** The probe runs vmaf against the Netflix golden CPU
   pair with `--backend cpu cuda sycl vulkan hip` and `|| echo WARN`s
   on missing devices. The signal we care about is the precise
   `built without X support` string — that means a meson flag silently
   flipped off and a real backend disappeared from libvmaf entirely
   (the precise failure mode that triggered ADR-0514 for HIP).
