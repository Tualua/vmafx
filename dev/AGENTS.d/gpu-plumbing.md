---
paths:
  - dev/Containerfile
  - dev/docker-compose.yml
  - dev/scripts/dev-mcp-entrypoint.sh
invariant: Entrypoint VK_DRIVER_FILES rewrite active; no HSA override; VA drivers in stage 1; NVIDIA graphics capability.
---
<!-- markdownlint-disable MD013 -->
# Full GPU backend plumbing invariants (ADR-0541)

## FFmpeg encoder exposure invariants (ADR-0541)

## Full GPU backend plumbing invariants (ADR-0541)

Four constraints closing last silent-fallback gaps surfaced
empirically against dev machine (NVIDIA RTX 4090, Intel Arc A380,
AMD `gfx1036`). Each corresponds to backend that would otherwise
land on CPU / lavapipe / `-ENODEV` despite device being visible to
kernel:

1. **Entrypoint's `VK_DRIVER_FILES` rewrite must stay in place.**
   `dev/scripts/dev-mcp-entrypoint.sh` enumerates every JSON under
   `/etc/vulkan/icd.d/` + `/usr/share/vulkan/icd.d/`, drops anything
   matching `lvp_*` / `lavapipe*`, pins `VK_DRIVER_FILES` to
   colon-separated allowlist of real ICDs. Without rewrite: software
   ICD wins on multi-vendor hosts where lavapipe sorts before real
   GPU ICDs. (Vulkan backend dropped per ADR-0726.) Do NOT replace
   this with static `ENV VK_DRIVER_FILES=…` in Containerfile —
   operators on CPU-only hosts (no real ICD visible) need lavapipe
   to remain fallback; entrypoint's "if any real ICD exists" guard
   preserves that.
2. **`HSA_OVERRIDE_GFX_VERSION` must NOT be reintroduced in
   `dev/docker-compose.yml` `common-env` (ADR-1225).** Under ROCm
   6.x override was mandatory: AMD `gfx1036` (Raphael iGPU, RDNA2 IP
   rev 10.3.6) wasn't on supported-GPU allowlist, so `hsa_init()`
   returned `HSA_STATUS_ERROR_OUT_OF_RESOURCES`, `rocminfo` reported
   "Unable to open /dev/kfd read-write: Invalid argument" even with
   `/dev/kfd` bind-mounted and video / render groups joined. ROCm 10
   supports `gfx1036` natively; override now actively harmful. Would
   alias agent to `gfx1030` while meson compiles `gfx1036` code
   objects for arch `rocm_agent_enumerator` reports.
   `HSA_ENABLE_SDMA=0` (RDNA2 iGPU SDMA-fault mitigation) and
   `ROCR_VISIBLE_DEVICES=0` (pin HIP to single AMD adapter) stay,
   should not be trimmed.
3. **`intel-media-va-driver-non-free` + `mesa-va-drivers` must stay
   in stage-1 apt list.** Intel compute-runtime
   (`libze_intel_gpu.so.1`) dlopens
   `/usr/lib/x86_64-linux-gnu/dri/iHD_drv_video.so` during
   `zeInit()`-time GPU capability probing. Without
   `intel-media-va-driver-non-free`: `vaInitialize()` returns
   `VA_STATUS_ERROR_UNKNOWN`, compute-runtime bails out of L0 driver
   enumeration, `sycl-ls` reports `Platforms: 0` on Intel Arc hosts.
   `mesa-va-drivers` provides `radeonsi_drv_video.so` for AMD
   equivalent.
4. **`NVIDIA_DRIVER_CAPABILITIES` must include `graphics` (in
   addition to `compute,utility,video`).** NVIDIA Container Toolkit
   only bind-mounts `nvidia_icd.json` into `/etc/vulkan/icd.d/` when
   `graphics` token set. Trimming env block to `compute,utility`
   silently disables NVIDIA Vulkan while leaving CUDA + nvidia-smi
   working — particularly hard regression to spot: every other lane
   stays green. Compose-file `common-env` block carries full token
   set with inline comment; do NOT trim it.
