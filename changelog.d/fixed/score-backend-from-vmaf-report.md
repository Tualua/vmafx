- **`vmaf-tune` and `vmafx-tune` no longer pick a GPU backend that their
  `vmaf` cannot run.** `--score-backend auto` used to trust the `--help` text,
  which names every backend on every build, plus vendor tools such as
  `nvidia-smi`: a CPU-only `vmaf` on a GPU host was sent `--backend cuda` and
  refused the run. Both tools now read `vmaf --list-backends`, accept `metal`,
  and try `cuda`, `sycl`, `hip`, `metal`, then `cpu`. The Python module and
  the Go package share one table of selection cases.
