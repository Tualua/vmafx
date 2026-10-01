- **`vmaf` no longer hangs after an out-of-memory on the device.**
  `vmaf_read_pictures()` kept the pair of pictures it was given when it failed
  before it reached an extractor (a non-increasing index, pictures that
  disagree with the stream, the picture pool, the CUDA ring buffer, the CUDA
  translation). Pictures from the CLI's pool then never came back, and
  `vmaf_close()` waited for them forever: with the device's memory taken by
  another process `vmaf --backend cuda` printed `problem reading pictures` and
  never exited, holding whatever lock the caller held. The call now owns both
  pictures on every return, as the failures after that point already did, so
  the CLI exits with `-ENOMEM` (status 244) at once. A caller that unref'd the
  pictures after an error, as `docs/api` used to say, must stop doing so
  ([ADR-1431](docs/adr/1431-read-pictures-owns-pictures-on-every-return.md),
  [API guide](docs/api/index.md#ownership-and-lifetime), Netflix/vmaf#1420,
  where the fork returns the error instead of asserting).
