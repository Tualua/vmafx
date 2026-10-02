---
paths:
  - dev/Containerfile
  - dev/docker-compose.yml
invariant: LD_LIBRARY_PATH includes tcm; no image ICD pins; /dev/dri directory bind-mounted; stage 3 backend probe.
---
<!-- markdownlint-disable MD013 -->
# GPU backend exposure invariants (ADR-0514 / Research-0138)

These four constraints must survive every rebase. Each corresponds
to real container-side regression that hid host GPU from libvmaf:

1. **`LD_LIBRARY_PATH` must include `${ONEAPI_ROOT}/tcm/latest/lib`.**
   oneAPI level-zero UR adapter dlopens `libhwloc.so.15` at
   adapter-load time; library lives only in `tcm/latest/lib` (not
   `compiler/latest/lib` or `umf/latest/lib` paths older env block
   covered). Dropping it silently breaks SYCL across every Intel GPU
   even if device passthrough otherwise correct.
2. **Do NOT set `VK_ICD_FILENAMES` or `VK_DRIVER_FILES` in image.**
   Vulkan loader's default search of `/etc/vulkan/icd.d/` +
   `/usr/share/vulkan/icd.d/` picks up both NVIDIA Container
   Toolkit's run-time bind-mount AND mesa intel/radeon/lavapipe
   ICDs. Pinning either env var to single file (especially prior
   `lvp_icd.x86_64.json`, which doesn't exist on disk) hides every
   real GPU. Operators needing to force single ICD can set env var
   at `docker exec` time per-invocation.
3. **`/dev/dri` bind-mounted as whole directory in
   `dev/docker-compose.yml` (ADR-0528).** Docker's `devices:`
   directive carries leaf device nodes but drops subdirectory
   entries such as `by-path/` and `by-id/`. Intel compute-runtime
   discovers Arc GPUs through udev-managed
   `pci-XXXX:YY:ZZ.W-render` symlinks inside `by-path/`; without
   them sycl-ls reports `Platforms: 0` even when
   `/dev/dri/renderD*` visible. Former `/dev/dri/by-path`-only bind
   (ADR-0514) vulnerable to PCI re-enumeration after reboot,
   suspend/resume, or GPU hotplug — path would no longer exist,
   container would fail to start. Fix mounts stable `/dev/dri`
   directory itself (kernel devtmpfs entry always present), drops
   separate `devices: /dev/dri:/dev/dri` entry (bind-mount subsumes
   it). Only `/dev/kfd` remains under `devices:` (single leaf node,
   no subdirectory dependency).
4. **Build-time backend probe loop in stage 3 must stay green for
   `cpu` + `cuda`, `WARN`-but-not-`built without X support` for GPU
   backends.** Probe runs vmaf against Netflix golden CPU pair with
   `--backend cpu cuda sycl hip` and `|| echo WARN`s on missing
   devices. Signal we care about = precise `built without X
   support` string. Means meson flag silently flipped off, real
   backend disappeared from libvmaf entirely (precise failure mode
   that triggered ADR-0514 for HIP).
