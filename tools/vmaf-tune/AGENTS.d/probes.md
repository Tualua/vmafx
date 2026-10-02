---
paths:
  - tools/vmaf-tune/src/vmaftune/resolution.py
  - tools/vmaf-tune/src/vmaftune/hw_devices.py
  - tools/vmaf-tune/tests/test_resolution.py
  - tools/vmaf-tune/tests/test_hw_devices.py
invariant: Hardware probing is opt-in by codec; dummy-encode resolution floor is 320x240; resolution rule is height-only.
---
<!-- markdownlint-disable MD024 -->
# Hardware and resolution probes

- **Hardware-encoder availability probing is opt-in by codec, not
  by flag.** `probe_encoder_available()` only runs 1-frame lavfi
  dummy encode when codec is in `HARDWARE_ENCODERS`. Adding new
  hardware encoder family (e.g. VAAPI) means appending its names to
  that tuple; encoder will then automatically pay dummy-encode cost
  on every `compare` invocation. CPU encoders short-circuit after
  `ffmpeg -encoders` listing grep.
- **Probe dummy-encode resolution floor is 320×240 (ADR-0601).**
  `probe_encoder_available()` uses `nullsrc=size=320x240:rate=24:
  duration=0.5` for 1-frame dummy encode. Do not lower this
  resolution: NVENC requires at least ~145×49 and QSV requires
  ~128×96; 64×64 (pre-fix value) was below both minima and caused
  every hardware encoder to fail probe with EINVAL on otherwise
  fully-working GPU hosts.
- **`resolution.py` decision rule is height-only.** `height >= 2160`
  picks `vmaf_4k_v0.6.1`; everything else picks `vmaf_v0.6.1`. Width
  is accepted in API for symmetry but ignored in body. Do not add
  per-codec / per-pixel-count branches without ADR-0289 follow-up —
  rule mirrors Netflix's published guidance and is only defensible
  default until fork ships its own intermediate models.
