- **`vmafx-tune-go ladder` scores each rung with the VMAF model its height selects.**
  It passed no `--model`, so a 2160p rung was scored with the default 1080p model;
  the Python `vmaf-tune ladder` uses `vmaf_v1.0.16_1d5h_2160` from 2160 lines up
  (ADR-0289). The Go rule and the Python rule now read one golden table in their
  tests, so they cannot drift. Ladders with no rung of 2160 lines or more score as before.
