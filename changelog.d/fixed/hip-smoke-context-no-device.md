- **`test_hip_smoke` passes on a host without an AMD GPU again.** Its context
  case still expected `vmaf_hip_context_new()` to succeed without a device;
  since the context selects its device first, the function returns `-ENODEV`
  there, and the case now checks that contract (and a populated context when
  a device is present).
