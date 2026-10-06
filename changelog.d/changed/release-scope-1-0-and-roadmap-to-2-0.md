- **Cloud-native work, GStreamer and an OBS-ready API join 1.0.0; the post-1.0
  roadmap is five themed releases ([ADR-2001](docs/adr/2001-release-scope-1-0-and-roadmap-to-2-0.md)).**
  RC4 adds the versioned scoring API contract, server mode with observability,
  a native GStreamer element with conformance of upstream's `vmaf` element, an
  API ready for OBS Studio and real-time FFmpeg GPU scoring; RC5 adds
  containers, Helm, the operator and the GPU pool arbiter; RC6 adds legacy GPU build variants (CUDA 12.x for sm_50 to sm_72, the Intel legacy compute runtime, every AMD target ROCm emits); RC7 grows to a bit-exact SIMD ladder on x86-64, AArch64, RISC-V, POWER and LoongArch; RC8 adds distributed
  throughput. After 1.0.0, releases 1.1 to 1.5 (integrations and live quality;
  encoder feedback, embedding and platforms; new metrics; metric A/B and more
  data; the next model generation) each run their own candidate cycle, and 2.0
  carries breaking changes only. See [the roadmap](docs/roadmap.md).
