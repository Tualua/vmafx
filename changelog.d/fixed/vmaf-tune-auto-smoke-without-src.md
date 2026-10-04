- **`vmaf-tune auto --smoke` runs without `--src`.** The smoke planner probes
  nothing, but the subcommand required `--src` and exited 2 before reading
  `--smoke`. `--src` is now required unless `--smoke`, and always with
  `--execute`; a smoke plan without a source records an empty `"src"`, as
  `vmafx-tune auto --smoke` does. See `docs/usage/vmaf-tune-auto.md`.
