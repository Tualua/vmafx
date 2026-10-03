- Rewrote the newcomer path of the documentation. The home page leads with
  five steps (get VMAFx, score a first pair, choose a backend, use the CLI, API
  or FFmpeg filter, reference). Getting started offers the published container
  images and the release download next to the source build, and the new
  "Score your first pair" page runs a score on the tracked test clips and
  explains the JSON output. The install pages now give the setup-script
  switches as `ENABLE_CUDA=true` / `ENABLE_SYCL=true` (the documented `=1`
  never had an effect), configure from the repository root with
  `meson setup build core`, install Meson from the hash-pinned lock on Ubuntu,
  add the MSVC `/experimental:c11atomics` flag, cover Metal on macOS, and share
  one Intel QSV page instead of four copies. The roadmap explains the release
  candidates for users and cites ADR-1490 for the RC7 to RC9 numbering.
