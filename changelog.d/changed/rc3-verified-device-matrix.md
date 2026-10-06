- `v1.0.0-rc.3` ships without waiting for outside-hardware reports
  ([ADR-1707](docs/adr/1707-rc3-exit-without-outside-hardware.md)). Outside reports
  arrived on 2026-10-05 (Apple M4 Pro macOS bundle #2118, RTX 3050 #2119, UHD 770 #2116 / #2122),
  closed three Metal rows (`T-GPU-FLOAT-ADM-FRAME-SUM-FLOOR-2026-10-01`,
  `T-GPU-FLOAT-ADM-TINY-FRAME-FLOOR-2026-10-01`, `T-GPU-TWIN-PARITY-GAPS-OUTSIDE-CUDA-2026-09-30`),
  declared 18 Metal twins exact, and verified Ampere sm_86. Release notes for rc.3:
  which twins were verified on which device.

  | Device | Status in rc.3 |
  | --- | --- |
  | NVIDIA RTX 4090 (Ada, sm_89), CUDA | verified: every CUDA twin exact or bounded for the math library |
  | NVIDIA RTX 3050 (Ampere, sm_86), CUDA | verified: 66 of 66 CUDA device tests pass, 19 parity-gate features identical |
  | Intel Arc A380 (Xe-HPG), SYCL | verified, no kernel in scratch memory |
  | Intel Arc B580 and Arc Pro B60 (Xe2), SYCL | verified ([ADR-1501](docs/adr/1501-sycl-float-adm-terms-large-grf-xe2.md)) |
  | AMD gfx1036 (RDNA2 graphics), HIP | verified |
  | Apple M4 Pro (Metal) | verified: 18 twins exact ([ADR-1498](docs/adr/1498-metal-twins-exact-designs.md)) |
  | CPU x86, AVX2 and AVX-512 (Zen 5) | verified against the scalar code |
  | CPU arm64 (NEON, SVE2) | verified under qemu, not on hardware |
  | NVIDIA Hopper, Blackwell, sm_80 Ampere | not yet verified |
  | AMD CDNA, RDNA1, RDNA3 to RDNA4, discrete RDNA2 | not yet verified |
  | Intel Xe-LP and Xe-LPG (UHD, Iris Xe, Arc graphics of Core Ultra) | not yet verified (UHD 770 reports measured; re-run pending after scratch fixes) |
  | Windows builds with an NVIDIA or an Intel GPU | not yet verified (built in CI, never run on a GPU) |

  The five rows that only these devices can close are carried in `docs/state.md`
  under "RC3 carried past rc.3: needs outside hardware", each with its tester
  package ([hardware we need](docs/usage/hardware-we-need.md)). A report that
  arrives later becomes a fix row in the next candidate; a twin that differs
  from the CPU is fixed, not given a tolerance.
