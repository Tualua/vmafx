---
paths:
  - core/src/hip/picture_hip.c
  - core/src/feature/hip/integer_adm_hip.c
invariant: HIP memory copy direction enum must match the intended transfer direction.
---
<!-- markdownlint-disable MD013 MD032 MD060 -->
# Memory copy direction enum discipline

Every `hipMemcpy*` call's direction enum **must match actual memory
placement** of source and destination pointers:

- `hipMemcpyHostToDevice`: source = host-accessible (CPU pointer), destination = device-side
- `hipMemcpyDeviceToHost`: source = device-side, destination = host-accessible (CPU or pinned)
- `hipMemcpyDeviceToDevice`: source and destination both device-side

Mismatches = undefined behavior on some HIP runtimes; may silently
corrupt results or trigger runtime faults.

**Established patterns:**

- Picture planes arrive from VMAF pipeline as CPU-side `VmafPicture` structs with `data[0..2]` pointers (host memory). Copying these into device-allocated staging buffers requires `hipMemcpyHostToDevice`.
- Readback buffers allocated via `hipHostMalloc` in `src/hip/kernel_template.c` = host-pinned memory, safe to use with `hipMemcpyDeviceToHost` for kernel output collection.

See 2026-05-16 GPU audit (no follow-up ADR filed; invariant stands on
its own; relevant `hipMemcpy*` direction tags in `integer_psnr_hip.c`
now at lines 212 / 359 / 364 post-refactor).
