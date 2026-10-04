- **`vmafx-tune-go compare` and `ladder` score their encodes, `ladder` encodes
  each rung at its own resolution, and usage errors exit 2.** The scorer handed
  `vmaf` the Matroska encode, which it cannot read, so every probe failed while
  both commands exited 0; it now decodes the encode (and a container reference)
  to Y4M first and refuses a raw `.yuv` reference, and bitrates are read from
  the container when the stream carries none. Each `ladder` rung now encodes
  with `-vf scale=W:H` and scores against the reference scaled the same way, as
  the Python `vmaf-tune ladder` does; a ladder in which no cell scores exits 2
  instead of writing an empty ladder. `auto --smoke` no longer needs `--src`.
  An unknown or unparseable flag and a missing required flag exit 2 on every
  subcommand, as in the Python CLI (they exited 1 outside `benchmark`,
  `encode-profile` and `sidecar`).
