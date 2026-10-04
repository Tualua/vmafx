- **Every HIP twin runs on the device `--hip_device` (or
  `VmafHipConfiguration.device_index`) names, and a broken HIP runtime is
  reported as an error.** `vmaf_hip_context_new()` stored its device index
  and selected nothing, and every HIP twin passed it a literal 0, so a twin
  ran on whatever device its thread had: a library caller scoring on another
  thread than the one that created the state got device 0. The twins now
  create their contexts on the imported state's device, libvmaf rebinds that
  device before each frame and the flush, and an index the runtime does not
  have is refused with `-EINVAL`. `vmaf_hip_device_count()` returned 0 when
  `hipGetDeviceCount()` failed; it now returns a negative errno (0 only when
  the runtime reports no device), and `vmaf_hip_list_devices()` and
  `vmaf_hip_state_init()` pass the error on instead of 0 or `-ENODEV`
  ([ADR-1523](docs/adr/1523-hip-twins-run-on-the-state-device.md)).
