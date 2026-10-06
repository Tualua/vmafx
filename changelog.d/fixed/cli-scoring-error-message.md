- `vmaf` prints `problem scoring picture N: libvmaf returned E` when libvmaf
  fails to score a frame it has read (a feature extractor refused the frame or
  its options), where it printed `problem reading pictures`, which read like
  the input read failure `problem while reading pictures` (exit 102). The exit
  status is unchanged. `docs/usage/cli.md` lists the case in the exit-code
  table.
